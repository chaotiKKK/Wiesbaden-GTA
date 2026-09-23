"""Erzeugt die Lackierung des ZWEITEN Hubschraubers.

Warum ueberhaupt - gemessen, nicht vermutet:

Der Ka-52 traegt auf allen zwoelf Schlitzen M_Ka52PBR mit vier echten
Texturen (basecolor, metallic, normal, roughness). Der zweite Hubschrauber
trug dagegen:
  - am Rumpf  M_WbHelicopter  - eine flache Farbe mit T_WbNoise,
  - an den Rotoren M_HeliRotorBase - das glTF-Standardmaterial des Imports,
    also weiss/generisch (T_White_Linear, T_Generic_N).
Das ist der graue Platzhalter, von dem der Auftrag spricht.

Warum keine Fototextur:

Das alte Landmarken-Modell ist ein glTF-Import ohne verlaessliches UV-Layout.
Eine aufgezogene Fototextur wuerde dort verzerren. Die Lackierung entsteht
darum aus der GEOMETRIE: der Normalenvektor im OBJEKTraum trennt Oberseite
von Bauch - das ist genau die Zweifarbigkeit, die ein Rettungshubschrauber
im Original hat, und sie bleibt beim Rollen und Nicken an Ort und Stelle
(im Weltraum gerechnet wuerde die Farbe mit der Fluglage wandern).

Die zweite Maschine wird zivil lackiert, damit die beiden im Bild nicht zu
verwechseln sind: der Ka-52 ist militaerisch dunkel, diese hier meldegelb
mit dunklem Bauch.

Aufruf:
  UnrealEditor.exe WiesbadenReal.uproject
      -ExecCmds="py Tools/create_helicopter_materials.py" -unattended -nosplash -nop4
"""

import unreal

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
MP = unreal.MaterialProperty
MAT_DIR = "/Game/Materials/City"


def log(m):
    unreal.log("###HELIMAT### %s" % m)


def neu(name):
    pfad = "%s/%s" % (MAT_DIR, name)
    if EAL.does_asset_exist(pfad):
        EAL.delete_asset(pfad)
    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
    if mat is None:
        raise SystemExit("FEHLER: %s konnte nicht erstellt werden" % pfad)

    # NANITE-FLAG. Ohne das ersetzt Unreal das Material auf einem Nanite-Mesh
    # durch das Default-Material - also durch genau das Grau, das hier
    # verschwinden soll. Im Log steht dann nur eine leise Warnung:
    #   "Material ... missing usage flag Nanite! Default Material will be used"
    # Gemessen beim ersten Versuch: die Lackierung war erzeugt, gespeichert und
    # zugewiesen - und im Bild trotzdem grau. Derselbe Fehler ist am Bus schon
    # einmal aufgetreten (Tools/fix_bus_materials.py).
    try:
        mat.set_editor_property("used_with_nanite", True)
    except Exception as exc:
        log("WARNUNG: Nanite-Flag nicht setzbar (%s) - Material bliebe grau." % exc)
    return mat, pfad


def macher(mat):
    def expr(cls, x, y):
        return MEL.create_material_expression(mat, cls, x, y)

    def c3(r, g, b, x, y):
        n = expr(unreal.MaterialExpressionConstant3Vector, x, y)
        n.set_editor_property("constant", unreal.LinearColor(r, g, b, 1.0))
        return n

    def c1(v, x, y):
        n = expr(unreal.MaterialExpressionConstant, x, y)
        n.set_editor_property("r", v)
        return n

    return expr, c3, c1


