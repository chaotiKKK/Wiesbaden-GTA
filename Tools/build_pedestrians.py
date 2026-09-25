"""Baut eine bessere Fussgaenger-Figur (Low-Poly, ~175 cm) in FUENF Material-
zonen (0 Haut, 1 Hemd, 2 Hose, 3 Haare, 4 Schuhe) und exportiert sie in VIER
Gangphasen als glTF, fuer DREI Koerpertypen (SM_WbPed2_N schlank, SM_WbPed2B_N
breit, SM_WbPed2C_N Kind; siehe BODIES). Die Zonen erlauben pro-Instanz
verschiedene Kleidungsfarben (Custom Data im Material).

Konvention wie die alten Posen (in UE gemessen): +X = Gehrichtung (Beine/Arme
schwingen vorn/hinten), +Y = Schulterbreite, +Z = oben, Ursprung an den FUESSEN
(z=0 unten). Gangphasen: 0 rechts vor / links Arm vor (gespreizt), 1 Durchgang
(zusammen), 2 links vor / rechts Arm vor (gespreizt), 3 Durchgang.

glTF-Export mit export_yup=True (sonst kippt die Figur auf UE-Y).
Aufruf: blender.exe -b -P build_pedestrians.py -- <outdir>
"""
import bpy, bmesh, sys, os, math

SKIN, SHIRT, TROUSER, HAIR, SHOE = 0, 1, 2, 3, 4

def out_dir():
    a = sys.argv
    if "--" in a and a[a.index("--")+1:]:
        return a[a.index("--")+1:][0]
    return "C:/freebuff/WiesbadenReal_Sicherung/WiesbadenReal/Content/Assets/People/Varied/src"

def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)

def mat(name, rgba):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes.get("Principled BSDF")
    b.inputs["Base Color"].default_value = rgba
    b.inputs["Roughness"].default_value = 0.7
    return m

# Alle Masse in build_person sind in ZENTIMETERN; glTF/UE skaliert *100
# (Meter->cm). Darum hier auf Meter herunterrechnen, damit die Figur 1,75 m
# gross importiert wird (nicht 175 m).
S = 0.01

def box(bm, cx, cy, cz, hx, hy, hz, mi, pivot_z=None, pitch_deg=0.0):
    """Quader (Mittelpunkt cx,cy,cz, Halbmasse hx,hy,hz in cm). Optional um die
    Y-Achse durch (cx, pivot_z) geneigt (Bein-/Armschwung vorn/hinten)."""
    cx, cy, cz, hx, hy, hz = cx*S, cy*S, cz*S, hx*S, hy*S, hz*S
    if pivot_z is not None:
        pivot_z = pivot_z*S
    xs = (cx-hx, cx+hx); ys = (cy-hy, cy+hy); zs = (cz-hz, cz+hz)
    verts = []
    a = math.radians(pitch_deg)
    ca, sa = math.cos(a), math.sin(a)
    for x in xs:
        for y in ys:
            for z in zs:
                if pivot_z is not None and pitch_deg != 0.0:
                    dx, dz = x - cx, z - pivot_z
                    nx = cx + dx*ca - dz*sa
                    nz = pivot_z + dx*sa + dz*ca
                    verts.append(bm.verts.new((nx, y, nz)))
                else:
                    verts.append(bm.verts.new((x, y, z)))
    # 8 Ecken: index = xi*4 + yi*2 + zi
    def v(xi, yi, zi): return verts[xi*4+yi*2+zi]
    quads = [
        (v(0,0,0),v(0,1,0),v(0,1,1),v(0,0,1)),
        (v(1,0,0),v(1,0,1),v(1,1,1),v(1,1,0)),
        (v(0,0,0),v(0,0,1),v(1,0,1),v(1,0,0)),
        (v(0,1,0),v(1,1,0),v(1,1,1),v(0,1,1)),
        (v(0,0,0),v(1,0,0),v(1,1,0),v(0,1,0)),
        (v(0,0,1),v(0,1,1),v(1,1,1),v(1,0,1)),
    ]
    for q in quads:
        try:
            f = bm.faces.new(q); f.material_index = mi
        except ValueError:
            pass

