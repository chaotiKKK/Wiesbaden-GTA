# Baut die rein prozeduralen M_Wb-BODEN-Materialien (kein Foto-Sample, nur
# T_WbNoise + Fmod-Muster) als schlichte AAA-PBR-Materialien neu, damit auch sie
# in der gebackenen Karte die AAA-Optik zeigen. Betroffen sind die HAeUFIGEN
# Boden-Kanaele (Unpaved ~389 Chunks, PavedStone ~162) - reine Bodenflaechen ohne
# Fenster, daher risikolos komplett neu aufbaubar (anders als die prozeduralen
# Fassaden mit Fenster-Graph, die absichtlich bleiben).
#
# Der Materialname bleibt (die gebackenen Chunks referenzieren ihn per Pfad) -
# nur der Graph wird AAA. Struktur = aaa_import_materials.build_material.
import unreal

mel = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
CITY = "/Game/Materials/City/"
TEX = "/Game/Materials/AAA/Textures/"

# Material -> (AAA-Textur-Basis, Tiling)
BUILD = {
    "M_WbPavedStone": ("Paving", 8.0),
    "M_WbUnpaved": ("GroundDirt", 45.0),
}

COL = unreal.MaterialSamplerType.SAMPLERTYPE_COLOR
NRM = unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL
LIN = unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE
MP = unreal.MaterialProperty


def load(name):
    return unreal.load_asset(TEX + name)


for mat_name, (base, tiling) in BUILD.items():
    mat = unreal.load_asset(CITY + mat_name)
    color = load(base + "_C")
    normal = load(base + "_N")
    rough = load(base + "_R")
    ao = load(base + "_AO")
    if not (mat and color and normal and rough):
        unreal.log_warning("GROUND %s: Asset(s) fehlen" % mat_name); continue

    mel.delete_all_material_expressions(mat)

    tile = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -1300, 0)
    tile.set_editor_property("parameter_name", "Tiling")
    tile.set_editor_property("default_value", float(tiling))
    uv = mel.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -1100, 0)
    mul = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -900, 0)
    mel.connect_material_expressions(uv, "", mul, "A")
    mel.connect_material_expressions(tile, "", mul, "B")

    def sample(tex, y, st):
        s = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSample, -600, y)
        s.set_editor_property("texture", tex)
        s.set_editor_property("sampler_type", st)
        mel.connect_material_expressions(mul, "", s, "UVs")
        return s

    cs = sample(color, -300, COL)
    mel.connect_material_property(cs, "RGB", MP.MP_BASE_COLOR)
    ns = sample(normal, 0, NRM)
    mel.connect_material_property(ns, "RGB", MP.MP_NORMAL)
    rs = sample(rough, 300, LIN)
    mel.connect_material_property(rs, "R", MP.MP_ROUGHNESS)
    if ao:
        aos = sample(ao, 600, LIN)
        mel.connect_material_property(aos, "R", MP.MP_AMBIENT_OCCLUSION)

    mel.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    unreal.log("GROUND %s: als AAA-%s neu gebaut (Tiling %.0f)." % (mat_name, base, tiling))

unreal.log("GROUND: fertig.")
