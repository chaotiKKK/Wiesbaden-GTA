"""Importiert die Birke (SM_WbTree_07.fbx) als Baumvariante nach Unreal.

- Ziel: /Game/Vegetation/Meshes/SM_WbTree_07 (der Spawner laedt SM_WbTree_01..07).
- import_uniform_scale hebt die 3,9-m-Birke auf ~10 m (Strassenbaum-Groesse,
  passend zu den 15-m-AAA-Baeumen; TreeScaleBoost skaliert nochmal 1,35).
- Materialien aus der FBX (Rinde braun, Blatt gruen). Das Blatt-Material wird
  ZWEISEITIG geschaltet, damit die duennen Blattpolygone von beiden Seiten
  sichtbar sind (sonst loechrige Krone).

Aufruf:
  UnrealEditor.exe WiesbadenReal.uproject -ExecCmds="py Tools/import_birch.py"
      -unattended -nosplash
"""

import unreal

FBX = r"C:/Users/HP/Downloads/_treeimport/birch_out/SM_WbTree_07.fbx"
DEST = "/Game/Vegetation/Meshes"
NAME = "SM_WbTree_07"
TARGET_SCALE = 2.5   # 3,9 m * 2,5 ~ 9,75 m


def log(m):
    unreal.log("###BIRCHIMP### %s" % m)


opts = unreal.FbxImportUI()
opts.import_mesh = True
opts.import_as_skeletal = False
opts.import_materials = True
opts.import_textures = True
opts.mesh_type_to_import = unreal.FBXImportType.FBXIT_STATIC_MESH
sm_data = opts.static_mesh_import_data
sm_data.import_uniform_scale = TARGET_SCALE
sm_data.combine_meshes = True
sm_data.generate_lightmap_u_vs = True
sm_data.auto_generate_collision = False

task = unreal.AssetImportTask()
task.filename = FBX
task.destination_path = DEST
task.destination_name = NAME
task.automated = True
task.replace_existing = True
task.save = True
task.options = opts

unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

asset_path = "%s/%s" % (DEST, NAME)
mesh = unreal.EditorAssetLibrary.load_asset(asset_path)
if not isinstance(mesh, unreal.StaticMesh):
    log("FEHLER: %s nicht als StaticMesh importiert" % asset_path)
    if unreal.SystemLibrary.get_command_line().find("-unattended") >= 0:
        unreal.SystemLibrary.quit_editor()
    raise SystemExit(1)

# Materialien protokollieren + Blatt-Material zweiseitig schalten.
mats = mesh.get_editor_property("static_materials")
log("Importiert: %s mit %d Material-Slots" % (NAME, len(mats)))
for sm in mats:
    slot = str(sm.material_slot_name)
    mi = sm.material_interface
    is_leaf = "leaf" in slot.lower() or "blatt" in slot.lower() or "summer" in slot.lower()
    log("  Slot '%s' -> %s%s" % (slot, mi.get_name() if mi else "None",
                                  "  [Blatt]" if is_leaf else ""))
    if is_leaf and isinstance(mi, unreal.Material):
        mi.set_editor_property("two_sided", True)
        unreal.EditorAssetLibrary.save_loaded_asset(mi)

unreal.EditorAssetLibrary.save_loaded_asset(mesh)
log("FERTIG - %s gespeichert." % asset_path)

if unreal.SystemLibrary.get_command_line().find("-unattended") >= 0:
    unreal.SystemLibrary.quit_editor()
