"""Importiert die gesammelten PBR-Texturen und baut daraus Materialien.

Quelle sind die Ordner unter Data/Raw/Materials, die
Tools/collect_materials.py anlegt - je Material ein Ordner mit
Color/Normal/Roughness und bei den Fassaden zusaetzlich Emission/Metallic.

Drei Einstellungen entscheiden hier ueber richtig und falsch, und alle drei
scheitern STUMM, wenn man sie vergisst:

* Normalmaps brauchen TC_NORMALMAP und sRGB AUS. Ohne das behandelt Unreal
  sie als Farbbild - die Oberflaeche wirkt dann flach und leicht blaustichig,
  ohne dass irgendwo ein Fehler erscheint.
* Rauheit, Metallic und Emission sind Zahlenfelder, keine Bilder: TC_GRAYSCALE
  bzw. TC_MASKS, sRGB AUS. Mit Farbraum-Korrektur sind alle Werte verschoben.
* Nur die Grundfarbe ist ein Bild und behaelt sRGB.

Die Texturgroesse ist bewusst 2K. Der Texturvorrat steht auf 1000 MB
(r.Streaming.PoolSize), die Karte hat 3965 MB - bei 22 Materialien mit bis zu
fuenf Kanaelen waere 4K nicht zu halten.

Aufruf (VOLLER Editor, wegen der Materialerzeugung):
  UnrealEditor.exe WiesbadenReal.uproject
      -ExecCmds="py Tools/import_materials.py" -unattended -nosplash
"""

import os

import unreal

PROJECT = unreal.Paths.project_dir()
SRC = os.path.join(PROJECT, "Data", "Raw", "Materials")

TEX_DIR = "/Game/Textures/City"
MAT_DIR = "/Game/Materials/City"
MASTER = "%s/M_WbSurface" % MAT_DIR

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
MP = unreal.MaterialProperty

# Kanal -> (Kompression, sRGB)
#
# TC_MASKS statt TC_GRAYSCALE fuer Metallic: Masken werden ohne
# Farbraum-Korrektur und ohne Alphakanal gespeichert, was fuer eine reine
# 0/1-Entscheidung wie "Glas oder Putz" genau richtig ist.
CHANNEL_SETTINGS = {
    "Color": (unreal.TextureCompressionSettings.TC_DEFAULT, True),
    "Normal": (unreal.TextureCompressionSettings.TC_NORMALMAP, False),
    "Roughness": (unreal.TextureCompressionSettings.TC_GRAYSCALE, False),
    "Metallic": (unreal.TextureCompressionSettings.TC_MASKS, False),
    "Emission": (unreal.TextureCompressionSettings.TC_DEFAULT, True),
}


def log(msg):
    unreal.log("###WBTEX### %s" % msg)


def import_texture(material_name, channel, path):
    asset_name = "T_%s_%s" % (material_name, channel)
    dest = "%s/%s" % (TEX_DIR, asset_name)

    task = unreal.AssetImportTask()
    task.filename = path
    task.destination_path = TEX_DIR
    task.destination_name = asset_name
    task.automated = True
    task.replace_existing = True
    task.save = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    texture = EAL.load_asset(dest)
    if texture is None:
        log("Import fehlgeschlagen: %s" % path)
        return None

    compression, srgb = CHANNEL_SETTINGS[channel]
    texture.set_editor_property("compression_settings", compression)
    texture.set_editor_property("srgb", srgb)
    EAL.save_loaded_asset(texture)
    return texture


DEFAULT_TEX_DIR = "/Game/Textures/Defaults"


