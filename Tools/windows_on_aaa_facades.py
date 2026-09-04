# Ruestet die AAA-Fassaden-Materialien (Runtime-Build) mit dem prozeduralen
# Fenster-/Etagen-/Gesims-/Sockelraster nach - genau wie M_WbBuildingWall
# (build_materials.make_wall). Bisher waren M_AAA_Facade* schlichtes PBR
# (Farbe/Normal/Rough) OHNE Fenster; der ungebackene Laufzeit-Build (Wand =
# M_AAA_FacadePlaster) zeigte deshalb glatte Waende ohne Detailtiefe.
#
# Jede Wand: AAA-Farbtextur -> add_facade_windows (Fenster aus den Fassaden-UVs,
# U in Metern / V in Geschossen) -> Sockel abdunkeln -> MP_BASE_COLOR. Rauheit:
# AAA-_R fuer die Wand, an den Fenstern auf Glas (0.10) gezogen. Normal: AAA-_N
# mit x2.2 verstaerkten Tangenten (wie boost_facade_normal auf M_WbBuildingWall)
# -> plastisches Relief bei flachem Licht.
#
# Eigenstaendig (build_materials.py ruft am Ende main() -> nicht importierbar;
# build_relief_facades.py baut beim Import -> Helfer hier kopiert). Baut NUR die
# vier M_AAA_-Fassaden; alles andere bleibt unberuehrt.
import unreal

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
MP = unreal.MaterialProperty
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()

MAT_DIR = "/Game/Materials/AAA"
AAA_TEX = "/Game/Materials/AAA/Textures"
NORMAL_STRENGTH = 2.2      # wie M_WbBuildingWall (boost_facade_normal)

COL = unreal.MaterialSamplerType.SAMPLERTYPE_COLOR
NRM = unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL
LIN = unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE


# -- Helfer (portiert aus build_materials.py / build_relief_facades.py) ------
def expr(mat, cls, x, y):
    return MEL.create_material_expression(mat, cls, x, y)

def conn(a, ao, b, bi):
    MEL.connect_material_expressions(a, ao, b, bi)

def c1(mat, v, x, y):
    n = expr(mat, unreal.MaterialExpressionConstant, x, y); n.set_editor_property("r", v); return n

def c3(mat, r, g, b, x, y):
    n = expr(mat, unreal.MaterialExpressionConstant3Vector, x, y)
    n.set_editor_property("constant", unreal.LinearColor(r, g, b, 1.0)); return n

def binop(mat, cls, a, b, x, y, a_out="", b_out=""):
    n = expr(mat, cls, x, y)
    if isinstance(a, (int, float)):
        n.set_editor_property("const_a", float(a))
    else:
        conn(a, a_out, n, "A")
    if isinstance(b, (int, float)):
        n.set_editor_property("const_b", float(b))
    else:
        conn(b, b_out, n, "B")
    return n

def mul(mat, a, b, x, y, a_out="", b_out=""):
    return binop(mat, unreal.MaterialExpressionMultiply, a, b, x, y, a_out, b_out)

def sub(mat, a, b, x, y, a_out="", b_out=""):
    return binop(mat, unreal.MaterialExpressionSubtract, a, b, x, y, a_out, b_out)

def div(mat, a, b, x, y, a_out="", b_out=""):
    return binop(mat, unreal.MaterialExpressionDivide, a, b, x, y, a_out, b_out)

def sat(mat, a, x, y, a_out=""):
    n = expr(mat, unreal.MaterialExpressionSaturate, x, y); conn(a, a_out, n, ""); return n

def frac(mat, a, x, y, a_out=""):
    n = expr(mat, unreal.MaterialExpressionFrac, x, y); conn(a, a_out, n, ""); return n

def channel(mat, src, ch, x, y, src_out=""):
    n = expr(mat, unreal.MaterialExpressionComponentMask, x, y)
    for c in "rgba":
        n.set_editor_property(c, c == ch.lower())
    conn(src, src_out, n, ""); return n

