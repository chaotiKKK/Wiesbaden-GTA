"""Importiert die Innenraum-Texturen und baut je Flaechenart ein Material.

Anlass: Der Bus-Innenraum war nur vertexgefaerbt (einfarbige Flaechen, kein
Textur-Asset) - "im Bus sind keine Texturen". Jetzt bekommt jede Flaechenart
(Boden/Sitz/Wand/Decke/Technik) eine kachelnde Textur; das Material
multipliziert sie mit der Vertexfarbe:

    T_WbBusInt<Art> -> MaterialExpressionTextureSample
                    -> Multiply(A = Textur, B = VertexColor)
                    -> BaseColor

Die Farbe bleibt damit die Vertexfarbe (ESWE-Gelb der Haltestangen, blauer
Sitzstoff), die Textur liefert nur die Oberflaeche.

Wichtig beim Verdrahten (aus dem Vertexfarben-Material gelernt): Der RGBA-Ausgang
der VertexColor-Node heisst "" (leer), NICHT "RGB" - mit "RGB" schlaegt
connect_material_property STILL fehl und BaseColor bleibt schwarz. Dieses Skript
prueft deshalb JEDE Verbindung und bricht mit Fehler ab, wenn eine nicht greift.

Aufruf (headless, ueber Tools/import_bus_interior.cmd):
  UnrealEditor-Cmd.exe WiesbadenReal.uproject -run=pythonscript
      -script=Tools/import_bus_interior.py -stdout -unattended -nopause -nosplash -nop4
"""
import os

import unreal

SRC = os.path.join(unreal.Paths.project_dir(), "Content", "Vehicles", "Bus", "Interior", "Source")
DEST = "/Game/Vehicles/Bus/Interior"

# Reihenfolge = Mesh-Abschnitt (WiesbadenBusInterior::ETile).
TILES = [
    ("T_WbBusIntBoden", "M_WbBusIntBoden"),
    ("T_WbBusIntSitz", "M_WbBusIntSitz"),
    ("T_WbBusIntWand", "M_WbBusIntWand"),
    ("T_WbBusIntDecke", "M_WbBusIntDecke"),
    ("T_WbBusIntTechnik", "M_WbBusIntTechnik"),
]

EAL = unreal.EditorAssetLibrary
ATH = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
MP = unreal.MaterialProperty


def log(msg):
    unreal.log("###WBINT### %s" % msg)


def wire(from_node, from_output, to_node, to_input, what):
    """Verbindet zwei Knoten und prueft das Ergebnis - stilles Scheitern war der
    Fehler im Vertexfarben-Material (schwarze Flaechen)."""
    if not MEL.connect_material_expressions(from_node, from_output, to_node, to_input):
        log("FEHLER: %s liess sich NICHT verbinden." % what)
        return False
    return True


def import_texture(name):
    path = os.path.join(SRC, name + ".png")
    if not os.path.exists(path):
        log("FEHLER: %s fehlt - Tools/make_bus_interior_textures.py laufen lassen." % path)
        return None
    task = unreal.AssetImportTask()
    task.filename = path
    task.destination_path = DEST
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = True
    ATH.import_asset_tasks([task])
    tex = EAL.load_asset("%s/%s" % (DEST, name))
    if tex is None:
        log("FEHLER: %s wurde nicht importiert." % name)
        return None
    # sRGB: die Texturen sind Helligkeitsmuster, keine Datenmaps.
    tex.set_editor_property("srgb", True)
    # Kacheln ausdruecklich einschalten: der Innenraum rechnet seine UVs in
    # Kachelweiten (eine Kachel = 200 cm), eine geclampte Textur wuerde die
    # langen Flaechen (Fussboden 8 m) zu einer einzigen verschmierten Kopie machen.
    for prop, value in (("address_x", unreal.TextureAddress.TA_WRAP),
                        ("address_y", unreal.TextureAddress.TA_WRAP)):
        try:
            tex.set_editor_property(prop, value)
        except Exception as exc:
            log("Hinweis: %s.%s nicht gesetzt (%s)" % (name, prop, exc))
    EAL.save_loaded_asset(tex)
    return tex


def build_material(matname, tex):
    mpath = "%s/%s" % (DEST, matname)
    if EAL.does_asset_exist(mpath):
        EAL.delete_asset(mpath)
    mat = ATH.create_asset(matname, DEST, unreal.Material, unreal.MaterialFactoryNew())
    if mat is None:
        log("FEHLER: %s liess sich nicht anlegen." % matname)
        return None

    # Zweiseitig wie das Vertexfarben-Material: die Wandstuecke sind duenne
    # Kaesten, die je nach Blickwinkel von innen auf ihre Rueckseite zeigen.
    mat.set_editor_property("two_sided", True)

    sample = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureSample, -600, 0)
    sample.set_editor_property("texture", tex)
    vertex = MEL.create_material_expression(mat, unreal.MaterialExpressionVertexColor, -600, 260)
    mul = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -300, 60)
    rough = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, -300, 340)
    rough.set_editor_property("r", 0.85)

    ok = wire(sample, "RGB", mul, "A", "%s: Textur -> Multiply.A" % matname)
    ok &= wire(vertex, "", mul, "B", "%s: VertexColor -> Multiply.B" % matname)
    if not MEL.connect_material_property(mul, "", MP.MP_BASE_COLOR):
        log("FEHLER: %s: Multiply -> BaseColor liess sich NICHT verbinden." % matname)
        ok = False
    MEL.connect_material_property(rough, "", MP.MP_ROUGHNESS)

    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)

    # Gegenprobe: welcher Knoten haengt wirklich an BaseColor? (Die Verbindung
    # oben kann nur scheitern, wenn die API etwas anderes tut als erwartet.)
    try:
        node = MEL.get_material_property_input_node(mat, MP.MP_BASE_COLOR)
        log("%s: BaseColor <- %s" % (matname, node.get_class().get_name() if node else "NICHTS"))
    except Exception as exc:
        log("%s: BaseColor-Gegenprobe nicht moeglich (%s)" % (matname, exc))
    return mat if ok else None


ok = 0
for texname, matname in TILES:
    tex = import_texture(texname)
    if tex is None:
        continue
    mat = build_material(matname, tex)
    if mat is None:
        continue
    ok += 1
    log("%s %dx%d -> %s" % (texname, tex.blueprint_get_size_x(), tex.blueprint_get_size_y(), matname))

log("ENDE ok=%d/%d" % (ok, len(TILES)))
