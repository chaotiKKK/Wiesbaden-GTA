"""Importiert citybus.glb, ersetzt /Game/Vehicles/Bus/SM_Bus (neutral lackiert).

Schont den Zielanzeige-Ordner /Game/Vehicles/Bus/Ziel (loescht NICHT den ganzen
Bus-Ordner wie import_bus.py). Legt ein neutrales Karosserie-Material M_WbBusBody
an, importiert das GLB in einen Temp-Pfad, aktiviert Nanite, setzt das Material,
ersetzt damit SM_Bus und raeumt die alten Tripo-Materialien weg.
"""
import unreal, os

def log(m): unreal.log("###CITYBUS### %s" % m)
EAL = unreal.EditorAssetLibrary
ATH = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
DEST = "/Game/Vehicles/Bus"
GLB = os.path.join(unreal.Paths.project_dir(), "Data", "Raw", "Bus", "citybus.glb")

# 1) neutrales Karosserie-Material (heller Lack, texturlos)
mpath = DEST + "/M_WbBusBody"
if EAL.does_asset_exist(mpath): EAL.delete_asset(mpath)
mat = ATH.create_asset("M_WbBusBody", DEST, unreal.Material, unreal.MaterialFactoryNew())
base = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -380, -60)
base.set_editor_property("constant", unreal.LinearColor(0.80, 0.80, 0.82, 1.0))
MEL.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
rough = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, -380, 120)
rough.set_editor_property("r", 0.45)
MEL.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
MEL.recompile_material(mat)
EAL.save_loaded_asset(mat)
log("Material M_WbBusBody angelegt")

# 2) GLB in Temp importieren
tmp = DEST + "/_cityimport"
if EAL.does_directory_exist(tmp): EAL.delete_directory(tmp)
t = unreal.AssetImportTask()
t.set_editor_property("filename", GLB)
t.set_editor_property("destination_path", tmp)
t.set_editor_property("automated", True)
t.set_editor_property("replace_existing", True)
t.set_editor_property("save", True)
ATH.import_asset_tasks([t])
sm = None; smpath = None
for p in EAL.list_assets(tmp, recursive=True):
    a = EAL.load_asset(p)
    if isinstance(a, unreal.StaticMesh): sm = a; smpath = p; break
if sm is None:
    log("FEHLER kein StaticMesh importiert"); log("ENDE"); raise SystemExit(0)

try:
    ns = sm.get_editor_property("nanite_settings"); ns.set_editor_property("enabled", True)
    sm.set_editor_property("nanite_settings", ns)
except Exception as e:
    log("Nanite: %s" % e)
sm.set_material(0, mat)
EAL.save_loaded_asset(sm)
b = sm.get_bounds(); e = b.box_extent
log("Rohbus UU X=%.1f Y=%.1f Z=%.1f" % (e.x * 2, e.y * 2, e.z * 2))

# 3) altes SM_Bus ersetzen, alte Tripo-Mats/Texturen entfernen (Ziel/ bleibt!)
np = DEST + "/SM_Bus"
if EAL.does_asset_exist(np):
    if not EAL.delete_asset(np): log("WARN altes SM_Bus nicht loeschbar")
ok = EAL.rename_asset(smpath, np)
log("rename %s -> SM_Bus: %s" % (smpath, ok))
if EAL.does_asset_exist(np): EAL.save_asset(np, only_if_is_dirty=False)
if EAL.does_directory_exist(DEST + "/bus"): EAL.delete_directory(DEST + "/bus")
if EAL.does_directory_exist(tmp): EAL.delete_directory(tmp)
log("SM_Bus ersetzt (neu, neutral). ENDE")
