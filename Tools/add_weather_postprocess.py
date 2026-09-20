"""Baut das Wetter-Overlay als POST-PROCESS-Material - Regen und Schnee ohne Niagara.

Warum ueberhaupt: die Niagara-Systeme muessen von Hand im Editor gebaut werden
(Python exportiert die Niagara-Editor-API nicht, nachgemessen in
Saved/probe_niagara_templates.txt). Ein Post-Process-Material dagegen ist ein
ganz normaler Materialgraph - und den kann Python vollstaendig bauen.

Was entsteht: /Game/Materials/PostProcess/M_WbWeatherOverlay, Domain
Post Process, angewandt NACH dem Tonemapper. Der C++-Code
(UWiesbadenWeatherFXComponent::UpdateOverlay) haengt es als Blendable in ein
unbegrenztes PostProcessVolume und setzt je Bild:

    RegenStaerke   0..1
    SchneeStaerke  0..1
    Schraeglage   -1..1   (Wind, im Bildraum)
    Farbe         LinearColor (Lichtfarbe der Sonne)
    Helligkeit     0..1   (Umgebungslicht; nachts gedaempft)

Die Streifen entstehen prozedural aus der Bildschirm-UV: Spalten per floor(),
je Spalte ein Hash fuer Versatz und Tempo, dazu frac() fuer die Wiederholung
nach unten. Kein einziges Bild-Asset noetig - dieselbe Technik wie beim
Fensterlicht (Tools/add_window_light.py).

Aufruf:
  UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<diese Datei>
"""

import unreal

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
MP = unreal.MaterialProperty

PKG_PATH = "/Game/Materials/PostProcess"
MAT_NAME = "M_WbWeatherOverlay"
RESULT = r"C:/freebuff/WiesbadenReal_Sicherung/WiesbadenReal/Saved/weather_overlay.txt"

import sys

log = []
PATTERN_ONLY = "nurmuster" in sys.argv
SCENE_ONLY = "nurszene" in sys.argv


# -- kleine Bausteine (wie in add_window_light.py) ---------------------------

def expr(mat, cls, x, y):
    return MEL.create_material_expression(mat, cls, x, y)


def conn(a, ao, b, bi):
    MEL.connect_material_expressions(a, ao, b, bi)


def c1(mat, v, x, y):
    n = expr(mat, unreal.MaterialExpressionConstant, x, y)
    n.set_editor_property("r", float(v))
    return n


def _binary(mat, cls, a, b, x, y, ao="", bo=""):
    """Zwei Eingaenge, jeder wahlweise Ausdruck ODER Zahl (beide Seiten!)."""
    n = expr(mat, cls, x, y)
    if isinstance(a, (int, float)):
        n.set_editor_property("const_a", float(a))
    else:
        conn(a, ao, n, "A")
    if isinstance(b, (int, float)):
        n.set_editor_property("const_b", float(b))
    else:
        conn(b, bo, n, "B")
    return n


def mul(mat, a, b, x, y, ao="", bo=""):
    return _binary(mat, unreal.MaterialExpressionMultiply, a, b, x, y, ao, bo)


def add(mat, a, b, x, y, ao="", bo=""):
    return _binary(mat, unreal.MaterialExpressionAdd, a, b, x, y, ao, bo)


def sub(mat, a, b, x, y, ao="", bo=""):
    return _binary(mat, unreal.MaterialExpressionSubtract, a, b, x, y, ao, bo)


def div(mat, a, b, x, y, ao="", bo=""):
    return _binary(mat, unreal.MaterialExpressionDivide, a, b, x, y, ao, bo)


def frac(mat, a, x, y, ao=""):
    n = expr(mat, unreal.MaterialExpressionFrac, x, y)
    conn(a, ao, n, "")
    return n


def floor_(mat, a, x, y, ao=""):
    n = expr(mat, unreal.MaterialExpressionFloor, x, y)
    conn(a, ao, n, "")
    return n


def sat(mat, a, x, y, ao=""):
    n = expr(mat, unreal.MaterialExpressionSaturate, x, y)
    conn(a, ao, n, "")
    return n


def sine(mat, a, x, y, ao=""):
    n = expr(mat, unreal.MaterialExpressionSine, x, y)
    conn(a, ao, n, "")
    return n


def mask(mat, source, ch, x, y, so=""):
    n = expr(mat, unreal.MaterialExpressionComponentMask, x, y)
    n.set_editor_property("r", ch == "r")
    n.set_editor_property("g", ch == "g")
    n.set_editor_property("b", ch == "b")
    n.set_editor_property("a", False)
    conn(source, so, n, "")
    return n


