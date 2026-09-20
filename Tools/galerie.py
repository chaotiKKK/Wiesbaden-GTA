"""Durchblaetterbare Galerie aus den Diagnose-Bildern.

WOFUER: Unter `Saved/Diagnose` liegen 270 Bilder und 1,9 GB. Darin steckt,
wie die Stadt aussieht - aber daneben auch Kontaktboegen, Ausschnitte,
Stau-Karten und Vorher/Nachher-Paare von Materialproben. Dieses Werkzeug
sucht die echten SPIELANSICHTEN heraus, rechnet sie auf Browser-Groesse
herunter und schreibt eine eigenstaendige HTML-Seite mit Tastatur-Navigation.

    python Tools/galerie.py                  # bauen
    python Tools/galerie.py --zeigen         # nur zeigen, was aufgenommen wuerde
    python Tools/galerie.py --hoechstens 40  # nur die neuesten 40

Ergebnis: `Saved/Diagnose/galerie/index.html` samt verkleinerten Bildern.

FUENF ENTSCHEIDUNGEN, die hier fest verdrahtet sind:

1. **Nach Datum gruppiert, neueste zuerst.** 63 der 89 Stadt-Aufnahmen
   stammen vom 04.09.2026; seither sind Strassenmoebel, Ampelprogramme,
   Fahrzeuge und der Sebbo-Hauptsitz dazugekommen. Eine Galerie, die ein
   Bild von damals neben eines von heute haengt, ohne das Datum zu nennen,
   behauptet etwas Falsches.
2. **Nur Spielbilder.** Erkannt am Seitenverhaeltnis (16:9 plus/minus) und
   an der Mindestbreite. Ein Kontaktbogen (4320 x 574) oder ein Ausschnitt
   faellt damit von selbst heraus, ohne Namensliste.
3. **Eine kurze Namensliste zusaetzlich**, fuer das, was im Format wie ein
   Spielbild aussieht, aber keines ist: Stau-Karten, Kartenausschnitte,
   UI-Aufnahmen. Was ausgeschlossen wurde, wird MIT GRUND ausgegeben - eine
   stille Auswahl liesse sich nicht pruefen.
4. **Verkleinert auf 1600 px als JPEG.** Die Originale sind bis zu 8 MB
   gross; ein Browser, der 90 davon laden soll, steht still.
5. **Keine fremden Quellen.** Kein CDN, kein externes Skript - die Seite
   braucht kein Netz.

ABER: sie braucht einen DATEISERVER. Der `htmlPath`-Modus von
`register_preview` serviert nur die eine HTML-Datei; die Bilder daneben laufen
auf 404, und im Browser sieht man den Aufbau ohne ein einziges Bild. Fuer die
Vorschau also:

    cd Saved/Diagnose/galerie
    python -m http.server 8790 --bind 127.0.0.1

und diese URL samt Prozessnummer registrieren. Beim Oeffnen als lokale Datei
(file://) funktioniert sie ohne Server.
"""
import argparse
import datetime
import html
import json
import os
import re
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    Image = None

WURZEL = Path(__file__).resolve().parent.parent
QUELLE = WURZEL / "Saved" / "Diagnose"
ZIEL = QUELLE / "galerie"

# Ein Spielbild ist breit und im Kinoformat. 16:9 = 1.778; die Spanne laesst
# 1920x1080 ebenso durch wie 1616x939 (Fenster mit Rahmen), schliesst aber
# Kontaktboegen (4320x574 = 7.5) und Hochformate aus.
MIN_BREITE = 1200
VERHAELTNIS = (1.50, 1.95)

# Was im Format wie ein Spielbild aussieht, aber keines ist.
AUSSCHLUSS_NAME = (
    ("staukarte", "Stau-Karte, keine Ansicht"),
    ("map_zoom", "Kartenausschnitt"),
    ("mapauf", "UI-Aufnahme"),
    ("mapzu", "UI-Aufnahme"),
    ("mappad", "UI-Aufnahme"),
    ("line_pdf", "Linienplan"),
    ("kontakt", "Kontaktbogen"),
    ("_inspect", "Pruefausschnitt"),
    ("zoom_", "Ausschnitt"),
    ("_crop", "Ausschnitt"),
    ("quelle_", "Quellgrafik, kein Spielbild"),
)
AUSSCHLUSS_ORDNER = ("galerie", "zoomcrop", "busmaterial")

WEB_BREITE = 1600
MINI_BREITE = 320


