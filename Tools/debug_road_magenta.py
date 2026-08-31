"""
Faerbt M_WbRoad leuchtend magenta - reines Diagnose-Werkzeug.

Zweck: Die Fahrbahn ist im Spiel unsichtbar, obwohl Geometrie, Hoehenlage,
Material, Normalen und Dreiecks-Wicklung nachweislich stimmen. Eine
unuebersehbare Farbe trennt die verbleibenden Faelle in EINEM Lauf:

  - Boden wird magenta  -> die grosse gruene Flaeche IST die Fahrbahn
  - magenta Baender     -> die Fahrbahn rendert, war nur schwer zu erkennen
  - kein magenta        -> die Fahrbahn wird tatsaechlich nicht gezeichnet

Das Material wird IN PLACE geaendert (Ausdruecke loeschen, neu verdrahten),
nicht geloescht und neu angelegt - so bleiben die Referenzen der 1984
gebackenen Chunk-Actors erhalten und ein 22-Minuten-Neubau entfaellt.

Rueckgaengig: Tools/build_materials.py erneut laufen lassen.
"""
import unreal

MEL = unreal.MaterialEditingLibrary
PATH = "/Game/Materials/City/M_WbRoad"

mat = unreal.EditorAssetLibrary.load_asset(PATH)
if mat is None:
    unreal.log_error("###WB### %s nicht gefunden" % PATH)
else:
    MEL.delete_all_material_expressions(mat)

    color = MEL.create_material_expression(
        mat, unreal.MaterialExpressionConstant3Vector, -400, 0)
    color.set_editor_property("constant", unreal.LinearColor(1.0, 0.0, 1.0, 1.0))
    MEL.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)

    # Selbstleuchtend, damit die Flaeche auch im Schatten nicht zu uebersehen ist.
    glow = MEL.create_material_expression(
        mat, unreal.MaterialExpressionConstant3Vector, -400, 250)
    glow.set_editor_property("constant", unreal.LinearColor(1.0, 0.0, 1.0, 1.0))
    MEL.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    MEL.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat)
    unreal.log("###WB### M_WbRoad auf Magenta gesetzt")
