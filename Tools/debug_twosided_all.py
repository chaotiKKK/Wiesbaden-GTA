"""
Schaltet ALLE Stadt-Materialien beidseitig - Diagnose der Dreiecks-Wicklung.

Beantwortet in EINEM Lauf, ob eine Flaeche fehlt, weil sie gar nicht existiert,
oder weil Backface-Culling sie entfernt. Genau so wurde die falsche Wicklung im
Fahrbahn-Band gefunden: Geometrie, Material, Bounds und Sichtbarkeitsflags
waren alle sauber, nur die Indexreihenfolge zeigte nach unten.

Rueckgaengig: Tools/build_materials.py erneut laufen lassen.
"""
import unreal

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary

NAMES = [
    "M_WbRoad", "M_WbSidewalk", "M_WbKerb", "M_WbLaneMarking", "M_WbCycleway",
    "M_WbTerrain", "M_WbBuildingRoof", "M_WbBuildingWall",
    "M_WbFacade_Putz", "M_WbFacade_Backstein", "M_WbFacade_Sandstein",
    "M_WbFacade_Glas", "M_WbFacade_Beton", "M_WbFacade_Fachwerk",
]

for name in NAMES:
    mat = EAL.load_asset("/Game/Materials/City/%s" % name)
    if mat is None:
        unreal.log_warning("###WB### %s nicht gefunden" % name)
        continue
    mat.set_editor_property("two_sided", True)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    unreal.log("###WB### %s beidseitig" % name)

unreal.log("###WB### FERTIG")
