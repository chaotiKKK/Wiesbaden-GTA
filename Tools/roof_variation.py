# Baut M_WbBuildingRoof neu: Daecher variieren je WELTPOSITIONS-REGION zwischen
# Schiefer (RoofClay) und terrakotta Ziegel (RoofTile) - so wird die Skyline nicht
# mehr uniform. M_WbBuildingRoof ist laut Dependency-Dump das von den GEBACKENEN
# Chunks der Default-Karte real referenzierte Dachmaterial.
#
# Drei Fallen, die hier zusammenkamen (alle per Screenshot-Zerlegung bewiesen):
#  1) Die gebackenen Dach-Mesh-Abschnitte haben KEINE brauchbaren UVs -> jeder
#     Mesh-UV-basierte TextureSample kollabiert zu Grau. Fix: WELTRAUM-UVs
#     (worldXY / TileCm) projizieren.
#  2) sel braucht Werte in {0,1}. MaterialExpressionRound schneidet frac in [0,1)
#     auf 0 ab -> konstant Schiefer. Fix: sel = floor(h + 0.5).
#  3) Mit 6 Textur-Samples (Farbe+Normal+Roughness je Satz) fiel das Material auf
#     die A-Seite zurueck (alles Schiefer). Fix: NUR die zwei Farb-Samples; Normal
#     ist auf flachen Daechern ohnehin vernachlaessigbar, Roughness als Konstante.
#
# Auswahl (magnitude-robust; Wiesbaden-GIS projiziert auf SEHR grosse
# Weltkoordinaten -> der klassische frac(sin(dot*K))-Hash verliert dort in float32
# die Praezision. Hier bleiben ALLE frac-Operanden O(100) -> exakt):
#   cell = floor(worldXY / CellCm)
#   h0   = frac(dot(cell, (0.137, 0.373)))
#   h1   = frac(h0 * (h0 + 34.12) * 13.37)
#   sel  = floor(h1 + 0.5)          # 0 (Schiefer) / 1 (Ziegel)
import os
import unreal

mel = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()

TEX = "/Game/Materials/AAA/Textures"
MAT = "/Game/Materials/City/M_WbBuildingRoof"
SRC = r"C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Saved\_aaa_source\RoofingTiles007"
PREFIX = "RoofingTiles007_2K-JPG"
TILE_CM = 300.0   # Textur-Wiederholung alle 3 m (Weltraum-UV) -> sichtbare Ziegel
CELL_CM = 1400.0  # ~14 m Region je Dachtyp (Gebaeude-Skala, sichtbarer Mix)
ROOF_ROUGHNESS = 0.75  # matte Dachflaeche (ersetzt die abgeklemmten Roughness-Maps)


def import_tex(mapname, name):
    abspath = os.path.join(SRC, "%s_%s.jpg" % (PREFIX, mapname))
    if not os.path.exists(abspath):
        unreal.log_warning("ROOFVAR: Quelle fehlt: %s" % abspath); return None
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", abspath)
    task.set_editor_property("destination_path", TEX)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)
    tools.import_asset_tasks([task])
    return unreal.load_asset(TEX + "/" + name)


# 1) Ziegel-Farbtextur importieren (Schiefer bereits vorhanden).
tile_c = import_tex("Color", "RoofTile_C")
clay_c = unreal.load_asset(TEX + "/RoofClay_C")

mat = unreal.load_asset(MAT)
if not (mat and tile_c and clay_c):
    unreal.log_warning("ROOFVAR: Asset(s) fehlen - Abbruch."); raise SystemExit

mel.delete_all_material_expressions(mat)


def node(cls, x, y):
    return mel.create_material_expression(mat, cls, x, y)


# -- Gemeinsame Weltposition (XY) -------------------------------------------
wp = node(unreal.MaterialExpressionWorldPosition, -1700, 300)
xy = node(unreal.MaterialExpressionComponentMask, -1520, 300)
xy.set_editor_property("r", True); xy.set_editor_property("g", True)
xy.set_editor_property("b", False); xy.set_editor_property("a", False)
mel.connect_material_expressions(wp, "", xy, "")


# -- Weltraum-UVs (worldXY / TileCm) je Farb-Sample -------------------------
def uv_node(y):
    tc = node(unreal.MaterialExpressionConstant, -1520, y + 120); tc.set_editor_property("r", TILE_CM)
    u = node(unreal.MaterialExpressionDivide, -1340, y)
    mel.connect_material_expressions(xy, "", u, "A")
    mel.connect_material_expressions(tc, "", u, "B")
    return u