def rgb(mat, source, x, y, so=""):
    """Nur die drei Farbkanaele.

    SceneTexture liefert float4 (RGBA). Ein float4 mit einem float3 zu
    addieren ist in HLSL kein Fehler, den man sieht - das MATERIAL scheitert,
    faellt auf das Standard-Post-Process zurueck und reicht die Szene
    unveraendert durch. Im Bild sieht das aus, als tue das Overlay nichts;
    nur der Diagnose-Modus ohne SceneTexture zeigte das Muster. Darum hier
    ausdruecklich maskieren.
    """
    n = expr(mat, unreal.MaterialExpressionComponentMask, x, y)
    n.set_editor_property("r", True)
    n.set_editor_property("g", True)
    n.set_editor_property("b", True)
    n.set_editor_property("a", False)
    conn(source, so, n, "")
    return n


def scalar(mat, name, default, x, y):
    n = expr(mat, unreal.MaterialExpressionScalarParameter, x, y)
    n.set_editor_property("parameter_name", name)
    n.set_editor_property("default_value", float(default))
    return n


def vector(mat, name, color, x, y):
    n = expr(mat, unreal.MaterialExpressionVectorParameter, x, y)
    n.set_editor_property("parameter_name", name)
    n.set_editor_property("default_value", color)
    return n


def band(mat, value, low, high, x, y, sharpness=30.0):
    """Weiches Fenster low..high um einen Wert (1 innen, 0 aussen)."""
    lower = sat(mat, mul(mat, sub(mat, value, low, x, y), sharpness, x + 120, y), x + 240, y)
    upper = sat(mat, mul(mat, sub(mat, high, value, x, y + 120), sharpness, x + 120, y + 120),
                x + 240, y + 120)
    return mul(mat, lower, upper, x + 360, y + 60)


def hash01(mat, seed_expr, salt, x, y):
    """Streuwert 0..1 aus einer ganzen Zahl. frac(n * 43.758 + salt)."""
    return frac(mat, add(mat, mul(mat, seed_expr, 43.7585, x, y), salt, x + 140, y), x + 280, y)


# -- die beiden Schichten ---------------------------------------------------

def rain_layer(mat, u, v, time, strength, slant, columns, rows, speed, tail, salt, x, y,
               half_width=0.10):
    """Eine Lage Regenstreifen. Gibt eine Maske 0..1 zurueck.

    Der Aufbau ist bewusst simpel: die Bildschirmbreite wird in Spalten
    geteilt, jede Spalte bekommt aus ihrer Nummer einen Hash - daraus Versatz
    und Tempo. So faellt nicht alles im Gleichschritt, ohne dass eine Textur
    noetig waere.
    """
    # Spalte; die Schraeglage verschiebt sie mit der Hoehe -> schraeger Fall.
    su = add(mat, mul(mat, u, columns, x, y),
             mul(mat, mul(mat, v, slant, x, y + 120), columns * 0.6, x + 140, y + 120),
             x + 300, y + 60)
    ci = floor_(mat, su, x + 440, y)
    cf = frac(mat, su, x + 440, y + 140)
    r = hash01(mat, ci, salt, x + 580, y)

    # Fallhoehe: Zeit x Tempo, je Spalte leicht anders, plus Startversatz.
    sv = add(mat,
             add(mat, mul(mat, v, rows, x + 580, y + 300),
                 mul(mat, time, mul(mat, add(mat, 0.7, mul(mat, r, 0.8, x + 580, y + 440),
                                             x + 720, y + 440), speed, x + 860, y + 440),
                     x + 1000, y + 380), x + 1140, y + 340),
             mul(mat, r, 11.0, x + 1140, y + 480), x + 1280, y + 380)
    vf = frac(mat, sv, x + 1420, y + 380)

    # Querschnitt: Fenster in der Spalte.
    #
    # Die Breite ist der Unterschied zwischen "sichtbarer Regen" und "nichts".
    # Mit 0.44..0.56 war der Streifen schmaler als ein Bildschirmpixel: das
    # Fenster traf je Spalte hoechstens EIN Pixel und auch das nur teilweise -
    # im Bild blieb ein kaum sichtbares Flimmern. Jetzt sind es rund 2-3 Pixel.
    across = band(mat, cf, 0.5 - half_width, 0.5 + half_width, x + 1420, y + 100,
                  sharpness=1.6 / max(half_width, 0.01))
    # Laengsprofil: vorne hell, nach hinten ausklingend -> Tropfenschweif.
    along = sat(mat, sub(mat, 1.0, div(mat, vf, tail, x + 1560, y + 380), x + 1700, y + 380),
                x + 1840, y + 380)

    # Nur ein Teil der Spalten regnet - das ist die Dichte.
    on = sat(mat, mul(mat, sub(mat, mul(mat, strength, 1.15, x + 1560, y + 620), r,
                               x + 1700, y + 620), 8.0, x + 1840, y + 620), x + 1980, y + 620)

    return mul(mat, mul(mat, across, along, x + 2120, y + 240),
               on, x + 2260, y + 400)


