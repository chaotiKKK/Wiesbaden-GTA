# Baut M_WbFacade_Backstein und M_WbFacade_Sandstein NEU: statt der rein
# prozeduralen Farbe (dark/light-Lerp, kein Relief) nutzen sie jetzt die
# AAA-Backstein-/Sandstein-Fototexturen (Farbe + NORMAL) fuer echtes
# Oberflaechenrelief - der prozedurale Fenster-/Etagen-/Gesims-/Sockel-Raster
# bleibt identisch (add_facade_windows aus build_materials.py hier repliziert).
#
# Eigenstaendig, weil build_materials.py am Modulende ungeschuetzt main() ruft
# (ein Import wuerde ALLE Materialien neu bauen und meine Anpassungen ueberschreiben).
# Baut NUR die zwei Backstein/Sandstein-Materialien; alle uebrigen bleiben unberuehrt.
import unreal

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
MP = unreal.MaterialProperty
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()

MAT_DIR = "/Game/Materials/City"
AAA_TEX = "/Game/Materials/AAA/Textures"

# -- Helfer (portiert aus build_materials.py) -------------------------------
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
    # A darf ein Float sein (z.B. band: sub(high, value)) -> const_a; sonst verdrahten.
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


# -- Fenster/Gesims/Erdgeschoss (identisch zu build_materials.add_facade_windows)
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
    return base, rough, metal


def new_material(name):
    path = "%s/%s" % (MAT_DIR, name)
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    return TOOLS.create_asset(name, MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())


COL = unreal.MaterialSamplerType.SAMPLERTYPE_COLOR
NRM = unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL


def build(name, aaa_base, u_tile, v_tile, socket):
    col_t = EAL.load_asset("%s/%s_C" % (AAA_TEX, aaa_base))
    nrm_t = EAL.load_asset("%s/%s_N" % (AAA_TEX, aaa_base))
    if not (col_t and nrm_t):
        unreal.log_warning("RELIEF %s: AAA-Textur %s fehlt." % (name, aaa_base)); return False

    mat = new_material(name)
    vc = expr(mat, unreal.MaterialExpressionVertexColor, -1200, 400)

    # AAA-Textur (Farbe + Normal) mit eigener Kachelung (v ~3x wegen Geschoss-V).
    uv = tex_coord(mat, -1500, -300, u_tile, v_tile)
    col = tex_sample(mat, col_t, -1200, -300, uv, COL)
    nrm = tex_sample(mat, nrm_t, -1200, 150, uv, NRM)

    # Fenster/Gesims/Erdgeschoss auf die AAA-Farbe legen (Raster unveraendert).
    wall_ww, rough, metal = add_facade_windows(mat, col, wall_out="RGB")

    # Sockel abdunkeln (wie make_facade).
    sock = c3(mat, socket[0], socket[1], socket[2], -1100, 300)
    base = lerp(mat, sock, wall_ww, socket_mask(mat, vc, -900, 500), -150, 0)

    MEL.connect_material_property(base, "", MP.MP_BASE_COLOR)
    MEL.connect_material_property(rough, "", MP.MP_ROUGHNESS)
    MEL.connect_material_property(metal, "", MP.MP_METALLIC)
    MEL.connect_material_property(nrm, "RGB", MP.MP_NORMAL)   # <-- echtes Relief

    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    unreal.log("RELIEF %s: AAA-%s (Farbe+Normal) + Fenster-Raster, Kachel u=%.1f v=%.1f." %
               (name, aaa_base, u_tile, v_tile))
    return True


build("M_WbFacade_Backstein", "FacadeBrick", 1.0, 3.0, (0.150, 0.100, 0.085))
build("M_WbFacade_Sandstein", "FacadeStone", 1.0, 3.0, (0.200, 0.175, 0.140))
unreal.log("RELIEF: fertig.")
