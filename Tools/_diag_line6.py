import json, math
d=json.load(open(r"C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Data\Raw\Bus\line6.json",encoding="utf-8"))
p=d["path"]
def dist(a,b):
    dy=(b[0]-a[0])*111320.0; dx=(b[1]-a[1])*111320.0*math.cos(math.radians(a[0])); return math.hypot(dx,dy)
jumps=[dist(p[i],p[i+1]) for i in range(len(p)-1)]
big=[j for j in jumps if j>150]
print("Punkte=%d, Spruenge>150m=%d, groesster=%.0fm, Summe der Spruenge=%.0fm"%(len(p),len(big),max(jumps),sum(big)))
print("Halte(Reihenfolge, erste 5):", [ (round(s[0],4),round(s[1],4)) for s in d["stops"][:5] ])