"""M_WbBuildingRoof: aus einer eingefaerbten Platte wird eine Dachflaeche.

WARUM - gemessen am Vorher-Bild (Saved/Diagnose/luft_wohn2.png):

Das Dach war eine FLACHE FARBE. Die Variation Schiefer/Ziegel je Region lief
zwar, aber jede Flaeche blieb ein einziger Farbton ohne Struktur - aus der
Luft wie aus der Strasse ein "eingefaerbter Box-Deckel". Genau das nennt der
Auftrag als Fehler.

WAS SICH AENDERT:

Eine PROZEDURALE Ziegel-/Schiefer-Struktur - Kachelreihen im Laeuferverband,
je Kachel eine leichte Verwitterung, dunklere Fugen und ein Verlauf je Kachel
(Pfannen-Schatten). Das liest sich aus jeder Entfernung als gedecktes Dach.

Fuenf Deckungen, je Gebaeude aus der Dach-Vertexfarbe (R) gelesen:
  * Terrakotta-Pfanne (warmes Rot)      - das Wohnhaus-Dach
  * Schiefer (blaugrau)                 - Gruenderzeit, Civic, Uni
  * Zink/Blech (grau, leicht metall)    - Anbauten, Moderne, Flachdach
  * Kupfergruen/Patina (verdigris)      - Kirche/Wahrzeichen mit Kuppel/Turmhelm
  * dunkler Schiefer (fast schwarz)     - Kirche/Wahrzeichen sonst
Aeltere Bakes (R=255 = Legacy) fallen auf die alte 14-m-Regionswuerfelung
(Terrakotta/Schiefer/Zink) zurueck und regredieren nicht.

KEINE TEXTUR: Das Muster ist reine Shader-Mathematik. Damit gibt es die
">2 Textur-Samples fallen auf Schiefer zurueck"-Falle nicht mehr (der Grund,
aus dem die alte Fassung ganz ohne Normal-/Roughness-Map auskommen musste),
und es kommt kein einziges Dreieck und kein Neubau der Stadt hinzu.

PROJEKTION: weiterhin Weltraum-Top-Down (WorldPosition.xy). Die gebackenen
Dach-Mesh-UVs sind gebaeude-lokal planar - fuer ein Ziegelmuster gleichwertig,
und die Weltprojektion haelt die Kachelphase ueber aneinanderstossende
Dachflaechen konsistent. Ein symmetrisches Kachelraster (nicht scharf
gerichtete Reihen) bleibt lesbar, egal wie der First zur Weltachse steht.

IN-PLACE: das bestehende Asset wird nur umgeschrieben, nicht neu angelegt -
so bleibt das Nanite-Nutzungsflag erhalten (sonst zeigt das Nanite-Mesh das
graue Default-Material, siehe Tools/fix_bus_materials.py). Zur Sicherheit wird
es zusaetzlich gesetzt.

Aufruf:
  UnrealEditor-Cmd.exe WiesbadenReal.uproject
    -ExecCmds="py exec(open('Tools/create_roof_material.py').read())"
    -unattended -nosplash -nop4 -nullrhi
"""

import unreal

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
MP = unreal.MaterialProperty
PATH = "/Game/Materials/City/M_WbBuildingRoof"

# Kachelmasse (cm). Pfanne ~26 x 40 cm.
TILE_W = 26.0
TILE_H = 40.0
CELL_CM = 1400.0   # Region je Deckungsart


def log(m):
    unreal.log("###ROOF### %s" % m)


mat = EAL.load_asset(PATH)
if mat is None:
    raise SystemExit("FEHLER: %s nicht gefunden" % PATH)

# In-place leeren, Flag setzen.
MEL.delete_all_material_expressions(mat)
try:
    mat.set_editor_property("used_with_nanite", True)
except Exception as exc:
    log("WARN: Nanite-Flag nicht setzbar (%s)" % exc)


# ---- Knoten-Helfer --------------------------------------------------------
def E(cls, x, y):
    return MEL.create_material_expression(mat, cls, x, y)


