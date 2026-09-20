"""Importiert die Baeume und Buesche und legt ihre Materialien an.

Quelle: "Trees & Bushes pack" von Painkiller5555, Creative Commons
Attribution 3.0 - siehe CREDITS.md. Die FBX-Dateien entstehen mit
Tools/Blender/export_vegetation.py.

Drei Dinge macht dieses Skript, die man leicht vergisst:

* import_uniform_scale = 1.0. Der Blender-Export liefert bereits Zentimeter.
  Mit der Vorgabe waere jeder Baum hundertfach zu gross - beim Kaefer war das
  schon einmal ein 175 m langes Auto.
* Detailstufen. Die Pflanzen haben 34.000 bis 356.000 Dreiecke, weil die
  Blaetter echte Geometrie sind und keine texturierten Flaechen. Bei 1,53
  Millionen Bauminstanzen ist ohne Reduktion schon ein Waldrand nicht zu
  zeichnen.
* Zweiseitige Materialien. Blattgeometrie ist einseitig modelliert und
  verschwindet sonst, sobald man von der falschen Seite schaut.

Das Paket bringt KEINE Texturen mit - nur Grundfarben je Materialslot. Die
stehen in vegetation.json und werden hier zu Materialinstanzen.

Aufruf:
  UnrealEditor-Cmd.exe WiesbadenReal.uproject -run=pythonscript
      -script="Tools/import_vegetation.py" -unattended -nosplash
"""

import json
import os

import unreal

SRC = os.path.join(unreal.Paths.project_dir(), "Data", "Raw", "Vegetation", "fbx")
MESH_DIR = "/Game/Vegetation/Meshes"
MAT_DIR = "/Game/Vegetation/Materials"
MASTER = "%s/M_WbPlant" % MAT_DIR

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
MP = unreal.MaterialProperty

# Detailstufen. Der Anteil bezieht sich auf die Dreiecke der Ausgangsstufe.
# Die groesste Pflanze faellt damit von 356.000 auf zuletzt 10.700 Dreiecke.
LOD_PERCENT = [1.0, 0.35, 0.12, 0.03]


def log(msg):
    unreal.log("###WBVEG### %s" % msg)


def build_master_material():
    """Ein Material fuer alle Pflanzenteile, Farbe als Parameter."""
    if EAL.does_asset_exist(MASTER):
        EAL.delete_asset(MASTER)

    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "M_WbPlant", MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())

    color = MEL.create_material_expression(
        mat, unreal.MaterialExpressionVectorParameter, -400, 0)
    color.set_editor_property("parameter_name", "BaseColor")
    color.set_editor_property("default_value", unreal.LinearColor(0.05, 0.09, 0.03, 1.0))
    MEL.connect_material_property(color, "", MP.MP_BASE_COLOR)

    rough = MEL.create_material_expression(
        mat, unreal.MaterialExpressionScalarParameter, -400, 250)
    rough.set_editor_property("parameter_name", "Roughness")
    rough.set_editor_property("default_value", 0.85)
    MEL.connect_material_property(rough, "", MP.MP_ROUGHNESS)

    # Blattgeometrie ist einseitig modelliert. Ohne Zweiseitigkeit sieht man
    # bei jedem zweiten Blatt durch den Baum hindurch.
    mat.set_editor_property("two_sided", True)

    # Pflicht, sonst zeichnet Unreal an den Instanz-Komponenten kommentarlos
    # das Standardmaterial - das graue Schachbrett.
    mat.set_editor_property("used_with_instanced_static_meshes", True)

    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    return mat


def make_instance(master, name, color):
    path = "%s/%s" % (MAT_DIR, name)
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    inst = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, MAT_DIR, unreal.MaterialInstanceConstant,
        unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(inst, master)
    MEL.set_material_instance_vector_parameter_value(
        inst, "BaseColor", unreal.LinearColor(color[0], color[1], color[2], 1.0))
    EAL.save_loaded_asset(inst)
    return inst


manifest_path = os.path.join(SRC, "vegetation.json")
if not os.path.exists(manifest_path):
    log("ABBRUCH: %s fehlt - erst Tools/Blender/export_vegetation.py laufen lassen." % manifest_path)
    raise SystemExit(1)

