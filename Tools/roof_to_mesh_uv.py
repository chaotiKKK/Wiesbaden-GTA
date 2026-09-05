# Stellt M_WbBuildingRoof von WELTRAUM-Projektion auf MESH-UVs um.
#
# Bisher projizierte das Dachmaterial die RoofClay-Textur ueber die Welt-
# position (WorldPosition), weil die frueher gebackene Dach-Geometrie keine
# brauchbaren UVs trug (georeferenzierte Welt-UVs verloren als float32 die
# Praezision -> graue Daecher). Der Gebaeudegenerator legt die Dach-UVs jetzt
# GEBAeUDE-LOKAL an (BuildingGenerator RoofUV: 1 UV-Einheit = 1 m relativ zum
# Gebaeude-Schwerpunkt), und nach dem Re-Bake tragen die Chunks diese UVs.
# Damit kann das Material die echten Mesh-UVs nutzen statt der Projektion.
#
# Kachelung: die Mesh-UV laeuft in Metern; RoofingTiles004 (Biberschwanz/
# Schuppen) bildet rund 1,7 m ab. Tiling 0,6 (= 1/1,67 m) bringt die Ziegel
# damit auf realistische Groesse.
#
# Aufruf (Editor oder -run=pythonscript):
#   UnrealEditor-Cmd.exe WiesbadenReal.uproject -run=pythonscript
#       -script="Tools/roof_to_mesh_uv.py" -unattended -nosplash -nop4

import os

import unreal

mel = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
TEX = "/Game/Materials/AAA/Textures"
MAT = "/Game/Materials/City/M_WbBuildingRoof"
TILING = 0.6   # 1 / (~1,67 m je Kachel), Mesh-UV in Metern


def log(msg):
    unreal.log("###WBROOF### %s" % msg)


def main():
    mat = unreal.load_asset(MAT)
    color = unreal.load_asset(TEX + "/RoofClay_C")
    normal = unreal.load_asset(TEX + "/RoofClay_N")
    rough = unreal.load_asset(TEX + "/RoofClay_R")
    if not (mat and color and normal and rough):
        log("Asset(s) fehlen - Abbruch.")
        raise SystemExit(1)

    mel.delete_all_material_expressions(mat)

    tile = mel.create_material_expression(
        mat, unreal.MaterialExpressionScalarParameter, -1300, 0)
    tile.set_editor_property("parameter_name", "Tiling")
    tile.set_editor_property("default_value", TILING)

    # MESH-UV statt WorldPosition: TextureCoordinate liest die gebackenen,
    # gebaeude-lokalen Dach-UVs.
    uv = mel.create_material_expression(
        mat, unreal.MaterialExpressionTextureCoordinate, -1100, 0)
    mul = mel.create_material_expression(
        mat, unreal.MaterialExpressionMultiply, -900, 0)
    mel.connect_material_expressions(uv, "", mul, "A")
    mel.connect_material_expressions(tile, "", mul, "B")

    def samp(tex, y, st):
        s = mel.create_material_expression(
            mat, unreal.MaterialExpressionTextureSample, -600, y)
        s.set_editor_property("texture", tex)
        s.set_editor_property("sampler_type", st)
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
    log("M_WbBuildingRoof auf Mesh-UVs umgestellt (Tiling %.2f)." % TILING)

    try:
        with open(os.path.join(unreal.Paths.project_dir(), "Saved", "roof_done.txt"),
                  "w", encoding="utf-8") as f:
            f.write("ok\n")
    except OSError as exc:
        log("Sentinel nicht geschrieben: %s" % exc)

    if os.environ.get("WB_QUIT"):
        unreal.SystemLibrary.quit_editor()


main()