def kandidaten():
    """Alle Bilddateien unter Saved/Diagnose, neueste zuerst."""
    if not QUELLE.exists():
        return []
    gefunden = []
    for pfad in QUELLE.rglob("*"):
        if pfad.suffix.lower() not in (".png", ".jpg", ".jpeg"):
            continue
        if any(teil in AUSSCHLUSS_ORDNER for teil in pfad.relative_to(QUELLE).parts[:-1]):
            continue
        gefunden.append(pfad)
    gefunden.sort(key=lambda p: p.stat().st_mtime, reverse=True)
    return gefunden


def beurteilen(pfad):
    """(aufnehmen, grund, groesse) - warum ein Bild dabei ist oder nicht."""
    name = pfad.name.lower()
    for muster, grund in AUSSCHLUSS_NAME:
        if muster in name:
            return False, grund, None
    if Image is None:
        return False, "Pillow fehlt - Groesse nicht pruefbar", None
    try:
        with Image.open(pfad) as im:
            groesse = im.size
    except Exception as fehler:
        return False, "nicht lesbar (%s)" % fehler, None
    breite, hoehe = groesse
    if breite < MIN_BREITE:
        return False, "zu klein (%d px breit)" % breite, groesse
    verhaeltnis = breite / max(hoehe, 1)
    if not (VERHAELTNIS[0] <= verhaeltnis <= VERHAELTNIS[1]):
        return False, "kein Spielformat (%.2f:1)" % verhaeltnis, groesse
    return True, "", groesse


def auswahl(hoechstens=None):
    """(aufgenommen, verworfen) - beide mit Begruendung."""
    drin, raus = [], []
    for pfad in kandidaten():
        ok, grund, groesse = beurteilen(pfad)
        if ok:
            drin.append((pfad, groesse))
        else:
            raus.append((pfad, grund))
    if hoechstens:
        drin = drin[:hoechstens]
    return drin, raus


def tag(pfad):
    return datetime.datetime.fromtimestamp(pfad.stat().st_mtime)


def verkleinern(pfad, ziel, breite):
    """Auf Browser-Groesse bringen. Liefert die neue Groesse."""
    with Image.open(pfad) as im:
        im = im.convert("RGB")
        if im.width > breite:
            hoehe = round(breite * im.height / im.width)
            im = im.resize((breite, hoehe), Image.LANCZOS)
        ziel.parent.mkdir(parents=True, exist_ok=True)
        im.save(ziel, "JPEG", quality=82, optimize=True)
        return im.size


def titel_aus_name(pfad):
    """Aus dem Dateinamen eine lesbare Zeile machen."""
    stamm = pfad.stem
    stamm = re.sub(r"_?\d{8}_\d{6}$", "", stamm)
    ordner = pfad.parent.name
    if ordner and ordner != "Diagnose":
        return "%s / %s" % (ordner, stamm)
    return stamm


def seite_bauen(eintraege):
    """Eigenstaendige HTML-Seite mit Tastatur-Navigation."""
    nach_tag = {}
    for e in eintraege:
        nach_tag.setdefault(e["tag"], []).append(e)
    # Nach ECHTEM Datum sortieren, nicht als Zeichenkette: "04.09.2026"
    # vor "20.09.2026" stimmt zufaellig, "01.10.2026" vor "20.09.2026"
    # nicht mehr. Solange alle Bilder aus einem Monat stammen, faellt so
    # ein Fehler nie auf.
    tage = sorted(nach_tag, key=lambda t: datetime.datetime.strptime(t, "%d.%m.%Y"),
                  reverse=True)

    daten = json.dumps([
        {"web": e["web"], "titel": e["titel"], "tag": e["tag"],
         "zeit": e["zeit"], "gross": e["gross"]}
        for e in eintraege], ensure_ascii=False)

    mini = []
    for t in tage:
        mini.append('<div class="tag"><h2>%s <span>%d Bilder</span></h2><div class="reihe">' %
                    (html.escape(t), len(nach_tag[t])))
        for e in nach_tag[t]:
            mini.append(
                '<button class="mini" data-i="%d" title="%s"><img src="%s" loading="lazy" alt=""></button>'
                % (e["index"], html.escape(e["titel"]), html.escape(e["mini"])))
        mini.append("</div></div>")

    # KEINE %-Formatierung fuer die Seite: die CSS-Regel "max-width:100%" und
    # das JS-Modulo werden davon als Formatzeichen gelesen und brechen den Bau
    # ab ("unsupported format character"). Platzhalter sind die ruhigere Wahl.
    ersetzungen = {
        "@@ANZAHL@@": str(len(eintraege)),
        "@@SPANNE@@": (tage[-1] + " bis " + tage[0]) if tage else "-",
        "@@DATEN@@": daten,
        "@@STREIFEN@@": chr(10).join(mini),
        "@@GEBAUT@@": datetime.datetime.now().strftime("%d.%m.%Y %H:%M"),
    }
    seite = SEITE
    for marke, wert in ersetzungen.items():
        seite = seite.replace(marke, wert)
    return seite