def C1(v, x, y):
    n = E(unreal.MaterialExpressionConstant, x, y)
    n.set_editor_property("r", float(v))
    return n


def C2(r, g, x, y):
    n = E(unreal.MaterialExpressionConstant2Vector, x, y)
    n.set_editor_property("r", float(r))
    n.set_editor_property("g", float(g))
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


def dot(a, b, x, y, ao="", bo=""):
    return binop(unreal.MaterialExpressionDotProduct, a, b, x, y, ao, bo)


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


def vmax(a, b, x, y, ao="", bo=""):
    return binop(unreal.MaterialExpressionMax, a, b, x, y, ao, bo)


def absn(a, x, y, ao=""):
    n = E(unreal.MaterialExpressionAbs, x, y)
    link(a, ao, n, "")
    return n


def clamp01(a, x, y, ao=""):
    # Ueber Min/Max (A/B-Pins) statt Clamp-Knoten - dessen Eingangspin heisst
    # "Input", der Link-Helfer trifft ihn mit "" nicht zuverlaessig.
    lo = vmax(a, C1(0.0, x - 150, y + 40), x - 20, y, ao=ao)
    return vmin(lo, C1(1.0, x - 150, y + 120), x + 120, y)


def mask2(a, ch_r, ch_g, x, y, ao=""):
    n = E(unreal.MaterialExpressionComponentMask, x, y)
    n.set_editor_property("r", ch_r)
    n.set_editor_property("g", ch_g)
    n.set_editor_property("b", False)
    n.set_editor_property("a", False)
    link(a, ao, n, "")
    return n


# ---- Weltposition ---------------------------------------------------------
wp = E(unreal.MaterialExpressionWorldPosition, -2400, -200)
xy = mask2(wp, True, True, -2200, -200)          # (cm,cm)
X = mask2(wp, True, False, -2200, -60)           # x
Y = mask2(wp, False, True, -2200, 60)            # y  (Mask g -> r-Kanal)

# ---- Region-Hash: Deckungsart je 14-m-Zelle -------------------------------
cellv = divc(xy, CELL_CM, -2000, -320)
cell = floor(cellv, -1850, -320)
hkA = C2(0.137, 0.373, -1850, -180)
dA = dot(cell, hkA, -1700, -320)
hA = frac(dA, -1560, -320)
mSlate = floor(add(hA, C1(0.5, -1560, -220), -1420, -320), -1300, -320)   # ~50 %

hkB = C2(0.271, 0.529, -1850, 480)
dB0 = dot(cell, hkB, -1700, 380)
dB = mul(dB0, C1(1.7, -1700, 500), -1560, 380)
hB = frac(dB, -1420, 380)
mZinc = floor(add(hB, C1(0.22, -1420, 480), -1300, 380), -1180, 380)      # ~22 %

# ---- Deckung je GEBAEUDE aus der Vertexfarbe (R) ---------------------------
#
# BuildRoof schreibt die Deckung in R (0/51/102/153/204 = Terrakotta / Schiefer /
# Zink / Kupfergruen / dunkler Schiefer; Schritt 51 = 255/5, fuenf Deckungen auf
# ganzen Stufen). 255 (Weiss) bedeutet "Legacy" -> aeltere Bakes (Dach-Verts
# weiss) fallen auf die Regionswahl (mSlate/mZinc) zurueck und regredieren nicht.
# Kirchen/Wahrzeichen tragen dabei Kupfergruen (Kuppel/Turmhelm) bzw. dunklen
# Schiefer - eine markante Deckung, die sie aus der Dachlandschaft heraushebt.
vc = E(unreal.MaterialExpressionVertexColor, -2000, 700)
vcR = mask2(vc, True, False, -1850, 700)   # nur R-Kanal
bLegacy = floor(add(vcR, C1(0.1, -1780, 1300), -1640, 1280), -1500, 1280)  # 1 wenn R>=0.9
vcIdx = floor(add(mul(vcR, C1(5.0, -1780, 820), -1640, 760),
                  C1(0.5, -1780, 900), -1500, 760), -1360, 760)   # 0..4 (Legacy: 5)