def oben_unten(mat, expr):
    """0 am Bauch, 1 auf der Oberseite - im OBJEKTraum, nicht im Weltraum.

    Im Weltraum gerechnet wanderte die Farbgrenze mit der Fluglage: beim
    Rollen waere die gelbe Oberseite ploetzlich an der Seite.
    """
    normal = expr(unreal.MaterialExpressionVertexNormalWS, -1500, 0)
    quelle = normal
    try:
        dreh = expr(unreal.MaterialExpressionTransform, -1250, 0)
        dreh.set_editor_property(
            "transform_source_type",
            unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_WORLD)
        dreh.set_editor_property(
            "transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_LOCAL)
        MEL.connect_material_expressions(normal, "", dreh, "")
        quelle = dreh
    except Exception as e:
        # Lieber die Weltnormale als gar keine Zweifarbigkeit - aber sagen,
        # dass die Farbgrenze dann mit der Fluglage wandert.
        log("WARNUNG: Objektraum-Normale nicht verfuegbar (%s) - Weltnormale." % e)

    # Z-Anteil der Normale, von -1..1 auf 0..1 gelegt.
    maske = expr(unreal.MaterialExpressionComponentMask, -1000, 0)
    maske.set_editor_property("r", False)
    maske.set_editor_property("g", False)
    maske.set_editor_property("b", True)
    maske.set_editor_property("a", False)
    MEL.connect_material_expressions(quelle, "", maske, "")

    halb = expr(unreal.MaterialExpressionMultiply, -850, 0)
    MEL.connect_material_expressions(maske, "", halb, "A")
    const = expr(unreal.MaterialExpressionConstant, -1000, 140)
    const.set_editor_property("r", 0.5)
    MEL.connect_material_expressions(const, "", halb, "B")

    versatz = expr(unreal.MaterialExpressionAdd, -700, 0)
    MEL.connect_material_expressions(halb, "", versatz, "A")
    const2 = expr(unreal.MaterialExpressionConstant, -850, 140)
    const2.set_editor_property("r", 0.5)
    MEL.connect_material_expressions(const2, "", versatz, "B")

    # Harte Kante statt weichem Verlauf: eine Lackierung hat eine Trennlinie,
    # keinen Farbverlauf. 0,42..0,58 laesst genug Weichzeichnung, damit die
    # Kante bei grober Geometrie nicht ausfranst.
    kante = expr(unreal.MaterialExpressionSmoothStep, -550, 0)
    kante.set_editor_property("const_min", 0.42)
    kante.set_editor_property("const_max", 0.58)
    MEL.connect_material_expressions(versatz, "", kante, "Value")
    return kante


# ---------------------------------------------------------------- Rumpf
mat, pfad = neu("M_WbHeliCivil")
expr, c3, c1 = macher(mat)

hoehe = oben_unten(mat, expr)

# Leichte Tonschwankung ueber weltbezogenes Rauschen - eine Lackflaeche ohne
# jede Variation wirkt wie Plastik.
noise = expr(unreal.MaterialExpressionNoise, -1500, 450)
noise.set_editor_property("scale", 0.012)
noise.set_editor_property("output_min", 0.88)
noise.set_editor_property("output_max", 1.0)

gelb_hell = c3(0.86, 0.60, 0.03, -1000, 320)    # Meldegelb, Oberseite
bauch = c3(0.055, 0.060, 0.070, -1000, 620)     # dunkles Graphit, Bauch

lack = expr(unreal.MaterialExpressionLinearInterpolate, -300, 400)
MEL.connect_material_expressions(bauch, "", lack, "A")
MEL.connect_material_expressions(gelb_hell, "", lack, "B")
MEL.connect_material_expressions(hoehe, "", lack, "Alpha")

getoent = expr(unreal.MaterialExpressionMultiply, -120, 400)
MEL.connect_material_expressions(lack, "", getoent, "A")
MEL.connect_material_expressions(noise, "", getoent, "B")
MEL.connect_material_property(getoent, "", MP.MP_BASE_COLOR)

# Lack glaenzt, der Bauch ist matter (Abrieb, Staub).
rauh = expr(unreal.MaterialExpressionLinearInterpolate, -300, 900)
MEL.connect_material_expressions(c1(0.52, -1000, 860), "", rauh, "A")
MEL.connect_material_expressions(c1(0.22, -1000, 1000), "", rauh, "B")
MEL.connect_material_expressions(hoehe, "", rauh, "Alpha")
MEL.connect_material_property(rauh, "", MP.MP_ROUGHNESS)

MEL.connect_material_property(c1(0.08, -300, 1150), "", MP.MP_METALLIC)
MEL.connect_material_property(c1(0.65, -300, 1260), "", MP.MP_SPECULAR)

MEL.recompile_material(mat)
EAL.save_loaded_asset(mat)
log("FERTIG: %s" % pfad)

# ---------------------------------------------------------------- Rotor
mat2, pfad2 = neu("M_WbHeliRotor")
expr2, c3_2, c1_2 = macher(mat2)

# Rotorblaetter sind fast schwarz mit hellen Spitzenmarkierungen - hier ohne
# Markierung, aber mit dem leichten Glanz von lackiertem Verbundwerkstoff.
MEL.connect_material_property(c3_2(0.022, 0.023, 0.026, -500, 0), "", MP.MP_BASE_COLOR)
MEL.connect_material_property(c1_2(0.34, -500, 200), "", MP.MP_ROUGHNESS)
MEL.connect_material_property(c1_2(0.25, -500, 340), "", MP.MP_METALLIC)
MEL.connect_material_property(c1_2(0.55, -500, 480), "", MP.MP_SPECULAR)

MEL.recompile_material(mat2)
EAL.save_loaded_asset(mat2)
log("FERTIG: %s" % pfad2)

if unreal.SystemLibrary.get_command_line().find("-unattended") >= 0:
    unreal.SystemLibrary.quit_editor()
