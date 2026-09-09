# Nerobergbahn-Trasse "schwarze Decke": M_WbVertexFarbe war einseitig -> die
# duennen Trassen-Baender zeigten von oben ihre gecullte Rueckseite (dunkles
# Inneres). Zweiseitig setzen, damit die Flaeche aus jedem Winkel mit der
# Ober-Vertexfarbe rendert. Idempotent; headless:
#   UnrealEditor-Cmd.exe <proj> -run=pythonscript -script="Tools/fix_trackmat_twosided.py"
import unreal, os

ROOT = r"C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal"
RESULT = os.path.join(ROOT, "fix_trackmat.result.txt")
PATH = "/Game/Materials/City/M_WbVertexFarbe"

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
lines = []
def note(s):
    lines.append(s); unreal.log("###TRACKMAT### " + s)

if not EAL.does_asset_exist(PATH):
    note("FEHLT: %s" % PATH)
else:
    mat = unreal.load_asset(PATH)
    before = mat.get_editor_property("two_sided")
    mat.set_editor_property("two_sided", True)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    after = unreal.load_asset(PATH)
    note("M_WbVertexFarbe two_sided %s -> %s" % (before, after.get_editor_property("two_sided")))

with open(RESULT, "w", encoding="utf-8") as f:
    f.write("\n".join(lines) + "\n")
unreal.log("###TRACKMAT### fertig -> " + RESULT)
