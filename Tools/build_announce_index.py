"""Ordnet die vorgerenderten Halteansagen den Halten EINER Linie ZU - ueber den
HALTENAMEN, nicht ueber die Reihenfolge.

Warum das noetig wurde: die Wellen heissen nach ihrer Position in der damaligen
Linie-6-Liste (A_00..A_18, die Linie war bis "Mudra-Kaserne" gebaut). Seit die
Linie 6 bis Mainz-Gonsenheim durchgeht, hat sie 40 Halte. Eine Zuordnung nach
Index spielte damit an der 5. Halte den Text von "Adlerstrasse" ab, obwohl dort
etwas anderes liegt.

Ergebnis: Data/Raw/Bus/announce_line<ref>.json
    {"stops": [{"index": 0, "name": "Nordfriedhof", "asset": "/Game/..."}, ...]}
Halte ohne Welle fehlen in der Liste und bleiben still - das ist gewollt, eine
falsche Ansage waere schlimmer als keine.

Quellen fuer Wellen, in dieser Reihenfolge:
  1. Tools/announce_text_line<ref>.json  (per Linie gerendert, Ordner
     /Game/Audio/Bus/Announce/L<ref>/A_NN - siehe make_bus_announcements.py)
  2. Tools/announce_text.json            (die alte, linienlose Ablage
     /Game/Audio/Bus/Announce/A_NN) - gilt nur, wenn die .uasset wirklich da ist.
Der Abgleich laeuft ueber den Namen, deshalb bekommt Linie 3 die zwoelf Halte
gesprochen, die sie mit Linie 6 gemeinsam hat (Hauptbahnhof, Landeshaus, ...).

Aufruf: python Tools/build_announce_index.py [ref ...]
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__)).replace("\\", "/")
PROJ = HERE.rsplit("/Tools", 1)[0]
BUS = PROJ + "/Data/Raw/Bus"
CONTENT = PROJ + "/Content/Audio/Bus/Announce"


def asset_exists(obj_path):
    """'/Game/Audio/Bus/Announce/A_00' -> Content/Audio/Bus/Announce/A_00.uasset"""
    rel = obj_path.replace("/Game/", "", 1)
    return os.path.exists(os.path.join(PROJ, "Content", rel + ".uasset"))


def legacy_waves():
    """Alte Ablage: Name -> /Game/Audio/Bus/Announce/<asset> (nur vorhandene)."""
    out = {}
    path = HERE + "/announce_text.json"
    if not os.path.exists(path):
        return out
    with open(path, encoding="utf-8") as f:
        for it in json.load(f):
            asset = it.get("asset")
            if not asset:
                continue
            obj = "/Game/Audio/Bus/Announce/" + asset
            if asset_exists(obj):
                out[it["name"]] = obj
    return out


def line_waves(ref):
    """Per Linie gerendert: Name -> /Game/Audio/Bus/Announce/L<ref>/<asset>."""
    out = {}
    path = "%s/announce_text_line%s.json" % (HERE, ref)
    if not os.path.exists(path):
        return out
    with open(path, encoding="utf-8") as f:
        for it in json.load(f):
            asset = it.get("asset")
            if not asset:
                continue
            obj = "/Game/Audio/Bus/Announce/L%s/%s" % (ref, asset)
            if asset_exists(obj):
                out[it["name"]] = obj
    return out


def build(ref):
    line_path = "%s/line%s.json" % (BUS, ref)
    if not os.path.exists(line_path):
        raise SystemExit("Liniendatei fehlt: %s" % line_path)
    with open(line_path, encoding="utf-8") as f:
        line = json.load(f)
    names = line.get("stop_names", [])

    pool = {}
    pool.update(legacy_waves())
    pool.update(line_waves(ref))   # per Linie gerendert hat Vorrang

    entries, silent = [], []
    for i, name in enumerate(names):
        asset = pool.get(name)
        if asset:
            entries.append({"index": i, "name": name, "asset": asset})
        else:
            silent.append(name)

    out_path = "%s/announce_line%s.json" % (BUS, ref)
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump({"line": line.get("ref", ref), "stops": entries}, f,
                  ensure_ascii=False, indent=1)

    print("Linie %-2s: %d/%d Halte mit Ansage, %d ohne"
          % (ref, len(entries), len(names), len(silent)))
    for e in entries[:4]:
        print("   %2d %-40s %s" % (e["index"], e["name"], e["asset"]))
    if len(entries) > 4:
        print("   ...")
    if silent:
        print("   still: " + ", ".join(silent[:8]) + (" ..." if len(silent) > 8 else ""))
    print("   -> %s" % os.path.basename(out_path))


def main():
    refs = sys.argv[1:] or ["6", "3"]
    for ref in refs:
        build(ref)


if __name__ == "__main__":
    main()
