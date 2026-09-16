# Holt amtliche Fahrbahnflaechen (ATKIS Basis-DLM, tn-ro:RoadArea) aus der
# offenen OGC-API des Geoportals Hessen: echte Fahrbahn-Polygone + Strassenname
# ("text"). Ergaenzt OSM (komplettes Netz, aber Breite nur ~5%) um amtliche
# Breiten/Formen der Hauptstrassen. Lizenz dl-de-zero-2.0.
#
# Aufruf: python fetch_atkis_roads.py "minLon,minLat,maxLon,maxLat" out.geojson
import urllib.request, ssl, json, sys

BASE = "https://www.geoportal.hessen.de/spatial-objects/723/collections/tn-ro:RoadArea/items"
CTX = ssl.create_default_context()


def get(url):
    r = urllib.request.urlopen(
        urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0", "Accept": "application/geo+json"}),
        timeout=120, context=CTX)
    return json.loads(r.read().decode("utf-8", "replace"))


def main():
    bbox = sys.argv[1] if len(sys.argv) > 1 else "8.14,50.02,8.36,50.14"
    outpath = sys.argv[2] if len(sys.argv) > 2 else "atkis_roads.geojson"
    LIMIT = 500
    feats = []
    offset = 0
    while True:
        d = get("%s?bbox=%s&limit=%d&offset=%d" % (BASE, bbox, LIMIT, offset))
        got = d.get("features", [])
        for f in got:
            feats.append({
                "type": "Feature",
                "geometry": f.get("geometry"),
                "properties": {"name": (f.get("properties") or {}).get("text"),
                               "id": (f.get("properties") or {}).get("localId")},
            })
        print("offset %d: +%d -> %d (numberMatched %s)" % (
            offset, len(got), len(feats), d.get("numberMatched")), flush=True)
        if len(got) < LIMIT:   # letzte Seite
            break
        offset += LIMIT
    named = sum(1 for f in feats if f["properties"]["name"])
    with open(outpath, "w", encoding="utf-8") as fp:
        json.dump({"type": "FeatureCollection", "features": feats}, fp, ensure_ascii=False)
    print("WROTE %d Fahrbahnflaechen (%d mit Name) -> %s" % (len(feats), named, outpath))


main()
