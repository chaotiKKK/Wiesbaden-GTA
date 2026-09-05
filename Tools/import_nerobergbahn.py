"""Importiert das Nerobergbahn-Ensemble (Wagen, Tal-/Bergstation, Viadukt).

Quelle der Meshes: Tools/Blender/make_nerobergbahn.py -> Data/Raw/Nerobergbahn/
(FBX + nerobergbahn.json). Das Manifest nennt je Materialslot Grundfarbe und
ART; daraus entstehen hier zwei Sorten Material:

  * gekachelte AAA-Materialien (Backstein/Putz/Stein/Dach) aus dem vorhandenen
    AAA-Texturensatz - EIN Master M_WbNb_Tiled mit Textur- und Kachel-
    Parametern, je Sorte eine Instanz.
  * Volltonlack (Blau/Gelb/Creme/Glas/Metall/Schwarz) - EIN Master
    M_WbNb_Paint mit Farb-, Metall- und Rauheitsparameter, je Slot eine
    Instanz.

Zugeordnet wird ueber den SLOTNAMEN (aus dem FBX uebernommen), nicht ueber den
Index - so ist die Zuordnung unabhaengig von der Slotreihenfolge des Importers.

Aufruf (im vollen Editor, nicht -run=pythonscript - set_material braucht ihn):
  UnrealEditor.exe WiesbadenReal.uproject
      -ExecCmds="py Tools/import_nerobergbahn.py" -unattended -nosplash -nop4
"""

import json
import os

import unreal

SRC = os.path.join(unreal.Paths.project_dir(), "Data", "Raw", "Nerobergbahn")
MESH_DIR = "/Game/Nerobergbahn/Meshes"
MAT_DIR = "/Game/Nerobergbahn/Materials"
TEX_DIR = "/Game/Materials/AAA/Textures"

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
MP = unreal.MaterialProperty
ATH = unreal.AssetToolsHelpers.get_asset_tools()

# Kachel-Sorte -> (Farb-, Normal-, Rauheitstextur, Kachelung).
TILED = {
    "brick":   ("FacadeBrick_C", "FacadeBrick_N", "FacadeBrick_R", 2.0),
    "plaster": ("FacadePlaster_C", "FacadePlaster_N", "FacadePlaster_R", 1.0),
    "stone":   ("FacadeStone_C", "FacadeStone_N", "FacadeStone_R", 1.5),
    "roof":    ("RoofClay_C", "RoofClay_N", "RoofClay_R", 2.0),
}

# Volltonlack-Sorte -> (Metallic, Roughness).
PAINT_PBR = {
    "paint": (0.10, 0.32),
    "glass": (0.05, 0.12),
    "metal": (0.90, 0.28),
    "dark":  (0.20, 0.55),
    "timber": (0.0, 0.80),
}


def log(msg):
    unreal.log("###WBNBIMP### %s" % msg)


def tex(name):
    path = "%s/%s" % (TEX_DIR, name)
    asset = EAL.load_asset(path)
    if asset is None:
        log("WARNUNG: Textur fehlt: %s" % path)
    return asset


