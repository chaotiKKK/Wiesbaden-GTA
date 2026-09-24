"""Durchscheinende FX-Materialien fuer den Heli: Rotor-Blur und Downwash-Staub.

Beide haben einen Skalar-Parameter 'Opacity', den der Heli zur Laufzeit ueber
eine MaterialInstanceDynamic aus Drehzahl bzw. Bodennaehe/Collective steuert.

Der Rotor-Blur ist KEINE flache graue Scheibe mehr (der "haessliche Kreis"),
sondern eine bewegungsunscharfe Rotorscheibe:
  * radial durchsichtig an der Nabe, dichter zur Blattspitze,
  * ein heller Spitzenring (die Blattspitzen fangen das Licht),
  * weiche, mitdrehende Geister-Blaetter (drei Keulen) - im Stand als Blaetter
    erkennbar, im Lauf zur Unschaerfe verschmiert.
Radius kommt aus Weltabstand/ObjektRadius (drehungsunabhaengig, robust); der
Blatt-Winkel aus dem in den OBJEKTRAUM gedrehten Ortsvektor (dreht mit der
Nabe, unabhaengig vom UV-Layout des Zylinders).
"""
import math
import unreal
MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
MP = unreal.MaterialProperty
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
DIR = "/Game/Materials/City"
TWO_PI = 2.0 * math.pi

def log(m): unreal.log("###HELIFX### %s" % m)

def expr(mat, cls, x, y):
    return MEL.create_material_expression(mat, cls, x, y)

def constant(mat, v, x, y):
    n = expr(mat, unreal.MaterialExpressionConstant, x, y)
    n.set_editor_property("r", v)
    return n

def mul(mat, a, ao, b, bo, x, y):
    n = expr(mat, unreal.MaterialExpressionMultiply, x, y)
    MEL.connect_material_expressions(a, ao, n, "A")
    MEL.connect_material_expressions(b, bo, n, "B")
    return n

def mul_c(mat, a, ao, c, x, y):
    """a * Konstante (ConstB am Multiply-Knoten)."""
    n = expr(mat, unreal.MaterialExpressionMultiply, x, y)
    MEL.connect_material_expressions(a, ao, n, "A")
    n.set_editor_property("const_b", c)
    return n

def add(mat, a, ao, b, bo, x, y):
    n = expr(mat, unreal.MaterialExpressionAdd, x, y)
    MEL.connect_material_expressions(a, ao, n, "A")
    MEL.connect_material_expressions(b, bo, n, "B")
    return n

def add_c(mat, a, ao, c, x, y):
    n = expr(mat, unreal.MaterialExpressionAdd, x, y)
    MEL.connect_material_expressions(a, ao, n, "A")
    n.set_editor_property("const_b", c)
    return n

