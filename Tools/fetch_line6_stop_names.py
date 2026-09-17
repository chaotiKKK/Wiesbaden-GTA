"""Holt die echten ESWE-Linie-6-Haltestellennamen aus OSM (Overpass) und ordnet
sie den 19 Halte-Koordinaten in Data/Raw/Bus/line6.json zu (naechster benannter
Halt je Koordinate). Nur Lesen/Auswerten - schreibt NICHTS, gibt eine Liste aus,
die dann geprueft und uebernommen wird.
"""
import json
import math
import urllib.request

HERE = __file__.replace("\\", "/").rsplit("/", 2)[0]  # .../WiesbadenReal
LINE = HERE + "/Data/Raw/Bus/line6.json"

with open(LINE, encoding="utf-8") as f:
    data = json.load(f)
stops = data["stops"]  # [[lat,lon], ...]
lats = [s[0] for s in stops]
lons = [s[1] for s in stops]
bbox = (min(lats) - 0.01, min(lons) - 0.01, max(lats) + 0.01, max(lons) + 0.01)

# Alle benannten Halte/Bahnsteige der ref=6-Busrelationen im Streckenkasten.
query = f"""
[out:json][timeout:120];
(
  relation["route"="bus"]["ref"="6"]({bbox[0]},{bbox[1]},{bbox[2]},{bbox[3]});
);
node(r)["name"];
out body;
"""

req = urllib.request.Request(
    "https://overpass-api.de/api/interpreter",
    data=("data=" + urllib.parse.quote(query)).encode("utf-8"),
    headers={"User-Agent": "WiesbadenReal/1.0 (line6 stop names)"},
)
print("Overpass-Abfrage laeuft ...")
with urllib.request.urlopen(req, timeout=180) as resp:
    osm = json.load(resp)

named = []
for el in osm.get("elements", []):
    if el.get("type") == "node" and "lat" in el and el.get("tags", {}).get("name"):
        named.append((el["lat"], el["lon"], el["tags"]["name"]))
print(f"Benannte Halte-Knoten aus OSM: {len(named)}")


def dist_m(a_lat, a_lon, b_lat, b_lon):
    # grobe aequirektangulaere Naeherung, reicht fuer <1 km
    x = math.radians(b_lon - a_lon) * math.cos(math.radians((a_lat + b_lat) / 2))
    y = math.radians(b_lat - a_lat)
    return math.hypot(x, y) * 6371000.0


result = []
for i, (lat, lon) in enumerate(stops):
    best = None
    for (nlat, nlon, name) in named:
        d = dist_m(lat, lon, nlat, nlon)
        if best is None or d < best[0]:
            best = (d, name)
    result.append((i, best[1] if best else "?", best[0] if best else -1))

print("\nIndex | Abstand m | Name")
for (i, name, d) in result:
    print(f"{i:2d} | {d:8.1f} | {name}")

names = [r[1] for r in result]
print("\nNamensliste (nur Name, in Reihenfolge):")
print(json.dumps(names, ensure_ascii=False, indent=0))

# Schreiben nur, wenn ALLE 19 Koordinaten sauber (< 80 m) einem benannten Halt
# zugeordnet wurden - sonst waeren falsche Namen schlimmer als keine.
maxd = max(r[2] for r in result)
if len(names) == len(stops) and maxd < 80.0 and "?" not in names:
    data["stop_names"] = names
    with open(LINE, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=1)
    print(f"\nline6.json aktualisiert: {len(names)} stop_names geschrieben "
          f"(groesster Abstand {maxd:.1f} m).")
else:
    print(f"\nNICHT geschrieben (groesster Abstand {maxd:.1f} m, {len(names)} Namen).")