def build_tiled_master():
    path = "%s/M_WbNb_Tiled" % MAT_DIR
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    mat = ATH.create_asset("M_WbNb_Tiled", MAT_DIR, unreal.Material,
                           unreal.MaterialFactoryNew())

    tiling = MEL.create_material_expression(
        mat, unreal.MaterialExpressionScalarParameter, -900, 400)
    tiling.set_editor_property("parameter_name", "Tiling")
    tiling.set_editor_property("default_value", 2.0)
    coord = MEL.create_material_expression(
        mat, unreal.MaterialExpressionTextureCoordinate, -720, 400)
    mul = MEL.create_material_expression(
        mat, unreal.MaterialExpressionMultiply, -560, 400)
    MEL.connect_material_expressions(coord, "", mul, "A")
    MEL.connect_material_expressions(tiling, "", mul, "B")

    def sampler(name, y, sampler_type, prop, default_tex):
        s = MEL.create_material_expression(
            mat, unreal.MaterialExpressionTextureSampleParameter2D, -360, y)
        s.set_editor_property("parameter_name", name)
        # Standardtextur PASSEND zum Sampler-Typ setzen. Ohne sie faellt der
        # Knoten auf die Engine-DefaultTexture (Color) zurueck; ein Normal-
        # oder Grayscale-Sampler auf einer Color-Textur laesst das GANZE
        # Master-Material nicht kompilieren ("Sampler type ... should be
        # Color") - dann zeigen alle Instanzen das Schachbrett.
        if default_tex is not None:
            s.set_editor_property("texture", default_tex)
        s.set_editor_property("sampler_type", sampler_type)
        MEL.connect_material_expressions(mul, "", s, "UVs")
        if prop is not None:
            MEL.connect_material_property(s, "", prop)
        return s

    sampler("BaseColor", 0, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR,
            MP.MP_BASE_COLOR, tex("FacadeBrick_C"))
    sampler("Normal", 300, unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL,
            MP.MP_NORMAL, tex("FacadeBrick_N"))
    sampler("Roughness", 600, unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE,
            MP.MP_ROUGHNESS, tex("FacadeBrick_R"))

    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    return mat


def build_paint_master():
    path = "%s/M_WbNb_Paint" % MAT_DIR
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    mat = ATH.create_asset("M_WbNb_Paint", MAT_DIR, unreal.Material,
                           unreal.MaterialFactoryNew())
    color = MEL.create_material_expression(
        mat, unreal.MaterialExpressionVectorParameter, -400, 0)
    color.set_editor_property("parameter_name", "BaseColor")
    color.set_editor_property("default_value", unreal.LinearColor(0.5, 0.5, 0.5, 1.0))
    MEL.connect_material_property(color, "", MP.MP_BASE_COLOR)

    metal = MEL.create_material_expression(
        mat, unreal.MaterialExpressionScalarParameter, -400, 250)
    metal.set_editor_property("parameter_name", "Metallic")
    metal.set_editor_property("default_value", 0.1)
    MEL.connect_material_property(metal, "", MP.MP_METALLIC)

    rough = MEL.create_material_expression(
        mat, unreal.MaterialExpressionScalarParameter, -400, 450)
    rough.set_editor_property("parameter_name", "Roughness")
    rough.set_editor_property("default_value", 0.35)
    MEL.connect_material_property(rough, "", MP.MP_ROUGHNESS)

    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    return mat


