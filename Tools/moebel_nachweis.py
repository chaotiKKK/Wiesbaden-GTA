"""Laufzeit-Nachweis fuer die Strassenmoebel: Ort waehlen, fotografieren, ablegen.

WOFUER: Zeigen, dass ein Moebel einer bestimmten Art im LAUFENDEN Spiel am
Strassenrand steht - mit Bild und Kennzahlen, nicht mit Behauptungen.

    python Tools/moebel_nachweis.py bench
    python Tools/moebel_nachweis.py waste_basket --radius 40
    python Tools/moebel_nachweis.py bench --nur-ort      (kein Spiel, nur suchen)

Ergebnis: Saved/Diagnose/moebel_nachweis/<art>_<zeit>/ mit den Bildern,
`kennzahlen.json` und `bericht.md`.

SIEBEN FALLEN, die hier fest verdrahtet sind - jede hat beim ersten Mal einen
Lauf gekostet:

1. Nur eine UNGEBACKENE Karte baut die Stadt zur Laufzeit, und nur dort laeuft
   SpawnFurniture. Darum `__AaaRuntimeShot`; die Default-Karte bleibt unberuehrt.
2. Der Spieler ist der HELIKOPTER. `-WbGoto` setzt ihn genau auf das Motiv, und
   dann fuellt er das Bild. Er wird deshalb `--spieler-weg` Meter daneben geparkt
   (Strassen-Streaming reicht 500 m weit, das genuegt).
3. Der Laufzeit-Pfad baut ohne Kollision (`bCreateCollision=False`). Ohne
   Kollision trifft der Bodentrace nichts, und die Kamera landet unter dem
   Gelaende - im Bild eine weisse Flaeche. Darum der -ini-Schalter.
4. Die OSM-Datei kommt per `-ini`-Ueberschreibung, damit `DefaultGame.ini`
   nicht angefasst wird.
5. Die Kennzahlen stehen in `Saved/Logs/WiesbadenReal.log` - NICHT in der
   stdout-Umleitung. Dort fehlen die Kategorien LogWbRoads/LogWbCore ganz.
6. Der Ort wird danach gewaehlt, was die PLATZIERUNGSREGELN uebrig lassen, nicht
   nach roher OSM-Dichte: von 3.607 Knoten ueberleben nur rund 2.000, und die
   dichtesten Haufen sind ausgerechnet Poller.
7. Argumente NIE ueber eine .cmd reichen: `cmd` trennt an Kommas, aus
   `-WbGoto=-63400,66100` wird `-WbGoto=-63400` plus `-WbShotDelay=66100`.
   Hier startet subprocess die Engine direkt mit einer Argumentliste.
"""
import argparse
import json
import math
import os
import re
import shutil
import subprocess
import sys
import time
import urllib.parse
import urllib.request

PROJEKT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ENGINE = r"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
KARTE = "/Game/Maps/__AaaRuntimeShot"
OSM_MOEBEL = "Data/Raw/OSM/wiesbaden.osm.moebel.json"
MOEBEL_CACHE = "Data/Raw/OSM/street_furniture.overpass.json"
WEGE_CACHE = "Data/Raw/OSM/wege_stadt.overpass.json"
ENGINE_LOG = "Saved/Logs/WiesbadenReal.log"
BILDER = "Saved/Diagnose"
ZIEL = "Saved/Diagnose/moebel_nachweis"

LAT0, LON0 = 50.0824, 8.24
KX = 111320.0 * math.cos(math.radians(LAT0))
CLIP = (LAT0 - 0.055, LON0 - 0.085, LAT0 + 0.055, LON0 + 0.085)

ARTEN = ("bench", "bollard", "waste_basket", "vending_machine",
         "recycling", "fire_hydrant", "post_box", "picnic_table")

