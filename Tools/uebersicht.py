"""Projektuebersicht, die ihre Zahlen selbst aus dem Repo zieht.

WOFUER: `preview.html` wurde von Hand gepflegt und war deshalb veraltet. Sie
meldete "Phase 1 (GIS-Pipeline) fertig" und "Noch offen: Phasen 2-12
(Fahrzeuge, Traffic-KI, Pedestrians, Player, Wanted-Level, UI, Audio, Wetter,
Optimierung)" - waehrend Verkehr, Wetter, Fahrzeuge, Busse und ein
15-geschossiger Hauptsitz laengst liefen. Auch die Pfade stimmten nicht mehr.

    python Tools/uebersicht.py            # preview.html neu schreiben
    python Tools/uebersicht.py --zeigen   # nur die Zahlen auf der Konsole

VIER REGELN, die diese Seite von der alten unterscheiden:

1. **Jede Zahl wird gemessen, keine behauptet.** Dateien zaehlen, Zeilen
   zaehlen, Testmakros zaehlen, die INI lesen, git fragen.
2. **Jede Zahl nennt ihre Quelle.** Ohne das ist eine erzeugte Seite nur eine
   Behauptung mit besserem Ruf - man muesste ihr glauben, statt nachsehen zu
   koennen.
3. **Laufzeit-Zahlen tragen den Zeitpunkt ihres Laufs.** Sie stammen aus dem
   Log der letzten Sitzung, nicht aus der Gegenwart. Eine Ausstattungsbilanz
   ohne Datum waere genau derselbe Fehler wie vorher, nur automatisch
   erzeugt.
4. **Was sich nicht messen laesst, steht nicht drin.** "Modul X ist fertig"
   ist keine Messung. Die Seite zeigt, WAS da ist - wie weit es gediehen ist,
   beurteilt ein Mensch.
"""
import argparse
import datetime
import html
import json
import os
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

WURZEL = Path(__file__).resolve().parent.parent
ZIEL = WURZEL / "preview.html"


