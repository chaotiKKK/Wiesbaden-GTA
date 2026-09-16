import bpy, sys, os

SRC = r"C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Data\Raw\Shuttle\tripo_convert_ad477794-a94e-42cd-980f-99f27b6028fe.obj"
OUT = r"C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Data\Raw\Shuttle\shuttle.glb"
TARGET_FACES = 250000

def log(m):
    print("###SHUTTLE### " + m); sys.stdout.flush()

bpy.ops.wm.read_factory_settings(use_empty=True)
log("import OBJ ...")
bpy.ops.wm.obj_import(filepath=SRC)
meshes = [o for o in bpy.context.scene.objects if o.type == 'MESH']
log("meshes: %d" % len(meshes))
if not meshes:
    log("FEHLER: kein Mesh"); raise SystemExit(1)
bpy.ops.object.select_all(action='DESELECT')
for o in meshes:
    o.select_set(True)
bpy.context.view_layer.objects.active = meshes[0]
if len(meshes) > 1:
    bpy.ops.object.join()
obj = bpy.context.view_layer.objects.active
faces = len(obj.data.polygons)
log("faces vor decimate: %d" % faces)
if faces > TARGET_FACES:
    m = obj.modifiers.new('dec', 'DECIMATE')
    m.decimate_type = 'COLLAPSE'
    m.ratio = float(TARGET_FACES) / faces
    bpy.ops.object.modifier_apply(modifier='dec')
    log("faces nach decimate: %d" % len(obj.data.polygons))
bpy.ops.object.origin_set(type='ORIGIN_GEOMETRY', center='BOUNDS')
obj.location = (0.0, 0.0, 0.0)
d = obj.dimensions
log("dims Blender X=%.4f Y=%.4f Z=%.4f" % (d.x, d.y, d.z))
mats = [s.material.name for s in obj.material_slots if s.material]
log("materialien: %s" % ", ".join(mats))
bpy.ops.object.select_all(action='DESELECT')
obj.select_set(True)
bpy.context.view_layer.objects.active = obj
log("export GLB ...")
bpy.ops.export_scene.gltf(filepath=OUT, export_format='GLB', use_selection=True)
sz = os.path.getsize(OUT) if os.path.exists(OUT) else -1
log("FERTIG glb bytes=%d" % sz)