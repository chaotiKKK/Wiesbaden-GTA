"""Import the reviewed wheel-free Beetle shell without touching the old scan.

UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=Tools/import_restored_beetle.py
"""
import os
import unreal

eal = unreal.EditorAssetLibrary
root = '/Game/Vehicles/Beetle/Restored'
name = 'SM_VWBeetle1969_Restored'
# The glTF importer groups its mesh and generated materials under the GLB
# filename, independent of AssetImportTask.destination_name.
target = root + '/restored_beetle_body/StaticMeshes/' + name
source = os.path.join(unreal.Paths.project_dir(),
                      'Data', 'Raw', 'Beetle', 'Herbie', 'restored_beetle_body.glb')
report = os.path.join(unreal.Paths.project_saved_dir(),
                      'Diagnose', 'restored_beetle_import.txt')
# This isolated folder is produced only by this script. Repeat imports update
# our generated shell and materials while preserving the old scanned mesh.
if not os.path.isfile(source):
    raise FileNotFoundError(source)

eal.make_directory(root)
task = unreal.AssetImportTask()
task.filename = source
task.destination_path = root
task.destination_name = name
task.automated = True
task.replace_existing = True
task.save = True
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
mesh = eal.load_asset(target)
if not isinstance(mesh, unreal.StaticMesh):
    raise RuntimeError('Restored body import did not create ' + target)
settings = mesh.get_editor_property('nanite_settings')
settings.set_editor_property('enabled', False)
mesh.set_editor_property('nanite_settings', settings)
eal.save_loaded_asset(mesh)
slots = mesh.get_editor_property('static_materials')
ext = mesh.get_bounds().box_extent
summary = ('%s: %d materials, dimensions %.1f x %.1f x %.1f cm\n%s' %
           (target, len(slots), ext.x*2, ext.y*2, ext.z*2,
            '\n'.join(str(slot.get_editor_property('material_slot_name'))
                      for slot in slots)))
os.makedirs(os.path.dirname(report), exist_ok=True)
with open(report, 'w', encoding='utf-8') as stream:
    stream.write(summary + '\n')
unreal.log('###RESTORED_BEETLE### ' + summary.replace('\n', ' | '))
