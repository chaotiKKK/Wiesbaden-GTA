# Strassen-Einblenden: weiches Dither-Fade am Streaming-Rand statt hartem
# Aufpoppen beim schnellen Fahren/Fliegen.
#
# Das Fade-Band muss den HOEHENADAPTIVEN Streaming-Radius kennen (900 m Boden ...
# 6000 m Luft), sonst wuerden im Flug ferne Strassen verschwinden. Deshalb liest
# das Material den aktuellen Radius aus einer Material-Parameter-Collection
# (MPC_WbStreaming.FadeRadiusM), die WiesbadenStreamingSource je Bild fuellt.
#
# Fade (Opacity-Mask, Blend = Masked):
#   dist   = |Kamera - Weltposition|                       (cm)
#   R      = FadeRadiusM * 100                              (cm, aus MPC)
#   Band   = R * FadeBandFrac                               (cm, Parameter)
#   fade   = saturate( (R - dist) / Band )
#   Mask   = DitherTemporalAA(fade)
# Nah (dist << R) -> fade 1 -> voll deckend; am Rand (dist -> R) -> fade 0 ->
# eingeblendet. Weil R mitwaechst, sitzt das Band in jeder Hoehe am Rand.
#
# Content/*.uasset ist git-ignoriert -> es werden nur Assets angelegt/geaendert;
# reproduzierbar und idempotent (bereits gefadete Materialien werden erkannt).
#
# Headless:
#   UnrealEditor-Cmd.exe <proj> -run=pythonscript -script="Tools/build_streaming_fade.py"
import unreal, os

ROOT = r"C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal"
SENTINEL = os.path.join(ROOT, "build_streaming_fade.done")

mel = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()

MPC_DIR = "/Game/Materials/AAA"
MPC_NAME = "MPC_WbStreaming"
MPC_PATH = MPC_DIR + "/" + MPC_NAME
PARAM = "FadeRadiusM"
# Default nur relevant, falls die MPC zur Laufzeit NICHT geschrieben wird - im
# Normalfall ueberschreibt WiesbadenStreamingSource den Wert je Bild mit dem
# gelebten Radius (per-Bild-Nachweis: Log "Fade-MPC: FadeRadiusM gesetzt").
DEFAULT_RADIUS_M = 900.0

# Alle Zell-gestreamten Oberflaechen eines Chunks bekommen dasselbe Einblenden,
# damit Haeuser UND Fahrbahn am Streaming-Rand konsistent weich erscheinen.
# Je Satz gebacken (M_Wb*, City) UND Laufzeit (M_AAA_). AUSGENOMMEN: Terrain -
# die Landscape ist immer resident (nicht per Zelle gestreamt), ein Radius-Fade
# wuerde ferne Wiesen faelschlich ausblenden. Transluzentes (z. B. Glas) wird in
# apply_fade uebersprungen, weil Masked es zerstoeren wuerde.
FADE_MATERIALS = [
    # Fahrbahn
    "/Game/Materials/AAA/M_AAA_RoadAsphalt",
    "/Game/Materials/City/M_WbRoad",
    # Gebaeude: Waende / Fassaden-Varianten
    "/Game/Materials/City/M_WbBuildingWall",
    "/Game/Materials/City/M_WbFacade_Putz",
    "/Game/Materials/City/M_WbFacade_Backstein",
    "/Game/Materials/City/M_WbFacade_Sandstein",
    "/Game/Materials/City/M_WbFacade_Beton",
    "/Game/Materials/City/M_WbFacade_Glas",
    "/Game/Materials/City/M_WbFacade_Fachwerk",
    "/Game/Materials/AAA/M_AAA_FacadePlaster",
    "/Game/Materials/AAA/M_AAA_FacadeBrick",
    "/Game/Materials/AAA/M_AAA_FacadeStone",
    "/Game/Materials/AAA/M_AAA_FacadeConcrete",
    # Gebaeude: Daecher
    "/Game/Materials/City/M_WbBuildingRoof",
    "/Game/Materials/AAA/M_AAA_RoofClay",
    "/Game/Materials/AAA/M_AAA_RoofVaried",
    # Uebrige Chunk-Bodenflaechen (Gehweg/Pflaster/unbefestigt) - KEIN Terrain.
    # M_WbKerb bewusst NICHT: der Bordstein ist ein DUENNES, senkrechtes Band.
    # Unter der DitherTemporalAA-Maske (Masked-Blend) stippelt es und wirkt
    # durchsichtig ("man schaut hindurch"). Der Bordstein bleibt opak (fix_kerb_solid.py).
    "/Game/Materials/City/M_WbSidewalk",
    "/Game/Materials/City/M_WbPavedStone",
    "/Game/Materials/City/M_WbUnpaved",
    "/Game/Materials/AAA/M_AAA_Paving",
    "/Game/Materials/AAA/M_AAA_GroundDirt",
]


