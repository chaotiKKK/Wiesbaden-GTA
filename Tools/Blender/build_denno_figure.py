"""Denno (Tripo-Figur) spieltauglich machen: dezimieren, Arme senken, 1,68 m, Blick -Y.

Quelle: Data/Raw/Denno/denno_source.glb (Kopie von "denno figure 3d model.glb";
Tripo, 6 Teile, 1,87 Mio. Dreiecke, 0,98 m normiert, T-Pose, kein Skelett).
Ziel:   Data/Raw/Denno/denno_figure.glb (-> Tools/import_denno_shop.py).

Die Figur hat kein Skelett. Die Arme werden darum per Gewichtsfeld um die
Schulter gedreht: volle Drehung ausserhalb der Schulter, weicher Uebergang
ueber Schulterkappe und Achsel - sonst reisst der Aermel am Rumpf auf.

Blender 5.2: blender -b -P Tools/Blender/build_denno_figure.py
"""
import bpy
import math
from pathlib import Path
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / 'Data/Raw/Denno/denno_source.glb'
OUT = ROOT / 'Data/Raw/Denno/denno_figure.glb'
PREVIEW = ROOT.parent / '.planning/denno-shop'
TARGET_HEIGHT_M = 1.68
# Denno ist nur hinter der Schaufensterscheibe zu sehen - im naechsten
# Blick (1,5 m vor dem Fenster) knapp 560 px hoch. 20k Dreiecke und Texturen
# auf einem Viertel der Tripo-Kantenlaenge sehen dort gleich aus wie 90k
# Dreiecke mit 2k-Texturen (Vergleich .planning/denno-shop/shots/assets).
TARGET_TRIS = 20000
TEXTURE_DIVISOR = 4        # 2048 -> 512, 1024 -> 256
TEXTURE_MIN = 128          # kleine Texturen nicht weiter verkleinern
ARM_OUTWARD_DEG = 12.0   # Haende haengen leicht neben der Huefte


def smoothstep(a, b, x):
    t = max(0.0, min(1.0, (x - a) / (b - a)))
    return t * t * (3 - 2 * t)


bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=str(SRC))
parts = [o for o in bpy.context.scene.objects if o.type == 'MESH']
for o in parts:
    o.select_set(True)
bpy.context.view_layer.objects.active = parts[0]
bpy.ops.object.parent_clear(type='CLEAR_KEEP_TRANSFORM')
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
bpy.ops.object.join()
body = bpy.context.view_layer.objects.active
body.name = 'SM_Denno'
for o in list(bpy.context.scene.objects):
    if o != body:
        bpy.data.objects.remove(o, do_unlink=True)

