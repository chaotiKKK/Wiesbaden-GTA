"""Leuchtendes Glas der Strassenlaternen: /Game/Materials/City/M_WbStreetLampGlass.

Emissiv = LensColor * Glow; der Ausstattungs-Spawner legt EINE Dynamic-
Instanz auf alle Glaeser und dreht Glow nach der Tageszeit (0 am Tag).

WICHTIG: used_with_instanced_static_meshes - die Glaeser sind HISM-Instanzen.
Ohne das Flag ersetzt UE das Material im Spiel still durch das Default-
Material (dieselbe Falle wie bei M_VehPaintVaried und den Fussgaengern).

UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=Tools/create_street_lamp_material.py
"""
import unreal

eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
mp = unreal.MaterialProperty
tools = unreal.AssetToolsHelpers.get_asset_tools()
root = '/Game/Materials/City'
name = 'M_WbStreetLampGlass'
path = root + '/' + name
if eal.does_asset_exist(path):
    eal.delete_asset(path)
mat = tools.create_asset(name, root, unreal.Material, unreal.MaterialFactoryNew())
mat.set_editor_property('used_with_instanced_static_meshes', True)

color = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -600, -120)
color.set_editor_property('parameter_name', 'LensColor')
color.set_editor_property('default_value', unreal.LinearColor(1.0, .78, .48, 1))
glow = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -600, 120)
glow.set_editor_property('parameter_name', 'Glow')
glow.set_editor_property('default_value', 0.0)
emit = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -300, 0)
mel.connect_material_expressions(color, '', emit, 'A')
mel.connect_material_expressions(glow, '', emit, 'B')
mel.connect_material_property(emit, '', mp.MP_EMISSIVE_COLOR)

# Tagsueber: mattes Milchglas (hell, leicht warm), nicht spiegelnd.
base = mel.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -300, 220)
base.set_editor_property('constant', unreal.LinearColor(.80, .78, .72, 1))
mel.connect_material_property(base, '', mp.MP_BASE_COLOR)
rough = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -300, 360)
rough.set_editor_property('r', .35)
mel.connect_material_property(rough, '', mp.MP_ROUGHNESS)

mel.recompile_material(mat)
eal.save_loaded_asset(mat)
unreal.log('###STREET_LAMP_GLASS### %s ism=%s' % (path, mat.get_editor_property('used_with_instanced_static_meshes')))