# (Spurbreite m, Gehwegbreite m) - Tabelle aus RoadTypeLibrary::ApplyBuiltInDefaults.
# Sie entscheidet, wie breit der "befestigte Streifen" ist, an dem die
# Andock-Regel misst. Wege ohne Gehweg sind der Grund fuer die meisten Verwuerfe.
WEGTYP = {
    "motorway": (3.75, 0.0), "motorway_link": (3.75, 0.0),
    "trunk": (3.50, 0.0), "trunk_link": (3.50, 0.0),
    "primary": (3.50, 3.0), "primary_link": (3.50, 2.5),
    "secondary": (3.25, 2.5), "secondary_link": (3.25, 2.5),
    "tertiary": (3.25, 2.5), "tertiary_link": (3.25, 2.5),
    "unclassified": (3.00, 2.0), "residential": (2.75, 2.0),
    "living_street": (2.50, 0.0), "service": (2.50, 0.0),
    "pedestrian": (3.00, 0.0), "footway": (1.80, 0.0),
    "cycleway": (1.60, 0.0), "path": (1.50, 0.0),
    "steps": (1.50, 0.0), "track": (2.50, 0.0),
}
EINSPURIG = {"footway", "path", "steps", "cycleway", "pedestrian", "service", "track"}

# Wege mit sichtbarer Pflasterkante - nur dort ist "steht am Rand" fotografierbar.
# An einem Feldweg oder einer Hofzufahrt sieht man die Kante im Bild nicht.
SICHTBARE_KANTE = {"residential", "unclassified", "tertiary", "secondary",
                   "primary", "living_street", "pedestrian"}


# -- Geometrie ---------------------------------------------------------------

def nach_xy(lat, lon):
    """Geo -> Weltkoordinaten in METERN (X Ost, Y Sued - wie der Converter)."""
    return ((lon - LON0) * KX, -(lat - LAT0) * 110574.0)


def abstand_zu_strecke(p, a, b):
    dx, dy = b[0] - a[0], b[1] - a[1]
    laenge = dx * dx + dy * dy
    t = 0.0 if laenge < 1e-9 else max(0.0, min(1.0, ((p[0]-a[0])*dx + (p[1]-a[1])*dy) / laenge))
    return math.hypot(p[0] - (a[0] + t*dx), p[1] - (a[1] + t*dy))


def wegbreiten(typ, tags):
    """Halbe Fahrbahnbreite und Gehwegbreite eines Wegs, in Metern."""
    spur, gehweg = WEGTYP[typ]
    spuren = 1 if typ in EINSPURIG else 2
    gemessen = None
    for schluessel in ("width", "carriageway_width"):
        try:
            gemessen = float(str(tags.get(schluessel, "")).split()[0])
            break
        except (ValueError, IndexError):
            gemessen = None
    fahrbahn = gemessen if (gemessen and 0.5 <= gemessen <= 40.0) else spuren * spur
    return fahrbahn * 0.5, gehweg


