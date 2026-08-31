"""Bringt das Spielermodell "Sebbo" nach Unreal.

Quelle ist Data/Raw/Sebbo, das Tools/Blender/build_sebbo.py schreibt: ein FBX
mit drei Materialschlitzen und die zugehoerigen Bilddateien.

Das Grundmaterial ist NICHT M_WbSurface. Jenes rechnet die Texturkoordinaten
ueber eine Kachelgroesse in Metern um - richtig fuer eine Hauswand, falsch fuer
eine Figur, deren UV-Abwicklung bereits stimmt. Mit ihm laege die Fototextur
des Gesichts irgendwo auf der Schulter.

Aufruf (VOLLER Editor, wegen Materialerzeugung und LOD-Aufbau):
  UnrealEditor.exe WiesbadenReal.uproject
      -ExecCmds="py Tools/import_sebbo.py" -unattended -nosplash
"""

import os

import unreal

PROJECT = unreal.Paths.project_dir()
SRC = os.path.join(PROJECT, "Data", "Raw", "Sebbo")

MESH_DIR = "/Game/Assets/People"
TEX_DIR = "/Game/Textures/People"
DEFAULT_TEX_DIR = "/Game/Textures/Defaults"
MAT_DIR = "/Game/Materials/People"
MASTER = "%s/M_WbFigur" % MAT_DIR
MESH = "%s/SM_Sebbo" % MESH_DIR

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
MP = unreal.MaterialProperty
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()

# Kanal -> (Kompression, sRGB)
CHANNEL_SETTINGS = {
    "Color": (unreal.TextureCompressionSettings.TC_DEFAULT, True),
    "Normal": (unreal.TextureCompressionSettings.TC_NORMALMAP, False),
    "Roughness": (unreal.TextureCompressionSettings.TC_GRAYSCALE, False),
}

# Materialschlitz -> (Dateipraefix, Rauheit falls keine Karte, Groesse)
#
# Die Fototextur des Scans bleibt bei 2048. Sie traegt Gesicht, Jacke und
# Kettensaege auf einem einzigen Blatt; auf 1024 heruntergerechnet zerfaellt
# das Gesicht. Hose und Stiefel sind glatte Flaechen und kommen mit 512 aus.
SLOTS = [
    ("Sebbo_Scan", "T_Sebbo_Scan", 2048),
    ("Sebbo_Hose", "T_Sebbo_Hose", 512),
    ("Sebbo_Schuhe", "T_Sebbo_Schuhe", 512),
]


def log(msg):
    unreal.log("###WBSEBBO### %s" % msg)


def import_texture(prefix, channel, size):
    path = os.path.join(SRC, "%s_%s.png" % (prefix, channel))
    if not os.path.exists(path):
        return None

    asset_name = "%s_%s" % (prefix, channel)
    dest = "%s/%s" % (TEX_DIR, asset_name)

    task = unreal.AssetImportTask()
    task.filename = path
    task.destination_path = TEX_DIR
    task.destination_name = asset_name
    task.automated = True
    task.replace_existing = True
    task.save = True
    TOOLS.import_asset_tasks([task])

    texture = EAL.load_asset(dest)
    if texture is None:
        log("Import fehlgeschlagen: %s" % path)
        return None

    compression, srgb = CHANNEL_SETTINGS[channel]
    texture.set_editor_property("compression_settings", compression)
    texture.set_editor_property("srgb", srgb)

    # Groesse deckeln.
    #
    # Der Texturvorrat der Karte ist knapp; ein 4K-Fotoscan neben 73
    # Stadttexturen hat den Editor schon einmal in den Speicherfehler
    # getrieben. never_stream AUS, damit die Figur mitstreamt wie alles andere.
    texture.set_editor_property("max_texture_size", size)
    texture.set_editor_property("never_stream", False)
    EAL.save_loaded_asset(texture)
    log("Textur: %s (%s, max %d)" % (asset_name, channel, size))
    return texture


