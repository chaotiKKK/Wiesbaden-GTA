"""Ersetzt AUSGEWAEHLTE Stadt-Materialien durch eine einfarbige Flaeche.

Diagnosewerkzeug fuer die Frage "welches Material kostet die Bildzeit".
`Tools/debug_colorize.py` faerbt alle auf einmal ein und beantwortet damit nur,
DASS die Materialien teuer sind. Um zu wissen, WELCHES, muss genau eines
weggenommen werden - und dann das naechste.

Auswahl ueber die Umgebungsvariable WB_FLAT, kommagetrennt:

    set WB_FLAT=M_WbTerrain
    UnrealEditor-Cmd.exe WiesbadenReal.uproject -run=pythonscript
        -script="Tools/debug_flatten_materials.py" -unattended -nosplash

Die Materialien werden IN PLACE umgeschrieben, damit die Verweise der
gebackenen Chunk-Actors erhalten bleiben - ein Neubau der Stadt ist nicht
noetig.

Rueckgaengig: Tools/build_materials.py erneut laufen lassen. Das ist kein
Nebensatz: Ohne diesen Lauf bleibt die Stadt einfarbig, und beim letzten Mal
ist der Rueckbau stumm fehlgeschlagen, weil das Skript ueber einen relativen
Pfad aufgerufen wurde. Zeitstempel der .uasset hinterher pruefen.
"""
import os

import unreal

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
MP = unreal.MaterialProperty

names = [n.strip() for n in os.environ.get("WB_FLAT", "").split(",") if n.strip()]
if not names:
    unreal.log_error("###WBFLAT### WB_FLAT ist leer - nichts zu tun.")

for name in names:
    path = "/Game/Materials/City/%s" % name
    mat = EAL.load_asset(path)
    if mat is None:
        unreal.log_error("###WBFLAT### %s nicht gefunden" % path)
        continue

    MEL.delete_all_material_expressions(mat)

    # Mittelgrau, matt. Bewusst NICHT selbstleuchtend: Der Lauf soll dieselbe
    # Beleuchtungsrechnung durchlaufen wie sonst, damit der Unterschied
    # wirklich nur die Knoten des Materials sind.
    base = MEL.create_material_expression(
        mat, unreal.MaterialExpressionConstant3Vector, -400, 0)
    base.set_editor_property("constant", unreal.LinearColor(0.18, 0.18, 0.18, 1.0))
    MEL.connect_material_property(base, "", MP.MP_BASE_COLOR)

    rough = MEL.create_material_expression(
        mat, unreal.MaterialExpressionConstant, -400, 250)
    rough.set_editor_property("r", 0.9)
    MEL.connect_material_property(rough, "", MP.MP_ROUGHNESS)

    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    unreal.log("###WBFLAT### %s eingeebnet" % name)

unreal.log("###WBFLAT### FERTIG (%d Material(ien))" % len(names))
