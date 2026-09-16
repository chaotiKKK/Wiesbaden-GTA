import unreal, os

GLB = os.path.join(unreal.Paths.project_dir(), "Data", "Raw", "Shuttle", "shuttle.glb")
DEST = "/Game/Props/Shuttle"

def log(m):
    unreal.log("###WBSHUTTLE### %s" % m)

EAL = unreal.EditorAssetLibrary
if not EAL.does_directory_exist(DEST):
    EAL.make_directory(DEST)

task = unreal.AssetImportTask()
task.filename = GLB
task.destination_path = DEST
task.automated = True
task.replace_existing = True
task.save = True
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
log("import fertig")

sm = None
smpath = None
for p in EAL.list_assets(DEST, recursive=True):
    a = EAL.load_asset(p)
    if isinstance(a, unreal.StaticMesh):
        sm = a
        smpath = p
        break

if sm is None:
    log("FEHLER: kein StaticMesh importiert")
else:
    newpath = DEST + "/SM_SpaceShuttle"
    if smpath != newpath and not EAL.does_asset_exist(newpath):
        EAL.rename_asset(smpath, newpath)
        sm = EAL.load_asset(newpath)
        smpath = newpath
    try:
        ns = sm.get_editor_property("nanite_settings")
        ns.set_editor_property("enabled", True)
        sm.set_editor_property("nanite_settings", ns)
        log("Nanite an")
    except Exception as e:
        log("Nanite setzen fehlgeschlagen: %s" % e)
    EAL.save_loaded_asset(sm)
    try:
        b = sm.get_bounds()
        ext = b.box_extent
        log("StaticMesh %s" % smpath)
        log("Groesse UU: X=%.1f Y=%.1f Z=%.1f" % (ext.x * 2.0, ext.y * 2.0, ext.z * 2.0))
    except Exception as e:
        log("bounds fehlgeschlagen: %s" % e)

# Alle importierten Assets auflisten
for p in EAL.list_assets(DEST, recursive=True):
    log("asset: %s" % p)

log("ENDE")