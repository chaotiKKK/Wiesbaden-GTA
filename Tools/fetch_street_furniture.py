"""Holt die OSM-Strassenmoeblierung (Baenke, Poller, Koerbe, ...) nach.

Teilprojekt 1 der Initiative "Stadt detailreicher"
(docs/superpowers/specs/2026-09-19-strassenrand-schmuck-design.md).

BEFUND 2026-09-20: Die gebackene Quelldatei `wiesbaden.osm.forest.json` enthaelt
zwar 1.761 `amenity=bench`, aber praktisch KEINE der uebrigen sieben Kategorien
(117 bollard, 6 fire_hydrant, je 1 waste_basket/recycling/vending_machine/
picnic_table, 0 post_box). Die urspruengliche Overpass-Abfrage (OSMDataParser.cpp)
fragt nur Strassen-, Gebaeude- und Ampel-Tags ab - die Strassenmoebel sind nie
mitgekommen. Ohne diesen Nachzug haette der geplante Bake-Pass schlicht keine
Daten.

Verfahren wie beim Wald-Nachzug (`fetch_osm_forest_relations.py`): Die fehlenden
Knoten werden per Overpass geholt und in eine KOPIE der OSM-Datei gemischt (das
Original bleibt unberuehrt). Bake danach mit
`WB_OSM_FILE=Data/Raw/OSM/wiesbaden.osm.moebel.json` (rebuild_city.py).

Aufruf (aus der Projektwurzel):
    python Tools/fetch_street_furniture.py            # holen (oder Cache) + mischen
    python Tools/fetch_street_furniture.py --zaehlen  # nur zaehlen, nichts schreiben
"""
import json
import os
import sys
import urllib.parse
import urllib.request

SRC = "Data/Raw/OSM/wiesbaden.osm.forest.json"
DST = "Data/Raw/OSM/wiesbaden.osm.moebel.json"
CACHE = "Data/Raw/OSM/street_furniture.overpass.json"

# Dasselbe Stadtrechteck wie beim Wald-Nachzug (~6 km um den Weltursprung):
# Moebel ausserhalb der gebauten Stadt stuenden auf blankem Terrain.
CENTER = (50.0824, 8.24)
CLIP = (CENTER[0] - 0.055, CENTER[1] - 0.085, CENTER[0] + 0.055, CENTER[1] + 0.085)

# Die acht Kategorien des Specs: (Art, OSM-Schluessel, OSM-Wert).
# Die Reihenfolge ist zugleich die Auswertungsreihenfolge - ein Knoten bekommt
# die ERSTE passende Art (OSM-Knoten tragen gelegentlich mehrere dieser Tags,
# etwa ein Automat am Recycling-Container).
KATEGORIEN = (
    ("bench",           "amenity",   "bench"),
    ("bollard",         "barrier",   "bollard"),
    ("waste_basket",    "amenity",   "waste_basket"),
    ("vending_machine", "amenity",   "vending_machine"),
    ("recycling",       "amenity",   "recycling"),
    ("fire_hydrant",    "emergency", "fire_hydrant"),
    ("post_box",        "amenity",   "post_box"),
    ("picnic_table",    "leisure",   "picnic_table"),
)

# Zusatz-Tags fuer die spaetere Variantenwahl (Material/Farbe/Bauart). Alles
# andere wird verworfen: die Datei ist 144 MB gross, jedes ueberfluessige Tag
# kostet Parse-Zeit im Bake.
ZUSATZ_TAGS = (
    "material", "colour", "backrest", "recycling_type", "fire_hydrant:type",
    "vending", "bollard", "covered", "seats",
)

# Unter diesem Schluessel steht die Art im Ergebnis - EINE Stelle, die der
# C++-Pass auswerten muss, statt acht Tag-Kombinationen nachzubauen.
ART_TAG = "wb:furniture"


def art_bestimmen(tags):
    """Liefert die Moebel-Art eines Knotens - oder None, wenn keine passt."""
    if not tags:
        return None
    for art, schluessel, wert in KATEGORIEN:
        if tags.get(schluessel) == wert:
            return art
    return None


