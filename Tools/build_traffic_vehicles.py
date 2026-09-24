"""Baut drei zusaetzliche Verkehrsteilnehmer als Low-Poly-Modelle in Blender und
exportiert sie als glTF (.glb): Transporter (Kastenwagen), Kombi und Bus.

Konvention wie das Kaefer-Verkehrsmesh (in UE gemessen):
  +X = vorne, +Y = rechts, +Z = oben, Ursprung = Mitte am BODEN (Reifen bei z=0).
Die Geometrie wird direkt in diesen Koordinaten gebaut; der glTF-Round-Trip
haelt beim Ka-52 X->X / Y->Y / Z->Z (in UE verifiziert), sonst korrigiert der
Spawner per Typ-Rotation.

Materialslots je Fahrzeug: 0 = Lack (Karosseriefarbe je Typ), 1 = Glas (dunkel),
2 = Reifen/Trim (schwarz). Der Spawner faerbt spaeter nur Slot 0 um, falls je ein
Farbmaterial gesetzt wird.

Aufruf ueber blender_lauf.cmd: blender -b -P build_traffic_vehicles.py -- <outdir>
"""
import bpy, bmesh, sys, os, math, mathutils

def out_dir():
    argv = sys.argv
    if "--" in argv:
        rest = argv[argv.index("--") + 1:]
        if rest:
            return rest[0]
    return "C:/freebuff/WiesbadenReal_Sicherung/WiesbadenReal/Content/Vehicles/Traffic/src"

def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)

