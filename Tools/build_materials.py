"""
Erzeugt die Stadt-Materialien und stellt die Beleuchtung der Map her.

Hintergrund: Die Material-Properties des WorldBuilders waren alle nullptr,
darum wurden Strassen, Gehwege, Gebaeude und Gelaende mit dem Default-
Material (Schachbrett) gerendert. Zusaetzlich fehlte im Level jede
DirectionalLight, weshalb WeatherFX nichts ansteuern konnte.
"""
import unreal
import os

MAT_DIR = "/Game/Materials/City"
TEX_DIR = "/Game/Textures/Facades"

# Fassaden global aufhellen. Die Wiesbaden-Referenz ist hell (Putz/Sandstein),
# aber die Fototexturen rendern in den fast immer verschatteten Strassenschlucht-
# Fassaden gegen Schwarz - kein Licht-/Belichtungshebel erreichte die statischen
# Chunk-Flaechen. Ein Faktor auf die Basisfarbe hebt genau diese Flaechen; 1.6
# haelt selbst helle Fassaden (~0.45) unter 1.0, dunkelt nichts aus.
FACADE_BRIGHTNESS = 1.6
# Kartenpfad zentral.
#
# Die Karte heisst seit dem Neubau vom 31.08. WiesbadenCity_Alkis3; die alte wurde
# entfernt. Werkzeuge, die noch auf sie zeigten, luden ins Leere UND
# meldeten es nicht - build_materials.py schrieb daraufhin
# "Landscape-Material neu verknuepft: 0 Actor(en)" statt 1.
#
# Ueber die Umgebungsvariable WB_MAP umstellbar, damit der naechste
# Kartenwechsel nicht wieder vier Dateien anfassen muss.
MAP = os.environ.get("WB_MAP", "/Game/Maps/WiesbadenCity_Alkis3")

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
MP = unreal.MaterialProperty

SRC = os.path.join(unreal.Paths.project_content_dir(), "Textures", "Facades")


def log(msg):
    unreal.log("###WB### %s" % msg)


# ---------------------------------------------------------------- Texturen
def import_noise_texture():
    """Importiert die kachelbare Rauschtextur (Tools/make_noise_texture.py).

    Sie ersetzt MaterialExpressionNoise. Gemessen kostete das berechnete
    Rauschen rund die Haelfte der Bildzeit:

        normale Materialien:  146 ms Bildzeit, Renderer 142-146 ms
        einfarbige:            70-99 ms,       Renderer     8,8 ms

    Dreioktaviges Simplex-Rauschen wird JE BILDPUNKT ausgewertet; bei
    1280 x 720 sind das ueber 900.000 Auswertungen im Bild, und das Gelaende
    fuellt fast den ganzen Schirm. Eine Texturabtastung kostet einen Bruchteil
    davon und sieht gleich aus.
    """
    dest = "%s/T_WbNoise" % TEX_DIR
    # Bewusst OHNE Fruehausstieg: Wird die PNG neu erzeugt, soll der naechste
    # Materialbau sie auch uebernehmen.
    src = os.path.join(unreal.Paths.project_content_dir(),
                       "Assets", "Source", "T_WbNoise.png")
    if not os.path.exists(src):
        log("Rauschtextur fehlt: %s - Materialien rechnen weiter mit Noise." % src)
        return None

    t = unreal.AssetImportTask()
    t.filename = src
    t.destination_path = TEX_DIR
    t.destination_name = "T_WbNoise"
    t.automated = True
    t.replace_existing = True
    t.save = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])

    if not EAL.does_asset_exist(dest):
        log("Rauschtextur konnte nicht importiert werden.")
        return None

    tex = EAL.load_asset(dest)
    # Graustufen ohne Farbraum-Korrektur: Das ist ein Zahlenfeld, kein Bild.
    tex.set_editor_property("srgb", False)
    tex.set_editor_property("compression_settings",
                            unreal.TextureCompressionSettings.TC_GRAYSCALE)
    EAL.save_loaded_asset(tex)
    return tex


def import_facade_textures():
    """Importiert die drei Putz-JPGs als Texturen (bisher nur Quelldateien)."""
    specs = [
        ("PutzFassade_Color.jpg", "T_Putz_Color", False, False),
        ("PutzFassade_NormalGL.jpg", "T_Putz_Normal", True, False),
        ("PutzFassade_Roughness.jpg", "T_Putz_Roughness", False, True),
    ]
    out = {}
    tasks = []
    for filename, asset_name, is_normal, is_gray in specs:
        dest = "%s/%s" % (TEX_DIR, asset_name)
        if EAL.does_asset_exist(dest):
            out[asset_name] = EAL.load_asset(dest)
            continue
        src = os.path.join(SRC, filename)
        if not os.path.exists(src):
            log("Quelltextur fehlt: %s" % src)
            continue
        t = unreal.AssetImportTask()
        t.filename = src
        t.destination_path = TEX_DIR
        t.destination_name = asset_name
        t.automated = True
        t.replace_existing = True
        t.save = True
        tasks.append((t, asset_name, is_normal, is_gray))

    if tasks:
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(
            [t for t, _, _, _ in tasks])
        for t, asset_name, is_normal, is_gray in tasks:
            tex = EAL.load_asset("%s/%s" % (TEX_DIR, asset_name))
            if tex is None:
                log("Import fehlgeschlagen: %s" % asset_name)
                continue
            # Kompression passend setzen, sonst wird die Normalmap als
            # Farbtextur behandelt und die Rauheit sRGB-verzerrt.
            if is_normal:
                tex.set_editor_property("compression_settings",
                                        unreal.TextureCompressionSettings.TC_NORMALMAP)
                tex.set_editor_property("srgb", False)
            elif is_gray:
                tex.set_editor_property("compression_settings",
                                        unreal.TextureCompressionSettings.TC_GRAYSCALE)
                tex.set_editor_property("srgb", False)
            EAL.save_asset("%s/%s" % (TEX_DIR, asset_name))
            out[asset_name] = tex
    return out


# ------------------------------------------------------------ Hilfsroutinen
def new_material(name):
    path = "%s/%s" % (MAT_DIR, name)
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
    return mat


def expr(mat, cls, x, y):
    return MEL.create_material_expression(mat, cls, x, y)


def c3(mat, r, g, b, x, y):
    n = expr(mat, unreal.MaterialExpressionConstant3Vector, x, y)
    n.set_editor_property("constant", unreal.LinearColor(r, g, b, 1.0))
    return n


def c1(mat, v, x, y):
    n = expr(mat, unreal.MaterialExpressionConstant, x, y)
    n.set_editor_property("r", v)
    return n


# Kachelweite der Rauschtextur in Zentimetern bei scale = 1.0.
#
# MaterialExpressionNoise multipliziert die Weltposition mit `scale` und hat
# im skalierten Raum Strukturen der Groesse 1. Bei Weltmass in Zentimetern
# waeren das bei scale = 8.0 also 1/8 Zentimeter - unsichtbar fein. Die
# Kommentare an den Aufrufstellen meinen ersichtlich etwas anderes ("Korn,
# ~12 cm" bei scale 8.0). Diese Absicht wird hier umgesetzt: 400/scale
# Zentimeter Kachelweite, und da die groebste Oktave ein Viertel der Kachel
# fuellt, ergibt scale 8.0 genau die gemeinten 12,5 cm.
NOISE_TILE_CM_AT_SCALE_1 = 400.0

