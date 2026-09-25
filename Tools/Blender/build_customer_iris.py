"""Iris (Tripo-Figur) als Kundin der Denno-Lieferungen: dezimieren, Arme senken,
1,66 m, Blick +X, drei Gangposen.

Quelle: Data/Raw/Kunde/iris_source.glb (Kopie von "iris 3d model.glb";
Tripo, 10 Teile, 1,94 Mio. Dreiecke, 0,98 m normiert, T-Pose, kein Skelett).
Ziel:   Data/Raw/Kunde/iris_customer.glb (-> Tools/import_customer_iris.py) mit
        drei Meshes aus DERSELBEN dezimierten Figur:
          SM_Iris_Stand    Durchgangsstellung (Warten, Gangphasen 1 und 3)
          SM_Iris_StrideA  Schritt: Bein +X-Seite vorn (Gangphase 0)
          SM_Iris_StrideB  Schritt gespiegelt (Gangphase 2)
        Das ist das Vier-Phasen-Schema der Fussgaenger (SM_WbPed2*_0..3):
        AWiesbadenDeliveryCustomer wechselt die Posen nach gegangener Strecke.

Kein Skelett: Arme und Beine werden per Gewichtsfeld um Schulter bzw. Huefte
gedreht - volle Drehung im Glied, weicher Uebergang am Gelenk, sonst reisst
der Stoff am Rumpf auf (wie build_denno_figure.py).

Blender 5.2: blender -b -P Tools/Blender/build_customer_iris.py
"""
import bpy
import math
from pathlib import Path
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / 'Data/Raw/Kunde/iris_source.glb'
OUT = ROOT / 'Data/Raw/Kunde/iris_customer.glb'
PREVIEW = ROOT.parent / '.planning/kunde-iris'
TARGET_HEIGHT_M = 1.66
# Die Kundin steht auf dem Gehweg; der Spieler sieht sie aus dem Auto (5-15 m)
# oder zu Fuss aus 2-3 m. 16k Dreiecke je Pose, Texturen hoechstens 512 px -
# dieselbe Regel wie Dennos Laden (Hygiene-Test).
TARGET_TRIS = 16000
TEXTURE_MAX = 512
TEXTURE_MIN = 128
ARM_OUTWARD_DEG = 10.0   # Haende haengen knapp neben der Huefte
# Schrittweite: die Fussgaenger-Posen sind im Schritt 81 cm tief (1,75 m Figur).
LEG_SWING_DEG = 21.0
ARM_SWING_DEG = 15.0


def smoothstep(a, b, x):
    t = max(0.0, min(1.0, (x - a) / (b - a)))
    return t * t * (3 - 2 * t)


def rotate_x(p, angle, pivot_y, pivot_z):
    """Um die Querachse (X) durch (pivot_y, pivot_z) drehen; negativ = nach vorn (-Y)."""
    dy, dz = p.y - pivot_y, p.z - pivot_z
    c, s = math.cos(angle), math.sin(angle)
    return Vector((p.x, pivot_y + dy * c - dz * s, pivot_z + dy * s + dz * c))


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
body.name = 'SM_Iris_Stand'
for o in list(bpy.context.scene.objects):
    if o != body:
        bpy.data.objects.remove(o, do_unlink=True)

# Saubere, eindeutige Namen statt der Tripo-Vorgabe ("iris+3d+model_tripo_
# part_0_basecolor"): der glTF-Import uebernimmt sie als Asset-Namen.
for mat in body.data.materials:
    part = mat.name.split('tripo_part_')[-1].split('_')[0]
    mat.name = 'M_Iris_Part%s' % part
    for node in mat.node_tree.nodes:
        if node.type == 'TEX_IMAGE' and node.image:
            img = node.image
            img.name = 'T_Iris_Part%s' % part
            w, h = img.size
            edge = max(w, h)
            if edge > TEXTURE_MAX:
                f = max(TEXTURE_MAX / edge, TEXTURE_MIN / min(w, h))
                img.scale(max(TEXTURE_MIN, round(w * f)), max(TEXTURE_MIN, round(h * f)))
            print('###IRIS texture', img.name, (w, h), '->', tuple(img.size))

