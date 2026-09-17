"""Holt Wald-MULTIPOLYGON-Relationen (landuse=forest / natural=wood) aus OSM nach.

Befund 2026-09-17: wiesbaden.osm.json enthaelt 1.418 Wald-Ways, aber 0 Wald-
Relationen - die grossen Waelder (Stadtwald/Taunus, Neroberg-Nordseite) sind in
OSM als Relationen erfasst und fehlten darum im Spiel. Der Waldfueller
(WiesbadenRegionAssets) liest nur Ways; deshalb werden die Aussenringe der
Relationen hier zu synthetischen geschlossenen Ways (landuse=forest) mit eigenen
ID-Bereichen und in eine KOPIE der OSM-Datei geschrieben (Original bleibt).
Bake: WB_OSM_FILE=<kopie> (rebuild_city.py).
"""
import json, math, os, urllib.request, urllib.parse

SRC = "Data/Raw/OSM/wiesbaden.osm.json"
DST = "Data/Raw/OSM/wiesbaden.osm.forest.json"
CACHE = "Data/Raw/OSM/forest_relations.overpass.json"

d = json.load(open(SRC, encoding="utf-8"))
els = d["elements"] if isinstance(d, dict) else d

# Auf die gespielte Stadt klippen: ~6 km um den Ursprung (50.0824 / 8.24). Ungeklippt
# fuellt der Waldfueller den ganzen Taunus (8,6 Mio. Baeume, Bake stirbt beim Speichern).
# Dasselbe Rechteck ist auch der Abfragekasten: Overpass liefert jede Relation, deren
# Kasten es schneidet, Randstuecke gehen also nicht verloren.
CENTER = (50.0824, 8.24)
CLIP = (CENTER[0] - 0.055, CENTER[1] - 0.085, CENTER[0] + 0.055, CENTER[1] + 0.085)   # lat0, lon0, lat1, lon1

def clip_ring(ring):
    """Sutherland-Hodgman gegen das Rechteck CLIP; liefert geschlossenen Ring oder []."""
    def inside(p, edge):
        k, v, keep_ge = edge
        return (p[k] >= v) if keep_ge else (p[k] <= v)
    def intersect(a, b, edge):
        k, v, _ = edge
        t = (v - a[k]) / ((b[k] - a[k]) or 1e-12)
        return (a[0] + t * (b[0] - a[0]), a[1] + t * (b[1] - a[1]))
    poly = ring[:-1] if ring[0] == ring[-1] else list(ring)
    for edge in ((0, CLIP[0], True), (1, CLIP[1], True), (0, CLIP[2], False), (1, CLIP[3], False)):
        out = []
        for i in range(len(poly)):
            cur, prev = poly[i], poly[i - 1]
            if inside(cur, edge):
                if not inside(prev, edge): out.append(intersect(prev, cur, edge))
                out.append(cur)
            elif inside(prev, edge):
                out.append(intersect(prev, cur, edge))
        poly = out
        if len(poly) < 3: return []
    return poly + [poly[0]]

def ring_area_m2(ring):
    lat0 = sum(c[0] for c in ring) / len(ring); k = 111320.0
    xy = [(c[1] * k * math.cos(math.radians(lat0)), c[0] * k) for c in ring]
    a = 0.0
    for i in range(len(xy) - 1):
        a += xy[i][0] * xy[i + 1][1] - xy[i + 1][0] * xy[i][1]
    return abs(a) / 2

q = f"""[out:json][timeout:180];
(
  relation["landuse"="forest"]({CLIP[0]},{CLIP[1]},{CLIP[2]},{CLIP[3]});
  relation["natural"="wood"]({CLIP[0]},{CLIP[1]},{CLIP[2]},{CLIP[3]});
);
out geom;"""
if os.path.exists(CACHE):
    osm = json.load(open(CACHE, encoding="utf-8")); print("Overpass-Cache:", CACHE)
