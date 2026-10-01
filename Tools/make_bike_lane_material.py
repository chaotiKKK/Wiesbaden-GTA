"""UE-Python: eigenes rotes Paint-Material fuer gebackene Radfahrstreifen."""
import unreal

PATH = "/Game/Materials/City/M_WbBikeLaneSurface"
if not unreal.EditorAssetLibrary.does_asset_exist(PATH):
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "M_WbBikeLaneSurface", "/Game/Materials/City", unreal.Material,
        unreal.MaterialFactoryNew())
    if material is None:
        raise RuntimeError("Radstreifen-Material konnte nicht angelegt werden")
    mel = unreal.MaterialEditingLibrary
    color = mel.create_material_expression(
        material, unreal.MaterialExpressionConstant3Vector, -300, 0)
    color.set_editor_property("constant", unreal.LinearColor(0.32, 0.035, 0.025, 1))
    mel.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
    rough = mel.create_material_expression(
        material, unreal.MaterialExpressionConstant, -300, 140)
    rough.set_editor_property("r", 0.82)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    material.set_editor_property("two_sided", False)
    mel.recompile_material(material)
    if not unreal.EditorAssetLibrary.save_loaded_asset(material):
        raise RuntimeError("Radstreifen-Material konnte nicht gespeichert werden")
    unreal.log("Radstreifen-Material angelegt: " + PATH)
else:
    unreal.log("Radstreifen-Material bereits vorhanden; unveraendert: " + PATH)