def ensure_mpc():
    if EAL.does_asset_exist(MPC_PATH):
        return unreal.load_asset(MPC_PATH)
    mpc = tools.create_asset(MPC_NAME, MPC_DIR, unreal.MaterialParameterCollection,
                             unreal.MaterialParameterCollectionFactoryNew())
    sp = unreal.CollectionScalarParameter()
    sp.set_editor_property("parameter_name", PARAM)
    sp.set_editor_property("default_value", DEFAULT_RADIUS_M)
    try:
        sp.set_editor_property("id", unreal.GuidLibrary.new_guid())
    except Exception:
        pass
    mpc.set_editor_property("scalar_parameters", [sp])
    EAL.save_loaded_asset(mpc)
    unreal.log("Fade: MPC angelegt %s (Skalar %s=%.0f)." % (MPC_PATH, PARAM, DEFAULT_RADIUS_M))
    return unreal.load_asset(MPC_PATH)


def already_faded(mat):
    for e in mel.get_material_expressions(mat):
        if isinstance(e, unreal.MaterialExpressionCollectionParameter):
            return True
    return False


def ex(mat, cls, x, y):
    return mel.create_material_expression(mat, cls, x, y)


def apply_fade(mat_path, mpc):
    if not EAL.does_asset_exist(mat_path):
        unreal.log_warning("Fade: Material fehlt, uebersprungen: " + mat_path)
        return "fehlt"
    mat = unreal.load_asset(mat_path)
    if already_faded(mat):
        unreal.log("Fade: schon vorhanden, uebersprungen: " + mat_path)
        return "schon"

    # Nur opake/maskierte Materialien: das Fade setzt Blend=Masked und haengt an
    # die Opacity-Mask. Ein transluzentes Material (Glas) hat bereits einen
    # Opacity-Pfad - Masked wuerde es zerstoeren -> ueberspringen.
    bm = mat.get_editor_property("blend_mode")
    if bm not in (unreal.BlendMode.BLEND_OPAQUE, unreal.BlendMode.BLEND_MASKED):
        unreal.log("Fade: nicht-opak (%s), uebersprungen: %s" % (bm, mat_path))
        return "nicht-opak"

    # dist = |Kamera - Weltposition|
    cam = ex(mat, unreal.MaterialExpressionCameraPositionWS, -2200, 900)
    wp = ex(mat, unreal.MaterialExpressionWorldPosition, -2200, 1050)
    dist = ex(mat, unreal.MaterialExpressionDistance, -1950, 950)
    mel.connect_material_expressions(cam, "", dist, "A")
    mel.connect_material_expressions(wp, "", dist, "B")

    # R (cm) = FadeRadiusM * 100
    cp = ex(mat, unreal.MaterialExpressionCollectionParameter, -2200, 650)
    cp.set_editor_property("collection", mpc)
    cp.set_editor_property("parameter_name", PARAM)
    c100 = ex(mat, unreal.MaterialExpressionConstant, -2200, 560)
    c100.set_editor_property("r", 100.0)
    r_cm = ex(mat, unreal.MaterialExpressionMultiply, -1950, 650)
    mel.connect_material_expressions(cp, "", r_cm, "A")
    mel.connect_material_expressions(c100, "", r_cm, "B")

    # Band (cm) = R * FadeBandFrac
    frac = ex(mat, unreal.MaterialExpressionScalarParameter, -2200, 430)
    frac.set_editor_property("parameter_name", "FadeBandFrac")
    frac.set_editor_property("default_value", 0.20)
    band = ex(mat, unreal.MaterialExpressionMultiply, -1700, 500)
    mel.connect_material_expressions(r_cm, "", band, "A")
    mel.connect_material_expressions(frac, "", band, "B")

    # numer = R - dist
    numer = ex(mat, unreal.MaterialExpressionSubtract, -1700, 800)
    mel.connect_material_expressions(r_cm, "", numer, "A")
    mel.connect_material_expressions(dist, "", numer, "B")

    # fade = clamp(numer / band, 0, 1)
    div = ex(mat, unreal.MaterialExpressionDivide, -1450, 720)
    mel.connect_material_expressions(numer, "", div, "A")
    mel.connect_material_expressions(band, "", div, "B")
    clamp = ex(mat, unreal.MaterialExpressionClamp, -1250, 720)
    clamp.set_editor_property("min_default", 0.0)
    clamp.set_editor_property("max_default", 1.0)
    mel.connect_material_expressions(div, "", clamp, "")

    # Mask = DitherTemporalAA(fade). DitherTemporalAA ist in UE eine
    # Material-FUNKTION (keine Expression-Klasse) -> per MaterialFunctionCall.
    dither = ex(mat, unreal.MaterialExpressionMaterialFunctionCall, -1050, 720)
    dfunc = unreal.load_asset(
        "/Engine/Functions/Engine_MaterialFunctions02/Utility/DitherTemporalAA.DitherTemporalAA")
    dither.set_editor_property("material_function", dfunc)

    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    mat.set_editor_property("opacity_mask_clip_value", 0.333)
    connect_dither(mat, clamp, dither)
    mel.recompile_material(mat)
    EAL.save_loaded_asset(mat)

    # Zweiter Durchgang: die Eingangs-Pins der Funktion sind erst nach
    # set_material_function + Speichern zuverlaessig da. Neu laden und den
    # Clamp-Ausgang sicher an "Alpha Threshold" haengen, sonst nutzt die
    # Funktion ihren DECKENDEN Default und das Fade bliebe wirkungslos.
    mat2 = unreal.load_asset(mat_path)
    clamp2 = dither2 = None
    for e in mel.get_material_expressions(mat2):
        cn = e.get_class().get_name()
        if cn == "MaterialExpressionClamp":
            clamp2 = e
        elif cn == "MaterialExpressionMaterialFunctionCall":
            dither2 = e
    if clamp2 and dither2:
        used = connect_dither(mat2, clamp2, dither2)
        mel.connect_material_property(dither2, "", unreal.MaterialProperty.MP_OPACITY_MASK)
        mel.recompile_material(mat2)
        EAL.save_loaded_asset(mat2)
        unreal.log("Fade: eingebaut in %s (Dither-Eingang '%s')" % (mat_path, used))
    else:
        unreal.log("Fade: eingebaut in " + mat_path)
    return "ok"


def connect_dither(mat, clamp, dither):
    """Clamp-Fade an den DitherTemporalAA-Eingang haengen; Eingangsname robust."""
    for name in ["Alpha Threshold", "AlphaThreshold", ""]:
        if mel.connect_material_expressions(clamp, "", dither, name):
            return name if name else "(erster Eingang)"
    return "(fehlgeschlagen)"


def main():
    results = {}
    mpc = ensure_mpc()
    for p in FADE_MATERIALS:
        try:
            results[p] = apply_fade(p, mpc)
        except Exception as e:
            results[p] = "FEHLER: %s" % e
            unreal.log_error("Fade: %s -> %s" % (p, e))
    with open(SENTINEL, "w", encoding="utf-8") as f:
        f.write("MPC=%s\n" % MPC_PATH)
        for p, r in results.items():
            f.write("%s = %s\n" % (p, r))
    unreal.log("Fade: fertig. " + "; ".join("%s=%s" % (os.path.basename(p), r) for p, r in results.items()))


main()
