# Beruhigt den harten Blau-Grid-Eindruck der Shader-Fenster aus der Ferne.
#
# add_facade_windows setzt die Fenster als dunkles, blaustichiges Glas
# (0.020, 0.028, 0.038) in ein regelmaessiges Raster ueber die helle Putzwand.
# Von oben/aus der Distanz liest sich dieses Raster als hartes blaues Gitter -
# die Stadt wirkt aus der Luft unruhig.
#
# Statt die Fenster generell zu entschaerfen (Nah-Detail geht verloren) wird
# der FERTIGE Basisfarbwert je nach Kameraentfernung (PixelDepth) zu einer
# kontrastarmen Fassung angehoben: die dunklen Fensterfelder wandern zur
# Wandhelligkeit, das Gitter loest sich mit der Entfernung auf. Nah bleibt
# alles unveraendert. Der Graph wird NICHT neu gebaut (das wuerde die
# AAA-Verdrahtung zuruecksetzen) - nur der Basisfarb-Ausgang umschlungen.
#
# Aufruf (Editor oder -run=pythonscript):
#   UnrealEditor-Cmd.exe WiesbadenReal.uproject -run=pythonscript
#       -script="Tools/soften_windows_distance.py" -unattended -nosplash -nop4

import os

import unreal

mel = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
MP = unreal.MaterialProperty
CITY = "/Game/Materials/City/"

# Fassaden mit Shader-Fenster-Raster (blaues Gitter). Glas/Beton (Foto-/
# Vorhangfassade) bleiben aussen vor.
MATS = [
    "M_WbBuildingWall",
    "M_WbFacade_Putz",
    "M_WbFacade_Backstein",
    "M_WbFacade_Sandstein",
]

NEAR_CM = 12000.0    # ab hier beginnt die Beruhigung (120 m)
FAR_CM = 48000.0     # ab hier voll wirksam (480 m)
MAX_FADE = 0.75      # wie stark die Ferne beruhigt (0..1)
LIFT_SCALE = 0.38    # kontrastarme Fassung: base*Scale + Offset
LIFT_OFFSET = 0.42


def log(msg):
    unreal.log("###WBWIN### %s" % msg)


def make(mat, cls, x, y):
    return mel.create_material_expression(mat, cls, x, y)


def process(mat_name):
    mat = unreal.load_asset(CITY + mat_name)
    if not mat:
        log("WARNUNG: %s fehlt" % mat_name)
        return False

    bc = mat.get_editor_property("base_color")
    src = bc.get_editor_property("expression") if bc else None
    if src is None:
        log("WARNUNG: %s ohne Basisfarb-Ausdruck - uebersprungen" % mat_name)
        return False

    x0, y0 = -260, 1700

    # Entfernungsfaktor: saturate((PixelDepth - NEAR)/(FAR-NEAR)) * MAX_FADE.
    depth = make(mat, unreal.MaterialExpressionPixelDepth, x0 - 900, y0)
    sub = make(mat, unreal.MaterialExpressionSubtract, x0 - 750, y0)
    mel.connect_material_expressions(depth, "", sub, "A")
    sub.set_editor_property("const_b", NEAR_CM)
    dv = make(mat, unreal.MaterialExpressionDivide, x0 - 600, y0)
    mel.connect_material_expressions(sub, "", dv, "A")
    dv.set_editor_property("const_b", max(FAR_CM - NEAR_CM, 1.0))
    clamp = make(mat, unreal.MaterialExpressionClamp, x0 - 450, y0)
    mel.connect_material_expressions(dv, "", clamp, "")
    fade = make(mat, unreal.MaterialExpressionMultiply, x0 - 300, y0)
    mel.connect_material_expressions(clamp, "", fade, "A")
    fade.set_editor_property("const_b", MAX_FADE)

    # Kontrastarme Fassung derselben Farbe: base*LIFT_SCALE + LIFT_OFFSET.
    scaled = make(mat, unreal.MaterialExpressionMultiply, x0 - 300, y0 - 220)
    mel.connect_material_expressions(src, "", scaled, "A")
    scaled.set_editor_property("const_b", LIFT_SCALE)
    calm = make(mat, unreal.MaterialExpressionAdd, x0 - 150, y0 - 220)
    mel.connect_material_expressions(scaled, "", calm, "A")
    calm.set_editor_property("const_b", LIFT_OFFSET)

    # Ergebnis: lerp(base, calm, fade) -> Basisfarbe.
    result = make(mat, unreal.MaterialExpressionLinearInterpolate, x0, y0 - 110)
    mel.connect_material_expressions(src, "", result, "A")
    mel.connect_material_expressions(calm, "", result, "B")
    mel.connect_material_expressions(fade, "", result, "Alpha")
    mel.connect_material_property(result, "", MP.MP_BASE_COLOR)

    mel.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    log("%s: Fenster-Beruhigung ab %.0f m (voll ab %.0f m, max %.0f%%)."
        % (mat_name, NEAR_CM / 100.0, FAR_CM / 100.0, MAX_FADE * 100.0))
    return True


def main():
    done = 0
    for name in MATS:
        if process(name):
            done += 1
    try:
        with open(os.path.join(unreal.Paths.project_dir(), "Saved", "windows_done.txt"),
                  "w", encoding="utf-8") as f:
            f.write("ok\n")
    except OSError as exc:
        log("Sentinel nicht geschrieben: %s" % exc)
    log("FERTIG: %d von %d Fassaden beruhigt." % (done, len(MATS)))
    if os.environ.get("WB_QUIT"):
        unreal.SystemLibrary.quit_editor()


main()
