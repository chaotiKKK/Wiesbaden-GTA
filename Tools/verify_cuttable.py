"""Gate fuer die Plasmacutter-Bildfolge (Plasmacutter, -WbCutShots).

Der Lauf selbst macht die Arbeit, das Gate beweist sie. Geprueft wird
beides, weil sich die beiden Fehlerarten nicht ausschliessen:

  Log  - vier Bilder gespeichert, Blickwinkel ~0 Grad, wirklich getrennt,
         Glut am Licht, Restzeit nimmt ueber die Winkel ab.
  Bild - die Glut sitzt wirklich im Bild (Pixelanteil), im Vergleichsbild
         vor dem Schnitt dagegen nicht.

Nur die Log-Zeilen zu pruefen war schon einmal genuegt und haette eine
Bildfolge als fertig durchgehen lassen, in der kein Trenn-Stueck zu sehen
war: alle drei Winkel zeigten 55 bis 80 Grad daneben, im Bild standen
Himmel und Strasse.

Zusaetzlich schreibt das Gate den Kontaktbogen
Saved/Diagnose/schnitt_uebersicht.html aus den Log-Werten. Handgeschriebene
Zahlen im HTML gehen sonst beim naechsten Lauf stillschweigend falsch.

Aufruf:
    python Tools/verify_cuttable.py [Logdatei] [--kein-html]

Rueckgabe: 0 = alles belegt, 1 = mindestens eine Pruefung rot.
"""

from __future__ import print_function

import io
import os
import re
import sys

try:
    from PIL import Image, ImageChops
except ImportError:
    print("Pillow fehlt - ohne die Bibliothek lassen sich die PNGs nicht "
          "messen. Installieren mit:  python -m pip install Pillow")
    sys.exit(1)

STANDARD_DIAG = os.path.join("Saved", "Diagnose")
STANDARD_LOG = os.path.join("Saved", "Logs", "wb_cut_schnitt.log")
DIAG = STANDARD_DIAG

# Erwartungen. Grenzen nicht schaerfer als das, was der Lauf tatsaechlich
# liefert: die Restzeit haengt an der Framerate, der Glutanteil an der
# Blickrichtung (schraeg von oben sieht die Flaeche, von der Seite die
# Kante). Zu enge Grenzen machen das Gate zum Flusigkeitsmessgerät.
GRENZE_GLUT_VORHER = 0.5     # Prozent, Vergleichsbild vor dem Schnitt
GRENZE_GLUT_NACHHER = 1.0    # Prozent, die drei Winkel danach
GRENZE_BLICKWINKEL = 15.0    # Grad Abweichung zwischen Blick und Stueck
MIN_ABSTAND_CM = 40.0
MAX_ABSTAND_CM = 800.0
MIN_LICHT_CANDELA = 500.0
MIN_BILD_KANTE = 640          # Bild kleiner als das ist kein brauchbarer Beleg

# (Dateiname, Bildname im Log) - Reihenfolge ist die Reihenfolge der Folge.
BILDER = [
    ("schnitt_00_vorher.png", "schnitt_00_vorher", True),
    ("schnitt_01_nah.png", "schnitt_01_nah", False),
    ("schnitt_02_schraeg.png", "schnitt_02_schraeg", False),
    ("schnitt_03_weit.png", "schnitt_03_weit", False),
]

RE_BILD = re.compile(
    r"WbCutShots: Bild (?P<name>\S+) gespeichert\..*?"
    r"(?:Kamera \((?P<kx>-?\d+), (?P<ky>-?\d+), (?P<kz>-?\d+)\), "
    r"(?P<abstand>[\d.]+) cm vom Ziel, Blickwinkel (?P<winkel>[\d.]+) Grad)")
RE_GLUTZEIT = re.compile(r"Glut noch (?P<rest>[\d.]+) s")
RE_LICHT = re.compile(r"Licht (?P<candela>\d+) Candela")
RE_SCHNITT = re.compile(
    r"WbCutShots t=[\d.]+: geschnitten = (?P<geschnitten>\d+), "
    r"gefallen = (?P<gefallen>\d+), Glut an (?P<glut>\w+)")
