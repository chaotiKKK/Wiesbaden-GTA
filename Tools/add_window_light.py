"""Gibt den Fassaden-Materialien beleuchtete Fenster.

Die Fenster der Stadt sind KEINE Geometrie: der BuildingGenerator legt die
Fassaden-UVs so an, dass U in Metern entlang der Wand und V in Geschossen
laeuft, und das Material zeichnet daraus ein Fensterraster (siehe
build_materials.add_facade_windows). Genau diese Maske nutzt dieses Skript ein
zweites Mal - als Emission.

Warum nicht jedes Fenster leuchtet: ein Haus, in dem alle Fenster gleich hell
sind, sieht aus wie ein Bueroklotz im Probebetrieb. Ein billiger Hash aus
Fensterachse und Geschoss entscheidet je Fenster, ob es an ist; die Schwelle
kommt aus dem Parameter FensterlichtStaerke, den der C++-Code nach Uhrzeit und
Nutzungsart setzt. Bei 0 ist alles dunkel, bei 1 leuchtet fast alles.

Aufruf:
  UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<diese Datei>
"""

import unreal

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
MP = unreal.MaterialProperty

MAT_DIR = "/Game/Materials/City"
RESULT = r"C:/freebuff/WiesbadenReal_Sicherung/WiesbadenReal/Saved/window_light.txt"

# Die Fassaden, die der gebackene Stadtbau tatsaechlich benutzt. Nachgesehen
# im Spiel (Protokoll der zugewiesenen Materialien), nicht geraten - es gibt im
# Projekt eine ZWEITE Fassadenfamilie (MI_WbFacade_* auf M_WbSurface), die auf
# der gebackenen Karte nirgends vorkommt.
FACADES = [
    "M_WbFacade_Putz",
    "M_WbFacade_Beton",
    "M_WbFacade_Glas",
    "M_WbFacade_Backstein",
    "M_WbFacade_Sandstein",
    "M_WbFacade_Fachwerk",
]

PARAM_NAME = "FensterlichtStaerke"

# Fensterraster - MUSS zu build_materials.add_facade_windows passen, sonst
# leuchtet das Licht neben dem Fenster.
BAY_METERS = 2.6
WINDOW_U_LOW, WINDOW_U_HIGH = 0.30, 0.74
WINDOW_V_LOW, WINDOW_V_HIGH = 0.34, 0.82


def expr(mat, cls, x, y):
    return MEL.create_material_expression(mat, cls, x, y)


def conn(a, ao, b, bi):
    MEL.connect_material_expressions(a, ao, b, bi)


def c1(mat, v, x, y):
    n = expr(mat, unreal.MaterialExpressionConstant, x, y)
    n.set_editor_property("r", v)
    return n


def c3(mat, r, g, b, x, y):
    n = expr(mat, unreal.MaterialExpressionConstant3Vector, x, y)
    n.set_editor_property("constant", unreal.LinearColor(r, g, b, 1.0))
    return n


def _binary(mat, cls, a, b, x, y, ao="", bo=""):
    """Zwei Eingaenge, jeder wahlweise ein Ausdruck ODER eine Zahl.

    Beide Seiten muessen Zahlen erlauben: band() rechnet "high - value", die
    Konstante steht dort LINKS. Nur B als Zahl zuzulassen bricht genau dort.
    """
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


def channel(mat, source, ch, x, y):
    n = expr(mat, unreal.MaterialExpressionComponentMask, x, y)
    n.set_editor_property("r", ch == "r")
    n.set_editor_property("g", ch == "g")
    n.set_editor_property("b", False)
    n.set_editor_property("a", False)
    conn(source, "", n, "")
    return n


def band(mat, value, low, high, x, y, sharpness=40.0):
    lower = sat(mat, mul(mat, sub(mat, value, low, x, y), sharpness, x + 150, y), x + 300, y)
    upper = sat(mat, mul(mat, sub(mat, high, value, x, y + 150), sharpness, x + 150, y + 150),
                x + 300, y + 150)
    return mul(mat, lower, upper, x + 450, y + 75)


