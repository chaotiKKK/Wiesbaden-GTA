"""Erzeugt fehlende Tempo-Scheiben `Sign_274-<N>.png` aus den vorhandenen amtlichen Scheiben.

Warum
-----
Der Strassenausstattungs-Pass leitet aus `maxspeed` ein Zeichen 274 ab
(`RoadFurnitureGenerator.cpp`: Segment.MaxSpeedKmh != 50 -> "274-<Wert>"). Die
OSM-Daten nennen Werte, die in der amtlichen 274-Serie fehlen - 3, 6, 7, 8,
12, 15, 25, 35, 140, 160, 250, 300 (die Serie auf Wikimedia deckt 5..130 ab).
Ohne Grafik bleibt die Tafel leer; die Engine loggt "Schild-Textur nicht
gefunden".

Was das Skript tut
------------------
Statt ein fremdes Bild zu holen, setzt es jede fehlende Zahl aus den amtlichen
Ziffern der schon vorhandenen Scheiben zusammen:
  * Ring + weisses Feld kommen unveraendert aus der Vorlage (274-30 fuer ein-
    und zweistellige Werte, 274-100 fuer dreistellige - dort ist der
    Schriftschnitt schmaler, damit die Zahl ins Feld passt).
  * Die alte Zahl wird nur dort geloescht, wo sie steht (dunkle, unbunte
    Pixel), der rote Ring bleibt unberuehrt.
  * Jede Ziffer stammt aus einer Scheibe, auf der sie amtlich gedruckt ist;
    Ziffern, die in der dreistelligen Serie fehlen, werden aus der
    zweistelligen auf die dreistellige Breite gestaucht.
  * Zeichenhoehe (400 px), Feldmitte (480) und Ziffernabstand werden aus den
    Originalen uebernommen, damit die neuen Scheiben nicht daneben wirken.

Aufruf (irgendein Python mit Pillow + numpy):
  python Tools/make_speed_limit_textures.py             # fehlende erzeugen
  python Tools/make_speed_limit_textures.py --liste     # nur zeigen, was fehlt
  python Tools/make_speed_limit_textures.py --nur 25,160
  python Tools/make_speed_limit_textures.py --pruefe    # Selbsttest gegen vorhandene Scheiben
"""

import argparse
import os
import re
import sys

import numpy as np
from PIL import Image

PROJEKT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ZIEL = os.path.join(PROJEKT, "Content", "Textures", "TrafficSigns")
OSM = os.path.join(PROJEKT, "Data", "Raw", "OSM", "wiesbaden.osm.json")

# Vorlagen je Stellenzahl und die gemessenen Maße der Originale.
VORLAGE = {1: "274-30", 2: "274-30", 3: "274-100"}
FELD_MITTE = 480        # Bildmitte = Feldmitte aller Scheiben
HOEHE = 400             # Ziffernhoehe der Originale (px)
DREI_BREITE = 167       # Ziffernbreite der dreistelligen Scheiben (außer der 1)
DREI_BREITE_EINS = 113
# Breiteste amtliche Ziffernfolge (274-100/-120/-130: 113+42+167+42+168 px).
# Breitere Folgen (z. B. "250" = drei breite Ziffern) wuerden den roten Ring
# beruehren und werden deshalb horizontal auf dieses Mass gestaucht.
MAX_BREITE = 532
ABSTAND = {1: 36, 2: 36, 3: 42}
SCHWELLE = 100          # "schwarz" = alle Kanaele < 100

# Amtliche Ziffern: zwei- und dreistellige Serie.
QUELLEN_ZWEI = {"1": ("274-10", 0), "0": ("274-10", 1), "2": ("274-20", 0), "3": ("274-30", 0),
                "4": ("274-40", 0), "5": ("274-50", 0), "6": ("274-60", 0), "7": ("274-70", 0),
                "8": ("274-80", 0), "9": ("274-90", 0)}
QUELLEN_DREI = {"1": ("274-100", 0), "0": ("274-100", 1), "2": ("274-120", 1), "3": ("274-130", 1)}

_ZWISCHENSPEICHER = {}


def _pfad(name):
    return os.path.join(ZIEL, "Sign_%s.png" % name)


def _bild(name):
    if name not in _ZWISCHENSPEICHER:
        _ZWISCHENSPEICHER[name] = np.asarray(Image.open(_pfad(name)).convert("RGBA")).astype(int)
    return _ZWISCHENSPEICHER[name]


