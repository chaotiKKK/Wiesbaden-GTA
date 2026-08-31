"""Repariert M_VWBeetle_Master - das Grundmaterial des Kaefers.

Das Material uebersetzt seit dem SM6-Wechsel nicht mehr:

    M_VWBeetle_Master.uasset: Failed to compile Material for platform
    PCD3D_SM6, Default Material will be used in game.
        (Node TextureSampleParameter2D) Sampler type is Normal,
            should be Color for /Engine/EngineResources/DefaultTexture
        (Node TextureSampleParameter2D) Sampler type is Linear Color,
            should be Color for /Engine/EngineResources/DefaultTexture   (3x)

Vier Textureingaenge haben KEINE Ersatztextur. Unreal setzt dort
DefaultTexture ein - ein Farbbild - und lehnt das Material ab, weil der
Abtasttyp etwas anderes verlangt. Der Kaefer war deshalb im Spiel durchgehend
grau, obwohl seine Texturen alle vorhanden und zugewiesen sind.

Warum das Material NEU gebaut und nicht berichtigt wird:

In UE 5.8 ist die Knotenliste eines fertigen Materials von Python aus nicht
lesbar - `Property 'Expressions' ... is protected`. Die vier fehlerhaften
Knoten lassen sich also nicht anfassen. Die PARAMETERNAMEN stehen aber in den
Materialinstanzen, und die sind lesbar. Daraus laesst sich ein Grundmaterial
bauen, das dieselben Namen anbietet; die Instanzen behalten damit ihre
Zuweisungen.

Aufruf (VOLLER Editor):
  UnrealEditor.exe WiesbadenReal.uproject
      -ExecCmds="py Tools/fix_beetle_material.py" -unattended -nosplash
"""

import os

import unreal

MASTER = "/Game/Vehicles/Beetle/M_VWBeetle_Master"
BEETLE_DIR = "/Game/Vehicles/Beetle"
DEFAULT_TEX_DIR = "/Game/Textures/Defaults"

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
MP = unreal.MaterialProperty
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()

# Kanal -> (Kompression, sRGB)
CHANNEL_SETTINGS = {
    "Color": (unreal.TextureCompressionSettings.TC_DEFAULT, True),
    "LinearColor": (unreal.TextureCompressionSettings.TC_DEFAULT, False),
    "Normal": (unreal.TextureCompressionSettings.TC_NORMALMAP, False),
    "Roughness": (unreal.TextureCompressionSettings.TC_GRAYSCALE, False),
}

# Welcher Parametername welche Rolle spielt. Der Abgleich laeuft ueber
# Teilzeichenketten, weil die Namen aus dem gekauften Modell stammen und nicht
# einer Regel folgen.
ROLES = [
    ("normal", "Normal"),
    ("roughness", "Roughness"),
    ("rough", "Roughness"),
    ("metallic", "Roughness"),
    ("metal", "Roughness"),
    ("occlusion", "Roughness"),
    ("ao", "Roughness"),
]


def log(msg):
    unreal.log("###WBBEETLE### %s" % msg)


def import_default(asset_name, channel, source_name=None):
    dest = "%s/%s" % (DEFAULT_TEX_DIR, asset_name)
    if EAL.does_asset_exist(dest):
        return EAL.load_asset(dest)

    path = os.path.join(unreal.Paths.project_dir(), "Data", "Raw", "Defaults",
                        "%s.png" % (source_name or asset_name))
    if not os.path.exists(path):
        log("Ersatztextur fehlt: %s" % path)
        return None

    if not EAL.does_directory_exist(DEFAULT_TEX_DIR):
        EAL.make_directory(DEFAULT_TEX_DIR)

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
        return None
    compression, srgb = CHANNEL_SETTINGS[channel]
    texture.set_editor_property("compression_settings", compression)
    texture.set_editor_property("srgb", srgb)
    texture.set_editor_property("never_stream", True)
    EAL.save_loaded_asset(texture)
    log("Ersatztextur: %s (%s)" % (asset_name, channel))
    return texture