def import_default(asset_name, channel):
    """Eine der drei Ersatztexturen aus Data/Raw/Defaults holen.

    Sie liegen in /Game/Textures/Defaults und werden von mehreren
    Grundmaterialien geteilt; erzeugt werden sie von
    Tools/Blender/make_default_textures.py.
    """
    dest = "%s/%s" % (DEFAULT_TEX_DIR, asset_name)
    if EAL.does_asset_exist(dest):
        return EAL.load_asset(dest)

    path = os.path.join(unreal.Paths.project_dir(), "Data", "Raw", "Defaults",
                        "%s.png" % asset_name)
    if not os.path.exists(path):
        log("Ersatztextur fehlt: %s" % path)
        return None

    task = unreal.AssetImportTask()
    task.filename = path
    task.destination_path = DEFAULT_TEX_DIR
    task.destination_name = asset_name
    task.automated = True
    task.replace_existing = True
    task.save = True
    TOOLS.import_asset_tasks([task])

    texture = EAL.load_asset(dest)
    if texture is None:
        log("Ersatztextur nicht ladbar: %s" % asset_name)
        return None

    compression, srgb = CHANNEL_SETTINGS[channel]
    texture.set_editor_property("compression_settings", compression)
    texture.set_editor_property("srgb", srgb)
    texture.set_editor_property("never_stream", True)
    EAL.save_loaded_asset(texture)
    log("Ersatztextur: %s (%s)" % (asset_name, channel))
    return texture