def mat(name, rgba, rough=0.6, metal=0.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    bsdf = m.node_tree.nodes.get("Principled BSDF")
    bsdf.inputs["Base Color"].default_value = rgba
    bsdf.inputs["Roughness"].default_value = rough
    bsdf.inputs["Metallic"].default_value = metal
    return m

def add_box(bm, x0, x1, y0, y1, z0, z1, mi):
    """Fuegt einen Quader (achsenparallel) mit Material-Index mi ein."""
    vs = [bm.verts.new((x, y, z)) for x in (x0, x1) for y in (y0, y1) for z in (z0, z1)]
    # vs Reihenfolge: (x0,y0,z0)(x0,y0,z1)(x0,y1,z0)(x0,y1,z1)(x1,y0,z0)...
    idx = lambda a, b, c: vs[a * 4 + b * 2 + c]
    quads = [
        (idx(0,0,0), idx(0,1,0), idx(0,1,1), idx(0,0,1)),  # -X
        (idx(1,0,0), idx(1,0,1), idx(1,1,1), idx(1,1,0)),  # +X
        (idx(0,0,0), idx(0,0,1), idx(1,0,1), idx(1,0,0)),  # -Y
        (idx(0,1,0), idx(1,1,0), idx(1,1,1), idx(0,1,1)),  # +Y
        (idx(0,0,0), idx(1,0,0), idx(1,1,0), idx(0,1,0)),  # -Z
        (idx(0,0,1), idx(0,1,1), idx(1,1,1), idx(1,0,1)),  # +Z
    ]
    for q in quads:
        try:
            f = bm.faces.new(q)
            f.material_index = mi
        except ValueError:
            pass

def add_wheel(bm, cx, cy, r, halfw, mi, seg=12):
    """Radscheibe: Zylinder mit Achse entlang Y (quer), Mitte (cx, cy, r)."""
    ring0 = []
    ring1 = []
    for i in range(seg):
        a = 2.0 * math.pi * i / seg
        x = cx + r * math.cos(a)
        z = r + r * math.sin(a)
        ring0.append(bm.verts.new((x, cy - halfw, z)))
        ring1.append(bm.verts.new((x, cy + halfw, z)))
    for i in range(seg):
        j = (i + 1) % seg
        try:
            f = bm.faces.new((ring0[i], ring0[j], ring1[j], ring1[i]))
            f.material_index = mi
        except ValueError:
            pass
    # Seitenkappen
    try:
        bm.faces.new(list(reversed(ring0))).material_index = mi
    except ValueError:
        pass
    try:
        bm.faces.new(ring1).material_index = mi
    except ValueError:
        pass

def add_wheels(bm, ax_front, ax_rear, y, r, halfw, mi):
    for cx in (ax_front, ax_rear):
        add_wheel(bm, cx, y, r, halfw, mi)
        add_wheel(bm, cx, -y, r, halfw, mi)

def finish(name, paint, glass, tire, build):
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    build(bm)
    bm.to_mesh(me)
    bm.free()
    obj = bpy.data.objects.new(name, me)
    obj.data.materials.append(paint)   # 0
    obj.data.materials.append(glass)   # 1
    obj.data.materials.append(tire)    # 2
    bpy.context.collection.objects.link(obj)
    # Normalen sauber nach aussen.
    me.calc_normals_split() if hasattr(me, "calc_normals_split") else None
    return obj

def export(obj, path):
    for o in bpy.context.scene.objects:
        o.select_set(False)
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    # export_yup=True (Standard): Blender-Z-up -> glTF-Y-up -> UE-Z-up (Round-Trip
    # haelt die Hochachse). Mit False landete die Hoehe faelschlich auf UE-Y.
    bpy.ops.export_scene.gltf(
        filepath=path, export_format='GLB', use_selection=True,
        export_yup=True, export_apply=True)
    print("###VEH### exported %s" % path)

# --- Geometrie je Typ (Meter, +X vorne, Ursprung Mitte/Boden) ----------------

def build_transporter(bm):
    P, G, T = 0, 1, 2
    # Kastenwagen: tiefer Vorbau + hoher Kasten. Laenge 4,9  Breite 1,9  Hoehe 2,35
    add_box(bm, -2.45, 1.55, -0.95, 0.95, 0.42, 2.35, P)   # Kasten
    add_box(bm,  1.55, 2.45, -0.92, 0.92, 0.42, 1.15, P)   # kurze Schnauze
    # Frontscheibe (schraeg angedeutet als senkrechtes Glasband vorn oben)
    add_box(bm,  1.48, 1.56, -0.9, 0.9, 1.15, 1.95, G)     # Windschutz
    add_box(bm, -2.0, 1.2, -0.97, -0.9, 1.2, 1.9, G)       # Seitenfenster L
    add_box(bm, -2.0, 1.2,  0.9, 0.97, 1.2, 1.9, G)        # Seitenfenster R
    add_wheels(bm, 1.55, -1.6, 0.83, 0.42, 0.20, T)

def build_kombi(bm):
    P, G, T = 0, 1, 2
    # Kombi: flacher Unterbau + Fahrgastzelle + Motorhaube. Laenge 4,75 Breite 1,8 Hoehe 1,5
    add_box(bm, -2.35, 2.35, -0.9, 0.9, 0.34, 0.86, P)     # Unterbau
    add_box(bm, -2.15, 1.35, -0.86, 0.86, 0.86, 1.48, P)  # Dach/Zelle
    add_box(bm,  1.35, 2.30, -0.86, 0.86, 0.5, 0.9, P)     # Motorhaube (leicht hoeher)
    add_box(bm,  1.28, 1.4, -0.82, 0.82, 0.9, 1.4, G)      # Windschutz
    add_box(bm, -2.05, 1.2, -0.9, -0.83, 0.95, 1.38, G)   # Seitenfenster L
    add_box(bm, -2.05, 1.2,  0.83, 0.9, 0.95, 1.38, G)    # Seitenfenster R
    add_box(bm, -2.2, -2.1, -0.8, 0.8, 0.95, 1.38, G)     # Heckscheibe
    add_wheels(bm, 1.5, -1.55, 0.34, 0.34, 0.16, T)

def build_bus(bm):
    P, G, T = 0, 1, 2
    # Solobus: langer Kasten. Laenge 10,0  Breite 2,5  Hoehe 3,1
    add_box(bm, -5.0, 5.0, -1.25, 1.25, 0.55, 3.05, P)     # Kasten
    add_box(bm,  4.9, 5.02, -1.2, 1.2, 1.4, 2.6, G)        # Frontscheibe
    add_box(bm, -4.95, -4.85, -1.2, 1.2, 1.4, 2.4, G)     # Heckscheibe
    add_box(bm, -4.6, 4.6, -1.27, -1.2, 1.5, 2.5, G)      # Fensterband L
    add_box(bm, -4.6, 4.6,  1.2, 1.27, 1.5, 2.5, G)       # Fensterband R
    add_box(bm, -5.02, -4.98, -1.25, 1.25, 0.55, 3.05, P) # Heckwand
    # Raeder: Vorderachse + Doppel-Hinterachse
    add_wheel(bm, 3.6, 1.25, 0.52, 0.22, T); add_wheel(bm, 3.6, -1.25, 0.52, 0.22, T)
    add_wheel(bm, -3.2, 1.25, 0.52, 0.22, T); add_wheel(bm, -3.2, -1.25, 0.52, 0.22, T)
    add_wheel(bm, -3.9, 1.25, 0.52, 0.22, T); add_wheel(bm, -3.9, -1.25, 0.52, 0.22, T)

def main():
    outdir = out_dir()
    os.makedirs(outdir, exist_ok=True)
    reset()
    glass = mat("Glass", (0.05, 0.07, 0.10, 1.0), rough=0.15, metal=0.0)
    tire  = mat("Tire",  (0.03, 0.03, 0.03, 1.0), rough=0.9)
    specs = [
        ("SM_TrafficTransporter", (0.92, 0.92, 0.94, 1.0), build_transporter),
        ("SM_TrafficKombi",       (0.12, 0.20, 0.42, 1.0), build_kombi),
        ("SM_TrafficBus",         (0.80, 0.30, 0.12, 1.0), build_bus),
    ]
    for name, paintrgba, build in specs:
        reset()
        glass = mat("Glass", (0.05, 0.07, 0.10, 1.0), rough=0.15)
        tire  = mat("Tire",  (0.03, 0.03, 0.03, 1.0), rough=0.9)
        paint = mat("Paint", paintrgba, rough=0.45, metal=0.1)
        obj = finish(name, paint, glass, tire, build)
        export(obj, os.path.join(outdir, name + ".glb"))
    print("###VEH### FERTIG")

main()
