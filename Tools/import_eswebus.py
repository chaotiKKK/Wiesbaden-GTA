"""Importiert eswe_bus.glb (texturierter Gelenkbus) und ersetzt SM_Bus.

Behaelt die mitgelieferten Materialien/Texturen (GLB) unter /Game/Vehicles/Bus/eswe,
verschiebt nur das StaticMesh nach SM_Bus. Schont /Game/Vehicles/Bus/Ziel, entfernt
die alten Tripo-Assets (bus/) und das neutrale M_WbBusBody.
"""
import unreal, os
def log(m): unreal.log("###ESWE### %s" % m)
EAL = unreal.EditorAssetLibrary
ATH = unreal.AssetToolsHelpers.get_asset_tools()
DEST = "/Game/Vehicles/Bus"
SUB = DEST + "/eswe"
GLB = os.path.join(unreal.Paths.project_dir(), "Data", "Raw", "Bus", "eswe_bus_clean.glb")

if EAL.does_directory_exist(SUB): EAL.delete_directory(SUB)
t = unreal.AssetImportTask()
t.set_editor_property("filename", GLB)
t.set_editor_property("destination_path", SUB)
t.set_editor_property("automated", True)
t.set_editor_property("replace_existing", True)
t.set_editor_property("save", True)
ATH.import_asset_tasks([t])

sm = None; smpath = None
for p in EAL.list_assets(SUB, recursive=True):
    a = EAL.load_asset(p)
    if isinstance(a, unreal.StaticMesh): sm = a; smpath = p; break
if sm is None:
    log("FEHLER kein StaticMesh importiert"); log("ENDE"); raise SystemExit(0)

try:
    ns = sm.get_editor_property("nanite_settings"); ns.set_editor_property("enabled", True)
    sm.set_editor_property("nanite_settings", ns)
except Exception as e:
    log("Nanite: %s" % e)
EAL.save_loaded_asset(sm)
b = sm.get_bounds(); e = b.box_extent
log("Rohbus UU X=%.1f Y=%.1f Z=%.1f" % (e.x * 2, e.y * 2, e.z * 2))

np = DEST + "/SM_Bus"
if EAL.does_asset_exist(np):
    if not EAL.delete_asset(np): log("WARN altes SM_Bus nicht loeschbar")
ok = EAL.rename_asset(smpath, np)
log("rename %s -> SM_Bus: %s" % (smpath, ok))
if EAL.does_asset_exist(np): EAL.save_asset(np, only_if_is_dirty=False)
if EAL.does_directory_exist(DEST + "/bus"): EAL.delete_directory(DEST + "/bus")
if EAL.does_asset_exist(DEST + "/M_WbBusBody"): EAL.delete_asset(DEST + "/M_WbBusBody")
log("SM_Bus ersetzt (texturierter Gelenkbus). ENDE")
