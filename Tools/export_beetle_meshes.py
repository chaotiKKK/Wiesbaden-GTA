"""Exportiert Kaefer-Karosserie und -Rad aus Unreal zurueck nach FBX.

Fuer die echte Fahrzeugphysik (Chaos Vehicles) braucht es ein SKELETT-Mesh mit
Radknochen. Der Kaefer liegt als zwei statische Meshes vor - Karosserie und
ein einzelnes Rad -, das Quellmodell, aus dem sie einmal getrennt wurden, ist
nicht mehr auf der Platte. Der Weg zurueck fuehrt deshalb ueber den Export aus
Unreal.

Aufruf (VOLLER Editor, nicht -run=pythonscript - der Exporter ist Editor-API):
  UnrealEditor.exe WiesbadenReal.uproject -ExecCmds="py Tools/export_beetle_meshes.py" -unattended -nosplash
"""

import os

import unreal

OUT = os.path.join(unreal.Paths.project_dir(), "Data", "Raw", "Beetle")
ASSETS = [
    "/Game/Vehicles/Beetle/SM_VWBeetle1969_Body",
    "/Game/Vehicles/Beetle/SM_VWBeetle_Wheel",
]


def log(msg):
    unreal.log("###WBEXP### %s" % msg)


os.makedirs(OUT, exist_ok=True)

for path in ASSETS:
    mesh = unreal.EditorAssetLibrary.load_asset(path)
    if mesh is None:
        log("FEHLT: %s" % path)
        continue

    target = os.path.join(OUT, mesh.get_name() + ".fbx")

    task = unreal.AssetExportTask()
    task.object = mesh
    task.filename = target
    task.automated = True
    task.prompt = False
    task.replace_identical = True

    options = unreal.FbxExportOption()
    options.set_editor_property("collision", False)
    options.set_editor_property("level_of_detail", False)
    options.set_editor_property("vertex_color", True)
    task.options = options

    if unreal.Exporter.run_asset_export_task(task) and os.path.exists(target):
        log("%s -> %s (%d Dreiecke)"
            % (mesh.get_name(), target, mesh.get_num_triangles(0)))
    else:
        log("Export fehlgeschlagen: %s" % mesh.get_name())

log("FERTIG")

if os.environ.get("WB_QUIT"):
    unreal.SystemLibrary.quit_editor()
