"""Legt die zwei Materialien fuer die Abfahrtsmonitor-Saeule an:
  /Game/Props/DFI/M_WbDfiPanel  - dunkles Anzeigepanel
  /Game/Props/DFI/M_WbDfiPole   - graue Metallsaeule
"""
import unreal
def log(m): unreal.log("###DFI### %s" % m)
EAL = unreal.EditorAssetLibrary
ATH = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
DEST = "/Game/Props/DFI"
if not EAL.does_directory_exist(DEST):
    EAL.make_directory(DEST)

def make(name, color, rough, metal=0.0):
    p = DEST + "/" + name
    if EAL.does_asset_exist(p): EAL.delete_asset(p)
    mat = ATH.create_asset(name, DEST, unreal.Material, unreal.MaterialFactoryNew())
    base = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -400, -60)
    base.set_editor_property("constant", unreal.LinearColor(color[0], color[1], color[2], 1.0))
    MEL.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    r = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, -400, 90)
    r.set_editor_property("r", rough)
    MEL.connect_material_property(r, "", unreal.MaterialProperty.MP_ROUGHNESS)
    if metal > 0.0:
        mm = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, -400, 210)
        mm.set_editor_property("r", metal)
        MEL.connect_material_property(mm, "", unreal.MaterialProperty.MP_METALLIC)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    log("Material %s" % p)

make("M_WbDfiPanel", (0.012, 0.012, 0.020), 0.55)
make("M_WbDfiPole", (0.18, 0.18, 0.20), 0.35, 0.8)
log("ENDE")
