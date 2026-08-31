"""Exportiert die Baeume und Buesche aus dem Blendswap-Paket als FBX.

Quelle: "Trees & Bushes pack" von Painkiller5555, Creative Commons
Attribution 3.0 (blendswap.com/blends/view/78071). Die Namensnennung steht in
CREDITS.md - sie ist Bedingung der Lizenz, nicht Hoeflichkeit.

Das Paket enthaelt 12 Pflanzen, deren Objektnamen NICHTS ueber ihre Art sagen:
"tree.003" ist ein Busch, "leaves.001" ein Baum. Unterschieden wird deshalb
nach der tatsaechlichen Hoehe in Weltmass (Objektmass mal Objektskalierung) -
sechs Pflanzen ueber 14 m sind Baeume, sechs unter 6 m sind Buesche. Das deckt
sich mit der Beschreibung des Pakets ("6 different Trees and 6 Bushes").

Drei Dinge muessen beim Export stimmen, sonst faellt es erst im Spiel auf:

* Die Objektskalierung wird ANGEWENDET. Im Paket steht sie zwischen 0,1275
  und 1,0 - unangewendet waere ein Baum im Spiel 34 m statt 4,4 m gross.
* Der Ursprung kommt auf den Stammfuss (Z-Minimum) und in die XY-Mitte. Der
  Spawner setzt Instanzen auf die Gelaendehoehe; ein Ursprung in der Krone
  liesse den Baum im Boden stecken. Genau dieser Fehler ist bei den
  Fussgaengern schon einmal passiert - sie schwebten, weil die Simulation die
  Koerpermitte lieferte und der Mesh-Ursprung an den Fuessen sass.
* apply_unit_scale liefert bereits Zentimeter. Beim Import in Unreal MUSS
  import_uniform_scale auf 1,0 stehen - sonst ist das Modell hundertfach zu
  gross (mit dem Kaefer schon erlebt: 175 m lang).

Aufruf:
  blender.exe --background "Trees & Bushes.blend" --python Tools/Blender/export_vegetation.py -- <Zielordner>
"""

import json
import math
import os
import sys

import bpy
import bmesh
from mathutils import Vector

OUT_DIR = sys.argv[sys.argv.index("--") + 1] if "--" in sys.argv else "."
os.makedirs(OUT_DIR, exist_ok=True)

# Hoehengrenze zwischen Baum und Busch in Metern. Die Verteilung im Paket ist
# eindeutig zweigipflig: sechs Pflanzen zwischen 14,8 und 17,5 m, sechs
# zwischen 3,9 und 5,3 m. Dazwischen liegt nichts.
TREE_MIN_HEIGHT_M = 10.0


def world_size(obj):
    bb = [Vector(v) for v in obj.bound_box]
    xs = [v.x for v in bb]
    ys = [v.y for v in bb]
    zs = [v.z for v in bb]
    return (
        (max(xs) - min(xs)) * abs(obj.scale.x),
        (max(ys) - min(ys)) * abs(obj.scale.y),
        (max(zs) - min(zs)) * abs(obj.scale.z),
    )


def material_colors(obj):
    """Grundfarbe je Materialslot - das Paket bringt KEINE Texturen mit."""
    out = []
    for slot in obj.data.materials:
        if slot is None:
            out.append(None)
            continue
        base = None
        if slot.use_nodes:
            for node in slot.node_tree.nodes:
                if node.type in {"BSDF_PRINCIPLED", "BSDF_DIFFUSE"}:
                    inp = node.inputs.get("Base Color") or node.inputs.get("Color")
                    if inp is not None and not inp.is_linked:
                        base = [round(c, 4) for c in inp.default_value[:3]]
                        break
        if base is None:
            base = [round(c, 4) for c in slot.diffuse_color[:3]]
        out.append({"name": slot.name, "base_color": base})
    return out


plants = []
for obj in list(bpy.data.objects):
    if obj.type != "MESH":
        continue
    sx, sy, sz = world_size(obj)
    # Die beiden Bodenplatten der Vorschauszene sind flach.
    if sz < 0.5:
        continue
    plants.append((obj, sz))

plants.sort(key=lambda p: -p[1])

tree_index = 0
bush_index = 0
manifest = []

for obj, height in plants:
    is_tree = height >= TREE_MIN_HEIGHT_M
    if is_tree:
        tree_index += 1
        name = "SM_WbTree_%02d" % tree_index
    else:
        bush_index += 1
        name = "SM_WbBush_%02d" % bush_index

    bpy.ops.object.select_all(action="DESELECT")
    copy = obj.copy()
    copy.data = obj.data.copy()
    copy.name = name
    copy.data.name = name
    bpy.context.collection.objects.link(copy)

    copy.location = (0.0, 0.0, 0.0)
    copy.rotation_euler = (0.0, 0.0, 0.0)

    bpy.context.view_layer.objects.active = copy
    copy.select_set(True)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)

    # Ursprung auf Stammfuss und XY-Mitte legen - durch Verschieben der
    # Geometrie, nicht des Objekts, damit der Ursprung wirklich dort landet.
    me = copy.data
    xs = [v.co.x for v in me.vertices]
    ys = [v.co.y for v in me.vertices]
    zs = [v.co.z for v in me.vertices]
    shift = Vector(((min(xs) + max(xs)) * 0.5, (min(ys) + max(ys)) * 0.5, min(zs)))
    for v in me.vertices:
        v.co -= shift

    tris = sum(len(p.vertices) - 2 for p in me.polygons)
    xs = [v.co.x for v in me.vertices]
    ys = [v.co.y for v in me.vertices]
    zs = [v.co.z for v in me.vertices]

    path = os.path.join(OUT_DIR, name + ".fbx")
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
        axis_forward="-Z",
        axis_up="Y",
    )

    manifest.append({
        "name": name,
        "source": obj.name,
        "kind": "tree" if is_tree else "bush",
        "tris": tris,
        "size_m": [round(max(xs) - min(xs), 2), round(max(ys) - min(ys), 2), round(max(zs) - min(zs), 2)],
        "zmin": round(min(zs), 4),
        "materials": material_colors(copy),
        "fbx": os.path.basename(path),
    })

    bpy.data.objects.remove(copy, do_unlink=True)

with open(os.path.join(OUT_DIR, "vegetation.json"), "w", encoding="utf-8") as f:
    json.dump({"plants": manifest}, f, indent=2, ensure_ascii=False)

print("###VEG### %d Pflanzen exportiert (%d Baeume, %d Buesche)"
      % (len(manifest), tree_index, bush_index))
for m in manifest:
    print("###VEG###   %-16s %-5s %7d Dreiecke  %s m" % (m["name"], m["kind"], m["tris"], m["size_m"]))
