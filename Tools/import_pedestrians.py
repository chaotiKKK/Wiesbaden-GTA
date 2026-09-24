import unreal, os
EAL = unreal.EditorAssetLibrary
DEST = "/Game/Assets/People/Varied"
SRC = os.path.join(unreal.Paths.project_dir(), "Content", "Assets", "People", "Varied", "src")
NAMES = ["SM_WbPed2_0", "SM_WbPed2_1", "SM_WbPed2_2", "SM_WbPed2_3"]
EAL.make_directory(DEST)
tasks = []
for n in NAMES:
    t = unreal.AssetImportTask()
    t.filename = os.path.join(SRC, n + ".glb")
    t.destination_path = DEST
    t.destination_name = n
    t.automated = True
    t.replace_existing = True
    t.save = True
    tasks.append(t)
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
lines = []
for p in EAL.list_assets(DEST, recursive=True):
    a = EAL.load_asset(p)
    if isinstance(a, unreal.StaticMesh):
        b = a.get_bounds(); o = b.box_extent; org = b.origin
        lines.append("%s origin=(%.1f,%.1f,%.1f) extent=(%.1f,%.1f,%.1f) slots=%d" % (
            p.split('/')[-1], org.x, org.y, org.z, o.x, o.y, o.z, len(a.static_materials)))
open(os.path.join(unreal.Paths.project_dir(), "ppl.txt"), "w").write("\n".join(lines) if lines else "NONE")
if unreal.SystemLibrary.get_command_line().find("-unattended") >= 0:
    unreal.SystemLibrary.quit_editor()