def band(mat, value, low, high, x, y, sharp=40.0):
    lower = sat(mat, mul(mat, sub(mat, value, low, x, y), sharp, x + 150, y), x + 300, y)
    upper = sat(mat, mul(mat, sub(mat, high, value, x, y + 150), sharp, x + 150, y + 150), x + 300, y + 150)
    return mul(mat, lower, upper, x + 450, y + 75)

def lerp(mat, a, b, alpha, x, y, a_out="", b_out="", alpha_out=""):
    n = expr(mat, unreal.MaterialExpressionLinearInterpolate, x, y)
    conn(a, a_out, n, "A"); conn(b, b_out, n, "B"); conn(alpha, alpha_out, n, "Alpha")
    return n

def tex_coord(mat, x, y, u_tile, v_tile):
    n = expr(mat, unreal.MaterialExpressionTextureCoordinate, x, y)
    n.set_editor_property("u_tiling", u_tile); n.set_editor_property("v_tiling", v_tile)
    return n

def tex_sample(mat, tex, x, y, uv, sampler):
    n = expr(mat, unreal.MaterialExpressionTextureSample, x, y)
    n.set_editor_property("texture", tex)
    n.set_editor_property("sampler_type", sampler)
    conn(uv, "", n, "UVs"); return n

def socket_mask(mat, vc, x, y):
    steep = expr(mat, unreal.MaterialExpressionMultiply, x, y)
    steep.set_editor_property("const_b", 6.0)
    conn(vc, "R", steep, "A")
    clamped = expr(mat, unreal.MaterialExpressionSaturate, x + 120, y)
    conn(steep, "", clamped, ""); return clamped


# -- Fenster/Gesims/Erdgeschoss (identisch zu build_materials.add_facade_windows,
#    zusaetzlich wird die Fenstermaske zurueckgegeben, damit die Wand-Rauheit
#    aus der AAA-_R-Textur an den Fenstern auf Glas gezogen werden kann) -------
def add_facade_windows(mat, wall_color, wall_out=""):
    uv = expr(mat, unreal.MaterialExpressionTextureCoordinate, -2300, 900)
    u = channel(mat, uv, "r", -2100, 800)
    v = channel(mat, uv, "g", -2100, 1000)
    bay = frac(mat, div(mat, u, 2.6, -1900, 800), -1750, 800)
    floor_pos = frac(mat, v, -1750, 1000)
    window = mul(mat,
                 band(mat, bay, 0.30, 0.74, -1550, 700),
                 band(mat, floor_pos, 0.34, 0.82, -1550, 1050),
                 -900, 880)
    cornice = band(mat, floor_pos, 0.90, 0.99, -1550, 1400)
    glass = c3(mat, 0.020, 0.028, 0.038, -700, 500)
    cornice_color = c3(mat, 0.045, 0.042, 0.038, -700, 1400)
    with_windows = lerp(mat, wall_color, glass, window, -450, 700, a_out=wall_out)
    base = lerp(mat, with_windows, cornice_color, cornice, -250, 900)
    rough = lerp(mat, c1(mat, 0.88, -700, 1700), c1(mat, 0.10, -700, 1800), window, -450, 1750)
    metal = mul(mat, window, 0.75, -450, 1950)
    return base, rough, metal, window


