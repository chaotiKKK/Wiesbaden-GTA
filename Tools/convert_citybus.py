import bpy, sys, os
SRC = r"C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Data\Raw\Bus\citybus_src\city+bus+3d+model.obj"
OUT = r"C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Data\Raw\Bus\citybus.glb"
def log(m): print("###CITYBUS### " + m); sys.stdout.flush()
bpy.ops.wm.read_factory_settings(use_empty=True)
log("import OBJ ...")
bpy.ops.wm.obj_import(filepath=SRC)
ms = [o for o in bpy.context.scene.objects if o.type == 'MESH']
if not ms: log("FEHLER kein Mesh"); raise SystemExit(1)
bpy.ops.object.select_all(action='DESELECT')
for o in ms: o.select_set(True)
bpy.context.view_layer.objects.active = ms[0]
if len(ms) > 1: bpy.ops.object.join()
obj = bpy.context.view_layer.objects.active
f = len(obj.data.polygons); log("faces vor: %d" % f)
if f > 250000:
    m = obj.modifiers.new('d', 'DECIMATE'); m.ratio = 250000.0 / f
    bpy.ops.object.modifier_apply(modifier='d'); log("faces nach: %d" % len(obj.data.polygons))
# Gleiche Pipeline wie convert_bus.py: Pivot in die Mesh-Mitte (der Actor gleicht
# ueber die Mesh-Unterkante aus), Objekt an den Ursprung.
bpy.ops.object.origin_set(type='ORIGIN_GEOMETRY', center='BOUNDS'); obj.location = (0, 0, 0)
d = obj.dimensions; log("dims X=%.3f Y=%.3f Z=%.3f" % (d.x, d.y, d.z))
bpy.ops.object.select_all(action='DESELECT'); obj.select_set(True); bpy.context.view_layer.objects.active = obj
bpy.ops.export_scene.gltf(filepath=OUT, export_format='GLB', use_selection=True)
log("FERTIG %d bytes" % (os.path.getsize(OUT) if os.path.exists(OUT) else -1))