SEITE = """<!doctype html>
<html lang="de"><head><meta charset="utf-8">
<title>Wiesbaden Real - Stadtansichten</title>
<style>
 :root{--bg:#12141a;--flaeche:#1b1e26;--rand:#2a2f3a;--text:#e6e9ef;--matt:#9aa3b2;--akzent:#e8b04b}
 *{box-sizing:border-box}
 body{margin:0;background:var(--bg);color:var(--text);
      font:15px/1.5 "Segoe UI",system-ui,sans-serif}
 header{padding:18px 24px;border-bottom:1px solid var(--rand);
        display:flex;align-items:baseline;gap:16px;flex-wrap:wrap}
 h1{margin:0;font-size:20px;letter-spacing:.3px}
 h1 b{color:var(--akzent);font-weight:600}
 .meta{color:var(--matt);font-size:13px}
 .buehne{position:relative;background:#000;display:flex;align-items:center;
         justify-content:center;min-height:52vh}
 .buehne img{max-width:100%;max-height:78vh;display:block}
 .pfeil{position:absolute;top:50%;transform:translateY(-50%);border:0;
        background:rgba(0,0,0,.45);color:#fff;font-size:30px;line-height:1;
        padding:18px 14px;cursor:pointer}
 .pfeil:hover{background:rgba(0,0,0,.75)}
 .pfeil.links{left:0} .pfeil.rechts{right:0}
 .bildzeile{display:flex;justify-content:space-between;gap:16px;
            padding:10px 24px;border-bottom:1px solid var(--rand);
            color:var(--matt);font-size:13px;flex-wrap:wrap}
 .bildzeile b{color:var(--text);font-weight:600}
 .tag{padding:16px 24px 4px}
 .tag h2{margin:0 0 10px;font-size:14px;color:var(--matt);font-weight:600;
         text-transform:uppercase;letter-spacing:.6px}
 .tag h2 span{color:#5d6675;font-weight:400;text-transform:none;letter-spacing:0}
 .reihe{display:flex;gap:8px;overflow-x:auto;padding-bottom:10px}
 .mini{flex:0 0 auto;border:2px solid transparent;background:none;padding:0;
       cursor:pointer;line-height:0;border-radius:3px}
 .mini img{width:160px;height:90px;object-fit:cover;border-radius:2px;
           opacity:.62;transition:opacity .12s}
 .mini:hover img{opacity:1}
 .mini.aktiv{border-color:var(--akzent)} .mini.aktiv img{opacity:1}
 footer{padding:16px 24px;color:#5d6675;font-size:12px;
        border-top:1px solid var(--rand)}
 kbd{background:var(--flaeche);border:1px solid var(--rand);border-radius:3px;
     padding:1px 6px;font-size:12px}
</style></head><body>
<header>
  <h1>Wiesbaden <b>Real</b> — Stadtansichten</h1>
  <div class="meta">@@ANZAHL@@ Spielbilder · @@SPANNE@@ ·
    <kbd>&larr;</kbd> <kbd>&rarr;</kbd> blaettern, <kbd>Pos1</kbd> neuestes</div>
</header>

<div class="buehne">
  <button class="pfeil links" id="zurueck" aria-label="zurueck">&#8249;</button>
  <img id="gross" alt="">
  <button class="pfeil rechts" id="vor" aria-label="weiter">&#8250;</button>
</div>
<div class="bildzeile">
  <span><b id="titel"></b></span>
  <span id="wann"></span>
  <span id="zaehler"></span>
</div>

@@STREIFEN@@

<footer>Gebaut am @@GEBAUT@@ aus <code>Saved/Diagnose</code> mit
<code>Tools/galerie.py</code>. Die Bilder sind auf 1600 px verkleinert;
die Originale bleiben unberuehrt.</footer>

<script>
const BILDER = @@DATEN@@;
let i = 0;
const gross = document.getElementById('gross');
const titel = document.getElementById('titel');
const wann  = document.getElementById('wann');
const zaehler = document.getElementById('zaehler');

function zeige(n){
  if (!BILDER.length) return;
  i = (n + BILDER.length) % BILDER.length;
  const b = BILDER[i];
  gross.src = b.web;
  titel.textContent = b.titel;
  wann.textContent = b.tag + ', ' + b.zeit + '  ·  ' + b.gross;
  zaehler.textContent = (i+1) + ' / ' + BILDER.length;
  document.querySelectorAll('.mini').forEach(m =>
    m.classList.toggle('aktiv', Number(m.dataset.i) === i));
  const a = document.querySelector('.mini.aktiv');
  if (a) a.scrollIntoView({block:'nearest', inline:'nearest'});
}
document.getElementById('vor').onclick = () => zeige(i+1);
document.getElementById('zurueck').onclick = () => zeige(i-1);
document.querySelectorAll('.mini').forEach(m =>
  m.onclick = () => zeige(Number(m.dataset.i)));
addEventListener('keydown', e => {
  if (e.key === 'ArrowRight') zeige(i+1);
  else if (e.key === 'ArrowLeft') zeige(i-1);
  else if (e.key === 'Home') zeige(0);
  else if (e.key === 'End') zeige(BILDER.length-1);
});
zeige(0);
</script></body></html>
"""


