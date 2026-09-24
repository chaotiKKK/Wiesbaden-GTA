import unreal, os
EAL = unreal.EditorAssetLibrary
DEST = "/Game/Vehicles/Traffic"
SRC = os.path.join(unreal.Paths.project_dir(), "Content", "Vehicles", "Traffic", "src")
NAMES = ["SM_TrafficTransporter", "SM_TrafficKombi", "SM_TrafficBus"]
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
        b = a.get_bounds()
        o = b.box_extent; org = b.origin
        try: ns = len(a.static_materials)
        except: ns = -1
        lines.append("%s origin=(%.2f,%.2f,%.2f) extent=(%.2f,%.2f,%.2f) slots=%d" % (
            p, org.x, org.y, org.z, o.x, o.y, o.z, ns))
with open(os.path.join(unreal.Paths.project_dir(), "veh_bounds.txt"), "w") as f:
    f.write("\n".join(lines) if lines else "NO STATICMESH IMPORTED")
if unreal.SystemLibrary.get_command_line().find("-unattended") >= 0:
    unreal.SystemLibrary.quit_editor()