def build_master():
    """Ein Grundmaterial fuer Figuren - UVs unveraendert, drei Karten."""
    if EAL.does_asset_exist(MASTER):
        EAL.delete_asset(MASTER)

    mat = TOOLS.create_asset("M_WbFigur", MAT_DIR, unreal.Material,
                             unreal.MaterialFactoryNew())

    def expr(cls, x, y):
        return MEL.create_material_expression(mat, cls, x, y)

    white = EAL.load_asset("/Engine/EngineResources/WhiteSquareTexture")
    flat_normal = import_default("T_WbVorgabe_Normal", "Normal")
    linear_white = import_default("T_WbVorgabe_Weiss", "Roughness")

    color = expr(unreal.MaterialExpressionTextureSampleParameter2D, -800, -300)
    color.set_editor_property("parameter_name", "BaseColor")
    color.set_editor_property("texture", white)
    MEL.connect_material_property(color, "", MP.MP_BASE_COLOR)

    # Die Ersatztexturen muessen zum ABTASTTYP passen.
    #
    # Ohne eigene Vorgabe steht hier /Engine/EngineResources/DefaultTexture -
    # ein Farbbild. Der Uebersetzer lehnt das Material dann ab und Unreal
    # zeichnet stumm das Standardmaterial:
    #
    #   "Sampler type is Normal, should be Color for .../DefaultTexture"
    #   "Sampler type is Linear Grayscale, should be Color for
    #    .../WhiteSquareTexture"
    #
    # Genau daran war die Figur im ersten Spiellauf grau - nicht am Modell,
    # nicht an den Texturen, sondern an zwei Vorgabewerten.
    normal = expr(unreal.MaterialExpressionTextureSampleParameter2D, -800, 200)
    normal.set_editor_property("parameter_name", "Normal")
    if flat_normal:
        normal.set_editor_property("texture", flat_normal)
    normal.set_editor_property(
        "sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    MEL.connect_material_property(normal, "", MP.MP_NORMAL)

    # Rauheit: Karte MAL Regler.
    #
    # Der Scan bringt keine Rauheitskarte mit - eine Fotoaufnahme kennt nur
    # Farbe. Fuer ihn bleibt die weisse Ersatztextur stehen und der Regler
    # bestimmt den Wert; Hose und Stiefel bringen ihre gebackene Karte mit.
    rough = expr(unreal.MaterialExpressionTextureSampleParameter2D, -800, 700)
    rough.set_editor_property("parameter_name", "Roughness")
    if linear_white:
        rough.set_editor_property("texture", linear_white)
    rough.set_editor_property(
        "sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE)

    rough_scale = expr(unreal.MaterialExpressionScalarParameter, -800, 950)
    rough_scale.set_editor_property("parameter_name", "RauheitFaktor")
    rough_scale.set_editor_property("default_value", 1.0)

    rough_mul = expr(unreal.MaterialExpressionMultiply, -400, 700)
    MEL.connect_material_expressions(rough, "R", rough_mul, "A")
    MEL.connect_material_expressions(rough_scale, "", rough_mul, "B")
    MEL.connect_material_property(rough_mul, "", MP.MP_ROUGHNESS)

    spec = expr(unreal.MaterialExpressionScalarParameter, -800, 1150)
    spec.set_editor_property("parameter_name", "Glanz")
    spec.set_editor_property("default_value", 0.35)
    MEL.connect_material_property(spec, "", MP.MP_SPECULAR)

    # PFLICHT fuer die animierte Figur: ohne dieses Flag uebersetzt Unreal
    # das Material nicht fuer Skelett-Meshes und zeichnet kommentarlos das
    # graue Standardmaterial. Genau so stand Sebbo im ersten Spiellauf da -
    # animiert, aber grau, und im Protokoll nur:
    #   "Material with missing usage flag was applied to skeletal mesh"
    mat.set_editor_property("used_with_skeletal_mesh", True)

    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    log("Grundmaterial: %s" % MASTER)
    return mat


def build_instance(master, name, textures, rough_factor):
    inst_name = "MI_%s" % name
    path = "%s/%s" % (MAT_DIR, inst_name)
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)

    inst = TOOLS.create_asset(inst_name, MAT_DIR,
                              unreal.MaterialInstanceConstant,
                              unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(inst, master)

    for channel, texture in textures.items():
        param = "BaseColor" if channel == "Color" else channel
        MEL.set_material_instance_texture_parameter_value(inst, param, texture)

    MEL.set_material_instance_scalar_parameter_value(
        inst, "RauheitFaktor", rough_factor)
    EAL.save_loaded_asset(inst)
    log("Material: %s (%s)" % (inst_name, ", ".join(sorted(textures))))
    return inst


def import_mesh():
    fbx = os.path.join(SRC, "SM_Sebbo.fbx")
    if not os.path.exists(fbx):
        raise RuntimeError("FBX fehlt: %s - erst build_sebbo.py laufen lassen" % fbx)

    if EAL.does_asset_exist(MESH):
        EAL.delete_asset(MESH)

    options = unreal.FbxImportUI()
    options.set_editor_property("import_mesh", True)
    options.set_editor_property("import_textures", False)
    options.set_editor_property("import_materials", False)
    options.set_editor_property("import_as_skeletal", False)
    options.set_editor_property("mesh_type_to_import",
                                unreal.FBXImportType.FBXIT_STATIC_MESH)

    smd = options.static_mesh_import_data
    # Der Massstab steckt bereits im FBX (Blender exportiert mit
    # apply_unit_scale in Zentimetern). Ein zweiter Faktor hier hat beim Kaefer
    # ein 17 m breites Auto ergeben.
    smd.set_editor_property("import_uniform_scale", 1.0)
    smd.set_editor_property("combine_meshes", True)
    smd.set_editor_property("generate_lightmap_u_vs", True)
    smd.set_editor_property("auto_generate_collision", False)
    smd.set_editor_property("normal_import_method",
                            unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS)

    task = unreal.AssetImportTask()
    task.filename = fbx
    task.destination_path = MESH_DIR
    task.destination_name = "SM_Sebbo"
    task.automated = True
    task.replace_existing = True
    task.save = True
    task.options = options
    TOOLS.import_asset_tasks([task])

    mesh = EAL.load_asset(MESH)
    if mesh is None:
        raise RuntimeError("Import fehlgeschlagen: %s" % fbx)

    bounds = mesh.get_bounds()
    size = bounds.box_extent * 2.0
    log("Mesh: %s, Groesse %.1f x %.1f x %.1f cm, %d Materialschlitze"
        % (MESH, size.x, size.y, size.z,
           len(mesh.get_editor_property("static_materials"))))
    return mesh


def assign_materials(mesh, instances):
    """Materialien den Schlitzen zuordnen.

    Nach Namen, nicht nach Reihenfolge: Blender sortiert die Schlitze beim
    Zusammenfuegen nach dem Zielobjekt, und wer sich auf Index 0/1/2 verlaesst,
    steckt die Figur bei der naechsten Aenderung in Hosenstoff.
    """
    slots = mesh.get_editor_property("static_materials")
    for i, slot in enumerate(slots):
        name = str(slot.get_editor_property("material_slot_name"))
        chosen = None
        for key, inst in instances.items():
            if key.lower() in name.lower():
                chosen = inst
                break
        if chosen is None:
            # Der Scan bringt aus der Quelldatei den Namen "Material.001" mit.
            chosen = instances.get("Sebbo_Scan")
            log("Schlitz %d '%s' nicht zuzuordnen - Scanmaterial gesetzt" % (i, name))
        mesh.set_material(i, chosen)
        log("Schlitz %d '%s' -> %s" % (i, name, chosen.get_name()))
    EAL.save_loaded_asset(mesh)


def build_lods(mesh):
    """Zwei Entfernungsstufen.

    Der Scan hat 80 000 Dreiecke. Sie kosten nichts, solange die Figur allein
    im Bild steht - aber die Fussgaenger sollen dasselbe Modell bekommen
    koennen, und dann zaehlt jede Stufe.
    """
    options = unreal.EditorScriptingMeshReductionOptions()
    options.reduction_settings = [
        unreal.EditorScriptingMeshReductionSettings(1.0, 0.0),
        unreal.EditorScriptingMeshReductionSettings(0.35, 1.0),
        unreal.EditorScriptingMeshReductionSettings(0.10, 1.0),
    ]
    options.auto_compute_lod_screen_size = True
    count = unreal.EditorStaticMeshLibrary.set_lods(mesh, options)
    if count and count > 0:
        EAL.save_loaded_asset(mesh)
        log("LODs gesetzt: %d Stufen" % count)
    else:
        # Diese Funktion meldet im Kommandozeilenbetrieb -1 und tut nichts,
        # OHNE einen Fehler zu werfen. Wer das nicht prueft, glaubt an LODs,
        # die es nicht gibt.
        log("LODs NICHT gesetzt (Rueckgabe %s) - voller Editor noetig" % count)


def main():
    for path in (TEX_DIR, DEFAULT_TEX_DIR, MAT_DIR, MESH_DIR):
        if not EAL.does_directory_exist(path):
            EAL.make_directory(path)

    master = build_master()

    instances = {}
    for slot_name, prefix, size in SLOTS:
        textures = {}
        for channel in ("Color", "Normal", "Roughness"):
            tex = import_texture(prefix, channel, size)
            if tex is not None:
                textures[channel] = tex
        if not textures:
            log("keine Texturen fuer %s gefunden" % slot_name)
            continue
        # Der Scan hat keine eigene Rauheitskarte; 0,72 laesst Jacke und Haut
        # matt und das Saegeblatt trotzdem nicht spiegeln.
        rough = 0.72 if "Roughness" not in textures else 1.0
        instances[slot_name] = build_instance(master, slot_name, textures, rough)

    mesh = import_mesh()
    assign_materials(mesh, instances)
    build_lods(mesh)
    log("FERTIG")


main()

# Editor schliessen.
#
# `-ExecCmds="py ..., quit"` tut es NICHT: der Editor blieb nach dem Import
# stehen und hielt die Projektsperre, sodass der naechste Lauf gar nicht erst
# hochkam. Von hier aus beendet er sich zuverlaessig.
if unreal.SystemLibrary.get_command_line().find("-unattended") >= 0:
    unreal.SystemLibrary.quit_editor()
