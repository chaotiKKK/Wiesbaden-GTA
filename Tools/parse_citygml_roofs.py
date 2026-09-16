# Streamt die 2,76-GB-LoD2-CityGML aus dem ZIP (ohne sie auf Platte/RAM voll zu
# laden) und extrahiert je Gebaeude die amtliche Dachform + Dachhoehe + Hoehe.
# Ausgabe: { alkis_id: {"roof":..., "rh":..., "h":..., "code":...} }
#
# Aufruf: python parse_citygml_roofs.py <zip> <entry.gml> <out.json> [max_buildings]
import zipfile, json, sys, os
from collections import Counter
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from citygml_roofs import parse_building_block

OPEN = "<bldg:Building"
CLOSE = "</bldg:Building>"


def main():
    zpath = sys.argv[1] if len(sys.argv) > 1 else r"C:\Users\HP\Downloads\Wiesbaden-LoD2.zip"
    entry = sys.argv[2] if len(sys.argv) > 2 else "Wiesbaden-LoD2.gml"
    outpath = sys.argv[3] if len(sys.argv) > 3 else "citygml_roofs.json"
    limit = int(sys.argv[4]) if len(sys.argv) > 4 else 0  # 0 = alle

    out = {}
    dist = Counter()
    buf = ""
    n = 0
    zf = zipfile.ZipFile(zpath)
    with zf.open(entry) as f:
        while True:
            chunk = f.read(4 * 1024 * 1024)
            if not chunk:
                break
            buf += chunk.decode("utf-8", "replace")
            while True:
                s = buf.find(OPEN)
                e = buf.find(CLOSE)
                if s == -1 or e == -1 or e < s:
                    break
                block = buf[s:e + len(CLOSE)]
                buf = buf[e + len(CLOSE):]
                aid, info = parse_building_block(block)
                if aid:
                    out[aid] = info
                    dist[info["roof"]] += 1
                    n += 1
                    if n % 20000 == 0:
                        print("  %d Gebaeude..." % n, flush=True)
                    if limit and n >= limit:
                        break
            if limit and n >= limit:
                break
    with open(outpath, "w", encoding="utf-8") as fp:
        json.dump(out, fp, ensure_ascii=False)
    rh = sum(1 for v in out.values() if "rh" in v)
    print("WROTE %d Gebaeude -> %s | mit Dachhoehe %d | Dachformen %s"
          % (len(out), outpath, rh, dict(dist)), flush=True)


main()