def build_person(bm, leg_deg, arm_deg, hs=1.0, ws=1.0, head_mul=1.0):
    """Figur in einer Gangphase. hs skaliert die HOEHE (Kind < 1), ws die BREITE
    (breiter Erwachsener > 1), head_mul den Kopf (Kind = groesserer Kopf).
    leg_deg: rechtes Bein vor (+); arm_deg gegenlaeufig."""
    HIP_Z, SHO_Z = 88.0*hs, 145.0*hs
    # Beine (Hose)
    box(bm, 0, 9.0*ws, HIP_Z*0.5, 8.0*ws, 7.0*ws, HIP_Z*0.5, TROUSER, pivot_z=HIP_Z, pitch_deg= leg_deg)
    box(bm, 0, -9.0*ws, HIP_Z*0.5, 8.0*ws, 7.0*ws, HIP_Z*0.5, TROUSER, pivot_z=HIP_Z, pitch_deg=-leg_deg)
    # Schuhe
    box(bm, 0, 9.0*ws, 5.0*hs, 12.0*ws, 8.0*ws, 5.0*hs, SHOE, pivot_z=HIP_Z, pitch_deg= leg_deg*0.6)
    box(bm, 0, -9.0*ws, 5.0*hs, 12.0*ws, 8.0*ws, 5.0*hs, SHOE, pivot_z=HIP_Z, pitch_deg=-leg_deg*0.6)
    # Rumpf (Hemd)
    box(bm, 0, 0, (HIP_Z+SHO_Z)*0.5, 11.0*ws, 17.0*ws, (SHO_Z-HIP_Z)*0.5 + 4.0*hs, SHIRT)
    # Arme (Hemd), an den Schultern geneigt (gegenlaeufig zu den Beinen)
    ARM_LEN = 52.0*hs
    box(bm, 0, 22.0*ws, SHO_Z-ARM_LEN*0.5, 6.0*ws, 6.0*ws, ARM_LEN*0.5, SHIRT, pivot_z=SHO_Z, pitch_deg=-arm_deg)
    box(bm, 0, -22.0*ws, SHO_Z-ARM_LEN*0.5, 6.0*ws, 6.0*ws, ARM_LEN*0.5, SHIRT, pivot_z=SHO_Z, pitch_deg= arm_deg)
    # Haende (Haut)
    box(bm, 0, 22.0*ws, SHO_Z-ARM_LEN, 6.5*ws, 6.5*ws, 6.0*hs, SKIN, pivot_z=SHO_Z, pitch_deg=-arm_deg)
    box(bm, 0, -22.0*ws, SHO_Z-ARM_LEN, 6.5*ws, 6.5*ws, 6.0*hs, SKIN, pivot_z=SHO_Z, pitch_deg= arm_deg)
    # Hals + Kopf + Haare
    box(bm, 0, 0, SHO_Z+5.0*hs, 5.0*ws, 5.0*ws, 5.0*hs, SKIN)
    hh = 9.0 * head_mul
    box(bm, 0, 0, 163.0*hs, hh*ws, hh*ws, hh*hs, SKIN)                 # Kopf
    box(bm, 0, 0, (163.0*hs + hh*hs*0.9), (hh+0.5)*ws, (hh+0.5)*ws, 4.0*hs, HAIR)  # Haare oben

MATS = [
    ("Skin",    (0.80, 0.62, 0.50, 1.0)),
    ("Shirt",   (0.30, 0.40, 0.65, 1.0)),
    ("Trouser", (0.22, 0.22, 0.26, 1.0)),
    ("Hair",    (0.20, 0.14, 0.10, 1.0)),
    ("Shoe",    (0.08, 0.08, 0.09, 1.0)),
]

# Gangphasen: (leg_deg, arm_deg). 0/2 gespreizt (gegengleich), 1/3 Durchgang.
PHASES = [ (22.0, 20.0), (0.0, 0.0), (-22.0, -20.0), (0.0, 0.0) ]

def export(obj, path):
    for o in bpy.context.scene.objects: o.select_set(False)
    obj.select_set(True); bpy.context.view_layer.objects.active = obj
    bpy.ops.export_scene.gltf(filepath=path, export_format='GLB',
                              use_selection=True, export_yup=True, export_apply=True)
    print("###PED### %s" % path)

# Koerpertypen (Schluessel = Namenszusatz): "" = A schlanker Erwachsener
# (SM_WbPed2_N), "B" breiter Erwachsener, "C" Kind (kleiner, Kopf relativ gross).
# Nur einen Typ neu bauen: -- <outdir> <tag>, z. B. "-- <outdir> C".
BODIES = {
    "":  dict(hs=1.0, ws=1.0,  head_mul=1.0),
    "B": dict(hs=1.0, ws=1.30, head_mul=1.0),
    "C": dict(hs=0.68, ws=0.82, head_mul=1.40),
}

def only_tags():
    a = sys.argv
    rest = a[a.index("--")+1:] if "--" in a else []
    return set(rest[1:]) if len(rest) > 1 else None

def main():
    d = out_dir(); os.makedirs(d, exist_ok=True)
    wanted = only_tags()
    for tag, spec in BODIES.items():
        if wanted is not None and tag not in wanted:
            continue
        for phase, (leg, arm) in enumerate(PHASES):
            reset()
            mats = [mat(n, c) for n, c in MATS]
            name = "SM_WbPed2%s_%d" % (tag, phase)
            me = bpy.data.meshes.new(name)
            bm = bmesh.new()
            build_person(bm, leg, arm, **spec)
            bm.to_mesh(me); bm.free()
            obj = bpy.data.objects.new(name, me)
            for m in mats: obj.data.materials.append(m)
            bpy.context.collection.objects.link(obj)
            export(obj, os.path.join(d, name + ".glb"))
    print("###PED### FERTIG")

main()
