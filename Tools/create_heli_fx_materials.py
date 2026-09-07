"""Durchscheinende FX-Materialien fuer den Heli: Rotor-Blur und Downwash-Staub.

Beide haben einen Skalar-Parameter 'Opacity', den der Heli zur Laufzeit ueber
eine MaterialInstanceDynamic aus Drehzahl bzw. Bodennaehe/Collective steuert.
"""
import unreal
MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
MP = unreal.MaterialProperty
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
DIR = "/Game/Materials/City"

def log(m): unreal.log("###HELIFX### %s" % m)

def make(name, r, g, b, dusty):
    p = "%s/%s" % (DIR, name)
    if EAL.does_asset_exist(p):
        EAL.delete_asset(p)
    m = TOOLS.create_asset(name, DIR, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property("two_sided", True)

    col = MEL.create_material_expression(m, unreal.MaterialExpressionConstant3Vector, -600, 0)
    col.set_editor_property("constant", unreal.LinearColor(r, g, b, 1.0))
    MEL.connect_material_property(col, "", MP.MP_BASE_COLOR)

    rough = MEL.create_material_expression(m, unreal.MaterialExpressionConstant, -600, 250)
    rough.set_editor_property("r", 0.9)
    MEL.connect_material_property(rough, "", MP.MP_ROUGHNESS)

    # Steuerbarer Opacity-Parameter (0 = unsichtbar).
    op = MEL.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -600, 450)
    op.set_editor_property("parameter_name", "Opacity")
    op.set_editor_property("default_value", 0.0)

    if dusty:
        # Staub: Opacity * Rauschen -> koerniger, lebendiger Belag.
        nz = MEL.create_material_expression(m, unreal.MaterialExpressionNoise, -600, 650)
        nz.set_editor_property("scale", 0.35)
        nz.set_editor_property("output_min", 0.35)
        nz.set_editor_property("output_max", 1.0)
        mul = MEL.create_material_expression(m, unreal.MaterialExpressionMultiply, -300, 500)
        MEL.connect_material_expressions(op, "", mul, "A")
        MEL.connect_material_expressions(nz, "", mul, "B")
        MEL.connect_material_property(mul, "", MP.MP_OPACITY)
    else:
        MEL.connect_material_property(op, "", MP.MP_OPACITY)

    MEL.recompile_material(m)
    EAL.save_loaded_asset(m)
    log(p)

make("M_WbRotorBlur", 0.13, 0.13, 0.145, dusty=False)   # dunkelgraue, durchscheinende Rotorscheibe
make("M_WbDownwash",  0.58, 0.52, 0.44, dusty=True)      # heller Staub (Downwash)
log("FERTIG")
if unreal.SystemLibrary.get_command_line().find("-unattended") >= 0:
    unreal.SystemLibrary.quit_editor()
