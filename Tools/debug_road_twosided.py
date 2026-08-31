"""Schaltet M_WbRoad beidseitig - Diagnose der Dreiecks-Wicklung."""
import unreal

mat = unreal.EditorAssetLibrary.load_asset("/Game/Materials/City/M_WbRoad")
if mat is None:
    unreal.log_error("###WB### M_WbRoad nicht gefunden")
else:
    mat.set_editor_property("two_sided", True)
    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat)
    unreal.log("###WB### M_WbRoad ist jetzt beidseitig")