# Die Weltposition wird vorher auf einen Kilometer zurueckgefaltet. Ohne das
# erreichen die Texturkoordinaten am Stadtrand sechsstellige Werte, und die
# 32-Bit-Gleitkommazahlen im Pixelschritt haben dort nicht mehr genug Stellen
# fuer die Nachkommastellen - das Rauschen wuerde zu Streifen zerfallen.
# Sichtbar ist die Faltung nicht: Die Textur ist kachelbar, und ein Kilometer
# ist bei jedem benutzten Massstab ein ganzzahliges Vielfaches der Kachel.
NOISE_WRAP_CM = 100000.0

NOISE_TEXTURE = None


def noise(mat, x, y, scale, levels=3):
    """Weltbezogenes Rauschen - bricht grosse Flaechen optisch auf.

    Abgetastet aus einer kachelbaren Textur statt je Bildpunkt berechnet.
    MaterialExpressionNoise wertete dreioktaviges Simplex-Rauschen fuer JEDEN
    Bildpunkt aus; gemessen kostete das rund die Haelfte der Bildzeit:

        normale Materialien:  146 ms Bildzeit, Renderer 142-146 ms
        einfarbige:            70-99 ms,       Renderer     8,8 ms

    `scale` behaelt seine Richtung - groesser heisst feiner - und wird ueber
    NOISE_TILE_CM_AT_SCALE_1 in Texturkoordinaten umgerechnet.

    `levels` wird nicht mehr gebraucht: Die vier Oktaven stecken in der
    Textur. Das Argument bleibt nur stehen, damit die Aufrufstellen
    unveraendert bleiben.
    """
    if NOISE_TEXTURE is None:
        # Rueckfall auf den berechneten Knoten, damit die Materialien auch
        # ohne Textur vollstaendig entstehen - dann eben langsam.
        n = expr(mat, unreal.MaterialExpressionNoise, x, y)
        n.set_editor_property("scale", scale)
        n.set_editor_property("levels", levels)
        n.set_editor_property("output_min", 0.0)
        n.set_editor_property("output_max", 1.0)
        return n

    world = expr(mat, unreal.MaterialExpressionWorldPosition, x - 400, y)

    flat = expr(mat, unreal.MaterialExpressionComponentMask, x - 300, y)
    flat.set_editor_property("r", True)
    flat.set_editor_property("g", True)
    flat.set_editor_property("b", False)
    flat.set_editor_property("a", False)
    MEL.connect_material_expressions(world, "", flat, "")

    wrap = expr(mat, unreal.MaterialExpressionFmod, x - 200, y)
    MEL.connect_material_expressions(flat, "", wrap, "A")
    MEL.connect_material_expressions(
        c1(mat, NOISE_WRAP_CM, x - 200, y + 120), "", wrap, "B")

    uv = expr(mat, unreal.MaterialExpressionMultiply, x - 100, y)
    uv.set_editor_property("const_b", scale / NOISE_TILE_CM_AT_SCALE_1)
    MEL.connect_material_expressions(wrap, "", uv, "A")

    sample = expr(mat, unreal.MaterialExpressionTextureSample, x, y)
    sample.set_editor_property("texture", NOISE_TEXTURE)
    # Graustufen ohne Farbraum-Korrektur - sonst weigert sich der Uebersetzer,
    # weil Abtasttyp und Texturkompression nicht zusammenpassen.
    sample.set_editor_property(
        "sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE)
    MEL.connect_material_expressions(uv, "", sample, "UVs")

    # Auf einen einzelnen Kanal zusammenziehen: Die Aufrufstellen geben das
    # Ergebnis als Mischwert weiter und erwarten dort eine Zahl, keinen Vektor.
    out = expr(mat, unreal.MaterialExpressionComponentMask, x + 120, y)
    out.set_editor_property("r", True)
    out.set_editor_property("g", False)
    out.set_editor_property("b", False)
    out.set_editor_property("a", False)
    MEL.connect_material_expressions(sample, "", out, "")

    return out


def lerp(mat, a, b, alpha, x, y, a_out="", b_out="", alpha_out=""):
    n = expr(mat, unreal.MaterialExpressionLinearInterpolate, x, y)
    MEL.connect_material_expressions(a, a_out, n, "A")
    MEL.connect_material_expressions(b, b_out, n, "B")
    MEL.connect_material_expressions(alpha, alpha_out, n, "Alpha")
    return n


def tex_sample(mat, texture, x, y, uv=None):
    n = expr(mat, unreal.MaterialExpressionTextureSample, x, y)
    n.set_editor_property("texture", texture)
    if uv is not None:
        MEL.connect_material_expressions(uv, "", n, "UVs")
    return n


def tex_coord(mat, x, y, u_tile, v_tile):
    n = expr(mat, unreal.MaterialExpressionTextureCoordinate, x, y)
    n.set_editor_property("u_tiling", u_tile)
    n.set_editor_property("v_tiling", v_tile)
    return n


def flip_green(mat, normal_sample, x, y):
    """
    Wandelt eine OpenGL-Normalmap (Gruen nach oben) in die von Unreal
    erwartete DirectX-Konvention (Gruen nach unten) um.
    """
    mask_r = expr(mat, unreal.MaterialExpressionComponentMask, x, y - 150)
    mask_r.set_editor_property("r", True)
    mask_r.set_editor_property("g", False)
    mask_r.set_editor_property("b", False)
    MEL.connect_material_expressions(normal_sample, "RGB", mask_r, "")

    mask_g = expr(mat, unreal.MaterialExpressionComponentMask, x, y)
    mask_g.set_editor_property("r", False)
    mask_g.set_editor_property("g", True)
    mask_g.set_editor_property("b", False)
    MEL.connect_material_expressions(normal_sample, "RGB", mask_g, "")

    mask_b = expr(mat, unreal.MaterialExpressionComponentMask, x, y + 150)
    mask_b.set_editor_property("r", False)
    mask_b.set_editor_property("g", False)
    mask_b.set_editor_property("b", True)
    MEL.connect_material_expressions(normal_sample, "RGB", mask_b, "")

    inv = expr(mat, unreal.MaterialExpressionOneMinus, x + 150, y)
    MEL.connect_material_expressions(mask_g, "", inv, "")

    app1 = expr(mat, unreal.MaterialExpressionAppendVector, x + 300, y - 75)
    MEL.connect_material_expressions(mask_r, "", app1, "A")
    MEL.connect_material_expressions(inv, "", app1, "B")

    app2 = expr(mat, unreal.MaterialExpressionAppendVector, x + 450, y)
    MEL.connect_material_expressions(app1, "", app2, "A")
    MEL.connect_material_expressions(mask_b, "", app2, "B")
    return app2


def _binary(mat, cls, a, b, x, y, a_out="", b_out=""):
    n = expr(mat, cls, x, y)
    if isinstance(a, (int, float)):
        n.set_editor_property("const_a", float(a))
    else:
        MEL.connect_material_expressions(a, a_out, n, "A")
    if isinstance(b, (int, float)):
        n.set_editor_property("const_b", float(b))
    else:
        MEL.connect_material_expressions(b, b_out, n, "B")
    return n


def sub(mat, a, b, x, y, a_out="", b_out=""):
    return _binary(mat, unreal.MaterialExpressionSubtract, a, b, x, y, a_out, b_out)


def mul(mat, a, b, x, y, a_out="", b_out=""):
    return _binary(mat, unreal.MaterialExpressionMultiply, a, b, x, y, a_out, b_out)


def div(mat, a, b, x, y, a_out="", b_out=""):
    return _binary(mat, unreal.MaterialExpressionDivide, a, b, x, y, a_out, b_out)


def frac(mat, a, x, y, a_out=""):
    n = expr(mat, unreal.MaterialExpressionFrac, x, y)
    MEL.connect_material_expressions(a, a_out, n, "")
    return n


def sat(mat, a, x, y, a_out=""):
    n = expr(mat, unreal.MaterialExpressionSaturate, x, y)
    MEL.connect_material_expressions(a, a_out, n, "")
    return n


def channel(mat, source, ch, x, y, source_out=""):
    """Einen Kanal herausmaskieren ('R'..'A')."""
    n = expr(mat, unreal.MaterialExpressionComponentMask, x, y)
    for c in "rgba":
        n.set_editor_property(c, c == ch.lower())
    MEL.connect_material_expressions(source, source_out, n, "")
    return n


# Steilheit der Sockelmaske.
#
# Die Wand eines Hauses ist EIN Viereck vom Erdgeschoss bis zum Dach
# (BuildingGenerator::AddQuad), und seine Vertexfarbe R laeuft von 0 unten bis
# 1 oben. Der Generator meint damit "Hoehe ueber Grund" fuer einen
# Schmutzverlauf - das Material las es als Sockelmaske und mischte deshalb die
# dunkle Sockelfarbe (0,150) ueber die GANZE Wandhoehe ein. Bei einem
# viergeschossigen Haus sind das zehn Meter Verlauf ins Dunkle; die Stadt sah
# entsprechend aus.
#
# Faktor 6 macht daraus wieder einen Sockel: Der Uebergang ist nach einem
# Sechstel der Wandhoehe abgeschlossen, bei 13 m Traufhoehe also nach gut
# zwei Metern.
SOCKET_STEEPNESS = 6.0


def socket_mask(mat, vertex_color, x, y):
    """Vertexfarbe R zu einer schmalen Sockelmaske schaerfen."""
    steep = expr(mat, unreal.MaterialExpressionMultiply, x, y)
    steep.set_editor_property("const_b", SOCKET_STEEPNESS)
    MEL.connect_material_expressions(vertex_color, "R", steep, "A")

    clamped = expr(mat, unreal.MaterialExpressionSaturate, x + 120, y)
    MEL.connect_material_expressions(steep, "", clamped, "")
    return clamped


def band(mat, value, low, high, x, y, sharpness=40.0):
    """
    Weiche 0/1-Maske fuer low <= value <= high.

    Bewusst ohne If-Knoten: saturate((v - low) * k) * saturate((high - v) * k)
    ist billiger und liefert eine leicht weiche Kante, die in der Ferne nicht
    flimmert.
    """
    lower = sat(mat, mul(mat, sub(mat, value, low, x, y), sharpness, x + 150, y), x + 300, y)
    upper = sat(mat, mul(mat, sub(mat, high, value, x, y + 150), sharpness, x + 150, y + 150), x + 300, y + 150)
    return mul(mat, lower, upper, x + 450, y + 75)


def finish(mat, landscape=False, ism=False):
    """
    Schliesst ein Material ab.

    Das ISM-Usage-Flag ist PFLICHT, nicht Kosmetik: fehlt
    bUsedWithInstancedStaticMeshes, weigert sich Unreal, das Material fuer
    diesen Vertex-Factory-Typ zu kompilieren, und zeichnet stattdessen
    kommentarlos das Default-Material - also wieder das Schachbrett, obwohl
    die Zuweisung sichtbar korrekt ist.
    """
    # Landscape braucht in UE 5.8 KEIN Usage-Flag mehr - Material.h kennt
    # kein bUsedWithLandscape (nachgeprueft). Der Parameter bleibt als
    # Dokumentation der Absicht erhalten.
    del landscape

    for flag, wanted in (("used_with_instanced_static_meshes", ism),):
        if not wanted:
            continue
        try:
            mat.set_editor_property(flag, True)
        except Exception as e:
            # Manche Usage-Flags sind je nach Version schreibgeschuetzt. Das
            # laut melden statt den Lauf abzubrechen - sonst bleiben die
            # uebrigen Materialien unerzeugt, und die Ursache waere im
            # fertigen Bild wieder nur "Schachbrett".
            unreal.log_error(
                "###WB### Usage-Flag %s an %s NICHT setzbar: %s"
                % (flag, mat.get_name(), e))
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)


