"""Import wheel-free ESWE body and wheel without replacing the original bus.

Run after bake_eswebus_wheels.py with UnrealEditor-Cmd -run=pythonscript.
The original SM_Bus owns the corrected project material instances.  Reuse them
by material slot name, then remove only this script's temporary import assets.
"""

import os
import unreal


eal = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
root = '/Game/Vehicles/Bus'
temporary = root + '/WheelImportTemp'
report_path = os.path.join(unreal.Paths.project_saved_dir(),
                           'Diagnose', 'eswe_wheel_import.txt')
lines = []


def report(message):
    lines.append(str(message))
    unreal.log('###ESWEWHEELS### ' + str(message))


def save_report():
    os.makedirs(os.path.dirname(report_path), exist_ok=True)
    with open(report_path, 'w', encoding='utf-8') as stream:
        stream.write('\n'.join(lines) + '\n')


original = eal.load_asset(root + '/SM_Bus')
if not isinstance(original, unreal.StaticMesh):
    report('ERROR: original SM_Bus missing')
    save_report()
    raise RuntimeError(lines[-1])
materials = {}
for slot in original.get_editor_property('static_materials'):
    name = str(slot.get_editor_property('material_slot_name'))
    material = slot.get_editor_property('material_interface')
    if material:
        materials[name] = material
report('Original bus: %d named material slots' % len(materials))

if eal.does_directory_exist(temporary):
    report('ERROR: temporary import folder already exists; inspect it manually')
    save_report()
    raise RuntimeError(lines[-1])
for name in ('SM_BusBody', 'SM_BusWheel'):
    if eal.does_asset_exist(root + '/' + name):
        report('ERROR: target %s already exists; preserving it' % name)
        save_report()
        raise RuntimeError(lines[-1])

for name, source in (('SM_BusBody', 'eswe_bus_body.glb'),
                     ('SM_BusWheel', 'eswe_bus_wheel.glb')):
    task = unreal.AssetImportTask()
    task.filename = os.path.join(unreal.Paths.project_dir(), 'Data', 'Raw', 'Bus', source)
    task.destination_path = temporary + '/' + name
    task.destination_name = name
    task.automated = True
    task.replace_existing = False
    task.save = True
    tools.import_asset_tasks([task])
    imported = [eal.load_asset(path)
                for path in eal.list_assets(task.destination_path, recursive=True)]
    meshes = [asset for asset in imported if isinstance(asset, unreal.StaticMesh)]
    if len(meshes) != 1:
        report('ERROR: %s imported %d StaticMeshes' % (name, len(meshes)))
        save_report()
        raise RuntimeError(lines[-1])
    mesh = meshes[0]
    slots = mesh.get_editor_property('static_materials')
    missing = []
    for index, slot in enumerate(slots):
        slot_name = str(slot.get_editor_property('material_slot_name'))
        material = materials.get(slot_name)
        if material is None:
            missing.append(slot_name)
        else:
            mesh.set_material(index, material)
    if missing:
        report('ERROR: %s material slots without old match: %s' % (name, missing))
        save_report()
        raise RuntimeError(lines[-1])
    settings = mesh.get_editor_property('nanite_settings')
    settings.set_editor_property('enabled', True)
    mesh.set_editor_property('nanite_settings', settings)
    eal.save_loaded_asset(mesh)
    old_path = mesh.get_path_name().split('.')[0]
    target = root + '/' + name
    if not eal.rename_asset(old_path, target):
        report('ERROR: could not move %s to %s' % (old_path, target))
        save_report()
        raise RuntimeError(lines[-1])
    eal.save_asset(target, only_if_is_dirty=False)
    bounds = mesh.get_bounds().box_extent
    report('%s: %d material slots reused; size %.1f x %.1f x %.1f cm' %
           (name, len(slots), bounds.x * 2, bounds.y * 2, bounds.z * 2))

# The temporary materials and duplicate textures are not referenced by either
# imported mesh once its original material instances have been assigned.
if not eal.delete_directory(temporary):
    report('WARNING: temporary import assets remain in ' + temporary)
else:
    report('Temporary import assets removed')
save_report()
