"""Liest die POSITION-min/max aller Primitive aus einer GLB (nur JSON-Header,
kein Buffer-Dekodieren) und meldet Gesamt-AABB, Pro-Primitiv-AABB und Ausreisser.
So laesst sich klaeren, ob nahezu-wuerfelige Bounds von Streugeometrie kommen.
"""
import struct, json, sys

path = sys.argv[1] if len(sys.argv) > 1 else r"C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Data\Raw\Bus\eswe_bus.glb"
with open(path, "rb") as f:
    data = f.read()

magic, version, length = struct.unpack("<III", data[:12])
assert magic == 0x46546C67, "kein glTF/GLB"
off = 12
gltf = None
while off < length:
    clen, ctype = struct.unpack("<II", data[off:off+8])
    body = data[off+8:off+8+clen]
    if ctype == 0x4E4F534A:  # JSON
        gltf = json.loads(body.decode("utf-8"))
    off += 8 + clen

acc = gltf["accessors"]
meshes = gltf.get("meshes", [])
prims = []
gmin = [1e30]*3; gmax = [-1e30]*3
for mi, m in enumerate(meshes):
    for pi, p in enumerate(m.get("primitives", [])):
        ai = p.get("attributes", {}).get("POSITION")
        if ai is None:
            continue
        a = acc[ai]
        mn = a.get("min"); mx = a.get("max")
        if not mn or not mx:
            continue
        size = [mx[k]-mn[k] for k in range(3)]
        cen = [(mx[k]+mn[k])/2 for k in range(3)]
        prims.append((mi, pi, mn, mx, size, cen, a.get("count", 0)))
        for k in range(3):
            gmin[k] = min(gmin[k], mn[k]); gmax[k] = max(gmax[k], mx[k])

gsize = [gmax[k]-gmin[k] for k in range(3)]
print("GESAMT-AABB min=%s max=%s" % (["%.3f"%v for v in gmin], ["%.3f"%v for v in gmax]))
print("GESAMT-GROESSE X=%.3f Y=%.3f Z=%.3f" % tuple(gsize))
print("Primitive: %d" % len(prims))
print()
# nach Vertex-Zahl sortiert (grosse = Hauptkoerper)
prims_by_count = sorted(prims, key=lambda t: -t[6])
print("Pro Primitiv (nach Vertexzahl):")
for (mi, pi, mn, mx, size, cen, cnt) in prims_by_count:
    print("  mesh%d prim%d  cnt=%6d  size X=%.2f Y=%.2f Z=%.2f  center X=%.2f Y=%.2f Z=%.2f" %
          (mi, pi, cnt, size[0], size[1], size[2], cen[0], cen[1], cen[2]))

# Ausreisser: Primitive, deren Zentrum weit vom Median liegt
import statistics
if len(prims) > 1:
    med = [statistics.median([p[5][k] for p in prims]) for k in range(3)]
    print("\nMedian-Zentrum: X=%.2f Y=%.2f Z=%.2f" % tuple(med))
    print("Ausreisser (Zentrum >2x groesste Median-Dim entfernt):")
    ref = max(statistics.median([p[4][k] for p in prims]) for k in range(3))
    for (mi, pi, mn, mx, size, cen, cnt) in prims:
        d = max(abs(cen[k]-med[k]) for k in range(3))
        if d > 2*ref:
            print("  mesh%d prim%d cnt=%d dist=%.2f  min=%s max=%s" %
                  (mi, pi, cnt, d, ["%.2f"%v for v in mn], ["%.2f"%v for v in mx]))