def collect_parameters():
    """Parameternamen aus den Materialinstanzen des Kaefers lesen."""
    names = {}
    for asset_path in EAL.list_assets(BEETLE_DIR, recursive=True):
        asset = EAL.load_asset(asset_path)
        if not isinstance(asset, unreal.MaterialInstanceConstant):
            continue
        try:
            values = asset.get_editor_property("texture_parameter_values")
        except Exception as exc:
            log("Instanz %s nicht lesbar (%s)" % (asset_path, exc))
            continue
        for entry in values:
            info = entry.get_editor_property("parameter_info")
            name = str(info.get_editor_property("name"))
            texture = entry.get_editor_property("parameter_value")
            names.setdefault(name, texture)
    return names


def role_for(name):
    lowered = name.lower()
    for needle, role in ROLES:
        if needle in lowered:
            return role
    return "Color"


def build_master(parameters, defaults):
    if EAL.does_asset_exist(MASTER):
        EAL.delete_asset(MASTER)

    mat = TOOLS.create_asset("M_VWBeetle_Master", BEETLE_DIR,
                             unreal.Material, unreal.MaterialFactoryNew())

    def expr(cls, x, y):
        return MEL.create_material_expression(mat, cls, x, y)

    sampler_for_role = {
        "Color": unreal.MaterialSamplerType.SAMPLERTYPE_COLOR,
        "LinearColor": unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR,
        "Normal": unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL,
        "Roughness": unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE,
    }

    y = -600
    made = {}
    for name in sorted(parameters):
        role = role_for(name)
        node = expr(unreal.MaterialExpressionTextureSampleParameter2D, -900, y)
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("sampler_type", sampler_for_role[role])

        # DIE Zeile, an der das alte Material gescheitert ist. Ohne eigene
        # Ersatztextur steht hier DefaultTexture - ein Farbbild - und der
        # Uebersetzer lehnt ab.
        fallback = parameters[name] or defaults.get(role)
        if fallback is not None:
            node.set_editor_property("texture", fallback)

        made.setdefault(role, []).append((name, node))
        log("Parameter %-28s -> %s" % (name, role))
        y += 400

    # Grundfarbe: der erste Farbparameter.
    if made.get("Color"):
        MEL.connect_material_property(made["Color"][0][1], "", MP.MP_BASE_COLOR)
    if made.get("Normal"):
        MEL.connect_material_property(made["Normal"][0][1], "", MP.MP_NORMAL)
    if made.get("Roughness"):
        MEL.connect_material_property(made["Roughness"][0][1], "R", MP.MP_ROUGHNESS)
    else:
        rough = expr(unreal.MaterialExpressionScalarParameter, -900, y)
        rough.set_editor_property("parameter_name", "Rauheit")
        rough.set_editor_property("default_value", 0.35)
        MEL.connect_material_property(rough, "", MP.MP_ROUGHNESS)

    # Der Kaefer steht auch als Verkehr in Instanz-Komponenten. Ohne das Flag
    # zeichnet Unreal dort kommentarlos das Standardmaterial.
    mat.set_editor_property("used_with_instanced_static_meshes", True)
    mat.set_editor_property("used_with_skeletal_mesh", True)

    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    return mat


def main():
    defaults = {
        "Normal": import_default("T_WbVorgabe_Normal", "Normal"),
        "Roughness": import_default("T_WbVorgabe_Weiss", "Roughness"),
        "LinearColor": import_default("T_WbVorgabe_LinearFarbe", "LinearColor",
                                      source_name="T_WbVorgabe_Maske"),
        "Color": EAL.load_asset("/Engine/EngineResources/WhiteSquareTexture"),
    }

    parameters = collect_parameters()
    if not parameters:
        log("Keine Materialinstanzen des Kaefers gefunden - nichts zu tun.")
        return

    log("Parameter aus den Instanzen: %d" % len(parameters))
    build_master(parameters, defaults)
    log("FERTIG: %s neu gebaut" % MASTER)


main()

if unreal.SystemLibrary.get_command_line().find("-unattended") >= 0:
    unreal.SystemLibrary.quit_editor()