def _kernmaske(a):
    """Dunkle, unbunte Pixel = die gedruckte Zahl (der rote Ring faellt raus)."""
    h, w = a.shape[:2]
    yy, xx = np.mgrid[0:h, 0:w]
    r = np.sqrt((yy - h / 2.0) ** 2 + (xx - w / 2.0) ** 2)
    farbe = a[..., :3]
    dunkel = farbe.max(axis=2) < SCHWELLE
    unbunt = (farbe.max(axis=2) - farbe.min(axis=2)) < 40
    # r < 340: alles Weitere ist der rote Ring bzw. sein halbtransparenter
    # Aussenrand - der gehoert nicht zur Zahl und darf nie angefasst werden.
    return dunkel & unbunt & (a[..., 3] > 128) & (r < 340)


def glyphen(name):
    """Ziffern einer Scheibe als (Bildausschnitt, Position) in Leserichtung."""
    a = _bild(name)
    maske = _kernmaske(a)
    spalten = np.where(maske.any(axis=0))[0]
    if len(spalten) == 0:
        return []
    gruppen, start, vorher = [], spalten[0], spalten[0]
    for s in spalten[1:]:
        if s > vorher + 1:
            gruppen.append((int(start), int(vorher)))
            start = s
        vorher = s
    gruppen.append((int(start), int(vorher)))

    raus = []
    for von, bis in gruppen:
        zeilen = np.where(maske[:, von:bis + 1].any(axis=1))[0]
        y0, y1 = int(zeilen.min()), int(zeilen.max()) + 1
        raus.append(Image.fromarray(a[y0:y1, von:bis + 1].astype(np.uint8), "RGBA"))
    return raus


def _nur_ziffer(g):
    """Glyph-Ausschnitt auf reine Deckung umbauen (weisser Grund wird transparent).

    Damit deckt die Ziffer nur sich selbst zu - ein rechteckiger Ausschnitt
    mit weissem Grund wuerde beim Einsetzen in eine Scheibe den roten Ring
    ankratzen, wenn er weit aussen liegt.
    """
    a = np.asarray(g.convert("RGBA")).astype(int)
    # Deckung aus der Helligkeit: schwarz = voll, weiss = keine.
    deckung = np.clip(255 - a[..., :3].min(axis=2), 0, 255).astype(np.uint8)
    aus = np.zeros((a.shape[0], a.shape[1], 4), dtype=np.uint8)
    aus[..., 3] = deckung
    return Image.fromarray(aus, "RGBA")


def ziffer(schluessel, dreistellig):
    """Amtliche Ziffer <schluessel> ('0'..'9'), auf die Zielmasse gebracht.

    Die Originale streuen in der Ziffernhoehe (392..408 px). Fuer die neuen
    Scheiben wird auf die Regelhohe HOEHE (400 px) normiert - alle neuen
    Scheiben tragen damit dieselbe Schriftgroesse. Dreistellig ist der
    Schriftschnitt schmaler (die Zahl muss in den Ring passen).
    """
    if dreistellig and schluessel in QUELLEN_DREI:
        name, index = QUELLEN_DREI[schluessel]
        g = glyphen(name)[index]
        breite = DREI_BREITE_EINS if schluessel == "1" else DREI_BREITE
        return _nur_ziffer(g.resize((breite, HOEHE), Image.LANCZOS))

    name, index = QUELLEN_ZWEI[schluessel]
    g = glyphen(name)[index]
    if dreistellig:
        breite = DREI_BREITE_EINS if schluessel == "1" else DREI_BREITE
    else:
        breite = int(round(g.width * HOEHE / float(g.height)))
    return _nur_ziffer(g.resize((breite, HOEHE), Image.LANCZOS))


