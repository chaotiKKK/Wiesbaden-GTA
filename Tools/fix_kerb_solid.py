# Bordstein wieder solide: M_WbKerb war per build_streaming_fade.py auf
# BLEND_MASKED (DitherTemporalAA-Maske) gestellt - beim duennen, senkrechten
# Bordstein-Band stippelt der Dither und man schaut hindurch. Zuruecksetzen auf
# opak, zusaetzlich zweiseitig (falls das Kerb-Mesh nur eine Flaeche hat, waere es
# von hinten unsichtbar). Idempotent; laeuft headless:
#   UnrealEditor-Cmd.exe <proj> -run=pythonscript -script="Tools/fix_kerb_solid.py"
import unreal, os

ROOT = r"C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal"
RESULT = os.path.join(ROOT, "fix_kerb_solid.result.txt")
PATH = "/Game/Materials/City/M_WbKerb"

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary

lines = []
def note(s):
    lines.append(s)
    unreal.log("###KERB### " + s)

if not EAL.does_asset_exist(PATH):
    note("FEHLT: %s" % PATH)
else:
    mat = unreal.load_asset(PATH)
    before_bm = mat.get_editor_property("blend_mode")
    before_ts = mat.get_editor_property("two_sided")
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    mat.set_editor_property("two_sided", True)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    after = unreal.load_asset(PATH)
    note("M_WbKerb: blend %s->%s, two_sided %s->%s (jetzt %s/%s)" % (
        before_bm, after.get_editor_property("blend_mode"),
        before_ts, True,
        after.get_editor_property("blend_mode"),
        after.get_editor_property("two_sided")))

with open(RESULT, "w", encoding="utf-8") as f:
    f.write("\n".join(lines) + "\n")
unreal.log("###KERB### fertig -> " + RESULT)
