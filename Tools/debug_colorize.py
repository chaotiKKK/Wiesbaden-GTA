"""
Faerbt alle Stadt-Materialien in unverwechselbare Leuchtfarben - Diagnose.

Zweck: Im gerenderten Bild liess sich nicht sicher sagen, WELCHE Flaeche man
vor sich hat. Gelaende, Fahrbahn, Gehweg und Fassade sahen in gedeckten
Farbtoenen aehnlich aus, und daraus wurden falsche Schluesse gezogen. Mit je
einer gesaettigten Farbe je Material beantwortet ein einziger Screenshot,
was gezeichnet wird und was fehlt.

Die Materialien werden IN PLACE umgeschrieben, damit die Referenzen der
gebackenen Chunk-Actors erhalten bleiben (kein Neubau noetig).

Rueckgaengig: Tools/build_materials.py erneut laufen lassen.
"""
import unreal

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
MP = unreal.MaterialProperty

# Material -> (R, G, B). Bewusst maximal unterscheidbar gewaehlt.
COLORS = {
    "M_WbRoad":            (1.0, 0.0, 1.0),   # Magenta  - Fahrbahn
    "M_WbSidewalk":        (0.0, 1.0, 1.0),   # Cyan     - Gehweg
    "M_WbKerb":            (1.0, 1.0, 0.0),   # Gelb     - Bordstein
    "M_WbLaneMarking":     (1.0, 1.0, 1.0),   # Weiss    - Markierung
    "M_WbCycleway":        (1.0, 0.5, 0.0),   # Orange   - Radweg
    "M_WbTerrain":         (0.0, 0.0, 1.0),   # Blau     - Gelaende
    "M_WbBuildingRoof":    (0.0, 1.0, 0.0),   # Gruen    - Dach
    "M_WbBuildingWall":    (1.0, 0.0, 0.0),   # Rot      - Fassade
    "M_WbFacade_Putz":     (1.0, 0.0, 0.0),
    "M_WbFacade_Backstein":(1.0, 0.0, 0.0),
    "M_WbFacade_Sandstein":(1.0, 0.0, 0.0),
    "M_WbFacade_Glas":     (1.0, 0.0, 0.0),
    "M_WbFacade_Beton":    (1.0, 0.0, 0.0),
    "M_WbFacade_Fachwerk": (1.0, 0.0, 0.0),

    # Diese beiden fehlten - ausgerechnet die, um die es geht.
    #
    # M_WbUnpaved traegt mit 3,5 Millionen Dreiecken die groesste Flaeche der
    # Stadt (Asphalt: 0,95 Millionen), und ohne eigene Leuchtfarbe war im
    # Diagnosebild nicht zu unterscheiden, ob eine Flaeche unbefestigte
    # Fahrbahn, Boeschung oder Gelaende ist.
    "M_WbUnpaved":         (1.0, 0.35, 0.0),  # Orangebraun - unbefestigt
    "M_WbPavedStone":      (0.6, 0.0, 1.0),   # Violett     - Pflaster
}

for name, (r, g, b) in COLORS.items():
    path = "/Game/Materials/City/%s" % name
    mat = EAL.load_asset(path)
    if mat is None:
        unreal.log_warning("###WB### %s nicht gefunden" % path)
        continue

    MEL.delete_all_material_expressions(mat)

    base = MEL.create_material_expression(
        mat, unreal.MaterialExpressionConstant3Vector, -400, 0)
    base.set_editor_property("constant", unreal.LinearColor(r, g, b, 1.0))
    MEL.connect_material_property(base, "", MP.MP_BASE_COLOR)

    # Selbstleuchtend: die Flaeche ist damit auch im Schatten eindeutig.
    glow = MEL.create_material_expression(
        mat, unreal.MaterialExpressionConstant3Vector, -400, 250)
    glow.set_editor_property("constant", unreal.LinearColor(r, g, b, 1.0))
    MEL.connect_material_property(glow, "", MP.MP_EMISSIVE_COLOR)

    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    unreal.log("###WB### %s -> (%.1f, %.1f, %.1f)" % (name, r, g, b))

unreal.log("###WB### FERTIG")
