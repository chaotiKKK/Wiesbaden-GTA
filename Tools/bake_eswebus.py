"""Blender-Headless: backt eswe_bus.glb zu EINEM sauberen Mesh.

Grund: UEs glTF-Importer verzerrt die Node-Skalen (Laenge auf ~Haelfte gestaucht).
Loesung: alle Teile joinen, alle Node-Transforme in die Vertices backen, auf reale
Breite (2,55 m) skalieren, achsen-treu (Laenge bleibt glTF-Z) als Einzel-Mesh-GLB
zurueckschreiben. Danach importiert UE unverzerrt; MeshOrient (0,-90,0) bleibt gueltig.

Aufruf:  blender -b -P bake_eswebus.py
"""
import bpy, os, math

SRC = r"C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Data\Raw\Bus\eswe_bus.glb"
OUT = r"C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Data\Raw\Bus\eswe_bus_clean.glb"
TARGET_WIDTH_M = 2.55  # reale Busbreite -> bestimmt die Gesamtskalierung

def log(m): print("###BAKE### %s" % m)

# 1) Szene leeren
bpy.ops.wm.read_factory_settings(use_empty=True)

# 2) GLB importieren
bpy.ops.import_scene.gltf(filepath=SRC)
meshes = [o for o in bpy.context.scene.objects if o.type == 'MESH']
log("importiert: %d Mesh-Objekte" % len(meshes))

# 3) alle Mesh-Objekte auswaehlen und joinen
bpy.ops.object.select_all(action='DESELECT')
for o in meshes:
    o.select_set(True)
bpy.context.view_layer.objects.active = meshes[0]
if len(meshes) > 1:
    bpy.ops.object.join()
obj = bpy.context.view_layer.objects.active
log("gejoint zu: %s" % obj.name)

# 4) alle Transforme in die Vertices backen (identische Objekt-Transform danach)
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)

# 5) Groesse messen (Blender Z-up nach glTF-Import: Hoehe=Z, Grundriss=X/Y)
dim = obj.dimensions
log("nach Join/Apply (m): X=%.4f Y=%.4f Z=%.4f" % (dim.x, dim.y, dim.z))
horiz = sorted([("X", dim.x), ("Y", dim.y)], key=lambda t: t[1])
width_axis, width_m = horiz[0]   # schmalere Grundriss-Achse = Breite
length_axis, length_m = horiz[1] # laengere = Laenge
log("Breite-Achse=%s (%.4f), Laenge-Achse=%s (%.4f), Hoehe=Z (%.4f)" %
    (width_axis, width_m, length_axis, length_m, dim.z))

# 6) uniform skalieren, sodass Breite = TARGET_WIDTH_M
s = TARGET_WIDTH_M / width_m
obj.scale = (s, s, s)
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
dim = obj.dimensions
log("nach Skalierung s=%.4f (m): X=%.4f Y=%.4f Z=%.4f" % (s, dim.x, dim.y, dim.z))

# 7) Ursprung mittig unten (Grundriss-Mitte, Boden bei Z=0)
bpy.ops.object.origin_set(type='ORIGIN_GEOMETRY', center='BOUNDS')
obj.location = (0.0, 0.0, dim.z / 2.0)  # hebt Boden auf Z=0
bpy.ops.object.transform_apply(location=True, rotation=False, scale=False)

# 8) als sauberes Einzel-Mesh-GLB exportieren (Achsen wie Original -> MeshOrient bleibt)
bpy.ops.object.select_all(action='DESELECT')
obj.select_set(True)
bpy.context.view_layer.objects.active = obj
bpy.ops.export_scene.gltf(
    filepath=OUT,
    export_format='GLB',
    use_selection=True,
    export_apply=True,
    export_yup=True,
)
log("geschrieben: %s" % OUT)
log("ENDE Laenge~%.2fm Breite~%.2fm Hoehe~%.2fm" % (length_m*s, TARGET_WIDTH_M, dim.z))
