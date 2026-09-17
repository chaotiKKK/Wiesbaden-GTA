import bpy, sys, os
SRC = r"C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Data\Raw\Bus\tripo_convert_456a8185-9fb2-4eb4-a71c-f1bf8f83cf2d.obj"
OUT = r"C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Data\Raw\Bus\bus.glb"
def log(m): print("###BUS### "+m); sys.stdout.flush()
bpy.ops.wm.read_factory_settings(use_empty=True)
log("import OBJ ...")
bpy.ops.wm.obj_import(filepath=SRC)
ms=[o for o in bpy.context.scene.objects if o.type=='MESH']
if not ms: log("FEHLER kein Mesh"); raise SystemExit(1)
bpy.ops.object.select_all(action='DESELECT')
for o in ms: o.select_set(True)
bpy.context.view_layer.objects.active=ms[0]
if len(ms)>1: bpy.ops.object.join()
obj=bpy.context.view_layer.objects.active
f=len(obj.data.polygons); log("faces vor: %d"%f)
if f>250000:
    m=obj.modifiers.new('d','DECIMATE'); m.ratio=250000.0/f
    bpy.ops.object.modifier_apply(modifier='d'); log("faces nach: %d"%len(obj.data.polygons))
bpy.ops.object.origin_set(type='ORIGIN_GEOMETRY', center='BOUNDS'); obj.location=(0,0,0)
d=obj.dimensions; log("dims X=%.3f Y=%.3f Z=%.3f"%(d.x,d.y,d.z))
bpy.ops.object.select_all(action='DESELECT'); obj.select_set(True); bpy.context.view_layer.objects.active=obj
bpy.ops.export_scene.gltf(filepath=OUT, export_format='GLB', use_selection=True)
log("FERTIG %d bytes"%(os.path.getsize(OUT) if os.path.exists(OUT) else -1))