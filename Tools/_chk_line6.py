import json, math
P=r"C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Data\Raw\Bus\line6.json"
d=json.load(open(P,encoding="utf-8"))
path=d["path"]; stops=d["stops"]
def dist(a,b):
    dy=(b[0]-a[0])*111320.0; dx=(b[1]-a[1])*111320.0*math.cos(math.radians(a[0])); return math.hypot(dx,dy)
print("START (Nordfriedhof?):", path[0], " ENDE:", path[-1])
# min-Abstand jedes Halts zur Polylinie
def mind(s):
    return min(dist(s,p) for p in path)
onroute=[s for s in stops if mind(s)<=40]
off=[s for s in stops if mind(s)>40]
print("Halte gesamt=%d, auf Route (<=40m)=%d, abseits=%d"%(len(stops),len(onroute),len(off)))
print("erste 3 auf Route:", onroute[:3])
print("erste 3 abseits (Mainz/andere Richtung?):", [(round(s[0],4),round(s[1],4)) for s in off[:3]])
# nur die On-Route-Halte behalten, nach Position entlang der Route sortieren
# (Projektion: kumulierte Bogenlaenge des naechsten Polylinien-Punkts)
cum=[0.0]
for i in range(1,len(path)): cum.append(cum[-1]+dist(path[i-1],path[i]))
def arclen(s):
    bi=min(range(len(path)), key=lambda i: dist(s,path[i])); return cum[bi]
onroute.sort(key=arclen)
d["stops"]=onroute
json.dump(d, open(P,"w",encoding="utf-8"), ensure_ascii=False)
print("Route-Laenge=%.0fm, gefilterte+sortierte Halte=%d"%(cum[-1],len(onroute)))