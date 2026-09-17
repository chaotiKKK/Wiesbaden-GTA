"""Holt die OSM-Buslinien (route=bus) vollstaendig: Wege MIT Knotenkoordinaten
und die benannten Halte-Knoten der Relation.

Warum die OSM-API und nicht Overpass: gebraucht wird die Relation MIT ihren
Mitgliedern in Reihenfolge UND den Koordinaten aller Knoten. Dafuer gibt es
`/api/0.6/relation/<id>/full.json` in einem Zug (Relation + Wege + Knoten samt
Tags). Overpass liefert dasselbe nur mit `out geom` auf die Relation, und genau
dieser Abruf lief zweimal in einen 504 (Gateway Timeout) - die Stadt-Abfrage ist
dafuer zu gross. Die Rohdatei wird unveraendert abgelegt, damit ein erneuter Bau
ohne Netzzugriff geht; der Bau selbst liegt in Tools/build_bus_line.py.

Aufruf:
    python Tools/fetch_bus_lines.py          # alle eingetragenen Linien
    python Tools/fetch_bus_lines.py 3 6      # nur diese

Ergebnis: Data/Raw/OSM/bus_route_<RelId>.json (OSM-API-Format, nicht anfassen).
"""
import json
import os
import sys
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__)).replace("\\", "/")
PROJ = HERE.rsplit("/Tools", 1)[0]
OSM_DIR = PROJ + "/Data/Raw/OSM"

# Welche Relation ist die Linie? Die IDs stammen aus einer Overpass-Suche
# (rel["route"="bus"]["ref"~"^(3|6)$"] im Stadtkasten). Je Fahrtrichtung gibt es
# eine eigene Relation, und aeltere/neuere Fassungen liegen nebeneinander - hier
# steht deshalb bewusst eine Tabelle statt einer Suche: so ist festgelegt, WELCHE
# Fassung gespielt wird, und ein erneuter Abruf liefert nicht ploetzlich eine
# andere. 17483739/18290457 sind die jeweils neueren (hoehere ID) Richtungen.
LINE_RELATIONS = {
    "6": (17483739, "Wiesbaden Nordfriedhof -> Mainz Gonsenheim Wildpark"),
    "3": (18290457, "Wiesbaden Nordfriedhof -> Biebrich Rheinufer (via Welfenstrasse)"),
}

API = "https://api.openstreetmap.org/api/0.6/relation/%d/full.json"
UA = {"User-Agent": "WiesbadenReal/1.0 (Buslinien fuer die Simulation)"}


def fetch(rel_id, force=False):
    out = "%s/bus_route_%d.json" % (OSM_DIR, rel_id)
    if os.path.exists(out) and not force:
        with open(out, encoding="utf-8") as f:
            d = json.load(f)
        if any(e.get("type") == "way" for e in d.get("elements", [])):
            print("  Relation %d liegt schon vor (%s, %d Elemente)"
                  % (rel_id, os.path.basename(out), len(d["elements"])))
            return d
    print("  hole Relation %d ..." % rel_id)
    req = urllib.request.Request(API % rel_id, headers=UA)
    with urllib.request.urlopen(req, timeout=300) as resp:
        d = json.load(resp)
    os.makedirs(OSM_DIR, exist_ok=True)
    with open(out, "w", encoding="utf-8") as f:
        json.dump(d, f, ensure_ascii=False)
    print("  %d Elemente -> %s" % (len(d.get("elements", [])), os.path.basename(out)))
    return d


def main():
    want = sys.argv[1:] or sorted(LINE_RELATIONS)
    for ref in want:
        if ref not in LINE_RELATIONS:
            raise SystemExit("Linie %s ist nicht eingetragen (LINE_RELATIONS)" % ref)
        rel_id, desc = LINE_RELATIONS[ref]
        print("Linie %s: %s" % (ref, desc))
        d = fetch(rel_id, force=("--force" in sys.argv))
        rel = next(e for e in d["elements"] if e["type"] == "relation")
        ways = sum(1 for m in rel["members"] if m["type"] == "way")
        stops = sum(1 for m in rel["members"] if m["type"] == "node" and m.get("role", "").startswith("stop"))
        print("    %d Weg-Mitglieder, %d Halte-Mitglieder, interval=%s"
              % (ways, stops, rel["tags"].get("interval", "-")))
    print("fertig.")


if __name__ == "__main__":
    main()