def make_tiled_instance(master, kind):
    name = "MI_Nb_%s" % kind
    path = "%s/%s" % (MAT_DIR, name)
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    inst = ATH.create_asset(name, MAT_DIR, unreal.MaterialInstanceConstant,
                            unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(inst, master)
    c, n, r, tiling = TILED[kind]
    for pname, asset in [("BaseColor", tex(c)), ("Normal", tex(n)), ("Roughness", tex(r))]:
        if asset is not None:
            MEL.set_material_instance_texture_parameter_value(inst, pname, asset)
    MEL.set_material_instance_scalar_parameter_value(inst, "Tiling", tiling)
    EAL.save_loaded_asset(inst)
    return inst


def make_paint_instance(master, slot):
    name = "MI_Nb_%s" % slot["name"]
    path = "%s/%s" % (MAT_DIR, name)
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    inst = ATH.create_asset(name, MAT_DIR, unreal.MaterialInstanceConstant,
                            unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(inst, master)
    rgb = slot["base_color"]
    MEL.set_material_instance_vector_parameter_value(
        inst, "BaseColor", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
    metal, rough = PAINT_PBR[slot["kind"]]
    MEL.set_material_instance_scalar_parameter_value(inst, "Metallic", metal)
    MEL.set_material_instance_scalar_parameter_value(inst, "Roughness", rough)
    EAL.save_loaded_asset(inst)
    return inst


def main():
    manifest_path = os.path.join(SRC, "nerobergbahn.json")
    if not os.path.exists(manifest_path):
        log("ABBRUCH: %s fehlt - erst make_nerobergbahn.py laufen lassen." % manifest_path)
        raise SystemExit(1)
    with open(manifest_path, "r", encoding="utf-8") as f:
        assets = json.load(f)["assets"]

    tiled_master = build_tiled_master()
    paint_master = build_paint_master()
    log("Master-Materialien angelegt.")

    tiled_instances = {kind: make_tiled_instance(tiled_master, kind) for kind in TILED}

    # Import der vier FBX.
    tasks = []
    for asset in assets:
        task = unreal.AssetImportTask()
        task.filename = os.path.join(SRC, asset["fbx"])
        task.destination_path = MESH_DIR
        task.destination_name = asset["name"]
        task.automated = True
        task.replace_existing = True
        task.save = True

        options = unreal.FbxImportUI()
        options.set_editor_property("import_mesh", True)
        options.set_editor_property("import_textures", False)
        options.set_editor_property("import_materials", False)
        options.set_editor_property("import_as_skeletal", False)
        options.set_editor_property("mesh_type_to_import",
                                    unreal.FBXImportType.FBXIT_STATIC_MESH)
        smd = options.static_mesh_import_data
        smd.set_editor_property("import_uniform_scale", 1.0)
        smd.set_editor_property("combine_meshes", True)
        smd.set_editor_property("generate_lightmap_u_vs", True)
        smd.set_editor_property("auto_generate_collision", False)
        options.set_editor_property("static_mesh_import_data", smd)
        task.options = options
        tasks.append((task, asset))

    ATH.import_asset_tasks([t for t, _ in tasks])

    # Paint-Instanzen je Slot ueber alle Assets sammeln (Slotname eindeutig).
    paint_cache = {}

    for task, asset in tasks:
        mesh_path = "%s/%s" % (MESH_DIR, asset["name"])
        mesh = EAL.load_asset(mesh_path)
        if mesh is None:
            log("Import fehlgeschlagen: %s" % asset["name"])
            continue

        slot_kind = {m["name"]: m for m in asset["materials"]}
        statics = mesh.get_editor_property("static_materials")
        for i, sm in enumerate(statics):
            slot_name = str(sm.get_editor_property("material_slot_name"))
            spec = slot_kind.get(slot_name)
            if spec is None:
                log("  %s Slot %d '%s' ohne Manifest-Eintrag" % (asset["name"], i, slot_name))
                continue
            kind = spec["kind"]
            if kind in TILED:
                inst = tiled_instances[kind]
            else:
                if slot_name not in paint_cache:
                    paint_cache[slot_name] = make_paint_instance(paint_master, spec)
                inst = paint_cache[slot_name]
            mesh.set_material(i, inst)

        # Kein Kollisionskoerper: Wagen wird bewegt/angehaengt, Bauwerke sind
        # Schmuck; Gelaende und Trasse tragen die Kollision.
        mesh.set_editor_property("body_setup", None)
        EAL.save_loaded_asset(mesh)

        bb = mesh.get_bounding_box()
        size = bb.max - bb.min
        log("%-20s importiert, Groesse %.0f x %.0f x %.0f cm, %d Slots"
            % (asset["name"], size.x, size.y, size.z, len(statics)))

    # Abschluss-Sentinel: das Editor-Protokoll erreicht den stdout-Redirect
    # nicht zuverlaessig, eine Datei schon.
    try:
        with open(os.path.join(SRC, "import_done.txt"), "w", encoding="utf-8") as f:
            f.write("ok\n")
    except OSError as exc:
        log("Sentinel nicht geschrieben: %s" % exc)

    log("FERTIG.")
    if os.environ.get("WB_QUIT"):
        unreal.SystemLibrary.quit_editor()


main()
