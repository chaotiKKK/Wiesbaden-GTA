"""Baut eine bessere Fussgaenger-Figur (Low-Poly, ~175 cm) in FUENF Material-
zonen (0 Haut, 1 Hemd, 2 Hose, 3 Haare, 4 Schuhe) und exportiert sie in VIER
Gangphasen als glTF (SM_WbPed2_0..3). Die Zonen erlauben spaeter pro-Instanz
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

def build_person(bm, leg_deg, arm_deg):
    """leg_deg: rechtes Bein nach vorn (+), linkes nach hinten. arm_deg
    gegenlaeufig zu den Beinen (rechter Arm zurueck, wenn rechtes Bein vor)."""
    # Masse in cm. Hueften bei z=88, Schultern bei z=145, Kopf bis 175.
    HIP_Z, SHO_Z = 88.0, 145.0
    # Beine (Hose): Mitte zwischen Huefte und Boden, um die Huefte geneigt.
    box(bm, 0, 9.0, HIP_Z*0.5, 8.0, 7.0, HIP_Z*0.5, TROUSER, pivot_z=HIP_Z, pitch_deg= leg_deg)   # rechtes Bein (+Y)
    box(bm, 0, -9.0, HIP_Z*0.5, 8.0, 7.0, HIP_Z*0.5, TROUSER, pivot_z=HIP_Z, pitch_deg=-leg_deg)   # linkes Bein
    # Schuhe an den Beinenden (leicht nach vorn); grob unter der Huefte, geneigt.
    box(bm, 0, 9.0, 5.0, 12.0, 8.0, 5.0, SHOE, pivot_z=HIP_Z, pitch_deg= leg_deg*0.6)
    box(bm, 0, -9.0, 5.0, 12.0, 8.0, 5.0, SHOE, pivot_z=HIP_Z, pitch_deg=-leg_deg*0.6)
    # Rumpf (Hemd)
    box(bm, 0, 0, (HIP_Z+SHO_Z)*0.5, 11.0, 17.0, (SHO_Z-HIP_Z)*0.5 + 4.0, SHIRT)
    # Arme (Hemd), an den Schultern geneigt (gegenlaeufig zu den Beinen)
    ARM_LEN = 52.0
    box(bm, 0, 22.0, SHO_Z-ARM_LEN*0.5, 6.0, 6.0, ARM_LEN*0.5, SHIRT, pivot_z=SHO_Z, pitch_deg=-arm_deg)  # rechter Arm
    box(bm, 0, -22.0, SHO_Z-ARM_LEN*0.5, 6.0, 6.0, ARM_LEN*0.5, SHIRT, pivot_z=SHO_Z, pitch_deg= arm_deg)  # linker Arm
    # Haende (Haut) an den Armenden
    box(bm, 0, 22.0, SHO_Z-ARM_LEN, 6.5, 6.5, 6.0, SKIN, pivot_z=SHO_Z, pitch_deg=-arm_deg)
    box(bm, 0, -22.0, SHO_Z-ARM_LEN, 6.5, 6.5, 6.0, SKIN, pivot_z=SHO_Z, pitch_deg= arm_deg)
    # Hals (Haut) + Kopf (Haut) + Haare (Kappe oben)
    box(bm, 0, 0, SHO_Z+5.0, 5.0, 5.0, 5.0, SKIN)
    box(bm, 0, 0, 163.0, 9.0, 9.0, 9.0, SKIN)        # Kopf
    box(bm, 0, 0, 171.0, 9.5, 9.5, 4.0, HAIR)         # Haare oben

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

def main():
    d = out_dir(); os.makedirs(d, exist_ok=True)
    for phase, (leg, arm) in enumerate(PHASES):
        reset()
        mats = [mat(n, c) for n, c in MATS]
        me = bpy.data.meshes.new("SM_WbPed2_%d" % phase)
        bm = bmesh.new()
        build_person(bm, leg, arm)
        bm.to_mesh(me); bm.free()
        obj = bpy.data.objects.new(me.name, me)
        for m in mats: obj.data.materials.append(m)
        bpy.context.collection.objects.link(obj)
        export(obj, os.path.join(d, "SM_WbPed2_%d.glb" % phase))
    print("###PED### FERTIG")

main()
