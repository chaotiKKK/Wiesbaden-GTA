"""M_VehPaintVaried: Fahrzeuglack, dessen Farbe PRO INSTANZ aus den Custom-Data-
Floats (CD0..2 = RGB) kommt. Der Traffic-Spawner legt dieses Material auf Slot 0
(Lack) der neuen Typen und setzt je Fahrzeug eine Farbe aus der Fahrzeug-Id.

WICHTIG: used_with_instanced_static_meshes, sonst liefert PerInstanceCustomData
im -game-Lauf 0 (Fahrzeuge waeren schwarz) - dieselbe Falle wie bei den
Fussgaenger-Materialien.
"""
import unreal
MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
MP = unreal.MaterialProperty
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
DIR = "/Game/Vehicles/Traffic/Mats"
EAL.make_directory(DIR)

name = "M_VehPaintVaried"
p = "%s/%s" % (DIR, name)
if EAL.does_asset_exist(p):
    EAL.delete_asset(p)
m = TOOLS.create_asset(name, DIR, unreal.Material, unreal.MaterialFactoryNew())
m.set_editor_property("used_with_instanced_static_meshes", True)

def cd(idx, y):
    n = MEL.create_material_expression(m, unreal.MaterialExpressionPerInstanceCustomData, -700, y)
    n.set_editor_property("data_index", idx)
    return n

c0, c1, c2 = cd(0, -100), cd(1, 40), cd(2, 180)
ap1 = MEL.create_material_expression(m, unreal.MaterialExpressionAppendVector, -480, -40)
MEL.connect_material_expressions(c0, "", ap1, "A")
MEL.connect_material_expressions(c1, "", ap1, "B")
ap2 = MEL.create_material_expression(m, unreal.MaterialExpressionAppendVector, -300, 20)
MEL.connect_material_expressions(ap1, "", ap2, "A")
MEL.connect_material_expressions(c2, "", ap2, "B")
MEL.connect_material_property(ap2, "", MP.MP_BASE_COLOR)

rough = MEL.create_material_expression(m, unreal.MaterialExpressionConstant, -400, 320)
rough.set_editor_property("r", 0.40)
MEL.connect_material_property(rough, "", MP.MP_ROUGHNESS)
metal = MEL.create_material_expression(m, unreal.MaterialExpressionConstant, -400, 440)
metal.set_editor_property("r", 0.15)
MEL.connect_material_property(metal, "", MP.MP_METALLIC)

MEL.recompile_material(m)
EAL.save_loaded_asset(m)
open("C:/freebuff/WiesbadenReal_Sicherung/WiesbadenReal/ppl.txt", "w").write("OK %s" % p)
if unreal.SystemLibrary.get_command_line().find("-unattended") >= 0:
    unreal.SystemLibrary.quit_editor()
