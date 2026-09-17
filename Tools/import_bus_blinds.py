"""Importiert die Bus-Blind-Texturen und legt drei Unlit-Emissive-Materialien an.

Texturen  -> /Game/Vehicles/Bus/Blind/T_WbBlind{Mainz,Nord,Route6}
Materialien-> /Game/Vehicles/Bus/Blind/M_WbBlind{Mainz,Nord,Route6}
   Unlit, zweiseitig, Emissive = Textur x Boost (LEDs leuchten, Gehaeuse dunkel).

Aufruf:
  UnrealEditor-Cmd.exe WiesbadenReal.uproject -run=pythonscript
      -script="Tools/import_bus_blinds.py" -unattended -nop4 -nosplash
"""
import os, unreal

def log(m): unreal.log("###BLIND### %s" % m)

EAL = unreal.EditorAssetLibrary
ATH = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
DEST = "/Game/Vehicles/Bus/Blind"
RAW = os.path.join(unreal.Paths.project_dir(), "Data", "Raw", "Bus", "blind")
if not EAL.does_directory_exist(DEST):
    EAL.make_directory(DEST)

# Eine Zeile je Schild - dieselbe Liste steht in make_bus_blind_textures.py, das die
# PNGs erzeugt. Slots, die es schon gibt, werden ersetzt (replace_existing), die
# Assetnamen bleiben gleich; neue Linien kommen hier dazu.
FILES = [("blind_mainz.png", "T_WbBlindMainz", "M_WbBlindMainz"),
         ("blind_nord.png", "T_WbBlindNord", "M_WbBlindNord"),
         ("blind_route6.png", "T_WbBlindRoute6", "M_WbBlindRoute6"),
         ("blind_l3_rheinufer.png", "T_WbBlindL3Rheinufer", "M_WbBlindL3Rheinufer"),
         ("blind_l3_nordfriedhof.png", "T_WbBlindL3Nordfriedhof", "M_WbBlindL3Nordfriedhof"),
         ("blind_l3_route3.png", "T_WbBlindL3Route3", "M_WbBlindL3")]

def import_texture(png, tex_name):
    if not os.path.exists(os.path.join(RAW, png)):
        log("uebersprungen (PNG fehlt): %s" % png)
        return None
    path = DEST + "/" + tex_name
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", os.path.join(RAW, png))
    t.set_editor_property("destination_path", DEST)
    t.set_editor_property("destination_name", tex_name)
    t.set_editor_property("automated", True)
    t.set_editor_property("replace_existing", True)
    t.set_editor_property("save", True)
    ATH.import_asset_tasks([t])
    return unreal.load_asset(path)

def make_material(mat_name, tex):
    p = DEST + "/" + mat_name
    if EAL.does_asset_exist(p):
        EAL.delete_asset(p)
    mat = ATH.create_asset(mat_name, DEST, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", True)
    sample = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureSample, -420, 0)
    sample.set_editor_property("texture", tex)
    boost = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, -420, 180)
    boost.set_editor_property("r", 1.6)   # LEDs etwas ueberstrahlen lassen
    mul = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -200, 0)
    MEL.connect_material_expressions(sample, "RGB", mul, "A")
    MEL.connect_material_expressions(boost, "", mul, "B")
    MEL.connect_material_property(mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    return mat

ok = 0
for png, tex_name, mat_name in FILES:
    tex = import_texture(png, tex_name)
    if tex is None:
        log("FEHLER Textur %s" % tex_name); continue
    make_material(mat_name, tex)
    ok += 1
    log("%s + %s" % (tex_name, mat_name))

log("ENDE ok=%d/%d" % (ok, len(FILES)))

# Editor beenden: ohne das laeuft der -run=pythonscript-Aufruf nach dem Skript
# weiter und haelt seine Logdatei offen - der naechste Lauf bricht dann mit
# "Datei wird von einem anderen Prozess verwendet" ab (siehe AGENTS.md).
if unreal.SystemLibrary.get_command_line().find("-unattended") >= 0:
    unreal.SystemLibrary.quit_editor()