def snow_layer(mat, u, v, time, strength, slant, cells, speed, radius, density, salt, x, y,
               shear=0.0):
    """Eine Lage Schneeflocken: runde Punkte, langsam, seitlich taumelnd.

    QUADRATISCHE ZELLEN, und das ist der ganze Trick. Die erste Fassung teilte
    die Breite in 38 Spalten und die Hoehe in 2,4 Zeilen - eine Zelle war damit
    24 x 375 Pixel gross. Ein Punkt, der in ZELLKOORDINATEN rund ist, wird
    darin zu einem 375 Pixel langen Strich: es schneite in Streifen, genau wie
    es regnete. Sichtbar wurde das erst im Bild, der Code sah richtig aus.

    Weil u bereits mit dem Seitenverhaeltnis multipliziert ist, messen u und v
    in derselben Einheit (Bildhoehen). Gleiche Teilung in beide Richtungen
    heisst darum: quadratische Zellen.
    """
    grid = cells

    # Gitter scheren: ohne Scherung stehen die Zellenspalten senkrecht und der
    # Schnee faellt in Reihen, die man als Raster liest. Der Wind kommt dazu,
    # die Grundscherung bleibt auch bei Windstille.
    lean = mul(mat, add(mat, slant, shear, x, y + 100), grid, x + 140, y + 100)
    su0 = add(mat, mul(mat, u, grid, x, y),
              mul(mat, v, lean, x + 300, y + 120),
              x + 440, y + 60)
    ci = floor_(mat, su0, x + 440, y)
    r = hash01(mat, ci, salt, x + 580, y)

    # Taumeln: seitlicher Sinus je Flocke, Phase aus dem Hash.
    wobble = mul(mat, sine(mat, add(mat, mul(mat, time, 0.9, x + 580, y + 200),
                                    mul(mat, r, 19.0, x + 580, y + 320), x + 720, y + 240),
                           x + 860, y + 240), 0.28, x + 1000, y + 240)
    cf = frac(mat, add(mat, su0, wobble, x + 1140, y + 120), x + 1280, y + 120)

    # Fallhoehe in DERSELBEN Teilung wie die Breite (quadratische Zellen).
    sv = add(mat,
             add(mat, mul(mat, v, grid, x + 580, y + 460),
                 mul(mat, time, mul(mat, add(mat, 0.6, mul(mat, r, 0.9, x + 580, y + 600),
                                             x + 720, y + 600), speed, x + 860, y + 600),
                     x + 1000, y + 540), x + 1140, y + 500),
             mul(mat, r, 7.0, x + 1140, y + 660), x + 1280, y + 540)
    vf = frac(mat, sv, x + 1420, y + 540)

    # Zweiter Hash je Spalte: Versatz aus der Zellenmitte und Helligkeit.
    #
    # Ohne ihn sitzt jede Flocke genau in der Mitte ihrer Zelle, und weil alle
    # Zellen gleich breit sind, faellt der Schnee in sauber ausgerichteten
    # Perlenschnueren - im Bild sofort als Raster zu erkennen. Der Versatz
    # bricht die Ausrichtung, die Helligkeit nimmt den Reihen die Gleichheit.
    r2 = hash01(mat, ci, salt + 0.4131, x + 580, y + 140)

    # Runder Punkt: quadratischer Abstand zur (versetzten) Zellenmitte, auf
    # den Radius normiert (radius in Zellbreiten).
    dx = sub(mat, cf, add(mat, 0.5, mul(mat, sub(mat, r2, 0.5, x + 1140, y + 20),
                                        0.44, x + 1280, y + 20), x + 1360, y + 20),
             x + 1420, y + 120)
    dy = sub(mat, vf, 0.5, x + 1560, y + 540)
    d2 = add(mat, mul(mat, dx, dx, x + 1560, y + 120), mul(mat, dy, dy, x + 1700, y + 540),
             x + 1840, y + 300)
    blob = sat(mat, sub(mat, 1.0,
                        mul(mat, d2, 1.0 / max(radius * radius, 1e-4), x + 1980, y + 300),
                        x + 2120, y + 300), x + 2260, y + 300)

    # Nicht jede Zelle traegt eine Flocke - sonst schneit es als Raster.
    on = sat(mat, mul(mat, sub(mat, mul(mat, strength, density, x + 1980, y + 660), r,
                               x + 2120, y + 660), 8.0, x + 2260, y + 660), x + 2400, y + 660)

    # Helligkeit je Flocke 0,55..1,0 - gleich helle Punkte sehen gedruckt aus.
    shade = add(mat, 0.55, mul(mat, r2, 0.45, x + 2400, y + 180), x + 2540, y + 180)

    return mul(mat, mul(mat, blob, shade, x + 2540, y + 300), on, x + 2680, y + 440)