def import_default(asset_name, channel):
    """Eine der drei Ersatztexturen aus Data/Raw/Defaults holen.

    Sie liegen in /Game/Textures/Defaults und werden von mehreren
    Grundmaterialien geteilt; erzeugt werden sie von
    Tools/Blender/make_default_textures.py.
    """
    dest = "%s/%s" % (DEFAULT_TEX_DIR, asset_name)
    if EAL.does_asset_exist(dest):
        return EAL.load_asset(dest)

    path = os.path.join(PROJECT, "Data", "Raw", "Defaults", "%s.png" % asset_name)
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
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

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
    """Ein Material fuer alle Oberflaechen, alles ueber Parameter.

    Ein Grundmaterial statt 22 einzelner: Unreal uebersetzt Shader je Material,
    und 22 fast gleiche Graphen waeren 22 Shader-Uebersetzungen bei jedem
    Start. Materialinstanzen teilen sich den Shader des Elternmaterials und
    tauschen nur die Texturen aus.
    """
    if EAL.does_asset_exist(MASTER):
        EAL.delete_asset(MASTER)

    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "M_WbSurface", MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())

    def expr(cls, x, y):
        return MEL.create_material_expression(mat, cls, x, y)

    # Weltbezogene Kachelung.
    #
    # Die Fassaden-UVs laufen U in Metern und V in Geschossen, die Fahrbahn in
    # Metern - eine Kachelgroesse in METERN ist damit fuer beide richtig, und
    # der Parameter sagt, wie viele Meter eine Texturkachel abdeckt.
    uv = expr(unreal.MaterialExpressionTextureCoordinate, -1400, 0)

    tiling = expr(unreal.MaterialExpressionScalarParameter, -1400, 200)
    tiling.set_editor_property("parameter_name", "KachelnProMeter")
    tiling.set_editor_property("default_value", 0.5)

    scaled = expr(unreal.MaterialExpressionMultiply, -1150, 60)
    MEL.connect_material_expressions(uv, "", scaled, "A")
    MEL.connect_material_expressions(tiling, "", scaled, "B")

    def sampler(name, x, y, default_texture=None):
        s = expr(unreal.MaterialExpressionTextureSampleParameter2D, x, y)
        s.set_editor_property("parameter_name", name)
        if default_texture is not None:
            s.set_editor_property("texture", default_texture)
        MEL.connect_material_expressions(scaled, "", s, "UVs")
        return s

    grey = EAL.load_asset("/Engine/EngineResources/WhiteSquareTexture")

    # Ersatztexturen, deren Kompression zum Abtasttyp passt.
    #
    # WhiteSquareTexture klingt neutral, ist aber ein FARBBILD mit
    # Farbraum-Korrektur. Steht es an einem Graustufen- oder Maskeneingang,
    # uebersetzt Unreal das Material NICHT und zeichnet stumm das
    # Standardmaterial. Im Protokoll steht dazu eine einzige Zeile:
    #
    #   "Sampler type is Linear Grayscale, should be Color for
    #    /Engine/EngineResources/WhiteSquareTexture"
    #
    # Die eigenen Vorgaben legt Tools/Blender/make_default_textures.py an; ihre
    # Kompression setzt der Import unten selbst, es bleibt nichts zu raten.
    flat_normal = import_default("T_WbVorgabe_Normal", "Normal")
    linear_white = import_default("T_WbVorgabe_Weiss", "Roughness")
    mask_white = import_default("T_WbVorgabe_Maske", "Metallic")

    color = sampler("BaseColor", -850, -400, grey)
    MEL.connect_material_property(color, "", MP.MP_BASE_COLOR)

    normal = sampler("Normal", -850, 100, flat_normal)
    normal.set_editor_property(
        "sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    MEL.connect_material_property(normal, "", MP.MP_NORMAL)

    rough = sampler("Roughness", -850, 600, linear_white or grey)
    rough.set_editor_property(
        "sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE)
    MEL.connect_material_property(rough, "R", MP.MP_ROUGHNESS)

    # MASKS, nicht LINEAR_GRAYSCALE: die Metallic-Karten werden oben als
    # TC_MASKS importiert (siehe CHANNEL_SETTINGS).
    metal = sampler("Metallic", -850, 1100, mask_white or grey)
    metal.set_editor_property(
        "sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)

    metal_scale = expr(unreal.MaterialExpressionMultiply, -450, 1100)
    metal_strength = expr(unreal.MaterialExpressionScalarParameter, -850, 1300)
    metal_strength.set_editor_property("parameter_name", "MetallicStaerke")
    metal_strength.set_editor_property("default_value", 0.0)
    MEL.connect_material_expressions(metal, "R", metal_scale, "A")
    MEL.connect_material_expressions(metal_strength, "", metal_scale, "B")
    MEL.connect_material_property(metal_scale, "", MP.MP_METALLIC)

    # Fensterlicht.
    #
    # Bei 104.458 Gebaeuden waere eine Lichtquelle je Fenster unbezahlbar. Als
    # Textur kostet es nichts, und die Staerke laesst sich zur Laufzeit an die
    # Tageszeit haengen - das Wettersystem kennt sie bereits.
    emissive = sampler("Emission", -850, 1600, grey)
    emissive_strength = expr(unreal.MaterialExpressionScalarParameter, -850, 1850)
    emissive_strength.set_editor_property("parameter_name", "FensterlichtStaerke")
    emissive_strength.set_editor_property("default_value", 0.0)

    emissive_scaled = expr(unreal.MaterialExpressionMultiply, -450, 1600)
    MEL.connect_material_expressions(emissive, "", emissive_scaled, "A")
    MEL.connect_material_expressions(emissive_strength, "", emissive_scaled, "B")
    MEL.connect_material_property(emissive_scaled, "", MP.MP_EMISSIVE_COLOR)

    # Pflicht, sonst zeichnet Unreal an Instanz-Komponenten kommentarlos das
    # Standardmaterial - das graue Schachbrett.
    mat.set_editor_property("used_with_instanced_static_meshes", True)

    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    return mat


def build_instance(master, name, textures, tiling_meters, metallic, emission):
    inst_name = "MI_Wb%s" % name
    path = "%s/%s" % (MAT_DIR, inst_name)
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)

    inst = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        inst_name, MAT_DIR, unreal.MaterialInstanceConstant,
        unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(inst, master)

    for channel, texture in textures.items():
        param = "BaseColor" if channel == "Color" else channel
        MEL.set_material_instance_texture_parameter_value(inst, param, texture)

    MEL.set_material_instance_scalar_parameter_value(
        inst, "KachelnProMeter", 1.0 / max(tiling_meters, 0.01))
    MEL.set_material_instance_scalar_parameter_value(inst, "MetallicStaerke", metallic)
    MEL.set_material_instance_scalar_parameter_value(inst, "FensterlichtStaerke", emission)

    EAL.save_loaded_asset(inst)
    return inst


# Kachelgroesse in METERN je Material.
#
# Nicht geraten, sondern an der Wirklichkeit ausgerichtet: Eine Ziegelwand
# zeigt auf zwei Metern rund 25 Schichten, eine Fassadentextur deckt ueblich
# ein bis zwei Geschosse ab, und Asphalt wirkt ab etwa vier Metern nicht mehr
# gekachelt.
TILING = {
    "Fahrbahn_Asphalt": 4.0, "Fahrbahn_Asphalt_Alt": 4.0, "Fahrbahn_Strasse": 6.0,
    "Fahrbahn_Markierung": 4.0,
    "Gehweg_Platten": 2.0, "Gehweg_Pflaster": 2.0, "Platz_Pflaster": 2.5,
    "Bordstein_Beton": 2.0,
    "Facade_Glasturm": 6.0, "Facade_Hochhaus_Nacht": 6.0,
    "Facade_Buerohaus": 6.0, "Facade_Nachkrieg": 6.0,
    "Facade_Backstein": 2.0, "Facade_Klinker": 2.0,
    "Facade_Ziegel": 2.0, "Facade_Sandstein": 2.0,
    "Gelaende_Wiese": 3.0, "Gelaende_Gras": 3.0, "Gelaende_Waldboden": 4.0,
    "Gelaende_Fels": 4.0, "Gelaende_Stein": 4.0, "Gelaende_Steinig": 5.0,
}

master = build_master()
log("Grundmaterial: %s" % MASTER)

built = 0
for folder in sorted(os.listdir(SRC)):
    path = os.path.join(SRC, folder)
    if not os.path.isdir(path):
        continue

    textures = {}
    for filename in sorted(os.listdir(path)):
        for channel in CHANNEL_SETTINGS:
            if filename.endswith(("_%s.png" % channel, "_%s.jpg" % channel,
                                  "_%s.exr" % channel)):
                texture = import_texture(folder, channel, os.path.join(path, filename))
                if texture is not None:
                    textures[channel] = texture
                break

    if "Color" not in textures:
        log("%-24s uebersprungen - keine Grundfarbe" % folder)
        continue

    # Fassaden mit Fensterlicht: Die Staerke bleibt hier bei 0 und wird zur
    # Laufzeit nach Tageszeit gesetzt. Ein Haus, dessen Fenster mittags
    # leuchten, sieht falscher aus als eines mit dunklen Fenstern nachts.
    emission = 0.0 if "Emission" in textures else 0.0
    metallic = 1.0 if "Metallic" in textures else 0.0

    build_instance(master, folder, textures, TILING.get(folder, 2.0),
                   metallic, emission)
    built += 1
    log("%-24s %s  Kachel %.1f m" % (folder, ", ".join(sorted(textures)),
                                     TILING.get(folder, 2.0)))

log("FERTIG: %d Materialien unter %s" % (built, MAT_DIR))

if os.environ.get("WB_QUIT"):
    unreal.SystemLibrary.quit_editor()