RE_FERTIG = re.compile(r"WbCutShots: fertig - getrennt (?P<getrennt>\d+)")
RE_KAMERA = re.compile(r"WbCutShots: eigene Kamera \(FOV (?P<fov>[\d.]+)\)")
RE_STUECK = re.compile(
    r"WbCutShots t=[\d.]+: Trenn-Stueck bei \(-?\d+, -?\d+, -?\d+\) aufgestellt, "
    r"Glutzeit (?P<glutzeit>[\d.]+) s \(Spielwert (?P<spielwert>[\d.]+) s\)")


def glut_mask(img):
    """Pixelweise Maske der gluehenden Kante - ohne numpy, ueber LUTs.

    Die Schwellen sind gegen das Abendlicht kalibriert: Strassenlampen
    sind warm (etwa 255, 200, 150), die Glut geht tiefer ins Rote und ist
    heller. Ohne diese Trennung waere das Vergleichsbild "glutend".
    """
    r, g, b = img.convert("RGB").split()
    hell = r.point(lambda v: 255 if v > 170 else 0)
    nichtzugruen = g.point(lambda v: 255 if v > 60 else 0)
    nichtzublau = b.point(lambda v: 255 if v < 150 else 0)
    rot_abzug = ImageChops.subtract(r, b).point(lambda v: 255 if v > 90 else 0)
    gruen_abzug = ImageChops.subtract(r, g).point(lambda v: 255 if v > 25 else 0)
    return ImageChops.multiply(
        ImageChops.multiply(hell, nichtzugruen),
        ImageChops.multiply(ImageChops.multiply(nichtzublau, rot_abzug), gruen_abzug))


def messe(pfad):
    """Glut-Anteil, Lage der Glut und Bildgroesse."""
    with Image.open(pfad) as im:
        w, h = im.size
        mask = glut_mask(im)
        treffer = mask.histogram()[255]
        box = mask.getbbox()
    anteil = 100.0 * treffer / float(w * h) if w and h else 0.0
    if box:
        x0, y0, x1, y1 = box
        box_text = ("x %d..%d (%.0f..%.0f %%), y %d..%d (%.0f..%.0f %%)"
                    % (x0, x1, 100.0 * x0 / w, 100.0 * x1 / w,
                       y0, y1, 100.0 * y0 / h, 100.0 * y1 / h))
    else:
        box_text = "keine Glut-Pixel"
    return anteil, box_text, (w, h)


def raster_text(img, breite=64, hoehe=24):
    """Grobe Bildaufteilung als Text: Glut, dunkel, hell, mittel."""
    px = img.convert("RGB").resize((breite, hoehe), Image.BOX)
    mask = glut_mask(img).resize((breite, hoehe), Image.BOX)
    zeilen = []
    for y in range(hoehe):
        zeile = []
        for x in range(breite):
            if mask.getpixel((x, y)) > 128:
                zeile.append("O")
            else:
                r, g, b = px.getpixel((x, y))
                s = r + g + b
                zeile.append("#" if s < 200 else ("." if s > 640 else "-"))
        zeilen.append("".join(zeile))
    return zeilen


def lies_log(pfad):
    """Alles aus dem Lauf holen, was das Gate braucht."""
    with io.open(pfad, "r", encoding="utf-8", errors="replace") as fh:
        text = fh.read()
    daten = {
        "bilder": {},
        "schnitt": RE_SCHNITT.search(text),
        "fertig": RE_FERTIG.search(text),
        "kamera": RE_KAMERA.search(text),
        "stueck": RE_STUECK.search(text),
    }
    for treffer in RE_BILD.finditer(text):
        daten["bilder"][treffer.group("name")] = {
            "kx": int(treffer.group("kx")),
            "ky": int(treffer.group("ky")),
            "kz": int(treffer.group("kz")),
            "abstand": float(treffer.group("abstand")),
            "winkel": float(treffer.group("winkel")),
            "rest": RE_GLUTZEIT.search(treffer.group(0)),
            "licht": RE_LICHT.search(treffer.group(0)),
        }
    return daten