def boosted_normal(mat, aaa_n, uv, x, y):
    """AAA-Normalmap mit x2.2 verstaerkten Tangenten an MP_NORMAL (wie
    boost_facade_normal auf M_WbBuildingWall)."""
    ns = tex_sample(mat, aaa_n, x, y, uv, NRM)
    # ComponentMask(R,G) * Staerke:
    mrg = expr(mat, unreal.MaterialExpressionComponentMask, x + 250, y - 60)
    mrg.set_editor_property("r", True); mrg.set_editor_property("g", True)
    mrg.set_editor_property("b", False); mrg.set_editor_property("a", False)
    conn(ns, "RGB", mrg, "")
    k = c1(mat, NORMAL_STRENGTH, x + 250, y + 60)
    scaled = mul(mat, mrg, k, x + 430, y - 20)
    mb = expr(mat, unreal.MaterialExpressionComponentMask, x + 250, y + 140)
    mb.set_editor_property("r", False); mb.set_editor_property("g", False)
    mb.set_editor_property("b", True); mb.set_editor_property("a", False)
    conn(ns, "RGB", mb, "")
    app = expr(mat, unreal.MaterialExpressionAppendVector, x + 620, y + 40)
    conn(scaled, "", app, "A")
    conn(mb, "", app, "B")
    return app


def new_material(name):
    path = "%s/%s" % (MAT_DIR, name)
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    return TOOLS.create_asset(name, MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())


def load(name):
    p = "%s/%s" % (AAA_TEX, name)
    return EAL.load_asset(p) if EAL.does_asset_exist(p) else None


def build(name, tex_base, u_tile, v_tile, socket):
    col_t = load(tex_base + "_C")
    nrm_t = load(tex_base + "_N")
    rough_t = load(tex_base + "_R")
    if not (col_t and nrm_t):
        unreal.log_warning("AAAWIN %s: Textur %s fehlt." % (name, tex_base)); return False

    mat = new_material(name)
    vc = expr(mat, unreal.MaterialExpressionVertexColor, -1200, 400)

    uv = tex_coord(mat, -2900, -200, u_tile, v_tile)
    col = tex_sample(mat, col_t, -2600, -200, uv, COL)

    wall, rough_win, metal, window = add_facade_windows(mat, col, wall_out="RGB")

    # Sockel abdunkeln (wie make_wall).
    sock = c3(mat, socket[0], socket[1], socket[2], -600, 250)
    base = lerp(mat, sock, wall, socket_mask(mat, vc, -400, 400), -100, 0)
    MEL.connect_material_property(base, "", MP.MP_BASE_COLOR)

    # Rauheit: AAA-_R fuer die Wand, an den Fenstern auf Glas (0.10). Ohne _R:
    # der konstante Fenster-Graph-Wert.
    if rough_t is not None:
        wall_rough = tex_sample(mat, rough_t, -2600, 1600, uv, LIN)
        rough = lerp(mat, wall_rough, c1(mat, 0.10, -700, 1850), window,
                     -250, 1750, a_out="R")
        MEL.connect_material_property(rough, "", MP.MP_ROUGHNESS)
    else:
        MEL.connect_material_property(rough_win, "", MP.MP_ROUGHNESS)
    MEL.connect_material_property(metal, "", MP.MP_METALLIC)

    # Normal: AAA-_N verstaerkt.
    nrm = boosted_normal(mat, nrm_t, uv, -2600, 2300)
    MEL.connect_material_property(nrm, "", MP.MP_NORMAL)

    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    unreal.log("AAAWIN %s: AAA-%s + Fenster-Raster + Normal x%.1f (Kachel u=%.2f v=%.2f)."
               % (name, tex_base, NORMAL_STRENGTH, u_tile, v_tile))
    return True


# Plaster wie M_WbBuildingWall (0.5/0.5); Brick/Stone isotrop (V~3xU wegen
# Geschoss-V); Concrete mittig.
build("M_AAA_FacadePlaster",  "FacadePlaster",  0.5, 0.5, (0.190, 0.172, 0.150))
build("M_AAA_FacadeBrick",    "FacadeBrick",    1.0, 3.0, (0.150, 0.100, 0.085))
build("M_AAA_FacadeStone",    "FacadeStone",    1.0, 3.0, (0.200, 0.175, 0.140))
build("M_AAA_FacadeConcrete", "FacadeConcrete", 0.5, 1.5, (0.160, 0.158, 0.150))
unreal.log("AAAWIN: fertig.")
