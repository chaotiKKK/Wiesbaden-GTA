"""Ueberbauungen in der ALKIS-JSON: Gebaeude, die eine Strasse UEBERSPANNEN.

ALKIS fuehrt ein Gebaeude mit seinem ganzen Grundriss - auch den Teil, der als
Bruecke ueber einer Strasse steht. Gebacken stand es damit bis zum Boden auf
allen Spuren: das LuisenForum (DEHE062000019FHP) ueber der Schwalbacher
Strasse. Dort blieben Spielerauto und Busse haengen, und der Bodenstrahl von
oben landete den haltenden Bus auf dem Dach (86,9 statt 46,9 m).

Beleg fuer die Durchfahrt: in OSM sind die Schwalbacher Strasse (Wege
1286981898, 1286981901) und beide Gehwege (1286981894, 1286981896) dort
`covered=yes`, ~15 m unter dem Gebaeude.

Loesung ohne C++: der Grundriss wird am Korridor (Rechteck quer ueber Fahrbahn
und Gehwege) aufgetrennt. Der Rest bleibt ein normales Gebaeude, der Korridor
wird ein eigener Way mit `min_height` - den setzt BuildingGenerator schon als
schwebenden Teil um (Wandfuss = Fundament-Unterkante + min_height, wie die Durchfahrten in
der Wilhelmstrasse). Idempotent: ein schon getrenntes Gebaeude wird erkannt.

Aufruf (Datei wird an Ort und Stelle ergaenzt, vorher Sicherung *.vor_ueberbauung):
    python Tools/alkis_ueberbauungen.py [Data/Raw/ALKIS/wiesbaden.alkis.lod2.json]
Auch Tools/apply_lod2_citygml.py ruft `apply_ueberbauungen` vor dem Schreiben auf,
damit eine neu erzeugte LoD2-JSON den Schnitt behaelt. Braucht `shapely`.
"""
import json
import math
import os
import shutil
import sys

LAT0, LON0 = 50.0824, 8.24
KX = 111320.0 * math.cos(math.radians(LAT0))

# Je Ueberbauung: ALKIS-ID, Korridor (lat, lon) als Viereck, min_height (m).
# ACHTUNG: BuildingGenerator rechnet min_height ab der Fundament-UNTERKANTE
# (tiefster Gelaendepunkt - FoundationDepthCm 150). 7,5 m = 6 m Durchfahrt am
# tiefsten Punkt des Brueckengrundrisses.
# Korridor LuisenForum: minimales gedrehtes Rechteck um den Gebaeudearm ueber
# der Strasse (aus den ueberdachten OSM-Gehwegen, .planning/luisenforum/),
# nach Osten 3 m und quer je 0,3 m ueber den Grundriss hinaus, damit der Schnitt
# den Arm sicher ganz erfasst. Westkante = Aussenwand des Hauptbaus.
UEBERBAUUNGEN = [
    {
        "alkis_id": "DEHE062000019FHP",
        "name": "LuisenForum ueber der Schwalbacher Strasse",
        "korridor": [(50.0785572, 8.2368535), (50.0785236, 8.2362321),
                     (50.0786889, 8.2362103), (50.0787225, 8.2368318)],
        "min_height_m": 7.5,
    },
]


def _xy(lat, lon):
    return ((lon - LON0) * KX, (lat - LAT0) * 111320.0)


def _latlon(x, y):
    return (round(LAT0 + y / 111320.0, 7), round(LON0 + x / KX, 7))


def apply_ueberbauungen(elements, log=print):
    """Trennt jede Ueberbauung auf (mutiert `elements`). Rueckgabe: Anzahl neu getrennter."""
    from shapely.geometry import Polygon

    nodes = {e["id"]: e for e in elements if isinstance(e, dict) and e.get("type") == "node"}
    next_id = max(nodes) + 1 if nodes else 1
    ways = [e for e in elements if isinstance(e, dict) and e.get("type") == "way"]
    done = 0
    for spec in UEBERBAUUNGEN:
        aid = spec["alkis_id"]
        if any(w.get("tags", {}).get("alkis:ueberbauung") == aid for w in ways):
            log("Ueberbauung %s: schon getrennt." % spec["name"])
            continue
        way = next((w for w in ways if w.get("tags", {}).get("alkis:id") == aid), None)
        if way is None:
            log("Ueberbauung %s: Gebaeude %s fehlt in den Daten - uebersprungen." % (spec["name"], aid))
            continue
        poly = Polygon([_xy(nodes[n]["lat"], nodes[n]["lon"]) for n in way["nodes"] if n in nodes])
        corr = Polygon([_xy(la, lo) for la, lo in spec["korridor"]])
        bridge = poly.intersection(corr)
        ground = poly.difference(corr)
        if bridge.is_empty or bridge.area < 10.0:
            log("Ueberbauung %s: Korridor trifft den Grundriss nicht - uebersprungen." % spec["name"])
            continue

        def parts(g):
            return [p for p in (g.geoms if hasattr(g, "geoms") else [g]) if p.geom_type == "Polygon" and p.area > 5.0]

        def ring_nodes(p):
            nonlocal next_id
            ids = []
            for x, y in list(p.exterior.coords)[:-1]:
                la, lo = _latlon(x, y)
                elements.append({"type": "node", "id": next_id, "lat": la, "lon": lo})
                ids.append(next_id)
                next_id += 1
            return ids + [ids[0]]

        base_tags = dict(way.get("tags", {}))
        ground_parts = parts(ground)
        # Bodenteil(e): der bestehende Way behaelt ID und Tags (Adressen, LoD2-Hoehe).
        first = True
        for p in ground_parts:
            ids = ring_nodes(p)
            if first:
                way["nodes"] = ids
                way["tags"]["alkis:ueberbauung"] = aid
                first = False
            else:
                tags = dict(base_tags, **{"alkis:id": aid + "-teil", "alkis:ueberbauung": aid})
                elements.append({"type": "way", "id": next_id, "nodes": ids, "tags": tags})
                next_id += 1
        for p in parts(bridge):
            tags = dict(base_tags, **{"alkis:id": aid + "-bruecke", "alkis:ueberbauung": aid,
                                      "min_height": "%g" % spec["min_height_m"]})
            elements.append({"type": "way", "id": next_id, "nodes": ring_nodes(p), "tags": tags})
            next_id += 1
        log("Ueberbauung %s: %.0f m2 -> Boden %s m2, Bruecke %.0f m2 ab %.1f m lichter Hoehe."
            % (spec["name"], poly.area, [round(p.area) for p in ground_parts], bridge.area, spec["min_height_m"] - 1.5))
        done += 1
    return done


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, "Data", "Raw", "ALKIS", "wiesbaden.alkis.lod2.json")
    with open(path, encoding="utf-8") as f:
        doc = json.load(f)
    n = apply_ueberbauungen(doc["elements"])
    if n == 0:
        print("Nichts zu tun: %s" % path)
        return 0
    backup = path + ".vor_ueberbauung"
    if not os.path.exists(backup):
        shutil.copyfile(path, backup)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(doc, f, ensure_ascii=False)
    print("Geschrieben: %s (%d Ueberbauung(en) getrennt, Sicherung %s)" % (path, n, os.path.basename(backup)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