def pruefe(log_daten, bild_daten):
    """Alle Pruefungen. Liefert eine Liste (Text, ok) - leer heisst nicht."""
    maengel = []

    def pruef(bedingung, was, folge=""):
        """was = die Aussage, folge = was es bedeutet, wenn sie nicht gilt.

        Beide Teile getrennt, weil der Text auch bei ok ausgegeben wird: ein
        Satz mit der Fehlerfolge an der Zeile, die [ok] traegt, liest sich
        wie ein Befund.
        """
        maengel.append(("%s%s" % (was, ("  ->  " + folge) if not bedingung and folge else ""),
                        bool(bedingung)))
        return bool(bedingung)

    # --- Lauf -------------------------------------------------------------
    if not log_daten["stueck"]:
        pruef(False, "Log: kein 'Trenn-Stueck aufgestellt'",
              "der Lauf kam nicht bis zum Bild")
    pruef(log_daten["kamera"] is not None,
          "Log: freie Bildkamera als ViewTarget gesetzt",
          "ohne sie zeigt die Folge den Spielblick des Pawn")
    schnitt = log_daten["schnitt"]
    if schnitt:
        pruef(schnitt.group("geschnitten") == "1",
              "Log: geschnitten = %s, erwartet 1" % schnitt.group("geschnitten"))
        pruef(schnitt.group("gefallen") == "1",
              "Log: gefallen = %s, erwartet 1" % schnitt.group("gefallen"),
              "es ist nichts abgefallen - im Bild waere nur der Rest zu sehen")
        pruef(schnitt.group("glut") == "ja",
              "Log: Glut an %s, erwartet ja" % schnitt.group("glut"),
              "die Kante leuchtet nicht")
    else:
        pruef(False, "Log: kein 'geschnitten = 1, gefallen = 1'",
              "es wurde nicht getrennt")
    if log_daten["fertig"]:
        pruef(log_daten["fertig"].group("getrennt") == "1",
              "Log: fertig - getrennt %s, erwartet 1"
              % log_daten["fertig"].group("getrennt"))
    else:
        pruef(False, "Log: keine Abschlusszeile 'fertig - getrennt'",
              "die Folge ist abgebrochen")

    # --- Bilder -----------------------------------------------------------
    for datei, bildname, vorher in BILDER:
        pfad = os.path.join(DIAG, datei)
        if not os.path.exists(pfad):
            pruef(False, "%s fehlt" % datei,
                  "der Lauf hat das Bild nicht abgelegt")
            continue
        if not os.path.getsize(pfad) > 10000:
            pruef(False, "%s ist %d Byte" % (datei, os.path.getsize(pfad)),
                  "zu klein fuer ein Bild")
            continue
        anteil, box, (w, h) = bild_daten[datei]
        if min(w, h) < MIN_BILD_KANTE:
            pruef(False, "%s ist nur %dx%d" % (datei, w, h))
            continue

        eintrag = log_daten["bilder"].get(bildname)
        if not eintrag:
            pruef(False, "Log: keine Zeile 'Bild %s gespeichert'" % bildname)
            continue
        pruef(eintrag["winkel"] <= GRENZE_BLICKWINKEL,
              "%s: Blickwinkel %.0f Grad, erlaubt %.0f"
              % (datei, eintrag["winkel"], GRENZE_BLICKWINKEL),
              "die Kamera sieht am Stueck vorbei - im Bild steht nicht das "
              "Trenn-Stueck")
        pruef(MIN_ABSTAND_CM <= eintrag["abstand"] <= MAX_ABSTAND_CM,
              "%s: %.0f cm vom Ziel, erlaubt %.0f..%.0f"
              % (datei, eintrag["abstand"], MIN_ABSTAND_CM, MAX_ABSTAND_CM))

        grenze = GRENZE_GLUT_VORHER if vorher else GRENZE_GLUT_NACHHER
        if vorher:
            pruef(anteil <= grenze,
                  "%s: Glut %.2f %%, erlaubt hoechstens %.2f %%"
                  % (datei, anteil, grenze),
                  "das Vergleichsbild glueht - der Unterschied zum "
                  "Geschnittenen traegt nichts")
        else:
            pruef(anteil >= grenze,
                  "%s: Glut %.2f %%, erwartet mindestens %.2f %%"
                  % (datei, anteil, grenze),
                  "im Bild ist keine gluehende Kante zu sehen")
            if eintrag["licht"]:
                candela = float(eintrag["licht"].group("candela"))
                pruef(candela >= MIN_LICHT_CANDELA,
                      "%s: Licht %.0f Candela, erwartet mindestens %.0f"
                      % (datei, candela, MIN_LICHT_CANDELA),
                      "die Glut leuchtet nicht")
            else:
                pruef(False, "%s: Log nennt keine Lichtstärke" % datei)

    # --- Abkuehlen ueber die Winkel --------------------------------------
    reste = [log_daten["bilder"].get(n, {}).get("rest") for _, n, v in BILDER
             if not v]
    werte = [float(m.group("rest")) for m in reste if m]
    if len(werte) < 2:
        pruef(False, "Log: Restzeit der Glut in weniger als zwei Winkeln genannt",
              "das Abkuehlen ist nicht belegt")
    else:
        pruef(werte == sorted(werte, reverse=True),
              "Log: Restzeit der Glut nicht fallend (%s)"
              % ", ".join("%.1f" % w for w in werte),
              "die Kante kuehlt ueber die drei Winkel nicht ab")
    return maengel