def git(*args):
    try:
        return subprocess.run(["git", *args], cwd=WURZEL, capture_output=True,
                              text=True, encoding="utf-8", errors="replace",
                              check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return ""


def verfolgte(muster):
    """Verfolgte Dateien zu einem Glob - nur was in git liegt, zaehlt."""
    roh = subprocess.run(["git", "ls-files", "-z", "--", *muster],
                         cwd=WURZEL, capture_output=True)
    return [t.decode("utf-8", "surrogateescape") for t in roh.stdout.split(b"\0") if t]


def zeilen(pfade):
    summe = 0
    for p in pfade:
        try:
            with open(WURZEL / p, "rb") as f:
                summe += f.read().count(b"\n")
        except OSError:
            pass
    return summe


# --------------------------------------------------------------------------
# Messungen
# --------------------------------------------------------------------------

def quelltext():
    """Dateien und Zeilen je Quellbereich."""
    bereiche = ("Core", "GIS", "World", "Vehicles", "Weapons", "Tests")
    zeilen_je = []
    for b in bereiche:
        dateien = verfolgte(["Source/WiesbadenReal/%s/*.cpp" % b,
                             "Source/WiesbadenReal/%s/*.h" % b])
        if dateien:
            zeilen_je.append((b, len(dateien), zeilen(dateien)))
    return zeilen_je


def automationstests():
    """(gesamt, je Gebiet) - gezaehlt an den MAKROAUFRUFEN, nicht am Dateitext.

    Ein Testname in einem KOMMENTAR sieht genauso aus wie eine Registrierung.
    Die erste Fassung las darum die ganze Datei und kam auf 249 Gebiets-
    Eintraege bei 247 Makros; die zwei Extras waren Kommentarzeilen wie
    `// "WiesbadenReal.Vehicles.CarLights" war ...`. Aufgefallen ist das nur,
    weil ein Test beide Zaehlwege gegeneinander haelt.
    """
    gesamt = 0
    gebiete = {}
    # Der Name ist das ZWEITE Argument des Makros, direkt hinter der Klasse.
    muster = re.compile(
        r"IMPLEMENT_SIMPLE_AUTOMATION_TEST\s*\(\s*[A-Za-z_][A-Za-z0-9_]*\s*,"
        r"\s*\"(WiesbadenReal\.[A-Za-z0-9_.]+)\"")
    for pfad in verfolgte(["Source/WiesbadenReal/Tests/*.cpp"]):
        text = (WURZEL / pfad).read_text(encoding="utf-8", errors="replace")
        gesamt += text.count("IMPLEMENT_SIMPLE_AUTOMATION_TEST")
        for name in muster.findall(text):
            teile = name.split(".")
            if len(teile) > 1:
                gebiete[teile[1]] = gebiete.get(teile[1], 0) + 1
    return gesamt, sorted(gebiete.items(), key=lambda kv: -kv[1])


def pythontests():
    """(Suiten, Einzeltests) - die Tests werden gezaehlt, nicht gefahren."""
    suiten = verfolgte(["Tools/test_*.py"])
    einzeln = 0
    for p in suiten:
        text = (WURZEL / p).read_text(encoding="utf-8", errors="replace")
        einzeln += len(re.findall(r"^\s+def test_", text, re.M))
    return len(suiten), einzeln


def karten():
    """Gespielte Karte, gebackene Karten, Groesse der externen Actors."""
    try:
        from karte import standard_karte
        live = standard_karte()
    except Exception:
        live = "unbekannt"
    stadtkarten = sorted(p.stem for p in (WURZEL / "Content" / "Maps").glob("WiesbadenCity_*.umap")) \
        if (WURZEL / "Content" / "Maps").exists() else []
    groessen = {}
    basis = WURZEL / "Content" / "__ExternalActors__" / "Maps"
    if basis.exists():
        for ordner in basis.iterdir():
            if ordner.is_dir():
                summe = 0
                for wurzel, _, dateien in os.walk(ordner):
                    for d in dateien:
                        try:
                            summe += os.path.getsize(os.path.join(wurzel, d))
                        except OSError:
                            pass
                groessen[ordner.name] = summe
    return live, stadtkarten, groessen


def schilder():
    png = len(list((WURZEL / "Content" / "Textures" / "TrafficSigns").glob("*.png"))) \
        if (WURZEL / "Content" / "Textures" / "TrafficSigns").exists() else 0
    katalog = 0
    pfad = WURZEL / "Content" / "Config" / "TrafficSignCatalog.json"
    if pfad.exists():
        try:
            katalog = len(json.loads(pfad.read_text(encoding="utf-8"))["signs"])
        except Exception:
            pass
    return png, katalog


# BIS ZUM ZEILENENDE fangen, nicht bis zum ersten Punkt.
#
# "[^.]*\." sah sauber aus und schnitt die Signalprogramm-Zeile mitten
# entzwei: "Umlauf 51 s im Mittel, Spanne 20" - denn die Spanne heisst
# "20..180 s" und enthaelt Punkte. Auf der Seite stand danach eine Zahl, die
# es so nicht gibt. Der abschliessende Punkt wird hinterher abgeschnitten.
LOG_MUSTER = (
    ("Ausstattung", r"Ausstattungs-Spawner: (.+)"),
    ("Strassennetz", r"Verkehr: (\d+ Spuren.+)"),
    ("Ampeln", r"Ampeln wirksam: (\d+ im Netz)"),
    ("Signalprogramm", r"Signalprogramm: (.+)"),
    ("Freigabegruppen", r"Freigabegruppen konfliktfrei: (.+)"),
)


def aus_dem_log():
    """Laufzeit-Zahlen aus dem letzten Spiel-Log, MIT dessen Zeitpunkt.

    Diese Zahlen sind keine Gegenwart: sie stammen aus der letzten Sitzung.
    Ohne den Zeitstempel waere die Seite genauso irrefuehrend wie die von
    Hand gepflegte.
    """
    log = WURZEL / "Saved" / "Logs" / "WiesbadenReal.log"
    if not log.exists():
        return None, []
    wann = datetime.datetime.fromtimestamp(log.stat().st_mtime)
    text = log.read_text(encoding="utf-8", errors="replace")
    treffer = []
    for titel, muster in LOG_MUSTER:
        gefunden = re.findall(muster, text)
        if gefunden:
            treffer.append((titel, gefunden[-1].strip().rstrip(".")))
    return wann, treffer


def lesbar(bytes_):
    for einheit in ("B", "KB", "MB", "GB"):
        if bytes_ < 1024 or einheit == "GB":
            return "%.1f %s" % (bytes_, einheit)
        bytes_ /= 1024.0
    return "%.1f GB" % bytes_


def messen():
    live, stadtkarten, actorgroessen = karten()
    testzahl, testgebiete = automationstests()
    py_suiten, py_tests = pythontests()
    png, katalog = schilder()
    logzeit, logzeilen = aus_dem_log()
    return {
        "quelltext": quelltext(),
        "tests": testzahl,
        "testgebiete": testgebiete,
        "py_suiten": py_suiten,
        "py_tests": py_tests,
        "live": live,
        "stadtkarten": stadtkarten,
        "actorgroessen": actorgroessen,
        "schild_png": png,
        "schild_katalog": katalog,
        "logzeit": logzeit,
        "logzeilen": logzeilen,
        "commits": git("rev-list", "--count", "HEAD") or "?",
        "zweig": git("rev-parse", "--abbrev-ref", "HEAD") or "?",
        "letzter": git("log", "-1", "--format=%ad|%h|%s", "--date=format:%d.%m.%Y"),
        "specs": sorted(p.name for p in (WURZEL / "docs" / "superpowers" / "specs").glob("*.md"))
        if (WURZEL / "docs" / "superpowers" / "specs").exists() else [],
        "werkzeuge": len(verfolgte(["Tools/*.py", "Tools/*.cmd", "Tools/*.ps1"])),
    }


# --------------------------------------------------------------------------
# Seite
# --------------------------------------------------------------------------

def zeile(name, wert, quelle):
    return ('<tr><td class="n">%s</td><td class="w">%s</td>'
            '<td class="q">%s</td></tr>'
            % (html.escape(name), html.escape(str(wert)), html.escape(quelle)))


def seite_bauen(m):
    datum, kurz, betreff = (m["letzter"].split("|") + ["", "", ""])[:3] \
        if m["letzter"] else ("?", "?", "")

    # -- Quelltext -------------------------------------------------------
    quell_zeilen = "".join(
        zeile(b, "%d Dateien, %s Zeilen" % (n, format(z, ",d").replace(",", ".")),
              "git ls-files Source/WiesbadenReal/%s" % b)
        for b, n, z in m["quelltext"])
    gesamt_dateien = sum(n for _, n, _ in m["quelltext"])
    gesamt_zeilen = sum(z for _, _, z in m["quelltext"])

    # -- Pruefungen ------------------------------------------------------
    gebiete = ", ".join("%s %d" % (g, n) for g, n in m["testgebiete"][:8])
    pruef_zeilen = (
        zeile("Automation-Tests (C++)", m["tests"],
              "IMPLEMENT_SIMPLE_AUTOMATION_TEST in Source/.../Tests")
        + zeile("davon je Gebiet", gebiete, "Testnamen WiesbadenReal.<Gebiet>.*")
        + zeile("Python-Suiten", "%d Dateien, %d Einzeltests"
                % (m["py_suiten"], m["py_tests"]), "Tools/test_*.py")
        + zeile("Werkzeuge", m["werkzeuge"], "Tools/*.py *.cmd *.ps1"))

    # -- Inhalte ---------------------------------------------------------
    actor = "".join(
        zeile("externe Actors %s" % name, lesbar(gr), "Content/__ExternalActors__")
        for name, gr in sorted(m["actorgroessen"].items()))
    inhalt_zeilen = (
        zeile("gespielte Karte", m["live"], "GameDefaultMap in Config/DefaultEngine.ini")
        + zeile("gebackene Stadtkarten", ", ".join(m["stadtkarten"]) or "keine",
                "Content/Maps/WiesbadenCity_*.umap")
        + actor
        + zeile("Verkehrszeichen im Katalog", m["schild_katalog"],
                "Content/Config/TrafficSignCatalog.json")
        + zeile("Schild-Grafiken", m["schild_png"], "Content/Textures/TrafficSigns/*.png")
        + zeile("Entwurfsdokumente", len(m["specs"]), "docs/superpowers/specs/*.md"))

    # -- Aus dem Log -----------------------------------------------------
    if m["logzeilen"]:
        log_zeilen = "".join(
            zeile(t, w, "Saved/Logs/WiesbadenReal.log") for t, w in m["logzeilen"])
        log_hinweis = ("Gemessen im Spiel am %s. Diese Zahlen sind der Stand "
                       "JENES Laufs, nicht der Gegenwart."
                       % m["logzeit"].strftime("%d.%m.%Y um %H:%M"))
    else:
        log_zeilen = ('<tr><td class="n">-</td><td class="w">kein Spiel-Log gefunden</td>'
                      '<td class="q">Saved/Logs/WiesbadenReal.log</td></tr>')
        log_hinweis = "Kein Spiel-Log vorhanden - die Stadt wurde hier noch nicht gefahren."

    # -- Git -------------------------------------------------------------
    git_zeilen = (
        zeile("Commits", m["commits"], "git rev-list --count HEAD")
        + zeile("Zweig", m["zweig"], "git rev-parse --abbrev-ref HEAD")
        + zeile("letzter Commit", "%s  %s  %s" % (datum, kurz, betreff[:60]), "git log -1"))

    ersetzungen = {
        "@@QUELLTEXT@@": quell_zeilen,
        "@@QUELL_GESAMT@@": "%d Dateien, %s Zeilen" % (
            gesamt_dateien, format(gesamt_zeilen, ",d").replace(",", ".")),
        "@@PRUEFUNGEN@@": pruef_zeilen,
        "@@INHALTE@@": inhalt_zeilen,
        "@@LOG@@": log_zeilen,
        "@@LOG_HINWEIS@@": html.escape(log_hinweis),
        "@@GIT@@": git_zeilen,
        "@@LIVE@@": html.escape(m["live"]),
        "@@TESTS@@": str(m["tests"]),
        "@@GEBAUT@@": datetime.datetime.now().strftime("%d.%m.%Y um %H:%M"),
    }
    seite = SEITE
    for marke, wert in ersetzungen.items():
        seite = seite.replace(marke, wert)
    return seite


SEITE = """<!doctype html>
<html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Wiesbaden Real - Projektuebersicht</title>
<style>
 :root{--bg:#12141a;--flaeche:#1b1e26;--rand:#2a2f3a;--text:#e6e9ef;
       --matt:#9aa3b2;--leise:#5d6675;--akzent:#e8b04b}
 *{box-sizing:border-box}
 body{margin:0;background:var(--bg);color:var(--text);
      font:15px/1.6 "Segoe UI",system-ui,sans-serif}
 .rahmen{max-width:980px;margin:0 auto;padding:0 24px 60px}
 header{border-bottom:1px solid var(--rand);padding:30px 0 22px;margin-bottom:8px}
 h1{margin:0 0 6px;font-size:27px;letter-spacing:.2px}
 h1 b{color:var(--akzent);font-weight:600}
 .unter{color:var(--matt);font-size:14px;max-width:70ch}
 .marken{margin-top:14px;display:flex;gap:8px;flex-wrap:wrap}
 .marke{background:var(--flaeche);border:1px solid var(--rand);border-radius:12px;
        padding:3px 11px;font-size:12px;color:var(--matt)}
 .marke b{color:var(--text);font-weight:600}
 section{margin-top:30px}
 h2{font-size:15px;margin:0 0 4px;letter-spacing:.5px;text-transform:uppercase;
    color:var(--matt)}
 .hinweis{color:var(--leise);font-size:13px;margin:0 0 12px;max-width:80ch}
 table{width:100%;border-collapse:collapse;background:var(--flaeche);
       border:1px solid var(--rand);border-radius:6px;overflow:hidden}
 td{padding:9px 14px;border-top:1px solid var(--rand);vertical-align:top}
 tr:first-child td{border-top:0}
 td.n{color:var(--matt);width:30%}
 td.w{font-weight:600}
 td.q{color:var(--leise);font-size:12px;font-family:Consolas,monospace;
      text-align:right;width:34%}
 footer{margin-top:40px;padding-top:18px;border-top:1px solid var(--rand);
        color:var(--leise);font-size:12px}
 code{background:var(--flaeche);border:1px solid var(--rand);border-radius:3px;
      padding:1px 5px;font-size:12px}
</style></head><body><div class="rahmen">

<header>
  <h1>Wiesbaden <b>Real</b></h1>
  <div class="unter">Open-World-Spiel im Stil von GTA V - die Landeshauptstadt
  Wiesbaden, gebaut aus echten Geodaten (OpenStreetMap, amtliches ALKIS/LoD2,
  DGM1-Gelaende). Unreal Engine 5.8, C++.</div>
  <div class="marken">
    <span class="marke">gespielt: <b>@@LIVE@@</b></span>
    <span class="marke"><b>@@TESTS@@</b> Automation-Tests</span>
    <span class="marke">1 UU = 1 cm</span>
    <span class="marke">World Partition</span>
  </div>
</header>

<section>
  <h2>Quelltext</h2>
  <p class="hinweis">Gezaehlt ueber die in git verfolgten Dateien - zusammen
  @@QUELL_GESAMT@@.</p>
  <table>@@QUELLTEXT@@</table>
</section>

<section>
  <h2>Pruefungen</h2>
  <p class="hinweis">Die Anzahl ist gezaehlt, nicht gefahren: sie sagt, wie
  viele Tests es GIBT. Ob sie gruen sind, sagt ein Lauf
  (<code>Tools/run_automation_test.cmd</code>).</p>
  <table>@@PRUEFUNGEN@@</table>
</section>

<section>
  <h2>Die Stadt im Spiel</h2>
  <p class="hinweis">@@LOG_HINWEIS@@</p>
  <table>@@LOG@@</table>
</section>

<section>
  <h2>Inhalte</h2>
  <table>@@INHALTE@@</table>
</section>

<section>
  <h2>Stand</h2>
  <table>@@GIT@@</table>
</section>

<footer>
  Diese Seite wird ERZEUGT, nicht gepflegt:
  <code>python Tools/uebersicht.py</code>. Jede Zahl steht mit ihrer Quelle
  daneben, damit sie sich nachsehen laesst. Zuletzt gebaut am @@GEBAUT@@.
  <br><br>
  Was hier NICHT steht: ob ein Teilbereich "fertig" ist. Das laesst sich nicht
  messen, und die Vorgaengerin dieser Seite hat genau daran jahrelang falsch
  gelegen - sie meldete die halbe Spielmechanik als offen, waehrend sie lief.
</footer>

</div></body></html>
"""


def hauptprogramm(argv=None):
    p = argparse.ArgumentParser(description="Projektuebersicht aus dem Repo erzeugen.")
    p.add_argument("--zeigen", action="store_true", help="nur die Zahlen ausgeben")
    p.add_argument("--ziel", help="andere Ausgabedatei")
    a = p.parse_args(argv)

    m = messen()
    if a.zeigen:
        for b, n, z in m["quelltext"]:
            print("  %-10s %3d Dateien %8d Zeilen" % (b, n, z))
        print("  Automation-Tests   %d   (%s)" % (
            m["tests"], ", ".join("%s %d" % g for g in m["testgebiete"][:6])))
        print("  Python             %d Suiten, %d Tests" % (m["py_suiten"], m["py_tests"]))
        print("  gespielte Karte    %s" % m["live"])
        print("  Schilder           %d Katalog, %d Grafiken"
              % (m["schild_katalog"], m["schild_png"]))
        print("  Commits            %s auf %s" % (m["commits"], m["zweig"]))
        if m["logzeilen"]:
            print("  Aus dem Log vom %s:" % m["logzeit"].strftime("%d.%m. %H:%M"))
            for t, w in m["logzeilen"]:
                print("     %-16s %s" % (t, w[:78]))
        return 0

    ziel = Path(a.ziel) if a.ziel else ZIEL
    ziel.write_text(seite_bauen(m), encoding="utf-8")
    print("Geschrieben: %s" % ziel)
    print("  %d Automation-Tests, %d Quelldateien, gespielte Karte %s"
          % (m["tests"], sum(n for _, n, _ in m["quelltext"]), m["live"]))
    return 0


if __name__ == "__main__":
    sys.exit(hauptprogramm())
