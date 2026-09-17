"""Echte zusammengesetzte AABB einer GLB: laeuft den Szenegraph ab, wendet die
Node-Weltmatrix auf die 8 Ecken jeder Primitiv-Box (POSITION min/max) an und
vereinigt sie. Meldet zusaetzlich die Gesamtausrichtung (welche Achse = Laenge).
"""
import struct, json, sys

path = sys.argv[1] if len(sys.argv) > 1 else r"C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Data\Raw\Bus\eswe_bus.glb"
with open(path, "rb") as f:
    data = f.read()
magic, version, length = struct.unpack("<III", data[:12])
assert magic == 0x46546C67
off = 12; gltf = None
while off < length:
    clen, ctype = struct.unpack("<II", data[off:off+8])
    body = data[off+8:off+8+clen]
    if ctype == 0x4E4F534A:
        gltf = json.loads(body.decode("utf-8"))
    off += 8 + clen

acc = gltf["accessors"]; nodes = gltf["nodes"]; meshes = gltf["meshes"]

def mul(a, b):  # 4x4 * 4x4 (column-major flat, glTF convention)
    r = [0.0]*16
    for c in range(4):
        for row in range(4):
            r[c*4+row] = sum(a[k*4+row]*b[c*4+k] for k in range(4))
    return r

def trs_matrix(n):
    if "matrix" in n:
        return n["matrix"]
    t = n.get("translation", [0,0,0])
    q = n.get("rotation", [0,0,0,1])
    s = n.get("scale", [1,1,1])
    x,y,z,w = q
    xx,yy,zz = x*x,y*y,z*z; xy,xz,yz=x*y,x*z,y*z; wx,wy,wz=w*x,w*y,w*z
    R = [
        1-2*(yy+zz), 2*(xy+wz),   2*(xz-wy),   0,
        2*(xy-wz),   1-2*(xx+zz), 2*(yz+wx),   0,
        2*(xz+wy),   2*(yz-wx),   1-2*(xx+yy), 0,
        0,0,0,1]
    S = [s[0],0,0,0, 0,s[1],0,0, 0,0,s[2],0, 0,0,0,1]
    M = mul(R, S)
    M[12],M[13],M[14] = t[0],t[1],t[2]
    return M

def xform(m, p):
    return [m[0]*p[0]+m[4]*p[1]+m[8]*p[2]+m[12],
            m[1]*p[0]+m[5]*p[1]+m[9]*p[2]+m[13],
            m[2]*p[0]+m[6]*p[1]+m[10]*p[2]+m[14]]

gmin=[1e30]*3; gmax=[-1e30]*3
scene = gltf.get("scenes", [{}])[gltf.get("scene",0)]
roots = scene.get("nodes", list(range(len(nodes))))
ident=[1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1]

def walk(ni, parent):
    global gmin, gmax
    n = nodes[ni]
    world = mul(parent, trs_matrix(n))
    if "mesh" in n:
        for p in meshes[n["mesh"]].get("primitives", []):
            ai = p.get("attributes",{}).get("POSITION")
            if ai is None: continue
            a = acc[ai]; mn=a.get("min"); mx=a.get("max")
            if not mn or not mx: continue
            for cx in (mn[0],mx[0]):
                for cy in (mn[1],mx[1]):
                    for cz in (mn[2],mx[2]):
                        wp = xform(world,[cx,cy,cz])
                        for k in range(3):
                            gmin[k]=min(gmin[k],wp[k]); gmax[k]=max(gmax[k],wp[k])
    for c in n.get("children", []):
        walk(c, world)

for r in roots:
    walk(r, ident)

size=[gmax[k]-gmin[k] for k in range(3)]
print("ZUSAMMENGESETZTE AABB (glTF Y-up):")
print("  min=%s" % ["%.3f"%v for v in gmin])
print("  max=%s" % ["%.3f"%v for v in gmax])
print("  GROESSE X=%.3f Y=%.3f Z=%.3f" % tuple(size))
order = sorted(range(3), key=lambda k:-size[k])
ax="XYZ"
print("  Laengste Achse=%s (%.3f), dann %s (%.3f), %s (%.3f)" %
      (ax[order[0]],size[order[0]],ax[order[1]],size[order[1]],ax[order[2]],size[order[2]]))
L=size[order[0]]
print("  Verhaeltnis L:B:H = 1 : %.3f : %.3f" % (size[order[1]]/L, size[order[2]]/L))
print("  (Echter Gelenkbus ~ 1 : 0.142 : 0.178;  Solobus ~ 1 : 0.21 : 0.27)")