# ---- Per-Gebaeude-Tonvariation aus der Vertexfarbe (G) ---------------------
#
# BuildRoof legt in G eine deterministische Tonstufe je Gebaeude-Id (0..255,
# UBuildingGenerator::RoofToneByte). Sie verschiebt die Deckungsfarbe leicht
# (+-TONE_AMP), damit eine Reihe gleichtypiger Haeuser nicht identisch wirkt;
# die DeckungsART (R) bleibt unberuehrt. G=0.5 (Byte 128) ist neutral (Faktor
# 1.0). Nur im NICHT-Legacy-Pfad wirksam: Legacy-Bakes tragen G=255, dort
# liefert lerp mit bLegacy den neutralen Faktor 1.0 zurueck - so bleiben
# aeltere Bakes unveraendert.
TONE_AMP = 0.09
vcG = mask2(vc, False, True, -1850, 1440)   # nur G-Kanal (-> r-Ausgang)
tone = add(C1(1.0 - TONE_AMP, -1700, 1520),
           mul(vcG, C1(2.0 * TONE_AMP, -1700, 1620), -1540, 1460),
           -1380, 1480)                       # 1-AMP .. 1+AMP, Mitte 1.0
toneApplied = lerp(tone, C1(1.0, -1380, 1620), bLegacy, -1200, 1500)

# ---- Deckungsfarben + Materialwerte je Deckung ----------------------------
terra = C3(0.52, 0.205, 0.115, -1150, -560)
slate = C3(0.150, 0.165, 0.200, -1150, -480)
zinc = C3(0.335, 0.350, 0.360, -1150, -400)
copper = C3(0.190, 0.450, 0.390, -1150, -320)   # Kupfer-Patina (verdigris)
dslate = C3(0.075, 0.085, 0.105, -1150, -240)   # dunkler Schiefer


# Auswahl je Index: sel_i = clamp01(1 - |vcIdx - i|) ist 1 GENAU bei Index i,
# sonst 0. Die gewichtete Summe waehlt so ohne Verzweigung eine Deckung. Ohne
# das clamp01 wuerden entfernte Indizes NEGATIV beitragen (Farbe abziehen).
def sel(i, y):
    d = absn(sub(vcIdx, C1(float(i), -1060, y + 30), -900, y), -760, y)
    return clamp01(sub(C1(1.0, -1060, y - 30), d, -620, y), -470, y)


s0 = sel(0, -560)
s1 = sel(1, -470)
s2 = sel(2, -380)
s3 = sel(3, -290)
s4 = sel(4, -200)


def wsum(pairs, x0, y0):
    acc = mul(pairs[0][0], pairs[0][1], x0, y0)
    for k, (val, s) in enumerate(pairs[1:], 1):
        acc = add(acc, mul(val, s, x0, y0 + 70 * k), x0 + 180, y0 + 70 * k)
    return acc


vcCol = wsum([(terra, s0), (slate, s1), (zinc, s2), (copper, s3), (dslate, s4)],
             -260, -560)
vcRough = wsum([(C1(0.82, -430, 40), s0), (C1(0.60, -430, 110), s1),
                (C1(0.45, -430, 180), s2), (C1(0.55, -430, 250), s3),
                (C1(0.62, -430, 320), s4)], -260, 40)
vcMetal = add(mul(C1(0.40, -430, 470), s2, -260, 470),
              mul(C1(0.10, -430, 550), s3, -260, 550), -80, 510)

# Legacy: Regionswahl wie bisher (Terrakotta/Schiefer/Zink je 14-m-Zelle).
legColS = lerp(terra, slate, mSlate, -260, -160)
legCol = lerp(legColS, zinc, mZinc, -100, -140)
legRoughTS = lerp(C1(0.82, -430, 640), C1(0.60, -430, 710), mSlate, -260, 660)
legRough = lerp(legRoughTS, C1(0.45, -430, 780), mZinc, -80, 700)
legMetal = mul(mZinc, C1(0.40, -430, 860), -100, 840)