tris = sum(len(p.vertices) - 2 for p in body.data.polygons)
mod = body.modifiers.new('decimate', 'DECIMATE')
mod.ratio = min(1.0, TARGET_TRIS / max(tris, 1))
bpy.ops.object.modifier_apply(modifier=mod.name)
print('###IRIS decimate', tris, '->', len(body.data.polygons))

verts = body.data.vertices
zs = [v.co.z for v in verts]
height = max(zs) - min(zs)
y_mid = (max(v.co.y for v in verts) + min(v.co.y for v in verts)) / 2

# --- Arme senken (wie Denno) ------------------------------------------------
span = max(abs(v.co.x) for v in verts)
far = [v.co for v in verts if abs(v.co.x) > 0.55 * span]
arm_z = sum(p.z for p in far) / len(far)
band = [abs(v.co.x) for v in verts if arm_z - 0.16 * height < v.co.z < arm_z - 0.09 * height]
shoulder_x = sorted(band)[int(len(band) * 0.97)]
print('###IRIS arm_z %.3f shoulder_x %.3f height %.3f' % (arm_z, shoulder_x, height))
theta = math.radians(90.0 - ARM_OUTWARD_DEG)
arm_weight = [0.0] * len(verts)
for v in verts:
    p = v.co
    ax = abs(p.x)
    w = smoothstep(shoulder_x - 0.015 * height, shoulder_x + 0.05 * height, ax) \
        * smoothstep(arm_z - 0.11 * height, arm_z - 0.05 * height, p.z)
    arm_weight[v.index] = w
    if w <= 0.0:
        continue
    side = 1.0 if p.x > 0 else -1.0
    t = theta * side
    px, pz = p.x - side * shoulder_x, p.z - arm_z
    rx = px * math.cos(t) + pz * math.sin(t)
    rz = -px * math.sin(t) + pz * math.cos(t)
    target = Vector((rx + side * shoulder_x, p.y, rz + arm_z))
    v.co = p.lerp(target, w)

# --- Schritt: wo die Beine auseinandergehen --------------------------------
# Von unten nach oben die erste Hoehe, in der in der Mitte (zwischen den
# Beinen) Stoff liegt = Schritt. Darueber liegt das Hueftgelenk.
x_mid = sum(v.co.x for v in verts) / len(verts)
gap = 0.012 * height
crotch_z = 0.46 * height
z = 0.15 * height
while z < 0.65 * height:
    if any(abs(v.co.x - x_mid) < gap and z <= v.co.z < z + 0.01 * height for v in verts):
        crotch_z = z
        break
    z += 0.005 * height
hip_z = crotch_z + 0.06 * height
print('###IRIS crotch_z %.3f hip_z %.3f (x_mid %.3f)' % (crotch_z, hip_z, x_mid))
leg_weight = [0.0] * len(verts)
for v in verts:
    p = v.co
    if arm_weight[v.index] > 0.0:
        continue   # die (gesenkten) Haende reichen bis zur Huefte - sie gehoeren dem Arm
    leg_weight[v.index] = (1.0 - smoothstep(crotch_z - 0.03 * height, hip_z, p.z)) \
        * smoothstep(0.0, 0.025 * height, abs(p.x - x_mid))

rest = [v.co.copy() for v in verts]