# --------------------------------------------------- Echte Stadt-Texturen
#
# Importiert von Tools/import_materials.py nach /Game/Textures/City. Die
# Materialien hier schreiben sich IN PLACE um, statt neue anzulegen: Die
# gebackene Stadt verweist auf M_WbRoad, M_WbBuildingWall und so weiter, und
# diese Pfade bleiben erhalten. Ein Stadt-Neubau von 36 Minuten entfaellt
# damit.
CITY_TEX_DIR = "/Game/Textures/City"


def city_tex(material, channel):
    """Textur aus dem Stadt-Satz; None, wenn sie fehlt.

    Bewusst None statt einer Ersatztextur: Ein Material, dem eine Karte fehlt,
    soll auf seinen bisherigen Weg zurueckfallen und nicht stumm mit einer
    weissen Flaeche weiterrechnen.
    """
    path = "%s/T_%s_%s" % (CITY_TEX_DIR, material, channel)
    return EAL.load_asset(path) if EAL.does_asset_exist(path) else None


def surface_from_texture(mat, material_name, meters_per_tile, x, y,
                         v_per_tile=None):
    """Farbe, Normale und Rauheit einer Stadt-Textur anschliessen.

    Rueckgabe: (Farbe, Rauheit) oder (None, None), wenn die Textur fehlt.

    Die Kachelung wird in METERN angegeben. Fassaden-UVs laufen U in Metern
    und V in Geschossen, Fahrbahn-UVs in Metern - eine Angabe in Metern passt
    damit fuer beide und sagt, was sie bedeutet. Ein nackter UV-Faktor wie
    "0,167" beantwortet die Frage "wie gross ist ein Ziegel" nicht.
    """
    color_tex = city_tex(material_name, "Color")
    if color_tex is None:
        return None, None

    # U und V getrennt.
    #
    # Fassaden-UVs laufen U in METERN entlang der Wand, V in GESCHOSSEN
    # (BuildingGenerator: "U in Metern entlang der Wand, V in Geschossen").
    # Beide Achsen durch dieselbe Meterzahl zu teilen war ein Denkfehler: Auf
    # der V-Achse hiess "durch 6" dann eine Texturkachel ueber SECHS
    # Geschosse, und die Fenster wurden bis zur Unkenntlichkeit gestreckt. Im
    # Bild sah das aus wie ein Karomuster aus Grautoenen.
    #
    # Fahrbahn und Gelaende haben beide Achsen in Metern; dort bleibt
    # v_per_tile leer und beide Werte sind gleich.
    v_tile = v_per_tile if v_per_tile is not None else meters_per_tile
    uv = tex_coord(mat, x - 500, y, 1.0 / meters_per_tile, 1.0 / v_tile)
    color = tex_sample(mat, color_tex, x - 200, y, uv)

    rough_tex = city_tex(material_name, "Roughness")
    if rough_tex is not None:
        rough = tex_sample(mat, rough_tex, x - 200, y + 400, uv)
        rough.set_editor_property(
            "sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE)
    else:
        rough = None

    normal_tex = city_tex(material_name, "Normal")
    if normal_tex is not None:
        normal = tex_sample(mat, normal_tex, x - 200, y + 800, uv)
        normal.set_editor_property(
            "sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
        MEL.connect_material_property(normal, "", MP.MP_NORMAL)

    return color, rough


# ------------------------------------------------------------- Materialien
def make_road():
    """
    Asphalt. Vertexfarbe: R = Verschleiss, G = Griffigkeit, A = Naesse.
    Verschlissene Fahrspuren werden heller und glatter - das zeichnet die
    real erzeugten Spurrillen sichtbar nach.
    """
    mat = new_material("M_WbRoad")
    vc = expr(mat, unreal.MaterialExpressionVertexColor, -900, 0)

    # Echter Asphalt, wenn vorhanden.
    #
    # Die Vertexfarbe bleibt in Gebrauch: R traegt den Verschleiss der
    # Fahrspuren, den die Strassenerzeugung berechnet hat. Sie hellt die
    # Textur auf, statt sie zu ersetzen - eine ausgefahrene Spur ist heller
    # und glatter als frischer Belag, aber sie ist immer noch Asphalt.
    tex_color, tex_rough = surface_from_texture(mat, "Fahrbahn_Asphalt", 4.0, -1100, -300)

    if tex_color is not None:
        worn = c3(mat, 0.075, 0.074, 0.070, -700, 200)
        grain_col = lerp(mat, tex_color, worn, vc, -350, -100, alpha_out="R")
        MEL.connect_material_property(grain_col, "", MP.MP_BASE_COLOR)

        if tex_rough is not None:
            rough_worn = c1(mat, 0.55, -700, 700)
            road_rough = lerp(mat, tex_rough, rough_worn, vc, -350, 600,
                              a_out="R", alpha_out="G")
            MEL.connect_material_property(road_rough, "", MP.MP_ROUGHNESS)
            finish(mat)
            return mat
    else:
        dark = c3(mat, 0.020, 0.020, 0.022, -900, -300)    # frischer Asphalt
        worn = c3(mat, 0.055, 0.054, 0.052, -900, -150)    # ausgefahrene Spur
        base = lerp(mat, dark, worn, vc, -600, -200, alpha_out="R")

        grain = noise(mat, -900, 250, 8.0)                 # Korn, ~12 cm
        grain_col = lerp(mat, base, worn, grain, -350, -100)
        MEL.connect_material_property(grain_col, "", MP.MP_BASE_COLOR)

    # Griffigkeit -> Rauheit: griffig = rau, polierte Spur = glatter.
    rough_hi = c1(mat, 0.88, -900, 450)
    rough_lo = c1(mat, 0.55, -900, 560)
    rough = lerp(mat, rough_lo, rough_hi, vc, -600, 500, alpha_out="G")
    MEL.connect_material_property(rough, "", MP.MP_ROUGHNESS)
    finish(mat)
    return mat


def make_sidewalk():
    """Gehwegplatten: heller Beton mit leichter Plattenstruktur."""
    mat = new_material("M_WbSidewalk")
    # Deutlich dunkler als zuvor (0.16..0.21): so hell wirkte der Gehweg im
    # Luftbild wie frischer Schnee und dominierte das Strassenbild.
    tex_color, tex_rough = surface_from_texture(mat, "Gehweg_Platten", 2.0, -800, -200)
    if tex_color is not None:
        MEL.connect_material_property(tex_color, "", MP.MP_BASE_COLOR)
        MEL.connect_material_property(
            tex_rough if tex_rough is not None else c1(mat, 0.82, -450, 250),
            "R" if tex_rough is not None else "", MP.MP_ROUGHNESS)
    else:
        # Deutlich dunkler als zuvor (0.16..0.21): so hell wirkte der Gehweg
        # im Luftbild wie frischer Schnee und dominierte das Strassenbild.
        a = c3(mat, 0.105, 0.102, 0.095, -800, -200)
        b = c3(mat, 0.145, 0.141, 0.133, -800, -50)
        n = noise(mat, -800, 150, 20.0)
        base = lerp(mat, a, b, n, -450, -100)
        MEL.connect_material_property(base, "", MP.MP_BASE_COLOR)
        MEL.connect_material_property(c1(mat, 0.82, -450, 250), "", MP.MP_ROUGHNESS)
    finish(mat)
    return mat


def make_kerb():
    """Bordstein: heller Granit, deutlich abgesetzt vom Gehweg."""
    mat = new_material("M_WbKerb")
    tex_color, tex_rough = surface_from_texture(mat, "Bordstein_Beton", 2.0, -800, 0)
    if tex_color is not None:
        MEL.connect_material_property(tex_color, "", MP.MP_BASE_COLOR)
        if tex_rough is not None:
            MEL.connect_material_property(tex_rough, "R", MP.MP_ROUGHNESS)
        else:
            MEL.connect_material_property(c1(mat, 0.70, -500, 200), "", MP.MP_ROUGHNESS)
    else:
        MEL.connect_material_property(c3(mat, 0.30, 0.30, 0.29, -500, 0), "",
                                      MP.MP_BASE_COLOR)
        MEL.connect_material_property(c1(mat, 0.70, -500, 200), "", MP.MP_ROUGHNESS)
    finish(mat)
    return mat


def make_photo_facade(name, texture_name, meters_along_wall=8.0,
                      floors_per_tile=2.0):
    """Fassade aus einer fotografierten Gebaeudetextur.

    Diese Texturen bringen die Fenster MIT - anders als die uebrigen
    Varianten, bei denen `add_facade_windows` sie aus der UV-Belegung rechnet.
    Der Shader-Weg war ein Behelf, solange es keine Fassadenbilder gab; ein
    Foto trifft Fensterteilung, Gesimse und Verwitterung genauer, als eine
    Formel es kann.

    Die Kachelung braucht ZWEI Werte, weil die UV-Achsen verschiedene Einheiten
    haben: U laeuft in Metern entlang der Wand, V in Geschossen. Acht Meter
    Wandlaenge und zwei Geschosse Hoehe entsprechen dem Bildausschnitt
    ueblicher Fassadenfotos. Passt es nicht, sieht man es sofort an
    gestauchten oder verzerrten Fenstern.
    """
    mat = new_material(name)
    vc = expr(mat, unreal.MaterialExpressionVertexColor, -1800, 900)

    tex_color, tex_rough = surface_from_texture(mat, texture_name,
                                                meters_along_wall, -1500, -300,
                                                v_per_tile=floors_per_tile)
    if tex_color is None:
        return None

    # Sockel: dieselbe geschaerfte Maske wie bei den uebrigen Fassaden.
    sock = c3(mat, 0.120, 0.115, 0.108, -1100, 500)
    base = lerp(mat, sock, tex_color, socket_mask(mat, vc, -1500, 700), -700, 0)
    bright = mul(mat, base, c1(mat, FACADE_BRIGHTNESS, -500, 250), -300, 0)
    MEL.connect_material_property(bright, "", MP.MP_BASE_COLOR)

    if tex_rough is not None:
        MEL.connect_material_property(tex_rough, "R", MP.MP_ROUGHNESS)
    else:
        MEL.connect_material_property(c1(mat, 0.85, -700, 400), "", MP.MP_ROUGHNESS)

    # Fensterglas spiegelt, Putz nicht - dafuer ist die Metallic-Karte da.
    metal_tex = city_tex(texture_name, "Metallic")
    if metal_tex is not None:
        uv = tex_coord(mat, -1500, 1200, 1.0 / meters_along_wall, 1.0 / floors_per_tile)
        metal = tex_sample(mat, metal_tex, -1100, 1200, uv)
        # MASKS, nicht LINEAR_GRAYSCALE.
        #
        # import_materials.py legt die Metallic-Karten als TC_MASKS an. Passt
        # der Abtasttyp nicht zur Kompression, weigert sich der Uebersetzer -
        # und zwar NICHT sichtbar: das Material faellt auf das Standardmaterial
        # zurueck, die Meldung steht nur im Protokoll:
        #
        #   "Sampler type is Linear Grayscale, should be Masks for
        #    /Game/Textures/City/T_Facade_Wohnhaus_Metallic"
        #
        # Ein einziger falscher Abtasttyp hat so die GANZE Stadt grau gemacht:
        # Wand, Putz, Beton, Sandstein, Backstein - alle sechs Fassaden teilen
        # sich diese Stelle.
        metal.set_editor_property(
            "sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
        MEL.connect_material_property(metal, "R", MP.MP_METALLIC)

    finish(mat)
    return mat


def make_wall(textures, name="M_WbBuildingWall"):
    """
    Putzfassade mit Fenstern. Vertexfarbe R = Sockelmaske (0 am Sockel).

    Die Putztextur liefert die feine Oberflaeche, die Fenster kommen wie bei
    den uebrigen Varianten aus der UV-Belegung (U in Metern, V in Geschossen).
    """
    mat = new_material(name)
    vc = expr(mat, unreal.MaterialExpressionVertexColor, -2600, 400)

    color_tex = textures.get("T_Putz_Color")
    if color_tex is not None:
        # UVs der Fassaden: U in Metern, V in Geschossen. 0.5 laesst das
        # Putzmuster alle 2 m bzw. alle 2 Geschosse wiederkehren.
        uv = tex_coord(mat, -2900, -200, 0.5, 0.5)
        col = tex_sample(mat, color_tex, -2600, -200, uv)
        col_out = "RGB"
    else:
        col = c3(mat, 0.42, 0.385, 0.335, -2600, -200)
        col_out = ""

    wall, rough_expr, metal_expr = add_facade_windows(mat, col, wall_out=col_out)

    socket = c3(mat, 0.190, 0.172, 0.150, -600, 250)
    base = lerp(mat, socket, wall, socket_mask(mat, vc, -400, 400), -100, 0)
    bright = mul(mat, base, c1(mat, FACADE_BRIGHTNESS, -300, 150), -50, 0)
    MEL.connect_material_property(bright, "", MP.MP_BASE_COLOR)
    MEL.connect_material_property(rough_expr, "", MP.MP_ROUGHNESS)
    MEL.connect_material_property(metal_expr, "", MP.MP_METALLIC)

    norm_tex = textures.get("T_Putz_Normal")
    if norm_tex is not None:
        uv3 = tex_coord(mat, -2900, 2300, 0.5, 0.5)
        ns = tex_sample(mat, norm_tex, -2600, 2300, uv3)
        flipped = flip_green(mat, ns, -2200, 2300)
        MEL.connect_material_property(flipped, "", MP.MP_NORMAL)
    finish(mat)
    return mat


def make_roof():
    """Ziegeldach in gedecktem Rot, mit Variation zwischen den Haeusern."""
    mat = new_material("M_WbBuildingRoof")
    a = c3(mat, 0.150, 0.058, 0.040, -800, -200)
    b = c3(mat, 0.215, 0.092, 0.060, -800, -50)
    # Dach-UVs sind weltbezogen in Metern -> Ziegel ueberall gleich gross.
    n = noise(mat, -800, 150, 3.0)
    base = lerp(mat, a, b, n, -450, -100)
    MEL.connect_material_property(base, "", MP.MP_BASE_COLOR)
    MEL.connect_material_property(c1(mat, 0.78, -450, 250), "", MP.MP_ROUGHNESS)
    finish(mat)
    return mat


def make_terrain():
    """
    Gelaende: Wiesen-/Erdmischung, grossflaechig moduliert.

    ALBEDO IST KEINE GESCHMACKSFRAGE. Hier standen 0.035/0.060/0.022 fuer die
    Wiese - eine Reflexion von rund 4 Prozent. Gruenes Gras liegt real bei 15
    bis 25 Prozent, die Werte waren also etwa viermal zu dunkel.

    Sichtbar wurde das nicht als "zu dunkles Gras", sondern am ganzen Bild: Die
    Wiese ist die groesste Flaeche der Stadt. Ist sie zu dunkel, hebt die
    Auto-Belichtung die Szene an, bis das MITTEL stimmt - und nimmt dabei allem
    Helleren den Kontrast. Ziegeldaecher wurden lachsfarben, Gehwege fast weiss,
    ueber der Stadt lag ein milchiger Schleier.

    Dagegen wurde bisher am falschen Ende angegangen: erst mit einer
    Belichtungsklammer, dann mit einem Himmelslicht auf 60 Prozent der
    Sonnenstaerke (real sind es etwa 15). Beides kuriert die Anzeige, nicht die
    Ursache.
    """
    mat = new_material("M_WbTerrain")

    # Echte Wiese, wenn vorhanden.
    #
    # Die Warnung oben gilt weiter und wird durch die Textur nicht
    # gegenstandslos: Das Gelaende ist die groesste Flaeche der Stadt, und
    # seine Helligkeit bestimmt ueber die Auto-Belichtung das GANZE Bild. Eine
    # fotografierte Wiese liegt von sich aus im richtigen Bereich - das ist
    # gerade ihr Vorteil gegenueber geratenen Farbwerten.
    tex_color, tex_rough = surface_from_texture(mat, "Gelaende_Wiese", 3.0, -1200, -250)
    if tex_color is not None:
        MEL.connect_material_property(tex_color, "", MP.MP_BASE_COLOR)
        MEL.connect_material_property(
            tex_rough if tex_rough is not None else c1(mat, 0.92, -250, 350),
            "R" if tex_rough is not None else "", MP.MP_ROUGHNESS)
        finish(mat, landscape=True)
        return mat

    grass = c3(mat, 0.085, 0.135, 0.050, -900, -250)
    dry = c3(mat, 0.150, 0.145, 0.085, -900, -100)
    broad = noise(mat, -900, 100, 0.06)      # grossraeumige Flecken
    base = lerp(mat, grass, dry, broad, -550, -150)

    fine = noise(mat, -900, 400, 1.5)        # feine Struktur
    dark = c3(mat, 0.055, 0.080, 0.035, -900, 550)
    final = lerp(mat, dark, base, fine, -250, 0, b_out="")
    MEL.connect_material_property(final, "", MP.MP_BASE_COLOR)
    MEL.connect_material_property(c1(mat, 0.92, -250, 350), "", MP.MP_ROUGHNESS)
    finish(mat, landscape=True)
    return mat


def add_facade_windows(mat, wall_color, wall_out=""):
    """
    Erzeugt Fenster, Gesimsband und Erdgeschoss rein im Shader.

    Moeglich wird das durch die UV-Belegung des BuildingGenerators:
    U laeuft in METERN entlang der Wand, V in GESCHOSSEN. frac(V) ist damit
    die Position innerhalb eines Geschosses, U geteilt durch den Achsabstand
    die Position innerhalb einer Fensterachse.

    Bewusst als Material und nicht als Geometrie: bei 104.458 Gebaeuden wuerden
    ausmodellierte Fensterlaibungen die Dreieckszahl vervielfachen, waehrend
    hier kein einziges Dreieck hinzukommt - und kein Stadt-Neubau noetig ist.

    Rueckgabe: (BasisFarbe, Rauheit, Metallic) zum Anschliessen.
    """
    uv = expr(mat, unreal.MaterialExpressionTextureCoordinate, -2300, 900)

    u = channel(mat, uv, "r", -2100, 800)
    v = channel(mat, uv, "g", -2100, 1000)

    # Fensterachsen: eine Achse je 2,6 m Wandlaenge.
    bay = frac(mat, div(mat, u, 2.6, -1900, 800), -1750, 800)
    floor_pos = frac(mat, v, -1750, 1000)

    # Fensteroeffnung innerhalb der Achse bzw. des Geschosses.
    window = mul(mat,
                 band(mat, bay, 0.30, 0.74, -1550, 700),
                 band(mat, floor_pos, 0.34, 0.82, -1550, 1050),
                 -900, 880)

    # Gesims: schmales Band am Geschossuebergang, gliedert die Fassade
    # horizontal auch dort, wo keine Fenster sitzen.
    cornice = band(mat, floor_pos, 0.90, 0.99, -1550, 1400)

    glass = c3(mat, 0.020, 0.028, 0.038, -700, 500)
    cornice_color = c3(mat, 0.045, 0.042, 0.038, -700, 1400)

    with_windows = lerp(mat, wall_color, glass, window, -450, 700, a_out=wall_out)
    base = lerp(mat, with_windows, cornice_color, cornice, -250, 900)

    # Glas ist glatt und spiegelnd, Putz rau.
    rough = lerp(mat, c1(mat, 0.88, -700, 1700), c1(mat, 0.10, -700, 1800),
                 window, -450, 1750)
    metal = mul(mat, window, 0.75, -450, 1950)

    return base, rough, metal


def make_facade(name, dark, light, roughness, metallic=0.0,
                noise_scale=2.5, socket=(0.150, 0.142, 0.130), windows=True):
    """
    Baut eine Fassade mit Fenstern, Gesims und abgesetztem Sockel.

    Der Sockel (Vertexfarbe R = 0) wird abgedunkelt, damit die Haeuser unten
    Halt bekommen; das Rauschen bricht die Wandflaechen auf, sonst wirken
    ganze Strassenzuege wie ein einziger Block.
    """
    mat = new_material(name)
    vc = expr(mat, unreal.MaterialExpressionVertexColor, -1200, 400)

    a = c3(mat, dark[0], dark[1], dark[2], -1100, -300)
    b = c3(mat, light[0], light[1], light[2], -1100, -150)
    n = noise(mat, -1100, 50, noise_scale)
    wall = lerp(mat, a, b, n, -750, -200)

    if windows:
        wall, rough_expr, metal_expr = add_facade_windows(mat, wall)
    else:
        rough_expr = c1(mat, roughness, -400, 350)
        metal_expr = c1(mat, metallic, -400, 500) if metallic > 0.0 else None

    sock = c3(mat, socket[0], socket[1], socket[2], -1100, 300)
    base = lerp(mat, sock, wall, socket_mask(mat, vc, -900, 500), -150, 0)
    MEL.connect_material_property(base, "", MP.MP_BASE_COLOR)

    MEL.connect_material_property(rough_expr, "", MP.MP_ROUGHNESS)
    if metal_expr is not None:
        MEL.connect_material_property(metal_expr, "", MP.MP_METALLIC)
    finish(mat)
    return mat


def make_facade_variants(textures):
    """
    Die sechs Bauweisen, die der BuildingGenerator vergibt. Index und
    Reihenfolge muessen zu FBuildingMeshSection::MaterialVariant passen.
    """
    out = {}

    # Fotografierte Fassaden - aber NUR fuer die Bauten, die sie zeigen.
    #
    # Hier standen einmal alle vier Varianten. Das war falsch, und zwar
    # sichtbar falsch: die ambientCG-Reihe "Facade001..020" besteht
    # AUSSCHLIESSLICH aus modernen Hochhaeusern - die Schnittstelle
    # verschlagwortet sie selbst mit "skyscraper", "glass", "reflective". Keine
    # einzige davon ist eine europaeische Putzfassade.
    #
    # Verdrahtet waren sie trotzdem nach ihrem ORDNERNAMEN, und der sagt, wofuer
    # sie gedacht waren, nicht was sie zeigen:
    #
    #   Facade_Wohnhaus  = Glas-Vorhangfassade  -> lag auf PUTZ
    #   Facade_Altbau    = nachts leuchtendes Hochhaus, tags schwarz
    #   Facade_Nachkrieg = Backsteinhochhaus    -> lag auf SANDSTEIN
    #
    # Der BuildingGenerator vergibt die Varianten nach Baujahr: Sandstein ist
    # die Gruenderzeit von 1850 bis 1920, Putz die Jahre 1920 bis 1960. Beides
    # bekam damit eine Skyline aus Spiegelglas.
    #
    # Putz, Backstein und Sandstein gehen deshalb zurueck auf den Shader-Weg
    # weiter unten. Der ist kein Behelf, sondern auf Wiesbaden abgestimmt:
    # Putzflaeche, Fenster aus der UV-Belegung, Gesimsband, Sockel, und die
    # Farbtoene sind an Klinker und Gruenderzeit-Sandstein gemessen.
    #
    # Die Glas-Vorhangfassade bleibt - auf GLAS, wohin sie gehoert. Die
    # bekommt der Generator ab Baujahr 1990 und bei Buerogebaeuden.
    photo = {
        "Glas": ("M_WbFacade_Glas", "Facade_Glasturm"),
        "Beton": ("M_WbFacade_Beton", "Facade_Buerohaus"),
    }

    for key, (material_name, texture_name) in photo.items():
        built = make_photo_facade(material_name, texture_name)
        if built is not None:
            out[key] = built
            log("Fassade %s aus Foto-Textur %s" % (key, texture_name))

    # 0 Putz - bekommt als einzige die echte Putztextur.
    if "Putz" not in out:
        out["Putz"] = make_wall(textures, "M_WbFacade_Putz")

    # 1 Backstein - Wiesbadener Klinker, roetlich-braun.
    if "Backstein" not in out:
        out["Backstein"] = make_facade(
            "M_WbFacade_Backstein", (0.205, 0.085, 0.060), (0.330, 0.150, 0.100),
            roughness=0.88, noise_scale=6.0, socket=(0.085, 0.040, 0.028))

    # 2 Sandstein - heller Gruenderzeit-Ton, praegt die Innenstadt.
    if "Sandstein" not in out:
        out["Sandstein"] = make_facade(
            "M_WbFacade_Sandstein", (0.365, 0.305, 0.220), (0.510, 0.435, 0.315),
            roughness=0.80, noise_scale=3.0, socket=(0.155, 0.128, 0.092))

    # 3 Glas - Buerofassade: metallisch und glatt, damit sie den Himmel
    # spiegelt und sich klar vom Wohnbestand abhebt.
    # Glas bekommt KEINE Fenster aufgemalt - eine Vorhangfassade ist bereits
    # durchgehend verglast.
    if "Glas" not in out:
        out["Glas"] = make_facade(
            "M_WbFacade_Glas", (0.020, 0.032, 0.040), (0.045, 0.070, 0.085),
            roughness=0.08, metallic=0.85, noise_scale=1.0,
            socket=(0.030, 0.035, 0.040), windows=False)

    # 4 Beton - Nachkriegsbau, kuehles Grau.
    if "Beton" not in out:
        out["Beton"] = make_facade(
            "M_WbFacade_Beton", (0.255, 0.238, 0.205), (0.380, 0.352, 0.305),
            roughness=0.85, noise_scale=2.0, socket=(0.125, 0.113, 0.098))

    # 5 Fachwerk - helles Gefach mit dunklem Balkenanteil im Rauschen.
    out["Fachwerk"] = make_facade(
        "M_WbFacade_Fachwerk", (0.115, 0.075, 0.050), (0.520, 0.475, 0.390),
        roughness=0.90, noise_scale=12.0, socket=(0.090, 0.068, 0.048))

    return out



def make_lane_marking():
    """
    Fahrbahnmarkierung. Muss hell und matt sein - laeuft sie ueber das
    Asphaltmaterial, ist sie auf der Fahrbahn schlicht unsichtbar.
    """
    mat = new_material("M_WbLaneMarking")
    MEL.connect_material_property(c3(mat, 0.62, 0.62, 0.60, -500, 0), "",
                                  MP.MP_BASE_COLOR)
    MEL.connect_material_property(c1(mat, 0.55, -500, 200), "", MP.MP_ROUGHNESS)
    finish(mat, ism=True)
    return mat


def make_cycleway():
    """Radweg: der in Deutschland uebliche rote Belag."""
    mat = new_material("M_WbCycleway")
    a = c3(mat, 0.085, 0.030, 0.022, -800, -200)
    b = c3(mat, 0.125, 0.048, 0.034, -800, -50)
    n = noise(mat, -800, 150, 8.0)
    MEL.connect_material_property(lerp(mat, a, b, n, -450, -100), "",
                                  MP.MP_BASE_COLOR)
    MEL.connect_material_property(c1(mat, 0.85, -450, 250), "", MP.MP_ROUGHNESS)
    finish(mat)
    return mat


def make_sign():
    """
    Schildtafel. Die Schild-Textur kommt als Parameter herein
    (SignTextureParameterName am WorldBuilder, Default "SignTexture");
    ResolveSignMaterial legt je Schild eine MaterialInstanceDynamic an.
    """
    mat = new_material("M_WbSign")
    # Beidseitig: Schilder sind Einzelflaechen und waeren von hinten unsichtbar.
    mat.set_editor_property("two_sided", True)

    p = expr(mat, unreal.MaterialExpressionTextureSampleParameter2D, -700, 0)
    p.set_editor_property("parameter_name", "SignTexture")
    default_tex = EAL.load_asset("/Game/Textures/TrafficSigns/Sign_206")
    if default_tex is None:
        # Irgendeine vorhandene Schildtextur als Vorgabe - ohne Textur
        # weigert sich der Sampler zu kompilieren.
        found = EAL.list_assets("/Game/Textures/TrafficSigns", False, False)
        if found:
            default_tex = EAL.load_asset(found[0])
    if default_tex is not None:
        p.set_editor_property("texture", default_tex)

    MEL.connect_material_property(p, "RGB", MP.MP_BASE_COLOR)
    MEL.connect_material_property(c1(mat, 0.35, -700, 400), "", MP.MP_ROUGHNESS)
    finish(mat, ism=True)
    return mat


def make_pole():
    """Schildpfosten: verzinkter Stahl."""
    mat = new_material("M_WbPole")
    MEL.connect_material_property(c3(mat, 0.42, 0.43, 0.44, -500, 0), "",
                                  MP.MP_BASE_COLOR)
    MEL.connect_material_property(c1(mat, 1.0, -500, 180), "", MP.MP_METALLIC)
    MEL.connect_material_property(c1(mat, 0.42, -500, 340), "", MP.MP_ROUGHNESS)
    finish(mat, ism=True)
    return mat


def make_delineator():
    """Leitpfosten: weisser Kunststoff."""
    mat = new_material("M_WbDelineator")
    MEL.connect_material_property(c3(mat, 0.68, 0.68, 0.66, -500, 0), "",
                                  MP.MP_BASE_COLOR)
    MEL.connect_material_property(c1(mat, 0.45, -500, 200), "", MP.MP_ROUGHNESS)
    finish(mat, ism=True)
    return mat


def make_reflector():
    """
    Reflektor am Leitpfosten. Leicht selbstleuchtend, damit er - wie das
    Vorbild im Scheinwerferlicht - auch nachts als Fahrbahnrand ablesbar ist.
    """
    mat = new_material("M_WbReflector")
    MEL.connect_material_property(c3(mat, 0.35, 0.030, 0.020, -600, 0), "",
                                  MP.MP_BASE_COLOR)
    MEL.connect_material_property(c3(mat, 0.55, 0.045, 0.030, -600, 200), "",
                                  MP.MP_EMISSIVE_COLOR)
    MEL.connect_material_property(c1(mat, 0.25, -600, 400), "", MP.MP_ROUGHNESS)
    finish(mat, ism=True)
    return mat


def make_pedestrian():
    """
    Fussgaenger-Platzhalter: gedeckte Kleidungsfarbe, matt.

    Bewusst zurueckhaltend: die Figuren sind skalierte Zylinder, je auffaelliger
    die Farbe, desto staerker faellt der Platzhalter ins Auge.
    """
    mat = new_material("M_WbPedestrian")
    a = c3(mat, 0.075, 0.080, 0.105, -800, -200)
    b = c3(mat, 0.145, 0.130, 0.120, -800, -50)
    # Feines Rauschen: so bekommt nicht jede Figur denselben Farbton.
    n = noise(mat, -800, 150, 30.0)
    MEL.connect_material_property(lerp(mat, a, b, n, -450, -100), "", MP.MP_BASE_COLOR)
    MEL.connect_material_property(c1(mat, 0.85, -450, 250), "", MP.MP_ROUGHNESS)
    finish(mat, ism=True)
    return mat



def make_furniture_materials():
    """Strassenausstattung - 50.859 Schilder und 177.822 Leitpfosten."""
    return {
        "LaneMarking": make_lane_marking(),
        "Cycleway": make_cycleway(),
        "Sign": make_sign(),
        "Pole": make_pole(),
        "Delineator": make_delineator(),
        "Reflector": make_reflector(),
        "Pedestrian": make_pedestrian(),
    }



def make_tree():
    """
    Baumkrone. Platzhalter auf Engine-Kegeln, aber mit Variation:
    Ohne sie stehen 1,53 Millionen exakt gleiche gruene Kegel in der Stadt,
    was schlimmer aussaehe als gar keine Baeume.
    """
    mat = new_material("M_WbTree")

    # Zwei Gruentoene, weltbezogen gemischt - benachbarte Baeume
    # unterscheiden sich dadurch sichtbar.
    dark = c3(mat, 0.020, 0.048, 0.014, -800, -200)
    light = c3(mat, 0.055, 0.095, 0.028, -800, -50)
    n = noise(mat, -800, 150, 0.4)
    MEL.connect_material_property(lerp(mat, dark, light, n, -450, -100), "",
                                  MP.MP_BASE_COLOR)

    MEL.connect_material_property(c1(mat, 0.90, -450, 250), "", MP.MP_ROUGHNESS)
    # Laub ist beidseitig sichtbar; ohne das wirkt der Kegel von innen hohl.
    mat.set_editor_property("two_sided", True)
    finish(mat, ism=True)
    return mat


def make_helicopter():
    """Helikopter-Lackierung. Die Zelle besteht aus Engine-Wuerfeln und trug
    ohne Material das Default-Schachbrett."""
    mat = new_material("M_WbHelicopter")
    MEL.connect_material_property(c3(mat, 0.045, 0.070, 0.050, -500, 0), "",
                                  MP.MP_BASE_COLOR)
    MEL.connect_material_property(c1(mat, 0.35, -500, 200), "", MP.MP_ROUGHNESS)
    MEL.connect_material_property(c1(mat, 0.6, -500, 350), "", MP.MP_METALLIC)
    finish(mat)
    return mat


def make_unpaved():
    """
    Unbefestigter Weg: Schotter, Kies, Erde.

    Ohne eigenes Material bekamen die 10.089 Pfade und 9.267 Feldwege der
    OSM-Daten dasselbe Asphaltmaterial wie eine Hauptstrasse - ein Waldweg sah
    aus wie eine Fahrbahn.
    """
    mat = new_material("M_WbUnpaved")
    a = c3(mat, 0.105, 0.088, 0.062, -800, -200)
    b = c3(mat, 0.155, 0.132, 0.095, -800, -50)
    n = noise(mat, -800, 150, 14.0)
    MEL.connect_material_property(lerp(mat, a, b, n, -450, -100), "",
                                  MP.MP_BASE_COLOR)
    MEL.connect_material_property(c1(mat, 0.95, -450, 250), "", MP.MP_ROUGHNESS)
    finish(mat)
    return mat


def make_paved_stone():
    """Pflaster und Naturstein - Fussgaengerzonen und Altstadtgassen."""
    mat = new_material("M_WbPavedStone")
    a = c3(mat, 0.085, 0.080, 0.076, -800, -200)
    b = c3(mat, 0.135, 0.128, 0.120, -800, -50)
    # Feines Rauschen zeichnet die einzelnen Steine nach.
    n = noise(mat, -800, 150, 26.0)
    MEL.connect_material_property(lerp(mat, a, b, n, -450, -100), "",
                                  MP.MP_BASE_COLOR)
    MEL.connect_material_property(c1(mat, 0.80, -450, 250), "", MP.MP_ROUGHNESS)
    finish(mat)
    return mat

def build_all_materials():
    global NOISE_TEXTURE
    NOISE_TEXTURE = import_noise_texture()
    log("Rauschtextur: %s" % ("geladen" if NOISE_TEXTURE else "FEHLT - Materialien rechnen"))

    textures = import_facade_textures()
    log("Texturen: %s" % ", ".join(sorted(textures.keys())) or "keine")
    made = {
        "Road": make_road(),
        "Sidewalk": make_sidewalk(),
        "Kerb": make_kerb(),
        # Die allgemeine Gebaeudewand ist eine PUTZWAND mit Fenstern aus der
        # UV-Belegung. Hier stand die Glas-Vorhangfassade Facade_Wohnhaus -
        # das Material, das die meisten Haeuser der Stadt tragen, war damit
        # Spiegelglas.
        "Wall": make_wall(textures),
        "Roof": make_roof(),
        "Terrain": make_terrain(),
    }
    made.update(make_facade_variants(textures))
    made.update(make_furniture_materials())
    made["Tree"] = make_tree()
    made["Helicopter"] = make_helicopter()
    made["Unpaved"] = make_unpaved()
    made["PavedStone"] = make_paved_stone()
    for k, v in made.items():
        log("Material %s -> %s" % (k, v.get_path_name() if v else "FEHLER"))
    return made


# ------------------------------------------------------------ Beleuchtung
def reassign_landscape_material(made):
    """
    Weist dem Landscape das Gelaende-Material neu zu.

    new_material() loescht das Asset und legt es neu an. Die
    Procedural-Mesh-Komponenten der Chunks finden es danach ueber den Pfad
    wieder, das Landscape NICHT: seine Referenz zeigt ins Leere und es rendert
    mit dem Default-Material - das komplette Gelaende wird zum Schachbrett.

    Faellt nur auf, wenn man den Materiallauf ohne anschliessenden Stadt-Neubau
    macht; genau dafuer ist er aber gedacht.
    """
    terrain = made.get("Terrain")
    if terrain is None:
        return

    EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    count = 0
    for actor in EAS.get_all_level_actors():
        if actor is None or not isinstance(actor, unreal.LandscapeProxy):
            continue
        actor.set_editor_property("landscape_material", terrain)
        count += 1

    log("Landscape-Material neu verknuepft: %d Actor(en)" % count)



def set_movable(actor):
    """Mobility sitzt an der Root-Komponente, nicht am Actor."""
    root = actor.get_editor_property("root_component")
    if root:
        root.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)


def ensure_lighting():
    """
    Stellt sicher, dass die Map eine vollstaendige Himmels- und
    Lichtausstattung hat. Ohne DirectionalLight findet WeatherFX nichts zum
    Ansteuern, ohne SkyLight fehlt jedes Umgebungslicht - Fassaden im
    Schatten werden dann schwarz.
    """
    EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors = EAS.get_all_level_actors()

    def find(cls):
        return [a for a in actors if a and isinstance(a, cls)]

    inventory = {
        "DirectionalLight": find(unreal.DirectionalLight),
        "SkyLight": find(unreal.SkyLight),
        "SkyAtmosphere": find(unreal.SkyAtmosphere),
        "ExponentialHeightFog": find(unreal.ExponentialHeightFog),
        "PostProcessVolume": find(unreal.PostProcessVolume),
    }
    for k, v in inventory.items():
        log("BESTAND %s: %d" % (k, len(v)))

    origin = unreal.Vector(0.0, 0.0, 20000.0)

    # -- Sonne -------------------------------------------------------------
    if inventory["DirectionalLight"]:
        sun = inventory["DirectionalLight"][0]
    else:
        sun = EAS.spawn_actor_from_class(
            unreal.DirectionalLight, origin,
            unreal.Rotator(0.0, -42.0, -35.0))
        sun.set_actor_label("Sonne")
        log("ANGELEGT DirectionalLight")
    comp = sun.get_component_by_class(unreal.DirectionalLightComponent)
    if comp:
        # Beweglich, damit WeatherFX Stand und Farbe zur Laufzeit aendern kann.
        set_movable(sun)
        comp.set_editor_property("intensity", 10.0)          # Lux, UE5-Default
        comp.set_editor_property("atmosphere_sun_light", True)
        comp.set_editor_property("dynamic_shadow_distance_movable_light", 30000.0)
        comp.set_editor_property("cast_shadows", True)

    # -- Himmelslicht ------------------------------------------------------
    if inventory["SkyLight"]:
        sky = inventory["SkyLight"][0]
    else:
        sky = EAS.spawn_actor_from_class(unreal.SkyLight, origin)
        sky.set_actor_label("Himmelslicht")
        log("ANGELEGT SkyLight")
    skc = sky.get_component_by_class(unreal.SkyLightComponent)
    if skc:
        set_movable(sky)
        # Echtzeit-Aufnahme: folgt dem wandernden Sonnenstand.
        skc.set_editor_property("real_time_capture", True)
        skc.set_editor_property("intensity", 1.0)

    # -- Atmosphaere, Nebel ------------------------------------------------
    if not inventory["SkyAtmosphere"]:
        a = EAS.spawn_actor_from_class(unreal.SkyAtmosphere, origin)
        a.set_actor_label("Atmosphaere")
        log("ANGELEGT SkyAtmosphere")

    if not inventory["ExponentialHeightFog"]:
        f = EAS.spawn_actor_from_class(unreal.ExponentialHeightFog,
                                       unreal.Vector(0.0, 0.0, 0.0))
        f.set_actor_label("Hoehennebel")
        fc = f.get_component_by_class(unreal.ExponentialHeightFogComponent)
        if fc:
            # Dezent: Tiefenwirkung ueber die Stadt, ohne Sicht zu nehmen.
            fc.set_editor_property("fog_density", 0.008)
            fc.set_editor_property("fog_height_falloff", 0.15)
            fc.set_editor_property("start_distance", 4000.0)
        log("ANGELEGT ExponentialHeightFog")

    return sun, sky


def main():
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(MAP)
    log("Map geladen: %s" % MAP)

    made = build_all_materials()
    reassign_landscape_material(made)

    # Beleuchtung bewusst NICHT hier: dafuer ist Tools/ensure_lighting.py da.
    # Vorher stand hier eine zweite Kopie der Lichtlogik - sie lief zuletzt und
    # hat die dort gepflegten Werte stillschweigend ueberschrieben.

    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    log("FERTIG")


main()
