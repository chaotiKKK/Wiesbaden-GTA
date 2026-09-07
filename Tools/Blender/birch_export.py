"""Exportiert die Birke (birch_tree.blend) als eine FBX-Baumvariante fuer Unreal.

Folgt der Konvention von export_vegetation.py:
- alle Mesh-Objekte zu EINEM Baum-Mesh verbinden (eine Variante = ein Mesh),
- Ursprung auf den Stammfuss (Z-Minimum) und in die XY-Mitte,
- Objektskalierung ANWENDEN, apply_unit_scale -> Zentimeter (Unreal-Import mit
  import_uniform_scale = 1.0),
- Texturen NEBEN die FBX kopieren (path_mode='COPY'), damit Unreal die
  Blatt-/Rinden-Bilder findet und daraus ein maskiertes Foliage-Material werden
  kann.

Zusaetzlich: Struktur (Objekte, Materialien, Bildtexturen inkl. Alpha-Nutzung)
protokollieren, damit das Unreal-Material richtig aufgesetzt wird.

Aufruf:
  blender.exe -b birch_tree.blend -P Tools/Blender/birch_export.py -- <OutDir> <MeshName>
"""

import os
import sys

import bpy
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT_DIR = argv[0] if len(argv) > 0 else "."
MESH_NAME = argv[1] if len(argv) > 1 else "SM_WbTree_07"
os.makedirs(OUT_DIR, exist_ok=True)


def log(m):
    print("###BIRCH### %s" % m)


# --- 1) Struktur protokollieren ------------------------------------------
meshes = [o for o in bpy.data.objects if o.type == "MESH"]
log("Mesh-Objekte: %d" % len(meshes))
for o in meshes:
    tris = sum(len(p.vertices) - 2 for p in o.data.polygons)
    log("  Objekt '%s': %d Dreiecke, %d Materialslots" % (o.name, tris, len(o.data.materials)))

for mat in bpy.data.materials:
    if mat is None:
        continue
    tex_info = []
    alpha_linked = False
    if mat.use_nodes:
        for node in mat.node_tree.nodes:
            if node.type == "TEX_IMAGE" and node.image is not None:
                tex_info.append(node.image.name)
            if node.type == "BSDF_PRINCIPLED":
                a = node.inputs.get("Alpha")
                if a is not None and a.is_linked:
                    alpha_linked = True
    log("  Material '%s': blend=%s texturen=%s alpha_verknuepft=%s"
        % (mat.name, getattr(mat, "blend_method", "?"), tex_info, alpha_linked))

# --- 2) Alle Meshes zu einem Baum verbinden ------------------------------
if not meshes:
    log("FEHLER: keine Mesh-Objekte")
    sys.exit(1)

bpy.ops.object.select_all(action="DESELECT")
for o in meshes:
    o.select_set(True)
bpy.context.view_layer.objects.active = meshes[0]
if len(meshes) > 1:
    bpy.ops.object.join()
merged = bpy.context.view_layer.objects.active
merged.name = MESH_NAME
merged.data.name = MESH_NAME

# Lage neutralisieren, Skalierung anwenden.
merged.location = (0.0, 0.0, 0.0)
merged.rotation_euler = (0.0, 0.0, 0.0)
bpy.ops.object.select_all(action="DESELECT")
merged.select_set(True)
bpy.context.view_layer.objects.active = merged
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)

# Ursprung auf Stammfuss (Z-min) + XY-Mitte, ueber die Geometrie verschoben.
me = merged.data
xs = [v.co.x for v in me.vertices]
ys = [v.co.y for v in me.vertices]
zs = [v.co.z for v in me.vertices]
shift = Vector(((min(xs) + max(xs)) * 0.5, (min(ys) + max(ys)) * 0.5, min(zs)))
for v in me.vertices:
    v.co -= shift

xs = [v.co.x for v in me.vertices]
ys = [v.co.y for v in me.vertices]
zs = [v.co.z for v in me.vertices]
tris = sum(len(p.vertices) - 2 for p in me.polygons)
log("Verbunden: %s, %d Dreiecke, Groesse %.2f x %.2f x %.2f m"
    % (MESH_NAME, tris, max(xs) - min(xs), max(ys) - min(ys), max(zs) - min(zs)))

# --- 3) FBX exportieren, Texturen daneben kopieren -----------------------
path = os.path.join(OUT_DIR, MESH_NAME + ".fbx")
bpy.ops.export_scene.fbx(
    filepath=path,
    use_selection=True,
    apply_unit_scale=True,
    global_scale=1.0,
    apply_scale_options="FBX_SCALE_NONE",
    object_types={"MESH"},
    mesh_smooth_type="FACE",
    use_mesh_modifiers=True,
    add_leaf_bones=False,
    bake_anim=False,
    path_mode="COPY",
    embed_textures=False,
    axis_forward="-Z",
    axis_up="Y",
)
log("FBX geschrieben: %s" % path)
# Texturen, die path_mode=COPY neben die FBX gelegt hat, auflisten.
for f in sorted(os.listdir(OUT_DIR)):
    if f.lower().endswith((".png", ".jpg", ".jpeg", ".tga", ".tif")):
        log("  Textur: %s" % f)
log("FERTIG")
