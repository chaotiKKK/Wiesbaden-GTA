"""M_WbSidewalk: aus glaenzend-blauem Belag wird matter Beton-Plattenweg.

WARUM - gemessen am Nahbild (Saved/Diagnose/nah_kante2/3.png):

Der Gehweg war stark SPIEGELND und blau: die Rauheit lag so niedrig, dass die
Flaeche den Himmel zurueckwarf und nass/glaesern wirkte - an den Kanten sah es
aus wie eine durchsichtige Pfuetze. Genau das nennt der Auftrag ("anstaendig
texturieren", "transparente Stellen an den Kanten").

WAS SICH AENDERT (reine Materialaenderung, KEIN Re-Bake):

Ein MATTER Beton-Plattenbelag - rechteckige Gehwegplatten (~45 cm) mit
dunklen Fugen, je Platte eine leichte Tonschwankung, Rauheit 0,92. Kein
Metallic, kein Glanz: die Flaeche wirft den Himmel nicht mehr zurueck.

WARUM PROZEDURAL statt Textur: volle Kontrolle ueber Rauheit und Farbe (die
alte Textur-Fassung geriet zu glatt), keine Textur-Abhaengigkeit, und das
Plattenraster kommt aus der WELTPOSITION - gleich gross, egal wie der Gehweg
zur Weltachse liegt.

IN-PLACE: das bestehende Asset M_WbSidewalk wird nur umgeschrieben. Die
gebackenen Strassen-Kacheln verweisen fest darauf (WiesbadenWorldBuilder.cpp
:1552), die Aenderung wirkt also sofort auf Alkis16 ohne Neubau.

Aufruf:
  UnrealEditor-Cmd.exe WiesbadenReal.uproject
    -ExecCmds="py exec(open('Tools/create_sidewalk_material.py').read())"
    -unattended -nosplash -nop4 -nullrhi
"""

import unreal

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
MP = unreal.MaterialProperty
PATH = "/Game/Materials/City/M_WbSidewalk"

SLAB_CM = 45.0     # Gehwegplatte ~45 cm
JOINT = 0.06       # Fugenbreite (Anteil einer Platte)


def log(m):
    unreal.log("###SIDEWALK### %s" % m)


mat = EAL.load_asset(PATH)
if mat is None:
    raise SystemExit("FEHLER: %s nicht gefunden" % PATH)

MEL.delete_all_material_expressions(mat)
# Blendmodus/2-seitig bleiben wie sie sind (OPAQUE, einseitig) - der Gehweg
# wird von oben gesehen, eine Rueckseite braucht er nicht.


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


def add(a, b, x, y, ao="", bo=""):
    return binop(unreal.MaterialExpressionAdd, a, b, x, y, ao, bo)


def sub(a, b, x, y, ao="", bo=""):
    return binop(unreal.MaterialExpressionSubtract, a, b, x, y, ao, bo)


def mul(a, b, x, y, ao="", bo=""):
    return binop(unreal.MaterialExpressionMultiply, a, b, x, y, ao, bo)


def divc(a, scalar, x, y, ao=""):
    n = E(unreal.MaterialExpressionDivide, x, y)
    link(a, ao, n, "A")
    n.set_editor_property("const_b", float(scalar))
    return n


def frac(a, x, y, ao=""):
    n = E(unreal.MaterialExpressionFrac, x, y)
    link(a, ao, n, "")
    return n


def floor(a, x, y, ao=""):
    n = E(unreal.MaterialExpressionFloor, x, y)
    link(a, ao, n, "")
    return n


def lerp(a, b, alpha, x, y, ao="", bo="", aao=""):
    n = E(unreal.MaterialExpressionLinearInterpolate, x, y)
    link(a, ao, n, "A")
    link(b, bo, n, "B")
    link(alpha, aao, n, "Alpha")
    return n


def smooth(mn, mx, val, x, y, vo=""):
    n = E(unreal.MaterialExpressionSmoothStep, x, y)
    n.set_editor_property("const_min", float(mn))
    n.set_editor_property("const_max", float(mx))
    link(val, vo, n, "Value")
    return n


def vmin(a, b, x, y, ao="", bo=""):
    return binop(unreal.MaterialExpressionMin, a, b, x, y, ao, bo)


def maskc(a, chr_, chg, x, y, ao=""):
    n = E(unreal.MaterialExpressionComponentMask, x, y)
    n.set_editor_property("r", chr_)
    n.set_editor_property("g", chg)
    n.set_editor_property("b", False)
    n.set_editor_property("a", False)
    link(a, ao, n, "")
    return n


# ---- Weltposition (Top-Down-Plattenraster) --------------------------------
wp = E(unreal.MaterialExpressionWorldPosition, -2200, 0)
X = maskc(wp, True, False, -2000, -80)
Y = maskc(wp, False, True, -2000, 80)

u = divc(X, SLAB_CM, -1800, -80)
v = divc(Y, SLAB_CM, -1800, 80)
cu = frac(u, -1640, -80)
cv = frac(v, -1640, 80)

# Plattennummer -> Tonschwankung je Platte.
idU = floor(u, -1640, 220)
idV = floor(v, -1640, 340)
hid0 = add(mul(idU, C1(0.131, -1500, 240), -1360, 220),
           mul(idV, C1(0.379, -1500, 360), -1360, 340), -1200, 280)
hid = frac(mul(hid0, C1(7.53, -1200, 400), -1060, 280), -920, 280)
tone = add(C1(0.86, -920, 400), mul(hid, C1(0.28, -800, 420), -660, 360), -520, 320)

# Beton: mittleres Grau mit leichtem Warmton, Ton je Platte.
light = C3(0.360, 0.352, 0.336, -900, -220)
dark_slab = C3(0.250, 0.244, 0.232, -900, -80)
slab = lerp(dark_slab, light, tone, -640, -160)

# Fugen: schmale dunkle Linien an den Plattenraendern (beide Achsen).
guA = smooth(0.0, JOINT, cu, -900, 560)
guB = smooth(0.0, JOINT, sub(C1(1.0, -1040, 660), cu, -940, 620), -760, 620)
gu = vmin(guA, guB, -620, 580)
gvA = smooth(0.0, JOINT, cv, -900, 800)
gvB = smooth(0.0, JOINT, sub(C1(1.0, -1040, 900), cv, -940, 860), -760, 860)
gv = vmin(gvA, gvB, -620, 820)
inSlab = mul(gu, gv, -480, 700)          # 1 in der Platte, 0 in der Fuge

joint_col = C3(0.150, 0.146, 0.140, -640, 700)
base = lerp(joint_col, slab, inSlab, -300, 200)
MEL.connect_material_property(base, "", MP.MP_BASE_COLOR)

# MATT: Rauheit hoch, in der Fuge noch etwas hoeher. Kein Metallic, kein Glanz.
rough = lerp(C1(0.95, -300, 560), C1(0.90, -300, 640), inSlab, -140, 600)
MEL.connect_material_property(rough, "", MP.MP_ROUGHNESS)
MEL.connect_material_property(C1(0.0, -140, 760), "", MP.MP_METALLIC)
MEL.connect_material_property(C1(0.25, -140, 860), "", MP.MP_SPECULAR)

MEL.recompile_material(mat)
EAL.save_loaded_asset(mat)
log("FERTIG: M_WbSidewalk - matter Beton-Plattenbelag, Platte %g cm." % SLAB_CM)

if unreal.SystemLibrary.get_command_line().find("-unattended") >= 0:
    unreal.SystemLibrary.quit_editor()
