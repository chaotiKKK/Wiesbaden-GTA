import unreal, os
GLB = os.path.join(unreal.Paths.project_dir(), "Data", "Raw", "Bus", "bus.glb")
DEST = "/Game/Vehicles/Bus"
def log(m): unreal.log("###WBBUS### %s" % m)
EAL = unreal.EditorAssetLibrary
if EAL.does_directory_exist(DEST): EAL.delete_directory(DEST)
EAL.make_directory(DEST)
t = unreal.AssetImportTask(); t.filename=GLB; t.destination_path=DEST; t.automated=True; t.replace_existing=True; t.save=True
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])
sm=None; smpath=None
for p in EAL.list_assets(DEST, recursive=True):
    a=EAL.load_asset(p)
    if isinstance(a, unreal.StaticMesh): sm=a; smpath=p; break
if sm is None: log("FEHLER kein StaticMesh")
else:
    np=DEST+"/SM_Bus"
    if smpath!=np: EAL.rename_asset(smpath,np); sm=EAL.load_asset(np); smpath=np
    try:
        ns=sm.get_editor_property("nanite_settings"); ns.set_editor_property("enabled",True); sm.set_editor_property("nanite_settings",ns)
    except Exception as e: log("Nanite: %s"%e)
    EAL.save_loaded_asset(sm)
    b=sm.get_bounds(); e=b.box_extent
    log("SM %s  UU X=%.1f Y=%.1f Z=%.1f"%(smpath, e.x*2, e.y*2, e.z*2))
log("ENDE")