"""Ersetzt Stadt-Oberflaechentexturen durch realistische PBR-Saetze.

Quelle: realistic-city-scene.zip (Drittanbieter-Kit, nur die verlaesslichen,
kachelbaren PBR-Saetze - NICHT die gebackenen Image_*-Atlanten, NICHT die
Gebaeude-Meshes). LIZENZ DES KITS IST VOM NUTZER VOR AUSLIEFERUNG ZU KLAEREN.

Ziel sind reine Flaechen ohne eingebrannte Fenster, damit nichts regressiert:
- Road05        -> T_Fahrbahn_Asphalt_{Color,Normal,Roughness}  (Strasse)
- Worn_Pavement -> T_Gehweg_Platten_{Color,Normal}              (Gehweg)
- concrete_02   -> T_Bordstein_Beton_{Color,Normal,Roughness}   (Bordstein/Beton)

Die Fassaden-Fototexturen (T_Facade_*) bleiben unberuehrt: sie bringen die
Fenster mit, kachelnder Beton wuerde sie loeschen.

Materialien muessen NICHT neu gebaut werden - make_road/make_sidewalk/make_kerb
laden diese Texturen bereits ueber surface_from_texture; die reimportierten
Bilddaten schlagen ohne Shader-Neubau durch.

Aufruf:
  UnrealEditor.exe WiesbadenReal.uproject -ExecCmds="py Tools/import_city_surface_tex.py"
      -unattended -nosplash -nop4
"""

import unreal

EAL = unreal.EditorAssetLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
DEST = "/Game/Textures/City"
SRC = r"C:/Users/HP/Downloads/_citytex"

COLOR, NORMAL, ROUGH = "color", "normal", "rough"

JOBS = [
    ("road_color.jpg",     "T_Fahrbahn_Asphalt_Color",     COLOR),
    ("road_normal.jpg",    "T_Fahrbahn_Asphalt_Normal",    NORMAL),
    ("road_rough.jpg",     "T_Fahrbahn_Asphalt_Roughness", ROUGH),
    ("pave_color.jpg",     "T_Gehweg_Platten_Color",       COLOR),
    ("pave_normal.jpg",    "T_Gehweg_Platten_Normal",      NORMAL),
    ("concrete_color.jpg", "T_Bordstein_Beton_Color",      COLOR),
    ("concrete_normal.jpg","T_Bordstein_Beton_Normal",     NORMAL),
    ("concrete_rough.jpg", "T_Bordstein_Beton_Roughness",  ROUGH),
]


def log(m):
    unreal.log("###CITYTEX### %s" % m)


for fname, asset, kind in JOBS:
    task = unreal.AssetImportTask()
    task.filename = "%s/%s" % (SRC, fname)
    task.destination_path = DEST
    task.destination_name = asset
    task.automated = True
    task.replace_existing = True
    task.save = True
    TOOLS.import_asset_tasks([task])

    tex = EAL.load_asset("%s/%s" % (DEST, asset))
    if tex is None:
        log("FEHLER: %s nicht importiert" % asset)
        continue
    # Kompression/Farbraum passend, sonst wird die Normalmap als Farbe behandelt
    # und die Rauheit sRGB-verzerrt.
    if kind == NORMAL:
        tex.set_editor_property("compression_settings",
                                unreal.TextureCompressionSettings.TC_NORMALMAP)
        tex.set_editor_property("srgb", False)
    elif kind == ROUGH:
        tex.set_editor_property("compression_settings",
                                unreal.TextureCompressionSettings.TC_GRAYSCALE)
        tex.set_editor_property("srgb", False)
    else:
        tex.set_editor_property("srgb", True)
    EAL.save_loaded_asset(tex)
    log("OK %s <- %s" % (asset, fname))

log("FERTIG")
if unreal.SystemLibrary.get_command_line().find("-unattended") >= 0:
    unreal.SystemLibrary.quit_editor()