def schreibe_html(log_daten, bild_daten):
    """Kontaktbogen aus den Log-Werten - keine Zahl steht hier fest."""
    zeilen = []
    for datei, bildname, vorher in BILDER:
        eintrag = log_daten["bilder"].get(bildname, {})
        anteil = bild_daten.get(datei, (0.0, "", (0, 0)))[0]
        messzeile = ("Glut %.2f %%" % anteil)
        if eintrag:
            messzeile += " &middot; %.0f cm" % eintrag["abstand"]
            messzeile += " &middot; Blickwinkel %.0f&deg;" % eintrag["winkel"]
            if eintrag["rest"]:
                messzeile += (" &middot; Glut noch %.1f s"
                              % float(eintrag["rest"].group("rest")))
            if eintrag["licht"]:
                messzeile += (" &middot; Licht %s Candela"
                              % eintrag["licht"].group("candela"))
        if vorher:
            messzeile += " &middot; vor dem Schnitt"
        zeilen.append(
            '  <figure>\n'
            '    <img src="%s" alt="%s">\n'
            '    <figcaption><div class="name">%s</div>\n'
            '      <div class="zahl">%s</div></figcaption>\n'
            '  </figure>'
            % (datei, datei.replace("_", " "),
               datei.replace("schnitt_", "").replace(".png", "").replace("_", " "),
               messzeile))
    glutzeit = ""
    stueck = log_daten["stueck"]
    if stueck:
        glutzeit = (", Glutzeit im Lauf %s s (Spielwert %s s, sonst waere das "
                    "dritte Bild bereits dunkel)"
                    % (stueck.group("glutzeit"), stueck.group("spielwert")))
    html = """<!DOCTYPE html>
<!-- Von Tools/verify_cuttable.py erzeugt - alle Zahlen stehen im Log des
     Laufs, keine ist hier eingetragen. Nicht von Hand aendern. -->
<html lang="de">
<head>
<meta charset="utf-8">
<title>Plasmacutter &ndash; Bildfolge</title>
<style>
  body { background:#14161a; color:#d8d8d8; font:14px/1.5 "Segoe UI",sans-serif; margin:24px; }
  h1 { font-size:20px; font-weight:600; }
  .kopf { color:#9aa0a6; margin-bottom:20px; max-width:900px; }
  .reihe { display:flex; flex-wrap:wrap; gap:20px; }
  figure { margin:0; background:#1c1f24; border:1px solid #2b2f36; border-radius:6px;
           padding:12px; width:600px; }
  figure img { width:100%%; display:block; border-radius:3px; background:#000; }
  figcaption { margin-top:10px; font-size:13px; }
  .name { font-weight:600; color:#fff; }
  .zahl { color:#ffa04a; font-variant-numeric:tabular-nums; }
</style>
</head>
<body>
<h1>Plasmacutter &ndash; Trennen mit gluehender Schnittkante</h1>
<p class="kopf">Vier Bilder aus einem Lauf (<code>-WbCutShots -WbZuFuss=6</code>),
aus der freien Bildkamera mit Blickwinkel auf den Bildgegenstand. Bild 00 ist
das Vergleichsbild vor dem Schnitt, 01&ndash;03 die drei Blickwinkel auf das
abgefallene Stueck%s.</p>
<div class="reihe">
%s
</div>
</body>
</html>
""" % (glutzeit, "\n".join(zeilen))
    pfad = os.path.join(DIAG, "schnitt_uebersicht.html")
    if DIAG != STANDARD_DIAG:
        return pfad   # in einer Kopie kein Kontaktbogen noetig
    with io.open(pfad, "w", encoding="utf-8", newline="") as fh:
        fh.write(html)
    return pfad