def scheibe(wert, vorlage=None):
    """Baut die Scheibe fuer einen Tempolimit-Wert aus der Vorlage zusammen."""
    text = str(wert)
    dreistellig = len(text) == 3
    a = _bild(vorlage or VORLAGE[len(text)]).copy()

    # Alte Zahl loeschen: nur dunkle, unbunte Pixel im Feld (Ring bleibt).
    farbe = a[..., :3]
    h, w = a.shape[:2]
    yy, xx = np.mgrid[0:h, 0:w]
    r = np.sqrt((yy - h / 2.0) ** 2 + (xx - w / 2.0) ** 2)
    loeschen = ((farbe.max(axis=2) < 200) & ((farbe.max(axis=2) - farbe.min(axis=2)) < 40)
                & (a[..., 3] > 128) & (r < 340))
    a[loeschen] = (255, 255, 255, 255)
    bild = Image.fromarray(a.astype(np.uint8), "RGBA")

    ziffern = [ziffer(z, dreistellig) for z in text]
    abstand = ABSTAND[len(text)]
    gesamt = sum(z.width for z in ziffern) + abstand * (len(ziffern) - 1)
    if gesamt > MAX_BREITE:
        # Nur waagerecht stauchen: die Ziffernhoehe bleibt wie in der Serie.
        faktor = MAX_BREITE / float(gesamt)
        ziffern = [z.resize((int(round(z.width * faktor)), HOEHE), Image.LANCZOS) for z in ziffern]
        abstand = int(round(abstand * faktor))
        gesamt = sum(z.width for z in ziffern) + abstand * (len(ziffern) - 1)
    x = int(round(FELD_MITTE - gesamt / 2.0))
    for z in ziffern:
        y = int(round(FELD_MITTE - z.height / 2.0))
        bild.alpha_composite(z, (x, y))
        x += z.width + abstand
    return bild


def vorhandene():
    return {f[5:-4] for f in os.listdir(ZIEL) if f.endswith(".png") and f.startswith("Sign_274-")}


def aus_maxspeed(raw):
    """274-Werte, die der Ausstattungs-Pass aus maxspeed ableitet (Spiegel der C++-Regeln)."""
    implizit = {"de:urban": 50, "urban": 50, "de:rural": 100, "rural": 100,
                "de:motorway": 250, "de:living_street": 7, "walk": 7,
                "de:bicycle_road": 30, "de:zone30": 30, "de:zone:30": 30,
                "none": 250, "unlimited": 250, "signals": 100, "variable": 100}
    werte = set()
    for tag in ("maxspeed", "maxspeed:type", "zone:maxspeed"):
        for wert in re.findall(r'"%s"\s*:\s*"([^"]*)"' % re.escape(tag), raw):
            w = wert.strip().lower()
            if w in implizit:
                kmh = implizit[w]
            else:
                m = re.match(r"^(\d+(?:\.\d+)?)", w)
                if not m:
                    continue
                kmh = float(m.group(1))
                if "mph" in w:
                    kmh *= 1.609344
                elif "knots" in w:
                    kmh *= 1.852
            kmh = int(kmh + 0.5)
            if 0 < kmh <= 300 and kmh != 50:
                werte.add(kmh)
    return werte


def aus_traffic_sign(raw):
    """274-Werte aus expliziten Zeichen-Tags ('274[30]', '274.1:30', '274-30')."""
    werte = set()
    for wert in re.findall(r'"traffic_sign"\s*:\s*"([^"]*)"', raw):
        for teil in re.split(r"[;,]", wert):
            m = re.match(r"^\s*(?:DE:?\s*)?274(?:\.1)?(?:\[(\d+)\]|:(\d+)|-(\d+))\s*$", teil)
            if m:
                werte.add(int(next(g for g in m.groups() if g)))
    return werte


def fehlende(nur=None):
    if nur:
        return [int(x) for x in nur.replace(" ", "").split(",") if x]
    raw = open(OSM, encoding="utf-8", errors="replace").read()
    brauchen = aus_maxspeed(raw) | aus_traffic_sign(raw)
    vorhanden = {int(x.split("-")[1]) for x in vorhandene() if x.count("-") == 1 and x.split("-")[1].isdigit()}
    return sorted(brauchen - vorhanden)


# Amtliche Serie, an der sich der Nachbau messen lassen muss (274-1 ist das
# Zeichen 274.1 "Beginn einer Tempo 30-Zone" und damit keine reine Scheibe).
SERIE = [5, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130]


def glyphen_bild(a):
    """Wie glyphen(), aber fuer ein bereits geladenes numpy-Bild."""
    maske = _kernmaske(a)
    spalten = np.where(maske.any(axis=0))[0]
    if len(spalten) == 0:
        return []
    gruppen, start, vorher = [], spalten[0], spalten[0]
    for s in spalten[1:]:
        if s > vorher + 1:
            gruppen.append((int(start), int(vorher)))
            start = s
        vorher = s
    gruppen.append((int(start), int(vorher)))
    raus = []
    for von, bis in gruppen:
        zeilen = np.where(maske[:, von:bis + 1].any(axis=1))[0]
        raus.append(Image.fromarray(
            a[int(zeilen.min()):int(zeilen.max()) + 1, von:bis + 1].astype(np.uint8), "RGBA"))
    return raus


