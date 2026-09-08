"""Legt die Materialien fuer vertexgefaerbte Geometrie an.

Anlass: Die Nerobergbahn-Trasse und der Garten von Nerotal 48 faerben ihre
Geometrie ueber VERTEXFARBEN - Schotterbett gegen Schiene, Fliese gegen
Sandstein gegen Palmwedel. Beide benutzten bislang
`/Engine/BasicShapes/BasicShapeMaterial`.

Dieses Material wertet Vertexfarben ueberhaupt nicht aus. Die Trasse waere
damit ein einziges graues Band den Berg hinauf gewesen und der Garten ein
grauer Klotz - der Kommentar im Bahn-Quelltext behauptete sogar das Gegenteil.

Angelegt werden zwei Materialien:

  M_WbVertexFarbe        - matt, fuer Stein, Holz, Blattwerk
  M_WbVertexFarbeWasser  - durchscheinend und glaenzend, fuer den Pool

Aufruf:
  UnrealEditor-Cmd.exe WiesbadenReal.uproject -run=pythonscript
      -script=Tools/build_vertexcolor_material.py -unattended -nop4
"""

import unreal

MAT_DIR = "/Game/Materials/City"

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
MP = unreal.MaterialProperty


def log(msg):
    unreal.log("###WBMAT### %s" % msg)


def new_material(name):
    path = "%s/%s" % (MAT_DIR, name)
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    return unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())


def expr(mat, cls, x, y):
    return MEL.create_material_expression(mat, cls, x, y)


def constant(mat, value, x, y):
    node = expr(mat, unreal.MaterialExpressionConstant, x, y)
    node.set_editor_property("r", value)
    return node


# -- 1) Mattes Material ------------------------------------------------------
mat = new_material("M_WbVertexFarbe")
if mat is None:
    log("ABBRUCH: M_WbVertexFarbe liess sich nicht anlegen.")
    raise SystemExit(1)

# Zweiseitig: Die Trassen-Baender (Schotterbett/Boeschung) sind duenne Streifen,
# deren sichtbare Flaeche je nach Blickwinkel die Rueckseite ist. Einseitig
# gecullt zeigte die Trasse dann das dunkle Innere -> "schwarze Decke" den Berg
# hinauf. Zweiseitig rendert die Flaeche aus jedem Winkel mit der Ober-Vertexfarbe.
mat.set_editor_property("two_sided", True)

vertex = expr(mat, unreal.MaterialExpressionVertexColor, -400, 0)
# WURZELFIX schwarze Trasse/Garten: Der RGBA-Ausgang von VertexColor heisst ""
# (leer), NICHT "RGB" - es gibt keinen Ausgang namens "RGB". Mit "RGB" schlug die
# Verbindung STILL fehl, BaseColor blieb unverbunden -> die Flaeche rendert SCHWARZ
# (unverbundene BaseColor = schwarz). Deshalb leerer Ausgangsname; Erfolg pruefen.
if not MEL.connect_material_property(vertex, "", MP.MP_BASE_COLOR):
    log("ABBRUCH: VertexColor -> BaseColor liess sich NICHT verbinden.")
    raise SystemExit(1)
log("VertexColor -> BaseColor verbunden (Ausgang '').")

# Matte Oberflaeche mit konstanter Rauheit.
#
# Frueher wurde die Rauheit aus der Helligkeit abgeleitet (dunkle Flaechen
# stumpfer, helle glatter) - ueber einen DotProduct-Knoten. Der liess sich per
# Python-API in UE 5.8 NICHT korrekt verdrahten: Input A blieb offen, die
# Kompilierung brach mit "Missing DotProduct input A" ab, das GANZE Material
# fiel auf das Default-Material zurueck und der Garten war grau (im Spiel-Log
# als "Failed to compile Material ... Default Material will be used").
#
# Eine konstante, matte Rauheit kompiliert zuverlaessig; der optische Verlust
# ist gering, weil Stein, Holz und Blattwerk ohnehin alle matt sind. Die
# Vertexfarben (der eigentliche Zweck) bleiben unveraendert.
MEL.connect_material_property(constant(mat, 0.85, -250, 140), "", MP.MP_ROUGHNESS)

MEL.recompile_material(mat)
EAL.save_loaded_asset(mat)
log("M_WbVertexFarbe angelegt.")


# -- 2) Wasser ---------------------------------------------------------------
water = new_material("M_WbVertexFarbeWasser")
if water is None:
    log("ABBRUCH: M_WbVertexFarbeWasser liess sich nicht anlegen.")
    raise SystemExit(1)

# Durchscheinend, damit der Beckenboden durchkommt. Ohne das waere der Pool
# eine blaue Platte - und die Fliesen darunter waeren umsonst gebaut.
water.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
water.set_editor_property("two_sided", True)

water_vertex = expr(water, unreal.MaterialExpressionVertexColor, -400, 0)
# Gleicher Fix wie oben: RGBA-Ausgang heisst "" (nicht "RGB").
if not MEL.connect_material_property(water_vertex, "", MP.MP_BASE_COLOR):
    log("ABBRUCH: Wasser VertexColor -> BaseColor liess sich NICHT verbinden.")
    raise SystemExit(1)

# Wasser ist glatt und nicht metallisch; die Spiegelung macht die Oberflaeche.
MEL.connect_material_property(constant(water, 0.05, -250, 120), "", MP.MP_ROUGHNESS)
MEL.connect_material_property(constant(water, 0.72, -250, 200), "", MP.MP_OPACITY)
MEL.connect_material_property(constant(water, 1.0, -250, 280), "", MP.MP_SPECULAR)

MEL.recompile_material(water)
EAL.save_loaded_asset(water)
log("M_WbVertexFarbeWasser angelegt.")

log("FERTIG.")
