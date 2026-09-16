# Injiziert die AMTLICHEN LoD2-CityGML-Werte (Hoehe + Dachform + Dachhoehe) je
# Gebaeude per alkis:id in die Overpass-ALKIS-JSON. Ersetzt das prozedurale
# Hoehen- UND Dach-Raten in einem Zug. Quelle: parse_citygml_roofs.py.
#
# Der C++-Baker nutzt height (Prio 1), roof:shape (ParseRoofShape) und roof:height
# (ParseLengthMeters) direkt -> kein C++-Eingriff noetig.
#
# Aufruf: python apply_lod2_citygml.py [roofs.json] [alkis_in] [alkis_out]
import json, os, sys


def _fmt(x):
    r = round(float(x), 2)
    return str(int(r)) if r == int(r) else ("%.2f" % r).rstrip("0").rstrip(".")


def enrich_elements(elements, data):
    """Setzt height/roof:shape/roof:height je Gebaeude-Way mit bekannter alkis:id.
    data: {alkis_id: {"h":.., "roof":.., "rh":..}}. Mutiert in-place.
    Gibt (n_height, n_roof, n_roofheight) zurueck."""
    nh = nr = nrh = 0
    for el in elements:
        if not isinstance(el, dict) or el.get("type") != "way":
            continue
        tags = el.get("tags")
        if not isinstance(tags, dict):
            continue
        aid = tags.get("alkis:id")
        if not aid:
            continue
        info = data.get(aid)
        if not info:
            continue
        h = info.get("h")
        if h is not None and float(h) > 1.0:
            tags["height"] = _fmt(h)
            nh += 1
        shape = info.get("roof")
        if shape:
            tags["roof:shape"] = shape          # amtlich, ueberschreibt evtl. alten Tag
            nr += 1
            rh = info.get("rh")
            if shape != "flat" and rh is not None and float(rh) > 0.0:
                tags["roof:height"] = _fmt(rh)
                nrh += 1
    return nh, nr, nrh


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    proj = os.path.dirname(here)
    roofs_path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        proj, "Data", "Raw", "LoD2", "wiesbaden_roofs.json")
    alkis_in = sys.argv[2] if len(sys.argv) > 2 else os.path.join(
        proj, "Data", "Raw", "ALKIS", "wiesbaden.alkis.json")
    alkis_out = sys.argv[3] if len(sys.argv) > 3 else os.path.join(
        proj, "Data", "Raw", "ALKIS", "wiesbaden.alkis.lod2.json")

    data = json.load(open(roofs_path, encoding="utf-8"))
    print("CityGML-Gebaeude geladen: %d (%s)" % (len(data), roofs_path))
    doc = json.load(open(alkis_in, encoding="utf-8"))
    els = doc.get("elements", [])
    total = sum(1 for e in els if isinstance(e, dict) and e.get("type") == "way")

    nh, nr, nrh = enrich_elements(els, data)
    json.dump(doc, open(alkis_out, "w", encoding="utf-8"), ensure_ascii=False)

    from collections import Counter
    dist = Counter(v.get("roof") for v in data.values())
    print("Ways: %d | Hoehe: %d (%.0f%%) | Dachform: %d | Dachhoehe: %d"
          % (total, nh, 100.0 * nh / max(1, total), nr, nrh))
    print("Dachform-Verteilung (amtlich):", dict(dist))
    print("Geschrieben -> %s" % alkis_out)
    probe = {"DEHE062000019vPE": "140", "DEHE062000019vDD": "144",
             "DEHE062000019vDI": "146", "DEHE062000019vBJ": "148 (Turm)"}
    for aid, nrn in probe.items():
        if aid in data:
            print("  Platter %s -> %s" % (nrn, data[aid]))


if __name__ == "__main__":
    main()
