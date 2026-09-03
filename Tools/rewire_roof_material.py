# Verdrahtet M_WbBuildingRoof auf die AAA-RoofClay-Texturen um. Der Graph hat
# KEINE Parameter und ist ueber Python nicht lesbar (geschuetzt) - daher Neuaufbau
# aus dem Backup-gesicherten Zustand. Kachelung als Skalar-Parameter "Tiling".
import unreal
mel = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
TEX = "/Game/Materials/AAA/Textures"
MAT = "/Game/Materials/City/M_WbBuildingRoof"
TILING = 22.0

mat = unreal.load_asset(MAT)
color = unreal.load_asset(TEX + "/RoofClay_C")
normal = unreal.load_asset(TEX + "/RoofClay_N")
rough = unreal.load_asset(TEX + "/RoofClay_R")
if not (mat and color and normal and rough):
    unreal.log_warning("ROOFWIRE: Asset(s) fehlen - Abbruch."); raise SystemExit

mel.delete_all_material_expressions(mat)

tile = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -1300, 0)
tile.set_editor_property("parameter_name", "Tiling"); tile.set_editor_property("default_value", TILING)
uv = mel.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -1100, 0)
mul = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -900, 0)
mel.connect_material_expressions(uv, "", mul, "A")
mel.connect_material_expressions(tile, "", mul, "B")

def samp(tex, y, st):
    s = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSample, -600, y)
    s.set_editor_property("texture", tex); s.set_editor_property("sampler_type", st)
    mel.connect_material_expressions(mul, "", s, "UVs")
    return s

cs = samp(color, -300, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
mel.connect_material_property(cs, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
ns = samp(normal, 0, unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
mel.connect_material_property(ns, "RGB", unreal.MaterialProperty.MP_NORMAL)
rs = samp(rough, 300, unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE)
mel.connect_material_property(rs, "R", unreal.MaterialProperty.MP_ROUGHNESS)

mel.recompile_material(mat)
EAL.save_loaded_asset(mat)
unreal.log("ROOFWIRE: M_WbBuildingRoof auf RoofClay umverdrahtet (Tiling %.0f)." % TILING)
