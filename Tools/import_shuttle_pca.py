import unreal, os
GLB = os.path.join(unreal.Paths.project_dir(), "Data", "Raw", "Shuttle", "shuttle_pca.glb")
DEST = "/Game/Props/Shuttle"
def log(m): unreal.log("###WBSHUTTLE### %s" % m)
EAL = unreal.EditorAssetLibrary
if EAL.does_directory_exist(DEST):
    EAL.delete_directory(DEST)
EAL.make_directory(DEST)
task = unreal.AssetImportTask()
task.filename = GLB; task.destination_path = DEST; task.automated = True; task.replace_existing = True; task.save = True
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
sm=None; smpath=None
for p in EAL.list_assets(DEST, recursive=True):
    a = EAL.load_asset(p)
    if isinstance(a, unreal.StaticMesh):
        sm=a; smpath=p; break
if sm is None:
    log("FEHLER kein StaticMesh")
else:
    newpath = DEST + "/SM_SpaceShuttle"
    if smpath != newpath:
        EAL.rename_asset(smpath, newpath); sm = EAL.load_asset(newpath); smpath=newpath
    try:
        ns = sm.get_editor_property("nanite_settings"); ns.set_editor_property("enabled", True); sm.set_editor_property("nanite_settings", ns)
    except Exception as e:
        log("Nanite: %s" % e)
    EAL.save_loaded_asset(sm)
    b = sm.get_bounds(); ext=b.box_extent
    log("SM %s  Groesse UU X=%.1f Y=%.1f Z=%.1f" % (smpath, ext.x*2, ext.y*2, ext.z*2))
log("ENDE")