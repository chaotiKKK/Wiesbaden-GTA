"""Erzeugt EIN eigenstaendiges Naturstein-Material fuer die Nerobergbahn-Pfeiler.

M_WbNaturstein: warmer, verwitterter Sandstein (Wiesbaden-typisch) - tonale
Variation ueber weltpositionsbezogenes Rauschen, matt. Bewusst OHNE Fenster
(anders als die Fassaden-Fototexturen), damit es auf Saeulen/Pfeilern stimmt.

Eigenstaendig gebaut (nicht ueber build_materials.main()), damit die uebrigen
Stadtmaterialien - inkl. offener WIP - NICHT neu gebacken werden.

Aufruf:
  UnrealEditor.exe WiesbadenReal.uproject
      -ExecCmds="py Tools/create_stone_material.py" -unattended -nosplash -nop4
"""

import unreal

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
MP = unreal.MaterialProperty
MAT_DIR = "/Game/Materials/City"
NAME = "M_WbNaturstein"


def log(m):
    unreal.log("###STONE### %s" % m)


path = "%s/%s" % (MAT_DIR, NAME)
if EAL.does_asset_exist(path):
    EAL.delete_asset(path)

mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
    NAME, MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
if mat is None:
    log("FEHLER: Material konnte nicht erstellt werden")
    raise SystemExit(1)


def expr(cls, x, y):
    return MEL.create_material_expression(mat, cls, x, y)


def c3(r, g, b, x, y):
    n = expr(unreal.MaterialExpressionConstant3Vector, x, y)
    n.set_editor_property("constant", unreal.LinearColor(r, g, b, 1.0))
    return n


def c1(v, x, y):
    n = expr(unreal.MaterialExpressionConstant, x, y)
    n.set_editor_property("r", v)
    return n


# Tonale Sandstein-Variation. Das Rauschen nutzt ohne Position-Eingang die
# WELTposition -> Steinmuster ist unabhaengig von der Pfeilerskalierung gleich
# gross (die Pfeiler sind unterschiedlich hoch skalierte Wuerfel).
noise = expr(unreal.MaterialExpressionNoise, -750, -50)
noise.set_editor_property("scale", 0.025)
noise.set_editor_property("output_min", 0.0)
noise.set_editor_property("output_max", 1.0)

hell = c3(0.46, 0.37, 0.26, -1050, -280)   # heller, warmer Sandstein
dunkel = c3(0.28, 0.22, 0.15, -1050, -40)  # dunklere, verwitterte Partie
base = expr(unreal.MaterialExpressionLinearInterpolate, -450, -150)
MEL.connect_material_expressions(hell, "", base, "A")
MEL.connect_material_expressions(dunkel, "", base, "B")
MEL.connect_material_expressions(noise, "", base, "Alpha")
MEL.connect_material_property(base, "", MP.MP_BASE_COLOR)

# Stein ist matt; leichte Rauheitsvariation ueber dasselbe Rauschen.
rough = expr(unreal.MaterialExpressionLinearInterpolate, -450, 300)
MEL.connect_material_expressions(c1(0.78, -1050, 220), "", rough, "A")
MEL.connect_material_expressions(c1(0.92, -1050, 380), "", rough, "B")
MEL.connect_material_expressions(noise, "", rough, "Alpha")
MEL.connect_material_property(rough, "", MP.MP_ROUGHNESS)

MEL.connect_material_property(c1(0.0, -450, 560), "", MP.MP_METALLIC)

MEL.recompile_material(mat)
EAL.save_loaded_asset(mat)
log("FERTIG: %s erstellt und gespeichert" % path)

if unreal.SystemLibrary.get_command_line().find("-unattended") >= 0:
    unreal.SystemLibrary.quit_editor()