def build_window_light(mat, log):
    """Haengt das Fensterlicht an den Emissive-Ausgang. Gibt True bei Erfolg."""
    y0 = 2600   # weit unter dem bestehenden Graphen, damit nichts ueberdeckt wird

    strength = expr(mat, unreal.MaterialExpressionScalarParameter, -2600, y0)
    strength.set_editor_property("parameter_name", PARAM_NAME)
    strength.set_editor_property("default_value", 0.0)   # aus, bis C++ es setzt

    uv = expr(mat, unreal.MaterialExpressionTextureCoordinate, -2600, y0 + 200)
    u = channel(mat, uv, "r", -2400, y0 + 150)
    v = channel(mat, uv, "g", -2400, y0 + 350)

    bay = div(mat, u, BAY_METERS, -2200, y0 + 150)

    # Dieselbe Fensteroeffnung wie in der Basisfarbe.
    window = mul(mat,
                 band(mat, frac(mat, bay, -2050, y0 + 150), WINDOW_U_LOW, WINDOW_U_HIGH,
                      -1900, y0 + 100),
                 band(mat, frac(mat, v, -2050, y0 + 350), WINDOW_V_LOW, WINDOW_V_HIGH,
                      -1900, y0 + 400),
                 -1300, y0 + 250)

    # Hash je Fenster aus Achsen- und Geschossnummer. frac(sin(...)) waere
    # teurer; dieses Produkt streut fuer ein Fensterraster gut genug und ist
    # ueber die ganze Stadt reproduzierbar.
    bay_i = floor_(mat, bay, -2200, y0 + 600)
    floor_i = floor_(mat, v, -2200, y0 + 800)
    h1 = mul(mat, bay_i, 0.7548776, -2000, y0 + 600)
    h2 = mul(mat, floor_i, 0.5698402, -2000, y0 + 800)
    h3 = mul(mat, bay_i, floor_i, -2000, y0 + 1000)
    hsum = add(mat, add(mat, h1, h2, -1800, y0 + 700), mul(mat, h3, 0.1372, -1800, y0 + 1000),
               -1650, y0 + 800)
    hash_val = frac(mat, mul(mat, hsum, 43.758, -1500, y0 + 800), -1350, y0 + 800)

    # An, wenn der Hash unter der Schwelle liegt. Die harte Kante ist gewollt:
    # ein Fenster ist an oder aus, nicht halb.
    lit = sat(mat, mul(mat, sub(mat, strength, hash_val, -1200, y0 + 800), 60.0,
                       -1050, y0 + 800), -900, y0 + 800)

    # Warmes Innenlicht. Die Staerke geht ZWEIMAL ein: einmal ueber die
    # Schwelle (wie viele Fenster), einmal als Helligkeit (wie hell) - abends
    # sollen es nicht nur mehr, sondern auch hellere Fenster sein.
    warm = c3(mat, 1.0, 0.82, 0.52, -1200, y0 + 1200)
    brightness = mul(mat, strength, 2.2, -1200, y0 + 1400)

    emissive = mul(mat,
                   mul(mat, window, lit, -700, y0 + 500),
                   mul(mat, warm, brightness, -700, y0 + 1300),
                   -400, y0 + 900)

    ok = MEL.connect_material_property(emissive, "", MP.MP_EMISSIVE_COLOR)
    log.append(f"   Emissiv verbunden: {ok}")
    return ok


def main():
    log = []
    changed = 0

    for name in FACADES:
        path = f"{MAT_DIR}/{name}"
        mat = EAL.load_asset(path)
        if mat is None:
            log.append(f"{name}: NICHT GEFUNDEN")
            continue

        existing = [str(n) for n in MEL.get_scalar_parameter_names(mat)]
        if PARAM_NAME in existing:
            log.append(f"{name}: hat {PARAM_NAME} bereits - uebersprungen")
            continue

        log.append(f"{name}: Fensterlicht wird ergaenzt")
        if build_window_light(mat, log):
            MEL.recompile_material(mat)
            EAL.save_asset(path)
            changed += 1

            # Gegenprobe: der Parameter MUSS danach am Material haengen, sonst
            # laeuft der C++-Code ins Leere und faerbt gar nichts.
            after = [str(n) for n in MEL.get_scalar_parameter_names(mat)]
            log.append(f"   Skalar-Parameter danach: {after}")
            if PARAM_NAME not in after:
                log.append("   FEHLER: Parameter fehlt trotz Aufbau")
        else:
            log.append("   FEHLER: Emissiv-Ausgang nicht verbunden")

    log.append(f"Fertig: {changed} Fassaden geaendert.")
    open(RESULT, "w", encoding="utf-8").write("\n".join(log))
    for line in log:
        unreal.log(line)


main()