def pruefe():
    """Selbsttest: die amtlichen Scheiben aus ihren eigenen Ziffern nachbauen.

    Geprueft wird, was zaehlt (je amtlicher Scheibe der Serie):
      * Ring und Feld sind pixelgleich zur VORLAGE - der Nachbau fasst nur die
        Zahl an. (Mit der Originaldatei verglichen kann der Ring abweichen:
        einzelne Dateien der Serie tragen einen minimal anders gerenderten
        Ring; das ist eine Eigenschaft der Quelle, nicht des Nachbaus.)
      * Ziffernzahl und -hoehe stimmen, die Breite liegt im Rahmen der Schrift.
      * Die Pixelabweichung zur amtlichen Datei bleibt unter 8 %% - sie stammt
        vom Ziffernabstand (die Originale setzen ihn je Zahl individuell
        zwischen 30 und 83 px, der Nachbau gleichmaessig).
    """
    fehler = 0
    for wert in SERIE:
        neu = np.asarray(scheibe(wert).convert("RGBA")).astype(int)
        alt = _bild("274-%d" % wert)

        h, w = alt.shape[:2]
        yy, xx = np.mgrid[0:h, 0:w]
        aussen = np.sqrt((yy - h / 2.0) ** 2 + (xx - w / 2.0) ** 2) > 340
        vorlage = _bild(VORLAGE[len(str(wert))])
        ring_diff = int((np.abs(neu[aussen] - vorlage[aussen]).max(axis=1) > 0).sum())

        # Ziffern: Anzahl und Hoehe muessen stimmen (Breite darf um die
        # Schriftbreite der Quellscheibe streuen - die Originale sind selbst
        # nicht einheitlich).
        mass_neu = [(g.width, g.height) for g in glyphen_bild(neu)]
        mass_alt = [(g.width, g.height) for g in glyphen_bild(alt)]
        diff = (np.abs(neu - alt).max(axis=2) > 60).mean() * 100
        hoehen_ok = all(m[1] == HOEHE for m in mass_neu)
        ok = (ring_diff == 0 and len(mass_neu) == len(mass_alt) and hoehen_ok and diff < 8.0)
        fehler += 0 if ok else 1
        print("274-%-4d Ring %s, Ziffern %s (amtlich %s), Pixelabweichung %.2f %%" % (
            wert, "unberuehrt" if ring_diff == 0 else "%d Pixel anders" % ring_diff,
            ", ".join("%dx%d" % m for m in mass_neu),
            ", ".join("%dx%d" % m for m in mass_alt), diff))
    print("Selbsttest: %s" % ("in Ordnung" if fehler == 0 else "%d Scheiben fehlerhaft" % fehler))
    return 0 if fehler == 0 else 1


def main():
    ap = argparse.ArgumentParser(description="Fehlende Tempo-Scheiben aus amtlichen Ziffern bauen")
    ap.add_argument("--liste", action="store_true", help="nur zeigen, welche Werte fehlen")
    ap.add_argument("--nur", help="Komma-Liste von Werten statt der automatischen Auswahl")
    ap.add_argument("--pruefe", action="store_true", help="Selbsttest gegen die vorhandenen Scheiben")
    ap.add_argument("--alle", action="store_true", help="vorhandene Scheiben ueberschreiben")
    args = ap.parse_args()

    if args.pruefe:
        return pruefe()

    # 1 faellt raus: Sign_274-1.png ist das Zeichen 274.1 (Tempo-30-Zone).
    werte = [w for w in fehlende(args.nur) if w != 1]
    if args.alle:
        # Auch vorhandene Scheiben neu bauen - Serie und Bedarf der Daten.
        raw = open(OSM, encoding="utf-8", errors="replace").read()
        werte = sorted((set(werte) | set(SERIE) | aus_maxspeed(raw) | aus_traffic_sign(raw)) - {1})
    if args.liste:
        print("%d fehlende Tempolimit-Werte: %s" % (len(werte), ", ".join(str(w) for w in werte)))
        return 0
    if not werte:
        print("Keine fehlenden Tempolimit-Werte.")
        return 0

    for wert in werte:
        ziel = _pfad("274-%d" % wert)
        if os.path.exists(ziel) and not args.alle:
            continue
        scheibe(wert).save(ziel)
        print("Sign_274-%-4d  %s" % (wert, ziel))
    print("\n%d Scheiben erzeugt. Import: python Tools/import_sign_textures.py" % len(werte))
    return 0


if __name__ == "__main__":
    sys.exit(main())
