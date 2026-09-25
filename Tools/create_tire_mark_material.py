"""M_WbTireMark: ein Deferred-Decal fuer Reifen-/Bremsspuren am Boden.

WARUM ein Decal (kein Mesh, kein Niagara): Bremsspuren liegen flach auf der
Fahrbahn und muessen sich der Strassenwoelbung anpassen - genau das kann ein
Deferred Decal, und es ist (anders als Niagara) in dieser Umgebung per Editor-
Python baubar. Das Fahrzeug spawnt beim Radspin/Blockieren kurze Decals entlang
der Spur (WiesbadenTireEffectsComponent).

Das Material ist ein dunkler, weicher Fleck: radialer Abfall aus der Decal-UV,
damit die aneinandergereihten Flecken zu einer Spur verschmelzen statt als
harte Kacheln zu erscheinen. Translucent, nur BaseColor+Opacity - kein Normal/
Roughness noetig.

Aufruf:
  UnrealEditor-Cmd.exe WiesbadenReal.uproject
    -ExecCmds="py exec(open('C:/.../Tools/create_tire_mark_material.py').read())"
    -unattended -nosplash -nop4 -nullrhi
"""

import unreal

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
MP = unreal.MaterialProperty
PKG = "/Game/Materials/City"
NAME = "M_WbTireMark"
PATH = PKG + "/" + NAME


def log(m):
    unreal.log("###TIREMARK### %s" % m)


mat = EAL.load_asset(PATH)
if mat is None:
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mat = tools.create_asset(NAME, PKG, unreal.Material, unreal.MaterialFactoryNew())
if mat is None:
    raise SystemExit("FEHLER: %s nicht anlegbar" % PATH)

mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_DEFERRED_DECAL)
mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
MEL.delete_all_material_expressions(mat)


def E(cls, x, y):
    return MEL.create_material_expression(mat, cls, x, y)


def C1(v, x, y):
    n = E(unreal.MaterialExpressionConstant, x, y)
    n.set_editor_property("r", float(v))
    return n


def C3(r, g, b, x, y):
    n = E(unreal.MaterialExpressionConstant3Vector, x, y)
    n.set_editor_property("constant", unreal.LinearColor(r, g, b, 1.0))
    return n


def link(a, ao, b, bi):
    MEL.connect_material_expressions(a, ao, b, bi)


def binop(cls, a, b, x, y, ao="", bo=""):
    n = E(cls, x, y)
    link(a, ao, n, "A")
    link(b, bo, n, "B")
    return n


def sub(a, b, x, y, ao="", bo=""):
    return binop(unreal.MaterialExpressionSubtract, a, b, x, y, ao, bo)


def mul(a, b, x, y, ao="", bo=""):
    return binop(unreal.MaterialExpressionMultiply, a, b, x, y, ao, bo)


# UV, zentriert auf (0.5,0.5) -> radialer Abstand -> weicher Fleck.
tc = E(unreal.MaterialExpressionTextureCoordinate, -900, 100)
centered = sub(tc, C1(0.5, -740, 240), -560, 140)          # (u-0.5, v-0.5)
dist = E(unreal.MaterialExpressionDistance, -380, 140)
link(centered, "", dist, "A")
# zweiter Eingang 0 -> Betrag des Vektors
link(C1(0.0, -560, 260), "", dist, "B")
# Opazitaet: 1 in der Mitte, 0 am Rand (Radius 0.5), weich per smoothstep.
soft = E(unreal.MaterialExpressionSmoothStep, -200, 140)
soft.set_editor_property("const_min", 0.5)
soft.set_editor_property("const_max", 0.15)   # min>max -> invertiert: nah=1, fern=0
link(dist, "", soft, "Value")
opacity = mul(soft, C1(0.92, -40, 260), 120, 180)

col = C3(0.02, 0.02, 0.02, 120, 0)   # fast schwarz
MEL.connect_material_property(col, "", MP.MP_BASE_COLOR)
MEL.connect_material_property(opacity, "", MP.MP_OPACITY)

MEL.recompile_material(mat)
EAL.save_loaded_asset(mat)
log("FERTIG: %s (Deferred Decal, weicher dunkler Fleck)." % PATH)

if unreal.SystemLibrary.get_command_line().find("-unattended") >= 0:
    unreal.SystemLibrary.quit_editor()