# -- Aufbau -----------------------------------------------------------------

def enum_value(enum_cls, *candidates):
    """Erster vorhandener Enum-Eintrag. Die Namen wechseln zwischen UE-Versionen."""
    for name in candidates:
        if hasattr(enum_cls, name):
            return getattr(enum_cls, name), name
    raise RuntimeError(f"Kein Eintrag aus {candidates} in {enum_cls}")


def build():
    path = f"{PKG_PATH}/{MAT_NAME}"
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
        log.append("Bestehendes Material geloescht (Neuaufbau).")

    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mat = tools.create_asset(MAT_NAME, PKG_PATH, unreal.Material,
                             unreal.MaterialFactoryNew())
    if mat is None:
        raise RuntimeError("Material konnte nicht angelegt werden")

    # Post-Process-Domain, NACH dem Tonemapper: sonst frisst die Belichtung
    # die Streifen, sobald die Szene hell ist.
    dom, dom_name = enum_value(unreal.MaterialDomain, "MD_POST_PROCESS")
    mat.set_editor_property("material_domain", dom)
    loc, loc_name = enum_value(unreal.BlendableLocation,
                               "BL_SCENE_COLOR_AFTER_TONEMAPPING",
                               "BL_AFTER_TONEMAPPING",
                               "BL_SCENE_COLOR_AFTER_DOF")
    mat.set_editor_property("blendable_location", loc)
    log.append(f"Domain={dom_name}, BlendableLocation={loc_name}")

    # -- Steuerung ----------------------------------------------------------
    p_rain = scalar(mat, "RegenStaerke", 0.0, -4200, -600)
    p_snow = scalar(mat, "SchneeStaerke", 0.0, -4200, -400)
    p_slant = scalar(mat, "Schraeglage", 0.15, -4200, -200)
    p_bright = scalar(mat, "Helligkeit", 1.0, -4200, 0)
    p_color = vector(mat, "Farbe", unreal.LinearColor(0.80, 0.86, 1.0, 1.0), -4200, 200)

    # -- Bildschirm-UV und Zeit --------------------------------------------
    # In der Post-Process-Domain liefert TextureCoordinate die Viewport-UV.
    uv = expr(mat, unreal.MaterialExpressionTextureCoordinate, -4200, 500)
    u0 = mask(mat, uv, "r", -4000, 460)
    v = mask(mat, uv, "g", -4000, 640)

    # Seitenverhaeltnis: ohne Korrektur werden die Streifen im Breitbild
    # gestaucht. ViewSize kommt aus der Engine, nicht aus einer Annahme.
    view = expr(mat, unreal.MaterialExpressionViewProperty, -4000, 820)
    try:
        vp, vp_name = enum_value(unreal.MaterialExposedViewProperty, "MEVP_VIEW_SIZE")
        view.set_editor_property("property", vp)
        aspect = div(mat, mask(mat, view, "r", -3840, 780), mask(mat, view, "g", -3840, 900),
                     -3700, 840)
        u = mul(mat, u0, aspect, -3560, 600)
        log.append(f"Seitenverhaeltnis aus {vp_name}")
    except Exception as e:                                   # pragma: no cover
        u = mul(mat, u0, 1.7778, -3560, 600)
        log.append(f"Seitenverhaeltnis fest 16:9 ({e!r})")

    time = expr(mat, unreal.MaterialExpressionTime, -4000, 1000)

    # -- Regen: zwei Lagen, die hintere kleiner und langsamer ---------------
    rain_near = rain_layer(mat, u, v, time, p_rain, p_slant,
                           columns=70, rows=4.0, speed=2.2, tail=0.45, salt=0.31,
                           x=-3400, y=-1400, half_width=0.065)
    rain_far = rain_layer(mat, u, v, time, p_rain, p_slant,
                          columns=150, rows=7.0, speed=1.6, tail=0.30, salt=0.77,
                          x=-3400, y=800, half_width=0.05)
    rain = add(mat, rain_near, mul(mat, rain_far, 0.5, -600, 800), -300, -200)

    # -- Schnee: zwei Lagen -------------------------------------------------
    # cells = Teilung in BEIDE Richtungen; speed in Zellen je Sekunde.
    snow_near = snow_layer(mat, u, v, time, p_snow, p_slant,
                           cells=24, speed=3.2, radius=0.21, density=0.50, salt=0.13,
                           x=-3400, y=3000, shear=0.18)
    snow_far = snow_layer(mat, u, v, time, p_snow, p_slant,
                          cells=49, speed=2.4, radius=0.17, density=0.38, salt=0.61,
                          x=-3400, y=5200, shear=0.42)
    snow = add(mat, snow_near, mul(mat, snow_far, 0.7, -600, 5200), -300, 3600)

    # -- Zusammensetzen -----------------------------------------------------
    # Verstaerkung vor dem Begrenzen: die Profile laufen nur an der Spitze
    # gegen 1, im Mittel liegen sie weit darunter. Ohne Gain bleibt selbst
    # dichter Regen ein Hauch.
    fall = sat(mat, mul(mat, add(mat, rain, snow, -120, 1600), 0.85, 20, 1600),
               160, 1600)
    tinted = mul(mat, mul(mat, fall, p_color, 320, 1200),
                 p_bright, 460, 1400)

    # Szene holen und die Tropfen daraufsetzen.
    scene = expr(mat, unreal.MaterialExpressionSceneTexture, 180, 2000)
    st, st_name = enum_value(unreal.SceneTextureId,
                             "PPI_POST_PROCESS_INPUT0", "PPI_POSTPROCESS_INPUT0")
    scene.set_editor_property("scene_texture_id", st)
    log.append(f"SceneTexture={st_name}")

    if SCENE_ONLY:
        # Diagnose-Modus: NUR die Szene durchreichen. Bleibt das Bild dabei
        # verdorben, liegt es am Durchreichen selbst und nicht am Muster.
        out = scene
        log.append("DIAGNOSE: nur die Szene, ohne Muster.")
    elif PATTERN_ONLY:
        # Diagnose-Modus: NUR das Muster auf Schwarz. Ueber der Szene laesst
        # sich nicht unterscheiden, ob ein Fleck vom Regen kommt oder von der
        # Szene selbst - hier kann er nur vom Muster kommen.
        out = tinted
        log.append("DIAGNOSE: nur das Muster, ohne Szene.")
    else:
        out = add(mat, rgb(mat, scene, 360, 2000, so="Color"), tinted, 520, 1700)

    ok = MEL.connect_material_property(out, "", MP.MP_EMISSIVE_COLOR)
    log.append(f"Emissiv verbunden: {ok}")
    if not ok:
        raise RuntimeError("Emissiv-Ausgang nicht verbunden")

    MEL.recompile_material(mat)
    EAL.save_asset(path)

    # Gegenprobe: die Parameter MUESSEN am Material haengen, sonst laeuft der
    # C++-Code ins Leere und faerbt gar nichts.
    scalars = sorted(str(n) for n in MEL.get_scalar_parameter_names(mat))
    vectors = sorted(str(n) for n in MEL.get_vector_parameter_names(mat))
    log.append(f"Skalar-Parameter: {scalars}")
    log.append(f"Vektor-Parameter: {vectors}")
    expected = {"RegenStaerke", "SchneeStaerke", "Schraeglage", "Helligkeit"}
    missing = expected - set(scalars)
    if missing or "Farbe" not in vectors:
        log.append(f"FEHLER: fehlende Parameter {missing or ''} {'Farbe' if 'Farbe' not in vectors else ''}")
    else:
        log.append("Vertrag erfuellt: alle Parameter vorhanden.")
    log.append(f"Gespeichert: {path}")


def main():
    try:
        build()
    except Exception as e:
        log.append(f"ABBRUCH: {e!r}")
    open(RESULT, "w", encoding="utf-8").write("\n".join(log))
    for line in log:
        unreal.log(line)


main()