else:
    req = urllib.request.Request("https://overpass-api.de/api/interpreter",
        data=("data=" + urllib.parse.quote(q)).encode(), headers={"User-Agent": "WiesbadenReal/1.0 forest relations"})
    print("Overpass (Stadtrechteck) ...")
    osm = json.load(urllib.request.urlopen(req, timeout=300))
    json.dump(osm, open(CACHE, "w", encoding="utf-8")); print("gecacht:", CACHE)
rels = [e for e in osm.get("elements", []) if e["type"] == "relation"]
print("Wald-Relationen:", len(rels))

def assemble_rings(ways):
    """Verkettet Wege ueber gemeinsame Endpunkte zu geschlossenen Ringen."""
    segs = [list(w) for w in ways if len(w) >= 2]
    rings = []
    while segs:
        ring = segs.pop(0)
        changed = True
        while changed and (ring[0] != ring[-1]):
            changed = False
            for i, s in enumerate(segs):
                if s[0] == ring[-1]: ring += s[1:]; segs.pop(i); changed = True; break
                if s[-1] == ring[-1]: ring += list(reversed(s))[1:]; segs.pop(i); changed = True; break
                if s[-1] == ring[0]: ring = s[:-1] + ring; segs.pop(i); changed = True; break
                if s[0] == ring[0]: ring = list(reversed(s))[:-1] + ring; segs.pop(i); changed = True; break
        if ring[0] == ring[-1] and len(ring) >= 4:
            rings.append(ring)
    return rings

NODE_BASE, WAY_BASE = 9_000_000_000_000, 9_100_000_000_000
new_nodes, new_ways = [], []
nid = wid = 0
for r in rels:
    outers = []
    for m in r.get("members", []):
        if m["type"] == "way" and m.get("role", "outer") in ("outer", "") and "geometry" in m:
            outers.append([(g["lat"], g["lon"]) for g in m["geometry"]])
    for raw_ring in assemble_rings(outers):
        ring = clip_ring(raw_ring)
        if len(ring) < 4:
            continue
        ids = []
        for (la, lo) in ring[:-1]:
            nid += 1; new_nodes.append({"type": "node", "id": NODE_BASE + nid, "lat": la, "lon": lo}); ids.append(NODE_BASE + nid)
        ids.append(ids[0])
        wid += 1
        new_ways.append({"type": "way", "id": WAY_BASE + wid, "nodes": ids,
                         "tags": {"landuse": "forest", "source:relation": str(r["id"]), "name": r.get("tags", {}).get("name", "")}})
print("synthetische Wald-Ways (geklippt):", len(new_ways), "Knoten:", len(new_nodes))
nodes_new = {n["id"]: (n["lat"], n["lon"]) for n in new_nodes}
total_m2 = sum(ring_area_m2([nodes_new[i] for i in w["nodes"]]) for w in new_ways)
print("Waldflaeche nachgeholt: %.1f km^2 -> ca. %d Fuell-Baeume bei 7,5-m-Raster" % (total_m2 / 1e6, total_m2 / 56.25))

# Deckt es den Neroberg jetzt?
def pip(p, cs):
    x, y = p[1], p[0]; inside = False
    for i in range(len(cs)):
        y1, x1 = cs[i]; y2, x2 = cs[(i + 1) % len(cs)]
        if (y1 > y) != (y2 > y) and x < (x2 - x1) * (y - y1) / (y2 - y1 + 1e-12) + x1: inside = not inside
    return inside
tests = {"Gipfel": (50.1005, 8.2255), "N Gipfel": (50.1030, 8.2250), "O": (50.1005, 8.2300), "NO": (50.1030, 8.2310), "W": (50.1005, 8.2200)}
for name, p in tests.items():
    hit = [w["tags"]["name"] or w["tags"]["source:relation"] for w in new_ways if pip(p, [nodes_new[i] for i in w["nodes"]])]
    print(f"  {name:9s}: {hit if hit else '-- kein Wald --'}")

els.extend(new_nodes); els.extend(new_ways)
json.dump(d, open(DST, "w", encoding="utf-8"), ensure_ascii=False)
print("geschrieben:", DST)