# Nicht-Legacy -> Vertexfarb-Deckung je Gebaeude, Legacy -> Region.
baseCol0 = lerp(vcCol, legCol, bLegacy, 120, -300)
baseCol = mul(baseCol0, toneApplied, 300, -320)   # Ton je Gebaeude
baseRough = lerp(vcRough, legRough, bLegacy, 120, 60)
metal = lerp(vcMetal, legMetal, bLegacy, 120, 500)

# ---- Prozedurales Kachelmuster --------------------------------------------
u = divc(X, TILE_W, -2000, -60)
v = divc(Y, TILE_H, -2000, 60)
row = floor(v, -1850, 120)

# Laeuferverband: jede zweite Reihe eine halbe Kachel versetzt.
rowHalf = frac(mul(row, C1(0.5, -1700, 200), -1560, 160), -1420, 160)
uu = add(u, rowHalf, -1280, 60)

cu = frac(uu, -1140, 40)
cv = frac(v, -1140, 160)

# Kachel-ID -> Verwitterung.
idU = floor(uu, -1140, 260)
idV = row
hidU = mul(idU, C1(0.113, -1000, 300), -860, 280)
hidV = mul(idV, C1(0.417, -1000, 420), -860, 400)
hid0 = add(hidU, hidV, -720, 340)
hid = frac(mul(hid0, C1(8.13, -720, 460), -600, 340), -470, 340)
weather = add(C1(0.80, -470, 460), mul(hid, C1(0.34, -340, 460), -220, 400), -100, 360)

# Fugen: dunkel an den Kachelraendern.
euL = smooth(0.0, 0.09, cu, -900, 560)
euR = smooth(0.0, 0.09, sub(C1(1.0, -1050, 660), cu, -940, 640), -780, 640)
eu = vmin(euL, euR, -640, 600)
evL = smooth(0.0, 0.10, cv, -900, 800)
evR = smooth(0.0, 0.10, sub(C1(1.0, -1050, 900), cv, -940, 880), -780, 880)
ev = vmin(evL, evR, -640, 840)
edge = mul(eu, ev, -500, 700)
groove = add(C1(0.5, -500, 820), mul(edge, C1(0.5, -360, 820), -220, 760), -100, 720)

# Pfannen-Verlauf: unten dunkler (Ueberdeckungsschatten), zur Reihenmitte heller.
curve = add(C1(0.85, -500, 980), mul(cv, C1(0.22, -360, 980), -220, 940), -100, 900)

shade0 = mul(weather, groove, 40, 400)
shade = mul(shade0, curve, 200, 460)

# ---- Ausgaenge ------------------------------------------------------------
finalCol = mul(baseCol, shade, 420, -200)
MEL.connect_material_property(finalCol, "", MP.MP_BASE_COLOR)

# Rauheit: Fugen etwas rauer.
roughAdj = sub(C1(1.15, 200, 700), mul(ev, C1(0.20, 60, 760), 200, 760), 340, 700)
finalRough = mul(baseRough, roughAdj, 480, 600)
MEL.connect_material_property(finalRough, "", MP.MP_ROUGHNESS)

MEL.connect_material_property(metal, "", MP.MP_METALLIC)

MEL.recompile_material(mat)
EAL.save_loaded_asset(mat)
log("FERTIG: M_WbBuildingRoof - 5 Deckungen je Gebaeude (Terrakotta/Schiefer/"
    "Zink/Kupfergruen/dunkler Schiefer, VC.R), Kachel %gx%g cm, Legacy-Region "
    "%g cm, Ton je Gebaeude +-%g%% (VC.G)."
    % (TILE_W, TILE_H, CELL_CM, TONE_AMP * 100.0))

if unreal.SystemLibrary.get_command_line().find("-unattended") >= 0:
    unreal.SystemLibrary.quit_editor()
