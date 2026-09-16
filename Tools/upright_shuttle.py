import bpy, math, os
import numpy as np

GLB = r"C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Data\Raw\Shuttle\shuttle.glb"
OUT = r"C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Data\Raw\Shuttle"
OUTGLB = os.path.join(OUT, "shuttle_upright.glb")

def P(m): print("###UP### " + m)

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=GLB)
ms = [o for o in bpy.context.scene.objects if o.type == 'MESH']
obj = ms[0]
bpy.ops.object.select_all(action='DESELECT')
obj.select_set(True)
bpy.context.view_layer.objects.active = obj
if len(ms) > 1:
    bpy.ops.object.join()
me = obj.data
n = len(me.vertices)
co = np.empty(n * 3, dtype=np.float64)
me.vertices.foreach_get('co', co)
co = co.reshape(n, 3)

def rot(cur, ax, ay):
    rx = math.radians(ax); ry = math.radians(ay)
    x = cur[:, 0]; y = cur[:, 1]; z = cur[:, 2]
    ya = y * math.cos(rx) - z * math.sin(rx)
    za = y * math.sin(rx) + z * math.cos(rx)
    xb = x * math.cos(ry) + za * math.sin(ry)
    zb = -x * math.sin(ry) + za * math.cos(ry)
    return np.stack([xb, ya, zb], axis=1)

def zext(ax, ay):
    r = rot(co, ax, ay)
    return r[:, 2].max() - r[:, 2].min()

best = (-1.0, 0, 0)
for ax in range(0, 181, 15):
    for ay in range(0, 181, 15):
        e = zext(ax, ay)
        if e > best[0]:
            best = (e, ax, ay)
b = best
for ax in range(b[1] - 14, b[1] + 15, 3):
    for ay in range(b[2] - 14, b[2] + 15, 3):
        e = zext(ax, ay)
        if e > best[0]:
            best = (e, ax, ay)
P("beste Rotation ax=%d ay=%d Zausdehnung=%.3f" % (best[1], best[2], best[0]))

r = rot(co, best[1], best[2])
# Oben/unten pruefen: die breitere Haelfte gehoert nach unten (Startrampe).
zmin = r[:, 2].min(); zmax = r[:, 2].max(); H = zmax - zmin
zr = r[:, 2] - zmin
bot = zr < 0.2 * H; top = zr > 0.8 * H
def spread(mask):
    if mask.sum() < 3: return 0.0
    return float(np.ptp(r[mask, 0])) * float(np.ptp(r[mask, 1]))
sb = spread(bot); st = spread(top)
P("Breite unten=%.3f oben=%.3f" % (sb, st))
if st > sb:
    # 180 um X kippen (Kopf/Fuss tauschen)
    r = rot(r, 180, 0)
    P("umgedreht (oben war breiter)")

# auf Boden setzen: min Z -> 0
r[:, 2] -= r[:, 2].min()
me.vertices.foreach_set('co', r.reshape(-1).astype(np.float32).copy())
me.update()
bpy.ops.object.origin_set(type='ORIGIN_GEOMETRY', center='BOUNDS')
obj.location = (0.0, 0.0, 0.0)
P("dims final X=%.3f Y=%.3f Z=%.3f" % (obj.dimensions.x, obj.dimensions.y, obj.dimensions.z))

# Bestaetigungs-Renders (Workbench)
sc = bpy.context.scene
sc.render.engine = 'BLENDER_WORKBENCH'
sc.render.resolution_x = 700; sc.render.resolution_y = 700
d = max(obj.dimensions.x, obj.dimensions.y, obj.dimensions.z)
cx, cy, cz = obj.location.x, obj.location.y, obj.location.z + obj.dimensions.z * 0.0
cd = bpy.data.cameras.new('C'); cam = bpy.data.objects.new('C', cd)
sc.collection.objects.link(cam); sc.camera = cam
cd.type = 'ORTHO'; cd.ortho_scale = d * 1.35
R = d * 4.0
zc = obj.dimensions.z * 0.5
def shot(name, loc, rr):
    cam.location = loc; cam.rotation_euler = rr
    sc.render.filepath = os.path.join(OUT, name)
    bpy.ops.render.render(write_still=True)
shot("view_up_front.png", (0.0, -R, zc), (math.radians(90), 0.0, 0.0))
shot("view_up_side.png",  (R, 0.0, zc),  (math.radians(90), 0.0, math.radians(90)))

bpy.ops.object.select_all(action='DESELECT'); obj.select_set(True); bpy.context.view_layer.objects.active = obj
bpy.ops.export_scene.gltf(filepath=OUTGLB, export_format='GLB', use_selection=True)
P("FERTIG %s (%d bytes)" % (OUTGLB, os.path.getsize(OUTGLB) if os.path.exists(OUTGLB) else -1))