# Saubere, eindeutige Namen statt der Tripo-Vorgabe ("denno+figure+3d+model_
# tripo_part_0_basecolor"): der glTF-Import uebernimmt sie als Asset-Namen.
for mat in body.data.materials:
    part = mat.name.split('tripo_part_')[-1].split('_')[0]
    mat.name = 'M_Denno_Part%s' % part
    for node in mat.node_tree.nodes:
        if node.type == 'TEX_IMAGE' and node.image:
            img = node.image
            img.name = 'T_Denno_Part%s' % part
            w, h = img.size
            nw = max(TEXTURE_MIN, w // TEXTURE_DIVISOR) if w > TEXTURE_MIN else w
            nh = max(TEXTURE_MIN, h // TEXTURE_DIVISOR) if h > TEXTURE_MIN else h
            if (nw, nh) != (w, h):
                img.scale(nw, nh)
            print('###DENNO texture', img.name, (w, h), '->', tuple(img.size))

tris = sum(len(p.vertices) - 2 for p in body.data.polygons)
mod = body.modifiers.new('decimate', 'DECIMATE')
mod.ratio = min(1.0, TARGET_TRIS / max(tris, 1))
bpy.ops.object.modifier_apply(modifier=mod.name)
print('###DENNO decimate', tris, '->', len(body.data.polygons))

# --- Arme senken ---------------------------------------------------------
verts = body.data.vertices
zs = [v.co.z for v in verts]
height = max(zs) - min(zs)
# Armhoehe: Mittel der deutlich ausserhalb des Rumpfes liegenden Punkte.
span = max(abs(v.co.x) for v in verts)
far = [v.co for v in verts if abs(v.co.x) > 0.55 * span]
arm_z = sum(p.z for p in far) / len(far)
# Schulter: aeusserste Rumpfbreite knapp unter der Armhoehe.
band = [abs(v.co.x) for v in verts if arm_z - 0.16 * height < v.co.z < arm_z - 0.09 * height]
shoulder_x = sorted(band)[int(len(band) * 0.97)]
print('###DENNO arm_z %.3f shoulder_x %.3f height %.3f' % (arm_z, shoulder_x, height))
theta = math.radians(90.0 - ARM_OUTWARD_DEG)
for v in verts:
    p = v.co
    ax = abs(p.x)
    w = smoothstep(shoulder_x - 0.015 * height, shoulder_x + 0.05 * height, ax) \
        * smoothstep(arm_z - 0.11 * height, arm_z - 0.05 * height, p.z)
    if w <= 0.0:
        continue
    side = 1.0 if p.x > 0 else -1.0
    t = theta * side
    px, pz = p.x - side * shoulder_x, p.z - arm_z
    rx = px * math.cos(t) + pz * math.sin(t)
    rz = -px * math.sin(t) + pz * math.cos(t)
    target = Vector((rx + side * shoulder_x, p.y, rz + arm_z))
    v.co = p.lerp(target, w)

# --- Masstab, Blickrichtung, Ursprung an den Fuessen ----------------------
scale = TARGET_HEIGHT_M / height
# Blickrichtung bleibt -Y (Tripo-Vorgabe); die Drehung zur Strasse setzt der
# Laden-Actor. Masstab direkt in die Vertices - kein Objekt-Transform, der im
# glTF-Knoten landen koennte.
for v in verts:
    v.co *= scale
lo = Vector((min(v.co.x for v in verts), min(v.co.y for v in verts), min(v.co.z for v in verts)))
hi = Vector((max(v.co.x for v in verts), max(v.co.y for v in verts), max(v.co.z for v in verts)))
offset = Vector(((lo.x + hi.x) / 2, (lo.y + hi.y) / 2, lo.z))
for v in verts:
    v.co -= offset
for poly in body.data.polygons:
    poly.use_smooth = True

OUT.parent.mkdir(parents=True, exist_ok=True)
bpy.ops.object.select_all(action='DESELECT')
body.select_set(True)
bpy.ops.export_scene.gltf(filepath=str(OUT), export_format='GLB', use_selection=True,
                          export_apply=True, export_yup=True,
                          export_image_format='JPEG', export_jpeg_quality=90)
dims = tuple(round(x, 3) for x in body.dimensions)
print('###DENNO exported', OUT, 'tris', len(body.data.polygons), 'dims', dims,
      'materials', len(body.data.materials))

# --- Vorschau -------------------------------------------------------------
scene = bpy.context.scene
scene.render.engine = 'BLENDER_WORKBENCH'
scene.display.shading.color_type = 'TEXTURE'
scene.display.shading.light = 'STUDIO'
scene.render.resolution_x = 900
scene.render.resolution_y = 1200
cam = bpy.data.objects.new('cam', bpy.data.cameras.new('cam'))
scene.collection.objects.link(cam)
scene.camera = cam
centre = Vector((0, 0, TARGET_HEIGHT_M * 0.5))
for name, d in [('vorn', Vector((0, -1, 0.12))), ('seite', Vector((1, 0, 0.12)))]:
    cam.location = centre + d.normalized() * 3.2
    cam.rotation_euler = (centre - cam.location).to_track_quat('-Z', 'Y').to_euler()
    scene.render.filepath = str(PREVIEW / ('denno_game_%s.png' % name))
    bpy.ops.render.render(write_still=True)
print('###DENNO done')