def pose(mesh_verts, stride_sign):
    """stride_sign +1: Bein auf der +X-Seite vorn, Arm dort hinten; -1 gespiegelt; 0 stehen."""
    for v in mesh_verts:
        p = rest[v.index]
        side = 1.0 if p.x > x_mid else -1.0
        out = p
        wl = leg_weight[v.index]
        if stride_sign and wl > 0.0:
            a = -math.radians(LEG_SWING_DEG) * stride_sign * side
            out = p.lerp(rotate_x(p, a, y_mid, hip_z), wl)
        wa = arm_weight[v.index]
        if stride_sign and wa > 0.0:
            a = math.radians(ARM_SWING_DEG) * stride_sign * side
            out = out.lerp(rotate_x(out, a, y_mid, arm_z), wa)
        v.co = out


# --- Masstab, Blick +X, Ursprung an den Fuessen ----------------------------
# Die Fussgaenger-Meshes blicken nach +X; Tripo blickt nach -Y: +90 Grad um Z.
# Blender (x, y, z) landet in UE als (x, -y, z) - +X bleibt +X.
scale = TARGET_HEIGHT_M / height
lo = Vector((min(p.x for p in rest), min(p.y for p in rest), min(p.z for p in rest)))
hi = Vector((max(p.x for p in rest), max(p.y for p in rest), max(p.z for p in rest)))
centre = Vector(((lo.x + hi.x) / 2, (lo.y + hi.y) / 2, lo.z))


def finish(obj):
    """Skalieren, drehen, Ursprung: XY-Mitte der STEHENDEN Figur (keine Posen-
    Spruenge), Z = tiefster Punkt DIESER Pose (im Schritt sinkt die Huefte)."""
    vs = obj.data.vertices
    for v in vs:
        p = (v.co - centre) * scale
        v.co = Vector((-p.y, p.x, p.z))
    floor = min(v.co.z for v in vs)
    for v in vs:
        v.co.z -= floor
    for poly in obj.data.polygons:
        poly.use_smooth = True


objects = []
for name, sign in [('SM_Iris_Stand', 0), ('SM_Iris_StrideA', 1), ('SM_Iris_StrideB', -1)]:
    obj = body if sign == 0 else body.copy()
    if obj is not body:
        obj.data = body.data.copy()
        bpy.context.scene.collection.objects.link(obj)
    obj.name = name
    obj.data.name = name
    objects.append((obj, sign))
for obj, sign in objects:
    pose(obj.data.vertices, sign)
    finish(obj)
    d = obj.dimensions
    print('###IRIS pose', obj.name, 'dims %.3f x %.3f x %.3f' % (d.x, d.y, d.z))

OUT.parent.mkdir(parents=True, exist_ok=True)
bpy.ops.object.select_all(action='DESELECT')
for obj, _ in objects:
    obj.select_set(True)
bpy.ops.export_scene.gltf(filepath=str(OUT), export_format='GLB', use_selection=True,
                          export_apply=True, export_yup=True,
                          export_image_format='JPEG', export_jpeg_quality=90)
print('###IRIS exported', OUT, 'tris', len(body.data.polygons), 'materials', len(body.data.materials))

# --- Vorschau: drei Posen nebeneinander, von der Seite und von vorn ---------
scene = bpy.context.scene
scene.render.engine = 'BLENDER_WORKBENCH'
scene.display.shading.color_type = 'TEXTURE'
scene.display.shading.light = 'STUDIO'
scene.render.resolution_x = 1500
scene.render.resolution_y = 900
for i, (obj, _) in enumerate(objects):
    obj.location = ((i - 1) * 1.1, 0.0, 0.0)
cam = bpy.data.objects.new('cam', bpy.data.cameras.new('cam'))
scene.collection.objects.link(cam)
scene.camera = cam
look = Vector((0, 0, TARGET_HEIGHT_M * 0.5))
for name, d in [('seite', Vector((0, -1, 0.08))), ('schraeg', Vector((0.8, -1, 0.15)))]:
    cam.location = look + d.normalized() * 4.4
    cam.rotation_euler = (look - cam.location).to_track_quat('-Z', 'Y').to_euler()
    scene.render.filepath = str(PREVIEW / ('iris_posen_%s.png' % name))
    bpy.ops.render.render(write_still=True)
print('###IRIS done')
