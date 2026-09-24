"""Create a small emissive material for the player's visible lamp lenses."""
import unreal

eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
mp = unreal.MaterialProperty
tools = unreal.AssetToolsHelpers.get_asset_tools()
root = '/Game/Vehicles/Beetle/Restored'
name = 'M_WbBeetleLamp'
path = root + '/' + name
if eal.does_asset_exist(path):
    unreal.log('###BEETLE_LAMP### Reusing ' + path)
else:
    eal.make_directory(root)
    mat = tools.create_asset(name, root, unreal.Material,
                             unreal.MaterialFactoryNew())
    mat.set_editor_property('two_sided', True)
    color = mel.create_material_expression(mat,
            unreal.MaterialExpressionVectorParameter, -500, -120)
    color.set_editor_property('parameter_name', 'LensColor')
    color.set_editor_property('default_value', unreal.LinearColor(.8,.1,.05,1))
    gain = mel.create_material_expression(mat,
            unreal.MaterialExpressionScalarParameter, -500, 140)
    gain.set_editor_property('parameter_name', 'Glow')
    gain.set_editor_property('default_value', .0)
    emit = mel.create_material_expression(mat,
            unreal.MaterialExpressionMultiply, -240, -20)
    mel.connect_material_expressions(color, '', emit, 'A')
    mel.connect_material_expressions(gain, '', emit, 'B')
    mel.connect_material_property(emit, '', mp.MP_EMISSIVE_COLOR)
    mel.connect_material_property(color, '', mp.MP_BASE_COLOR)
    rough = mel.create_material_expression(mat,
            unreal.MaterialExpressionConstant, -260, 280)
    rough.set_editor_property('r', .22)
    mel.connect_material_property(rough, '', mp.MP_ROUGHNESS)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    unreal.log('###BEETLE_LAMP### Created ' + path)