with open(manifest_path, "r", encoding="utf-8") as f:
    plants = json.load(f)["plants"]

master = build_master_material()
log("Grundmaterial angelegt: %s" % MASTER)

# Detailstufen setzen - zwei moegliche Wege.
#
# `StaticMeshEditorSubsystem` ist im Kommandozeilenlauf NICHT verfuegbar
# (get_editor_subsystem liefert None, geprueft). Die aeltere
# EditorStaticMeshLibrary gibt es dort, also erst die, dann das Subsystem.
def apply_lods(mesh, options):
    if hasattr(unreal, "EditorStaticMeshLibrary"):
        return unreal.EditorStaticMeshLibrary.set_lods(mesh, options)
    subsystem = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    if subsystem is None:
        raise RuntimeError("Kein Weg, Detailstufen zu setzen.")
    return subsystem.set_lods(mesh, options)

tasks = []
for plant in plants:
    task = unreal.AssetImportTask()
    task.filename = os.path.join(SRC, plant["fbx"])
    task.destination_path = MESH_DIR
    task.destination_name = plant["name"]
    task.automated = True
    task.replace_existing = True
    task.save = True

    options = unreal.FbxImportUI()
    options.set_editor_property("import_mesh", True)
    options.set_editor_property("import_textures", False)
    options.set_editor_property("import_materials", False)
    options.set_editor_property("import_as_skeletal", False)
    options.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH)

    smd = options.static_mesh_import_data
    # 1.0, nicht die Vorgabe: Der Export liefert schon Zentimeter.
    smd.set_editor_property("import_uniform_scale", 1.0)
    smd.set_editor_property("combine_meshes", True)
    smd.set_editor_property("generate_lightmap_u_vs", True)
    smd.set_editor_property("auto_generate_collision", False)
    options.set_editor_property("static_mesh_import_data", smd)
    task.options = options
    tasks.append((task, plant))

unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t for t, _ in tasks])

built = 0
for task, plant in tasks:
    path = "%s/%s" % (MESH_DIR, plant["name"])
    mesh = EAL.load_asset(path)
    if mesh is None:
        log("Import fehlgeschlagen: %s" % plant["name"])
        continue

    # Materialinstanzen je Slot, Farben aus dem Blend.
    for index, slot in enumerate(plant["materials"]):
        if slot is None:
            continue
        inst = make_instance(master, "MI_%s_%d" % (plant["name"], index), slot["base_color"])
        mesh.set_material(index, inst)

    # Keine Kollision. Ein Baum, gegen den man faehrt, waere zwar richtig -
    # aber 1,53 Millionen Kollisionskoerper aus Blattgeometrie sind es nicht.
    # Die Fahrzeuge tasten ohnehin nur nach unten.
    mesh.set_editor_property("body_setup", None)

    reduction = unreal.StaticMeshReductionOptions()
    settings = []
    for percent in LOD_PERCENT:
        opt = unreal.StaticMeshReductionSettings()
        opt.set_editor_property("percent_triangles", percent)
        settings.append(opt)
    reduction.set_editor_property("reduction_settings", settings)
    reduction.set_editor_property("auto_compute_lod_screen_size", True)
    apply_lods(mesh, reduction)

    EAL.save_loaded_asset(mesh)
    built += 1

    tris = [mesh.get_num_triangles(i) for i in range(mesh.get_num_lods())]
    log("%-16s %-5s %d Stufen, Dreiecke %s" % (
        plant["name"], plant["kind"], mesh.get_num_lods(), tris))

log("FERTIG: %d von %d Pflanzen importiert." % (built, len(plants)))

# Im vollen Editor selbst beenden.
#
# Der Lauf MUSS im Editor stattfinden, nicht als Kommandozeilenwerkzeug: Die
# Editor-Skriptbibliothek prueft intern, ob sie "im Editor und nicht im Spiel"
# laeuft, und `set_lods` meldete im Kommandozeilenlauf stumm -1 statt Stufen
# zu bauen. Beendet wird deshalb hier statt ueber -ExecCmds - dort schluckt
# der Semikolon-Trenner das "Quit" in den Python-Aufruf hinein.
if os.environ.get("WB_QUIT"):
    unreal.SystemLibrary.quit_editor()
