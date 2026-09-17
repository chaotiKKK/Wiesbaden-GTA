import json, math
from collections import defaultdict
SRC = r"C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Data\Raw\OSM\line6_17483739.json"
OUT = r"C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Data\Raw\Bus\line6.json"

def rnd(pt): return (round(pt[0], 7), round(pt[1], 7))

data = json.load(open(SRC, encoding='utf-8'))
rel = next(e for e in data['elements'] if e['type'] == 'relation')
members = rel['members']
ways = []
for m in members:
    if m['type'] == 'way' and m.get('geometry') and (m.get('role') in ('', None)):
        g = [(p['lat'], p['lon']) for p in m['geometry']]
        if len(g) >= 2: ways.append(g)

ep = defaultdict(list)
for i, g in enumerate(ways):
    ep[rnd(g[0])].append((i, 0)); ep[rnd(g[1])].append((i, 1))

# Start = noerdlichster Endpunkt mit Grad 1 (Nordfriedhof-Terminus)
deg1 = [k for k, v in ep.items() if len(v) == 1]
if deg1:
    start_pt = max(deg1, key=lambda k: k[0]); start_way, start_end = ep[start_pt][0]
else:
    start_way, start_end = 0, 0

used = [False] * len(ways)
path = []
def add_way(g, flip):
    seq = list(reversed(g)) if flip else g
    if path and rnd(path[-1]) == rnd(seq[0]): path.extend(seq[1:])
    else: path.extend(seq)

cur, flip = start_way, (start_end == 1)
while True:
    used[cur] = True
    add_way(ways[cur], flip)
    end_pt = rnd(path[-1])
    nxt = next(((wi, we) for (wi, we) in ep.get(end_pt, []) if not used[wi]), None)
    if nxt is None: break
    cur, we = nxt; flip = (we == 1)

stops = [(m['lat'], m['lon']) for m in members if m['type'] == 'node' and 'stop' in (m.get('role') or '')]
out = {'ref': rel['tags'].get('ref'), 'from': rel['tags'].get('from'), 'to': rel['tags'].get('to'),
       'colour': rel['tags'].get('colour'),
       'path': [[round(a,7), round(b,7)] for (a,b) in path],
       'stops': [[round(a,7), round(b,7)] for (a,b) in stops]}
json.dump(out, open(OUT, 'w', encoding='utf-8'), ensure_ascii=False)

def dist(a, b):
    dy=(b[0]-a[0])*111320.0; dx=(b[1]-a[1])*111320.0*math.cos(math.radians(a[0])); return math.hypot(dx,dy)
jumps=[dist(path[i],path[i+1]) for i in range(len(path)-1)]
big=[j for j in jumps if j>150]
L=sum(jumps)
print("###L6### Wege benutzt=%d/%d, Punkte=%d, Spruenge>150m=%d, groesster=%.0fm, Gesamtlaenge=%.0fm, Halte=%d"
      % (sum(used), len(ways), len(path), len(big), max(jumps) if jumps else 0, L, len(stops)))