def moebel_knoten(elemente):
    """Filtert Overpass-Elemente auf uebernehmbare Moebel-Knoten.

    Liefert (nach Id sortierte Knotenliste, Zaehler je Art, Zahl der
    verworfenen Elemente). Verworfen wird, was kein Knoten ist, was keine
    Koordinate hat oder dessen Tags in keine Kategorie fallen - gezaehlt statt
    geraten, wie das Spec es verlangt.
    """
    knoten = {}
    verworfen = 0
    for el in elemente:
        if el.get("type") != "node" or el.get("lat") is None or el.get("lon") is None:
            verworfen += 1
            continue
        quelle = el.get("tags") or {}
        art = art_bestimmen(quelle)
        if art is None:
            verworfen += 1
            continue
        # Das artgebende Tag bleibt zusaetzlich im Original stehen, damit die
        # Datei auch ohne dieses Skript lesbar bleibt.
        tags = {ART_TAG: art}
        for _, schluessel, wert in KATEGORIEN:
            if quelle.get(schluessel) == wert:
                tags[schluessel] = wert
        for schluessel in ZUSATZ_TAGS:
            if schluessel in quelle:
                tags[schluessel] = quelle[schluessel]
        knoten[int(el["id"])] = {
            "type": "node",
            "id": int(el["id"]),
            "lat": float(el["lat"]),
            "lon": float(el["lon"]),
            "tags": tags,
        }
    zaehler = {}
    for k in knoten.values():
        art = k["tags"][ART_TAG]
        zaehler[art] = zaehler.get(art, 0) + 1
    return [knoten[i] for i in sorted(knoten)], zaehler, verworfen


def einmischen(elemente, moebel):
    """Mischt die Moebel-Knoten in die Elementliste der OSM-Kopie.

    Knoten, die schon in der Basis stehen (die 1.761 Baenke!), bekommen nur die
    fehlenden Tags dazu. Ein ZWEITER Eintrag mit derselben Id waere kein
    harmloses Duplikat: der C++-Parser haelt die Knoten in einer TMap, der
    zweite Eintrag ueberschreibt den ersten - und ein Way, der diesen Knoten
    benutzt, bekaeme stillschweigend eine andere Geometrie.

    Liefert (ergaenzt, angereichert).
    """
    nach_id = {}
    for el in elemente:
        if el.get("type") == "node" and el.get("id") is not None:
            nach_id[int(el["id"])] = el
    ergaenzt = 0
    angereichert = 0
    for neu in moebel:
        vorhanden = nach_id.get(neu["id"])
        if vorhanden is None:
            elemente.append(neu)
            ergaenzt += 1
            continue
        tags = vorhanden.setdefault("tags", {})
        vorher = len(tags)
        for schluessel, wert in neu["tags"].items():
            tags.setdefault(schluessel, wert)
        if len(tags) != vorher:
            angereichert += 1
    return ergaenzt, angereichert


def overpass_abfrage():
    """Die Abfrage fuer alle acht Kategorien im Stadtrechteck."""
    zeilen = "".join(
        '  node["{0}"="{1}"]({2},{3},{4},{5});\n'.format(
            schluessel, wert, CLIP[0], CLIP[1], CLIP[2], CLIP[3])
        for _, schluessel, wert in KATEGORIEN)
    return "[out:json][timeout:180];\n(\n" + zeilen + ");\nout body;"


def overpass_holen():
    if os.path.exists(CACHE):
        print("Overpass-Cache:", CACHE)
        with open(CACHE, encoding="utf-8") as f:
            return json.load(f)
    daten = ("data=" + urllib.parse.quote(overpass_abfrage())).encode()
    req = urllib.request.Request(
        "https://overpass-api.de/api/interpreter", data=daten,
        headers={"User-Agent": "WiesbadenReal/1.0 street furniture"})
    print("Overpass (Stadtrechteck, 8 Kategorien) ...")
    with urllib.request.urlopen(req, timeout=300) as antwort:
        osm = json.load(antwort)
    os.makedirs(os.path.dirname(CACHE), exist_ok=True)
    with open(CACHE, "w", encoding="utf-8") as f:
        json.dump(osm, f)
    print("Antwort gesichert:", CACHE)
    return osm


def main(argv):
    nur_zaehlen = "--zaehlen" in argv

    osm = overpass_holen()
    elemente_roh = osm.get("elements", [])
    moebel, zaehler, verworfen = moebel_knoten(elemente_roh)
    print("Overpass: {0} Elemente, {1} Moebel uebernommen, {2} verworfen".format(
        len(elemente_roh), len(moebel), verworfen))
    for art, _, _ in KATEGORIEN:
        print("  {0:16s} {1:5d}".format(art, zaehler.get(art, 0)))

    if nur_zaehlen:
        return 0

    print("Basis lesen:", SRC)
    with open(SRC, encoding="utf-8") as f:
        basis = json.load(f)
    elemente = basis["elements"] if isinstance(basis, dict) else basis
    vorher = len(elemente)
    ergaenzt, angereichert = einmischen(elemente, moebel)
    print("Elemente {0} -> {1} ({2} neu, {3} vorhandene Knoten ergaenzt)".format(
        vorher, len(elemente), ergaenzt, angereichert))

    with open(DST, "w", encoding="utf-8") as f:
        json.dump(basis, f)
    print("Geschrieben: {0} ({1:.1f} MB)".format(DST, os.path.getsize(DST) / 1e6))
    print("Bake danach mit WB_OSM_FILE=" + DST)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