def main(argv):
    global DIAG
    logpfad = STANDARD_LOG
    mit_html = True
    args = list(argv[1:])
    while args:
        arg = args.pop(0)
        if arg == "--kein-html":
            mit_html = False
        elif arg == "--diag":
            DIAG = args.pop(0)
        elif arg.startswith("--diag="):
            DIAG = arg[len("--diag="):]
        else:
            logpfad = arg
    print("Gate Plasmacutter-Bildfolge")
    print("Log : %s" % logpfad)
    print("Bild : %s\\schnitt_0*.png" % DIAG)
    print()
    if not os.path.exists(logpfad):
        print("ROT  Log fehlt - erst den Lauf fahren "
              "(Tools\\verify_cuttable.cmd).")
        return 1

    log_daten = lies_log(logpfad)
    bild_daten = {}
    for datei, _bildname, _vorher in BILDER:
        pfad = os.path.join(DIAG, datei)
        if os.path.exists(pfad):
            bild_daten[datei] = messe(pfad)

    maengel = pruefe(log_daten, bild_daten)

    print("Gemessen:")
    for datei, _bildname, vorher in BILDER:
        if datei in bild_daten:
            anteil, box, (w, h) = bild_daten[datei]
            print("  %-22s %5dx%-5d Glut %6.2f %%   %s"
                  % (datei, w, h, anteil, box))
        else:
            print("  %-22s fehlt" % datei)
    print()

    rot = 0
    for text, ok in maengel:
        if not ok:
            rot += 1
        print("  [%s] %s" % ("ok" if ok else "ROT", text))
    print()

    if mit_html:
        pfad = schreibe_html(log_daten, bild_daten)
        print("Kontaktbogen: %s" % pfad)
        # Bildraster nur zum Nachsehen im Log - vier Bilder x 24 Zeilen
        # sind sonst unlesbar.
        for datei, bildname, vorher in BILDER:
            if vorher or datei not in bild_daten:
                continue
            anteil = bild_daten[datei][0]
            if anteil < GRENZE_GLUT_NACHHER:
                continue
            with Image.open(os.path.join(DIAG, datei)) as im:
                print()
                print("  %s (Glut %.2f %%)" % (datei, anteil))
                for zeile in raster_text(im):
                    print("    " + zeile)
        print()

    if rot:
        print("GATE ROT - %d von %d Pruefungen fehlgeschlagen."
              % (rot, len(maengel)))
        return 1
    print("GATE GRUEN - %d Pruefungen, alle belegt: getrennt, Stueck "
          "gefallen, Glut an Licht und im Bild, Restzeit faellt ueber die drei "
          "Winkel, Vergleichsbild ohne Glut." % len(maengel))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