def make_downwash():
    name = "M_WbDownwash"
    p = "%s/%s" % (DIR, name)
    if EAL.does_asset_exist(p):
        EAL.delete_asset(p)
    m = TOOLS.create_asset(name, DIR, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property("two_sided", True)

    col = expr(m, unreal.MaterialExpressionConstant3Vector, -600, 0)
    col.set_editor_property("constant", unreal.LinearColor(0.58, 0.52, 0.44, 1.0))
    MEL.connect_material_property(col, "", MP.MP_BASE_COLOR)

    rough = constant(m, 0.9, -600, 250)
    MEL.connect_material_property(rough, "", MP.MP_ROUGHNESS)

    op = expr(m, unreal.MaterialExpressionScalarParameter, -600, 450)
    op.set_editor_property("parameter_name", "Opacity")
    op.set_editor_property("default_value", 0.0)

    nz = expr(m, unreal.MaterialExpressionNoise, -600, 650)
    nz.set_editor_property("scale", 0.35)
    nz.set_editor_property("output_min", 0.35)
    nz.set_editor_property("output_max", 1.0)
    grain = mul(m, op, "", nz, "", -300, 500)
    MEL.connect_material_property(grain, "", MP.MP_OPACITY)

    MEL.recompile_material(m)
    EAL.save_loaded_asset(m)
    log(p)

def make_rotor_blur():
    name = "M_WbRotorBlur"
    p = "%s/%s" % (DIR, name)
    if EAL.does_asset_exist(p):
        EAL.delete_asset(p)
    m = TOOLS.create_asset(name, DIR, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("two_sided", True)

    # -- Radialkoordinate R = Weltabstand(Pixel, Objektmitte) / ObjektRadius ----
    wpos = expr(m, unreal.MaterialExpressionWorldPosition, -1700, -300)
    opos = expr(m, unreal.MaterialExpressionObjectPositionWS, -1700, -120)
    orad = expr(m, unreal.MaterialExpressionObjectRadius, -1700, 60)

    dist = expr(m, unreal.MaterialExpressionDistance, -1450, -220)
    MEL.connect_material_expressions(wpos, "", dist, "A")
    MEL.connect_material_expressions(opos, "", dist, "B")

    rdiv = expr(m, unreal.MaterialExpressionDivide, -1250, -160)
    MEL.connect_material_expressions(dist, "", rdiv, "A")
    MEL.connect_material_expressions(orad, "", rdiv, "B")
    R = expr(m, unreal.MaterialExpressionClamp, -1080, -160)  # R in 0..1
    MEL.connect_material_expressions(rdiv, "", R, "")
    R.set_editor_property("min_default", 0.0)
    R.set_editor_property("max_default", 1.0)

    # -- Nabenloch: unter ~15 % Radius nichts (dort sitzt die feste Nabe) -------
    hub = expr(m, unreal.MaterialExpressionSmoothStep, -880, -360)
    hub.set_editor_property("const_min", 0.05)
    hub.set_editor_property("const_max", 0.15)
    MEL.connect_material_expressions(R, "", hub, "Value")

    # -- Aussenkante rund ausblenden (das Netz ist etwas GROESSER als der echte
    # Blattkreis, s. Code): Opazitaet faellt zwischen R 0,86..0,95 auf 0, bevor die
    # facettierte Zylinderkante (R~1) erreicht ist -> saubere Kreisscheibe. -------
    edgess = expr(m, unreal.MaterialExpressionSmoothStep, -880, -220)
    edgess.set_editor_property("const_min", 0.86)
    edgess.set_editor_property("const_max", 0.95)
    MEL.connect_material_expressions(R, "", edgess, "Value")
    edge = expr(m, unreal.MaterialExpressionOneMinus, -700, -220)
    MEL.connect_material_expressions(edgess, "", edge, "")

    # -- Spitzenring: breite, helle Keule um R ~ 0,84 (die Spitzenspur) ---------
    # Der dominante Teil der Scheibe: eine echte Rotorscheibe ist innen fast durch-
    # sichtig, das helle Band an den Blattspitzen traegt das Bild.
    # ring = (1 - |R-0.84|/0.09)^2, geklemmt
    ringoff = add_c(m, R, "", -0.84, -880, -80)
    ringabs = expr(m, unreal.MaterialExpressionAbs, -720, -80)
    MEL.connect_material_expressions(ringoff, "", ringabs, "")
    ringn = mul_c(m, ringabs, "", 11.111, -560, -80)         # |dR|/0.09
    ringinv = expr(m, unreal.MaterialExpressionOneMinus, -420, -80)
    MEL.connect_material_expressions(ringn, "", ringinv, "")
    ringcl = expr(m, unreal.MaterialExpressionClamp, -280, -80)
    MEL.connect_material_expressions(ringinv, "", ringcl, "")
    ringcl.set_editor_property("min_default", 0.0)
    ringcl.set_editor_property("max_default", 1.0)
    ring = mul(m, ringcl, "", ringcl, "", -120, -80)         # ^2 -> weiche Kante

    # -- Radialkoerper: 0,12 Grund + 0,26*R (Innen fast durchsichtig) + 1,15*Ring
    bodyR = mul_c(m, R, "", 0.26, -560, 60)
    bodybase = add_c(m, bodyR, "", 0.12, -400, 60)
    ringw = mul_c(m, ring, "", 1.15, -120, 40)
    radsum = add(m, bodybase, "", ringw, "", 40, 40)
    radhub = mul(m, radsum, "", hub, "", 220, -20)           # * Nabenloch
    radial = mul(m, radhub, "", edge, "", 380, -60)          # * Aussenkante

    # -- Winkel des in den Objektraum gedrehten Ortsvektors ---------------------
    dvec = expr(m, unreal.MaterialExpressionSubtract, -1450, 260)
    MEL.connect_material_expressions(wpos, "", dvec, "A")
    MEL.connect_material_expressions(opos, "", dvec, "B")
    local = expr(m, unreal.MaterialExpressionTransform, -1250, 260)
    local.set_editor_property("transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_WORLD)
    local.set_editor_property("transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_LOCAL)
    MEL.connect_material_expressions(dvec, "", local, "")
    lx = expr(m, unreal.MaterialExpressionComponentMask, -1080, 220)
    lx.set_editor_property("r", True); lx.set_editor_property("g", False)
    lx.set_editor_property("b", False); lx.set_editor_property("a", False)
    MEL.connect_material_expressions(local, "", lx, "")
    ly = expr(m, unreal.MaterialExpressionComponentMask, -1080, 340)
    ly.set_editor_property("r", False); ly.set_editor_property("g", True)
    ly.set_editor_property("b", False); ly.set_editor_property("a", False)
    MEL.connect_material_expressions(local, "", ly, "")
    ang = expr(m, unreal.MaterialExpressionArctangent2, -900, 280)  # atan2(Y, X)
    MEL.connect_material_expressions(ly, "", ang, "Y")
    MEL.connect_material_expressions(lx, "", ang, "X")

    # -- Drei weiche Geister-Blaetter: 0,60 + 0,40 * (0.5+0.5*cos(3*ang))^2 ------
    ang3 = mul_c(m, ang, "", 3.0, -720, 280)
    cos3 = expr(m, unreal.MaterialExpressionCosine, -560, 280)
    cos3.set_editor_property("period", TWO_PI)   # cos(2pi*X/period) = cos(X)
    MEL.connect_material_expressions(ang3, "", cos3, "")
    lobe = add_c(m, mul_c(m, cos3, "", 0.5, -420, 280), "", 0.5, -280, 280)  # 0..1
    lobe2 = mul(m, lobe, "", lobe, "", -140, 280)                            # schaerfer
    ghost = add_c(m, mul_c(m, lobe2, "", 0.55, 20, 280), "", 0.45, 180, 280) # 0.45..1.0

    # -- Opazitaet = radial * ghost * Opacity-Parameter, geklemmt ---------------
    op = expr(m, unreal.MaterialExpressionScalarParameter, 220, 460)
    op.set_editor_property("parameter_name", "Opacity")
    op.set_editor_property("default_value", 0.0)
    rg = mul(m, radial, "", ghost, "", 400, 120)
    opac = mul(m, rg, "", op, "", 560, 200)
    opcl = expr(m, unreal.MaterialExpressionClamp, 720, 200)
    MEL.connect_material_expressions(opac, "", opcl, "")
    opcl.set_editor_property("min_default", 0.0)
    opcl.set_editor_property("max_default", 1.0)
    MEL.connect_material_property(opcl, "", MP.MP_OPACITY)

    # -- Farbe (Emissive, unlit): dunkles Metallgrau, am Spitzenring hell -------
    dark = expr(m, unreal.MaterialExpressionConstant3Vector, 400, -300)
    dark.set_editor_property("constant", unreal.LinearColor(0.06, 0.06, 0.07, 1.0))
    tip = expr(m, unreal.MaterialExpressionConstant3Vector, 400, -160)
    tip.set_editor_property("constant", unreal.LinearColor(0.80, 0.82, 0.88, 1.0))
    emis = expr(m, unreal.MaterialExpressionLinearInterpolate, 620, -220)
    MEL.connect_material_expressions(dark, "", emis, "A")
    MEL.connect_material_expressions(tip, "", emis, "B")
    MEL.connect_material_expressions(ring, "", emis, "Alpha")
    # Blattstreifen fangen Licht: die Geister-Keulen heben das Emissive leicht an
    # (helle, mitdrehende Streifen = die Bewegungsunschaerfe der Blaetter).
    bladeglow = mul_c(m, lobe2, "", 0.16, 620, -60)
    emis2 = add(m, emis, "", bladeglow, "", 800, -180)
    MEL.connect_material_property(emis2, "", MP.MP_EMISSIVE_COLOR)

    MEL.recompile_material(m)
    EAL.save_loaded_asset(m)
    log(p)

make_rotor_blur()
make_downwash()
log("FERTIG")
if unreal.SystemLibrary.get_command_line().find("-unattended") >= 0:
    unreal.SystemLibrary.quit_editor()