class Wegnetz:
    """Strecken der Stadt in einem 50-m-Gitter - fuer die Abstandsfrage."""

    ZELLE = 50.0

    def __init__(self, elemente):
        self.strecken = []
        self.zellen = {}
        for weg in elemente:
            geo = weg.get("geometry")
            tags = weg.get("tags", {})
            typ = tags.get("highway")
            if not geo or typ not in WEGTYP:
                continue
            halb, gehweg = wegbreiten(typ, tags)
            punkte = [nach_xy(p["lat"], p["lon"]) for p in geo]
            for a, b in zip(punkte, punkte[1:]):
                self._eintragen(len(self.strecken), a, b)
                self.strecken.append((a, b, halb, gehweg, typ))

    def _eintragen(self, index, a, b):
        x0, x1 = sorted((a[0], b[0]))
        y0, y1 = sorted((a[1], b[1]))
        for cx in range(int(x0 // self.ZELLE), int(x1 // self.ZELLE) + 1):
            for cy in range(int(y0 // self.ZELLE), int(y1 // self.ZELLE) + 1):
                self.zellen.setdefault((cx, cy), []).append(index)

    def naechster(self, p, max_ring=4):
        """(Abstand, halbe Fahrbahn, Gehweg, Typ) des naechsten Wegs - oder None."""
        cx, cy = int(p[0] // self.ZELLE), int(p[1] // self.ZELLE)
        bester = None
        for ring in range(max_ring + 1):
            if bester and (ring - 1) * self.ZELLE > bester[0]:
                break
            for ix in range(cx - ring, cx + ring + 1):
                for iy in range(cy - ring, cy + ring + 1):
                    if ring and abs(ix - cx) != ring and abs(iy - cy) != ring:
                        continue
                    for index in self.zellen.get((ix, iy), ()):
                        a, b, halb, gehweg, typ = self.strecken[index]
                        d = abstand_zu_strecke(p, a, b)
                        if bester is None or d < bester[0]:
                            bester = (d, halb, gehweg, typ)
        return bester


# -- Die Platzierungsregel, wie der Bake-Pass sie anwendet --------------------

def ueberlebt(art, naechster_weg, reichweite_m=1.5):
    """Bleibt dieses Moebel nach der Andock-Regel stehen?

    Nachbau von URoadFurnitureGenerator::PlaceStreetFurniture. Gemessen an
    3.607 echten Knoten weicht der Nachbau um 0,7 % vom Bake ab (1474 gegen
    1464 Verwuerfe) - genug, um einen Ort zu waehlen, zu wenig fuer eine
    Aussage ueber ein EINZELNES Moebel.
    """
    if naechster_weg is None:
        return False
    if art == "bollard":
        return True          # Poller duerfen auf der Fahrbahn stehen
    abstand, halb, gehweg, _typ = naechster_weg
    return (abstand - (halb + gehweg)) <= reichweite_m


def moebel_art(element):
    tags = element.get("tags", {})
    for schluessel in ("amenity", "barrier", "emergency", "leisure"):
        if schluessel in tags and tags[schluessel] in ARTEN:
            return tags[schluessel]
    return None


def ort_waehlen(knoten, netz, art, radius_m=40.0, reichweite_m=1.5,
                nur_sichtbare_kante=True):
    """Bester Fotoplatz fuer eine Art: dort, wo die meisten UEBERLEBEN.

    Nicht die dichteste Stelle - die dichtesten Haufen sind Poller, und die
    Haelfte aller Knoten faellt vorher der Andock-Regel zum Opfer.
    """
    kandidaten = []
    for element in knoten:
        eigene_art = moebel_art(element)
        if eigene_art is None:
            continue
        p = nach_xy(element["lat"], element["lon"])
        weg = netz.naechster(p)
        if not ueberlebt(eigene_art, weg, reichweite_m):
            continue
        if eigene_art == art and (not nur_sichtbare_kante or (weg and weg[3] in SICHTBARE_KANTE)):
            kandidaten.append((p, weg))

    if not kandidaten:
        return None

    # Um jeden Kandidaten zaehlen, wie viele ueberlebende Moebel der Art
    # mitkommen - je mehr im Bild, desto aussagekraeftiger das Foto.
    beste = None
    orte = [p for p, _ in kandidaten]
    for p, weg in kandidaten:
        nah = sum(1 for q in orte if math.hypot(p[0]-q[0], p[1]-q[1]) <= radius_m)
        marke = (nah, -weg[0])
        if beste is None or marke > beste[0]:
            beste = (marke, p, weg, nah)
    _marke, p, weg, nah = beste
    return {
        "x_cm": round(p[0] * 100.0),
        "y_cm": round(p[1] * 100.0),
        "art": art,
        "gleiche_art_im_umkreis": nah,
        "radius_m": radius_m,
        "abstand_zur_wegachse_m": round(weg[0], 2),
        "wegtyp": weg[3],
        "befestigt_bis_m": round(weg[1] + weg[2], 2),
    }


# -- Posen -------------------------------------------------------------------

def posen_text(x_cm, y_cm, hoehe_m=1.7, abstand_m=6.0, richtungen=(0, 90, 180, 270)):
    """Posendatei: Augenhoehe rundherum, plus eine Uebersicht von schraeg oben.

    Format je Zeile (UWiesbadenCitySubsystem::ApplyShotPose):
        Hoehe_m, AtX_cm, AtY_cm, Yaw, Pitch, Vorwaerts_m, LookYaw, LookPitch
    Die Hoehe haengt am BODENTRACE, nicht an einer Weltkoordinate - darum ist
    1,7 m wirklich Augenhoehe, auch am Hang.
    """
    zeilen = ["# Hoehe_m, AtX_cm, AtY_cm, Yaw, Pitch, Vorwaerts_m, LookYaw, LookPitch",
              "# Erzeugt von Tools/moebel_nachweis.py - Augenhoehe rund um das Motiv."]
    for yaw in richtungen:
        zeilen.append("%.1f, %d, %d, %d, 0, %.1f, %d, -8"
                      % (hoehe_m, x_cm, y_cm, yaw, abstand_m, (yaw + 180) % 360))
    zeilen.append("%.1f, %d, %d, 45, 0, %.1f, 225, -30" % (hoehe_m + 4.0, x_cm, y_cm, abstand_m + 6.0))
    return "\n".join(zeilen) + "\n"


# -- Kennzahlen aus dem Engine-Log -------------------------------------------

MUSTER = {
    "uebernommen": re.compile(
        r"Strassenmoebel: (\d+) von (\d+) OSM-Knoten uebernommen "
        r"\((\d+) angedockt, (\d+) im Gebaeude verworfen, (\d+) ohne befestigten Rand"),
    "gestellt": re.compile(
        r"Strassenmoebel gestellt: (\d+) Moebel, (\d+) Teil-Instanzen in (\d+) Zeichengruppen"),
    "bildzeit": re.compile(r"Mittel ([\d.]+) ms \((\d+) Bilder/s\)"),
}


def kennzahlen_lesen(logtext):
    """Zieht die Moebel- und Bildzeit-Zahlen aus dem Engine-Log."""
    werte = {}
    treffer = MUSTER["uebernommen"].search(logtext)
    if treffer:
        werte.update({
            "uebernommen": int(treffer.group(1)),
            "osm_knoten": int(treffer.group(2)),
            "angedockt": int(treffer.group(3)),
            "verworfen_gebaeude": int(treffer.group(4)),
            "verworfen_ohne_rand": int(treffer.group(5)),
        })
    treffer = MUSTER["gestellt"].search(logtext)
    if treffer:
        werte.update({
            "gestellte_moebel": int(treffer.group(1)),
            "teil_instanzen": int(treffer.group(2)),
            "zeichengruppen": int(treffer.group(3)),
        })
    treffer = MUSTER["bildzeit"].findall(logtext)
    if treffer:
        werte["bildzeit_ms"] = float(treffer[-1][0])
        werte["bilder_je_s"] = int(treffer[-1][1])
    return werte


# -- Lauf --------------------------------------------------------------------

def engine_argumente(posen_datei, spieler_x_cm, spieler_y_cm, verzoegerung_s=20):
    """Die Startzeile - als LISTE, damit kein cmd an Kommas trennt."""
    ini = "-ini:Game:[/Script/WiesbadenReal.WiesbadenGameInstance]:"
    return [
        ENGINE, os.path.join(PROJEKT, "WiesbadenReal.uproject"), KARTE, "-game",
        ini + "OsmFilePath=" + OSM_MOEBEL,
        ini + "bCreateCollision=True",
        "-WbNoLumen", "-WbTime=13",
        "-WbGoto=%d,%d" % (spieler_x_cm, spieler_y_cm),
        "-WbShotWhenReady", "-WbShotDelay=%d" % verzoegerung_s,
        "-WbShotPoseFile=" + posen_datei, "-WbPoseSettle=3",
        "-windowed", "-ResX=1600", "-ResY=900", "-stdout", "-nop4",
    ]


def json_holen(pfad, abfrage, was):
    """Overpass-Antwort mit Cache - die Stadt aendert sich nicht stuendlich."""
    voll = os.path.join(PROJEKT, pfad)
    if os.path.exists(voll):
        with open(voll, encoding="utf-8") as f:
            return json.load(f)
    print("Overpass: %s ..." % was)
    req = urllib.request.Request(
        "https://overpass-api.de/api/interpreter",
        data=("data=" + urllib.parse.quote(abfrage)).encode(),
        headers={"User-Agent": "WiesbadenReal/1.0 moebel-nachweis"})
    try:
        with urllib.request.urlopen(req, timeout=600) as antwort:
            daten = json.load(antwort)
    except (urllib.error.URLError, OSError) as fehler:
        # Overpass ist oft ueberlastet (504) - das ist kein Fehler dieses
        # Werkzeugs, und der Abbruch soll das sagen statt einen Stapel zu werfen.
        raise SystemExit(
            "Overpass nicht erreichbar (%s).\n"
            "Spaeter erneut versuchen, oder eine vorhandene Antwort nach\n"
            "  %s\nlegen - die Datei wird als Cache benutzt." % (fehler, pfad))
    os.makedirs(os.path.dirname(voll), exist_ok=True)
    with open(voll, "w", encoding="utf-8") as f:
        json.dump(daten, f)
    print("  gesichert:", pfad)
    return daten


def daten_laden():
    moebel = json_holen(MOEBEL_CACHE, "[out:json][timeout:180];\n(\n" + "".join(
        '  node["%s"="%s"](%f,%f,%f,%f);\n' % ((k, v) + CLIP) for k, v in (
            ("amenity", "bench"), ("barrier", "bollard"), ("amenity", "waste_basket"),
            ("amenity", "vending_machine"), ("amenity", "recycling"),
            ("emergency", "fire_hydrant"), ("amenity", "post_box"),
            ("leisure", "picnic_table"))) + ");\nout body;", "Strassenmoebel")
    wege = json_holen(WEGE_CACHE,
                      '[out:json][timeout:300];way["highway"](%f,%f,%f,%f);out geom;' % CLIP,
                      "Wegenetz der Stadt")
    knoten = [e for e in moebel["elements"] if e.get("lat")]
    return knoten, Wegnetz(wege["elements"])


def lauf(ort, ordner, spieler_weg_m, hoehe_m, abstand_m, zeitlimit_s=420):
    os.makedirs(ordner, exist_ok=True)
    posen = os.path.join(ordner, "posen.txt")
    with open(posen, "w", encoding="utf-8") as f:
        f.write(posen_text(ort["x_cm"], ort["y_cm"], hoehe_m, abstand_m))

    alt = os.path.join(PROJEKT, BILDER)
    for name in os.listdir(alt):
        if name.startswith("WbSeries_") and name.endswith(".png"):
            os.remove(os.path.join(alt, name))

    args = engine_argumente(posen, ort["x_cm"] + int(spieler_weg_m * 100), ort["y_cm"])
    print("Spiel startet (Spieler %d m abseits geparkt) ..." % spieler_weg_m)
    with open(os.path.join(ordner, "start.log"), "w", encoding="utf-8") as log:
        prozess = subprocess.Popen(args, cwd=PROJEKT, stdout=log, stderr=subprocess.STDOUT)
        try:
            prozess.wait(timeout=zeitlimit_s)
        except subprocess.TimeoutExpired:
            prozess.kill()
            print("  ZEITLIMIT - Lauf abgebrochen.")

    bilder = sorted(n for n in os.listdir(alt)
                    if n.startswith("WbSeries_") and n.endswith(".png"))
    for name in bilder:
        shutil.move(os.path.join(alt, name), os.path.join(ordner, name))
    return bilder


def bericht_schreiben(ordner, ort, kennzahlen, bilder):
    zeilen = [
        "# Laufzeit-Nachweis: %s" % ort["art"], "",
        "Erzeugt von `Tools/moebel_nachweis.py` am %s." % time.strftime("%Y-%m-%d %H:%M"), "",
        "## Ort", "",
        "* Welt X %d cm, Y %d cm" % (ort["x_cm"], ort["y_cm"]),
        "* %d Moebel dieser Art im %.0f-m-Umkreis, die die Regeln ueberleben"
        % (ort["gleiche_art_im_umkreis"], ort["radius_m"]),
        "* naechster Weg: %s, Achse %.2f m entfernt, befestigt bis %.2f m"
        % (ort["wegtyp"], ort["abstand_zur_wegachse_m"], ort["befestigt_bis_m"]), "",
        "## Kennzahlen aus dem Lauf", "",
    ]
    if kennzahlen:
        for schluessel, wert in sorted(kennzahlen.items()):
            zeilen.append("* %s: %s" % (schluessel, wert))
    else:
        zeilen.append("* KEINE - das Log trug keine Moebel-Zeile (Lauf gescheitert?).")
    zeilen += ["", "## Bilder", ""]
    zeilen += ["* %s" % b for b in bilder] or ["* KEINE"]
    zeilen.append("")
    with open(os.path.join(ordner, "bericht.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(zeilen))


def main(argv=None):
    p = argparse.ArgumentParser(description="Laufzeit-Nachweis der Strassenmoebel.")
    p.add_argument("art", choices=ARTEN, help="Welche Moebelart fotografiert werden soll.")
    p.add_argument("--radius", type=float, default=40.0, help="Umkreis fuer die Ortswahl (m).")
    p.add_argument("--hoehe", type=float, default=1.7, help="Kamerahoehe ueber Grund (m).")
    p.add_argument("--abstand", type=float, default=6.0, help="Kameraabstand zum Motiv (m).")
    p.add_argument("--spieler-weg", type=float, default=70.0,
                   help="Wie weit der Spieler-Helikopter daneben geparkt wird (m).")
    p.add_argument("--reichweite", type=float, default=1.5,
                   help="Andock-Reichweite der Regel (m) - fuer Kalibrier-Vergleiche.")
    p.add_argument("--alle-wegtypen", action="store_true",
                   help="Auch Orte an Feldwegen/Hofzufahrten zulassen (Kante unsichtbar).")
    p.add_argument("--nur-ort", action="store_true", help="Nur den Ort suchen, nicht starten.")
    args = p.parse_args(argv)

    knoten, netz = daten_laden()
    ort = ort_waehlen(knoten, netz, args.art, args.radius, args.reichweite,
                      nur_sichtbare_kante=not args.alle_wegtypen)
    if ort is None:
        print("Kein Ort gefunden: keine %s ueberlebt die Regeln an einem Weg mit "
              "sichtbarer Kante. Mit --alle-wegtypen noch einmal versuchen." % args.art)
        return 1

    print("Ort: X=%d Y=%d cm | %d %s im %.0f-m-Umkreis | %s, Achse %.2f m, befestigt bis %.2f m"
          % (ort["x_cm"], ort["y_cm"], ort["gleiche_art_im_umkreis"], args.art,
             ort["radius_m"], ort["wegtyp"], ort["abstand_zur_wegachse_m"],
             ort["befestigt_bis_m"]))
    if args.nur_ort:
        return 0

    ordner = os.path.join(PROJEKT, ZIEL, "%s_%s" % (args.art, time.strftime("%Y%m%d_%H%M%S")))
    bilder = lauf(ort, ordner, args.spieler_weg, args.hoehe, args.abstand)

    logpfad = os.path.join(PROJEKT, ENGINE_LOG)
    logtext = ""
    if os.path.exists(logpfad):
        with open(logpfad, encoding="utf-8", errors="replace") as f:
            logtext = f.read()
        shutil.copy(logpfad, os.path.join(ordner, "engine.log"))
    kennzahlen = kennzahlen_lesen(logtext)

    with open(os.path.join(ordner, "kennzahlen.json"), "w", encoding="utf-8") as f:
        json.dump({"ort": ort, "kennzahlen": kennzahlen, "bilder": bilder}, f,
                  indent=2, ensure_ascii=False)
    bericht_schreiben(ordner, ort, kennzahlen, bilder)

    print("%d Bilder, %d Kennzahlen -> %s"
          % (len(bilder), len(kennzahlen), os.path.relpath(ordner, PROJEKT)))
    return 0 if bilder else 1


if __name__ == "__main__":
    raise SystemExit(main())