def bauen(hoechstens=None, nur_zeigen=False):
    if Image is None:
        print("ABBRUCH: Pillow fehlt (pip install pillow).", file=sys.stderr)
        return 2

    drin, raus = auswahl(hoechstens)
    print("AUFGENOMMEN: %d Spielbilder" % len(drin))
    if not drin:
        print("  keine - unter %s liegt nichts im Spielformat." % QUELLE)
        return 1

    nach_tag = {}
    for pfad, groesse in drin:
        nach_tag.setdefault(tag(pfad).strftime("%d.%m.%Y"), []).append(pfad)
    for t in sorted(nach_tag, key=lambda x: datetime.datetime.strptime(x, "%d.%m.%Y"), reverse=True):
        print("   %s  %3d Bilder" % (t, len(nach_tag[t])))

    print("\nWEGGELASSEN: %d" % len(raus))
    gruende = {}
    for pfad, grund in raus:
        gruende.setdefault(grund.split(" (")[0], []).append(pfad.name)
    for grund, namen in sorted(gruende.items(), key=lambda kv: -len(kv[1])):
        print("   %-28s %3d   z. B. %s" % (grund, len(namen), namen[0]))

    if nur_zeigen:
        print("\n(nur gezeigt - nichts geschrieben)")
        return 0

    ZIEL.mkdir(parents=True, exist_ok=True)
    eintraege = []
    for index, (pfad, groesse) in enumerate(drin):
        stamm = "%03d_%s" % (index, re.sub(r"[^A-Za-z0-9_-]", "_", pfad.stem))[:80]
        web = ZIEL / (stamm + ".jpg")
        mini = ZIEL / (stamm + "_mini.jpg")
        verkleinern(pfad, web, WEB_BREITE)
        verkleinern(pfad, mini, MINI_BREITE)
        wann = tag(pfad)
        eintraege.append({
            "index": index,
            "web": web.name,
            "mini": mini.name,
            "titel": titel_aus_name(pfad),
            "tag": wann.strftime("%d.%m.%Y"),
            "zeit": wann.strftime("%H:%M"),
            "gross": "%d x %d" % groesse,
        })

    seite = ZIEL / "index.html"
    seite.write_text(seite_bauen(eintraege), encoding="utf-8")
    groesse_mb = sum(p.stat().st_size for p in ZIEL.glob("*.jpg")) / 1024 / 1024
    print("\nGeschrieben: %s" % seite)
    print("  %d Bilder, zusammen %.1f MB (Originale unberuehrt)." % (len(eintraege), groesse_mb))
    return 0


def hauptprogramm(argv=None):
    p = argparse.ArgumentParser(description="Galerie der Diagnose-Bilder bauen.")
    p.add_argument("--hoechstens", type=int, help="nur die neuesten N Bilder")
    p.add_argument("--zeigen", action="store_true", help="nur die Auswahl zeigen")
    a = p.parse_args(argv)
    return bauen(a.hoechstens, a.zeigen)


if __name__ == "__main__":
    sys.exit(hauptprogramm())
