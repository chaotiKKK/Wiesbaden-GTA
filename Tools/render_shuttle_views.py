import bpy, math, os
GLB = r"C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Data\Raw\Shuttle\shuttle.glb"
OUT = r"C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Data\Raw\Shuttle"

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=GLB)
meshes = [o for o in bpy.context.scene.objects if o.type == 'MESH']
obj = meshes[0]
bpy.ops.object.select_all(action='DESELECT')
obj.select_set(True)
bpy.context.view_layer.objects.active = obj
if len(meshes) > 1:
    bpy.ops.object.join()
bpy.ops.object.origin_set(type='ORIGIN_GEOMETRY', center='BOUNDS')
obj.location = (0.0, 0.0, 0.0)
d = max(obj.dimensions.x, obj.dimensions.y, obj.dimensions.z)
print("###SV### dims X=%.3f Y=%.3f Z=%.3f" % (obj.dimensions.x, obj.dimensions.y, obj.dimensions.z))

sc = bpy.context.scene
sc.render.engine = 'BLENDER_WORKBENCH'
sc.render.resolution_x = 700
sc.render.resolution_y = 700
sc.render.film_transparent = False
cd = bpy.data.cameras.new('C')
cam = bpy.data.objects.new('C', cd)
sc.collection.objects.link(cam)
sc.camera = cam
cd.type = 'ORTHO'
cd.ortho_scale = d * 1.35
R = d * 4.0

def shot(name, loc, rot):
    cam.location = loc
    cam.rotation_euler = rot
    sc.render.filepath = os.path.join(OUT, name)
    bpy.ops.render.render(write_still=True)
    print("###SV### %s" % name)

shot("view_front.png", (0.0, -R, 0.0), (math.radians(90), 0.0, 0.0))          # Blick +Y
shot("view_side.png",  (R, 0.0, 0.0),  (math.radians(90), 0.0, math.radians(90)))  # Blick -X
shot("view_top.png",   (0.0, 0.0, R),  (0.0, 0.0, 0.0))                        # Blick -Z (von oben)
print("###SV### FERTIG")