def sample(tex, y, uv):
    s = node(unreal.MaterialExpressionTextureSample, -1050, y)
    s.set_editor_property("texture", tex)
    s.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    mel.connect_material_expressions(uv, "", s, "UVs")
    return s


clayC = sample(clay_c, -500, uv_node(-500))    # Schiefer
tileClr = sample(tile_c, -260, uv_node(-260))  # terrakotta Ziegel

# -- Auswahl aus Weltposition: pro Region 0 (Schiefer) oder 1 (Ziegel) ------
cell = node(unreal.MaterialExpressionConstant, -1520, 820); cell.set_editor_property("r", CELL_CM)
div = node(unreal.MaterialExpressionDivide, -1340, 700)
mel.connect_material_expressions(xy, "", div, "A")
mel.connect_material_expressions(cell, "", div, "B")
flr = node(unreal.MaterialExpressionFloor, -1180, 700)   # cell = floor(worldXY/CellCm)
mel.connect_material_expressions(div, "", flr, "")

# h0 = frac(dot(cell, (0.137, 0.373)))
hv = node(unreal.MaterialExpressionConstant2Vector, -1180, 830)
hv.set_editor_property("r", 0.137); hv.set_editor_property("g", 0.373)
dot = node(unreal.MaterialExpressionDotProduct, -1020, 700)
mel.connect_material_expressions(flr, "", dot, "A")
mel.connect_material_expressions(hv, "", dot, "B")
h0 = node(unreal.MaterialExpressionFrac, -880, 700)
mel.connect_material_expressions(dot, "", h0, "")

# h1 = frac(h0 * (h0 + 34.12) * 13.37)
addb = node(unreal.MaterialExpressionAdd, -740, 780)
cb = node(unreal.MaterialExpressionConstant, -880, 860); cb.set_editor_property("r", 34.12)
mel.connect_material_expressions(h0, "", addb, "A")
mel.connect_material_expressions(cb, "", addb, "B")
mul1 = node(unreal.MaterialExpressionMultiply, -600, 700)
mel.connect_material_expressions(h0, "", mul1, "A")
mel.connect_material_expressions(addb, "", mul1, "B")
cc = node(unreal.MaterialExpressionConstant, -600, 840); cc.set_editor_property("r", 13.37)
mul2 = node(unreal.MaterialExpressionMultiply, -480, 700)
mel.connect_material_expressions(mul1, "", mul2, "A")
mel.connect_material_expressions(cc, "", mul2, "B")
frac = node(unreal.MaterialExpressionFrac, -360, 700)
mel.connect_material_expressions(mul2, "", frac, "")

# sel = floor(h1 + 0.5)  -> zuverlaessig 0/1
half = node(unreal.MaterialExpressionConstant, -360, 840); half.set_editor_property("r", 0.5)
selAdd = node(unreal.MaterialExpressionAdd, -240, 700)
mel.connect_material_expressions(frac, "", selAdd, "A")
mel.connect_material_expressions(half, "", selAdd, "B")
sel = node(unreal.MaterialExpressionFloor, -120, 700)
mel.connect_material_expressions(selAdd, "", sel, "")

# -- Farbe: lerp(Schiefer, Ziegel, sel) -------------------------------------
colLerp = node(unreal.MaterialExpressionLinearInterpolate, 220, -350)
mel.connect_material_expressions(clayC, "RGB", colLerp, "A")
mel.connect_material_expressions(tileClr, "RGB", colLerp, "B")
mel.connect_material_expressions(sel, "", colLerp, "Alpha")
mel.connect_material_property(colLerp, "", unreal.MaterialProperty.MP_BASE_COLOR)

# -- Roughness als Konstante (flaches Dach, Normal-/Roughness-Maps weggelassen,
#    weil >2 Textur-Samples das Material auf die A-Seite zuruckfallen liessen) --
rough = node(unreal.MaterialExpressionConstant, 220, 20); rough.set_editor_property("r", ROOF_ROUGHNESS)
mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)

mel.recompile_material(mat)
EAL.save_loaded_asset(mat)
unreal.log("ROOFVAR: M_WbBuildingRoof variiert Schiefer/Ziegel je %.0f-cm-Region "
           "(Weltraum-UV %.0f cm, sel=floor+0.5, farbbasiert)." % (CELL_CM, TILE_CM))
