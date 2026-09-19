"""Importiert Sylvias GLB als Skeletal Mesh und legt das Shavings-System an.

Der GLB-Importer laeuft ueber Interchange. Der Quellpfad kann fuer die lokale
Wiederholung mit SYLVIA_GLTF gesetzt werden; standardmaessig wird der uebliche
unversionierte Rohdatenpfad unter Data/Raw/Sylvia verwendet.
"""

import os
import unreal


SRC = os.environ.get(
    "SYLVIA_GLTF",
    os.path.join(unreal.Paths.project_dir(), "Data", "Raw", "Sylvia", "sylvia.glb"),
)
DEST = "/Game/Assets/People/Sylvia"
MESH_PATH = DEST + "/sylvia/SkeletalMeshes/tripo_part_0"
NIAGARA_DEST = "/Game/Niagara/NS_SylviaWoodShavings"
NIAGARA_TEMPLATE = "/Niagara/DefaultAssets/Templates/Systems/FountainLightweight"
EXPECTED_SKELETAL_PARTS = 15
EAL = unreal.EditorAssetLibrary


def log(message):
    unreal.log("###WBSYLVIA### %s" % message)


def quit_unattended_editor():
    if "-unattended" in unreal.SystemLibrary.get_command_line():
        unreal.SystemLibrary.quit_editor()


if not os.path.isfile(SRC):
    log("ABBRUCH: GLB fehlt: %s" % SRC)
    quit_unattended_editor()
    raise SystemExit(1)

if EAL.does_directory_exist(DEST):
    EAL.delete_directory(DEST)
EAL.make_directory(DEST)

task = unreal.AssetImportTask()
task.set_editor_property("filename", SRC)
task.set_editor_property("destination_path", DEST)
task.set_editor_property("destination_name", "SK_Sylvia")
task.set_editor_property("automated", True)
task.set_editor_property("replace_existing", True)
task.set_editor_property("save", True)
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

assets = []
skeletal_assets = []
for path in EAL.list_assets(DEST, recursive=True):
    assets.append(path)
    asset = EAL.load_asset(path)
    if isinstance(asset, unreal.SkeletalMesh):
        skeletal_assets.append((path, asset))

if len(skeletal_assets) != EXPECTED_SKELETAL_PARTS:
	log("ABBRUCH: %d SkeletalMesh-Teile erwartet, gefunden %d; Importierte Assets: %s" %
		(EXPECTED_SKELETAL_PARTS, len(skeletal_assets), ", ".join(assets)))
	quit_unattended_editor()
	raise SystemExit(1)

_, skeletal = next(
	(path, asset) for path, asset in skeletal_assets
	if path.endswith("/tripo_part_0.tripo_part_0"))

EAL.save_loaded_asset(skeletal)
bounds = skeletal.get_bounds()
extent = bounds.box_extent
log("Skelett-Mesh: %s; Masse %.1f x %.1f x %.1f cm" %
    (MESH_PATH, extent.x * 2.0, extent.y * 2.0, extent.z * 2.0))
log("Skelett: %s" % skeletal.get_editor_property("skeleton"))

if EAL.does_asset_exist(NIAGARA_DEST):
    EAL.delete_asset(NIAGARA_DEST)
effect = EAL.duplicate_asset(NIAGARA_TEMPLATE, NIAGARA_DEST)
if effect is None:
    log("ABBRUCH: Niagara-Vorlage konnte nicht kopiert werden")
    quit_unattended_editor()
    raise SystemExit(1)
EAL.save_asset(NIAGARA_DEST)
log("Niagara: %s aus %s" % (NIAGARA_DEST, NIAGARA_TEMPLATE))
log("FERTIG")
quit_unattended_editor()
