import bpy, math, os
import numpy as np

GLB = r"C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Data\Raw\Shuttle\shuttle.glb"
OUT = r"C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Data\Raw\Shuttle"

def P(m): print("###C### " + m)

def build(tag, ang):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=GLB)
    ms = [o for o in bpy.context.scene.objects if o.type == 'MESH']
    obj = ms[0]
    bpy.ops.object.select_all(action='DESELECT'); obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    if len(ms) > 1: bpy.ops.object.join()
    me = obj.data
    n = len(me.vertices)
    co = np.empty(n*3, dtype=np.float64); me.vertices.foreach_get('co', co); co = co.reshape(n,3)
    r = math.radians(ang)
    x = co[:,0]; y = co[:,1]; z = co[:,2]
    ya = y*math.cos(r) - z*math.sin(r)
    za = y*math.sin(r) + z*math.cos(r)
    out = np.stack([x, ya, za], axis=1)
    out[:,2] -= out[:,2].min()
    me.vertices.foreach_set('co', out.reshape(-1).astype(np.float32).copy())
    me.update()
    bpy.ops.object.origin_set(type='ORIGIN_GEOMETRY', center='BOUNDS'); obj.location=(0,0,0)
    P("%s dims X=%.2f Y=%.2f Z=%.2f" % (tag, obj.dimensions.x, obj.dimensions.y, obj.dimensions.z))
    sc = bpy.context.scene
    sc.render.engine = 'BLENDER_WORKBENCH'; sc.render.resolution_x=600; sc.render.resolution_y=600
    d = max(obj.dimensions.x, obj.dimensions.y, obj.dimensions.z); zc = obj.dimensions.z*0.5
    cd = bpy.data.cameras.new('C'); cam = bpy.data.objects.new('C', cd); sc.collection.objects.link(cam); sc.camera = cam
    cd.type='ORTHO'; cd.ortho_scale=d*1.35; R=d*4
    for nm, loc, rr in [("front",(0,-R,zc),(math.radians(90),0,0)), ("side",(R,0,zc),(math.radians(90),0,math.radians(90)))]:
        cam.location=loc; cam.rotation_euler=rr
        sc.render.filepath=os.path.join(OUT,"cand_%s_%s.png"%(tag,nm)); bpy.ops.render.render(write_still=True)
    bpy.ops.object.select_all(action='DESELECT'); obj.select_set(True); bpy.context.view_layer.objects.active=obj
    bpy.ops.export_scene.gltf(filepath=os.path.join(OUT,"shuttle_%s.glb"%tag), export_format='GLB', use_selection=True)
    P("%s exportiert" % tag)

build("p45", 45)
build("m45", -45)
P("FERTIG")