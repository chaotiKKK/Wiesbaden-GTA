"""Bake the ESWE bus into a wheel-free body and one animated wheel mesh.

The source GLB keeps each wheel's tire and hub in separate Tripo objects.  The
old bake joined every object, so wheel rotation in Unreal was impossible.  This
script leaves that source and the existing SM_Bus untouched and writes two new
GLBs for Tools/import_eswebus_wheels.py.
"""

import bpy
from mathutils import Matrix, Vector
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'Data/Raw/Bus/eswe_bus.glb'
DEST = ROOT / 'Data/Raw/Bus'
WHEEL_PARTS = {
    'tripo_part_7', 'tripo_part_17', 'tripo_part_34',  # front axle
    'tripo_part_12', 'tripo_part_18', 'tripo_part_28',
    'tripo_part_32', 'tripo_part_35',                   # middle axle
    'tripo_part_6', 'tripo_part_9', 'tripo_part_27',
    'tripo_part_29',                                   # rear axle
}
WHEEL_DONOR = {'tripo_part_17', 'tripo_part_34'}
WHEEL_GROUPS = {
    'front_left': WHEEL_DONOR,
    'front_right': {'tripo_part_7'},
    'middle_left': {'tripo_part_18', 'tripo_part_32', 'tripo_part_35'},
    'middle_right': {'tripo_part_12', 'tripo_part_28'},
    'rear_left': {'tripo_part_6', 'tripo_part_29'},
    'rear_right': {'tripo_part_9', 'tripo_part_27'},
}


def world_bounds(objects):
    points = [obj.matrix_world @ Vector(corner)
              for obj in objects for corner in obj.bound_box]
    return (Vector(min(v[i] for v in points) for i in range(3)),
            Vector(max(v[i] for v in points) for i in range(3)))


def join(objects, name):
    bpy.ops.object.select_all(action='DESELECT')
    for obj in objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    if len(objects) > 1:
        bpy.ops.object.join()
    result = bpy.context.view_layer.objects.active
    result.name = name
    return result


def export(obj, path):
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.export_scene.gltf(filepath=str(path), export_format='GLB',
                              use_selection=True, export_apply=True,
                              export_yup=True)
    print('###BUSWHEELS### exported', path, 'dimensions',
          tuple(round(d, 3) for d in obj.dimensions), flush=True)


bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=str(SOURCE))
parts = [obj for obj in bpy.context.scene.objects if obj.type == 'MESH']
names = {obj.name for obj in parts}
assert WHEEL_PARTS <= names, WHEEL_PARTS - names
lo, hi = world_bounds(parts)
scale = 2.55 / (hi.x - lo.x)
origin = Vector(((lo.x + hi.x) * .5, (lo.y + hi.y) * .5, lo.z))

# Apply every glTF node transform before changing scale or joining.  This gives
# both exports exactly the origin and real-world size of the old clean GLB.
for obj in parts:
    transform = Matrix.Scale(scale, 4) @ Matrix.Translation(-origin) @ obj.matrix_world.copy()
    obj.data.transform(transform)
    obj.parent = None
    obj.matrix_world = Matrix.Identity(4)

for name, part_names in WHEEL_GROUPS.items():
    group_vertices = [v.co for obj in parts if obj.name in part_names
                      for v in obj.data.vertices]
    low = Vector(min(v[i] for v in group_vertices) for i in range(3))
    high = Vector(max(v[i] for v in group_vertices) for i in range(3))
    print('###BUSWHEELS###', name, 'bounds',
          tuple(round(x, 3) for x in low),
          tuple(round(x, 3) for x in high), flush=True)

donor = [obj for obj in parts if obj.name in WHEEL_DONOR]
donor_vertices = [vertex.co for obj in donor for vertex in obj.data.vertices]
wheel_lo = Vector(min(v[i] for v in donor_vertices) for i in range(3))
wheel_hi = Vector(max(v[i] for v in donor_vertices) for i in range(3))
wheel_center = (wheel_lo + wheel_hi) * .5
print('###BUSWHEELS### wheel center in body coordinates',
      tuple(round(x, 3) for x in wheel_center), flush=True)
for obj in donor:
    obj.data.transform(Matrix.Translation(-wheel_center))

body = join([obj for obj in parts if obj.name not in WHEEL_PARTS], 'SM_BusBody')
export(body, DEST / 'eswe_bus_body.glb')
wheel = join(donor, 'SM_BusWheel')
export(wheel, DEST / 'eswe_bus_wheel.glb')
