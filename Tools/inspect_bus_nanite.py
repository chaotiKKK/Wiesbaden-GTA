"""Prueft, WARUM Busse ohne Textur rendern koennen: Nanite-Flags.

Im Log steht fuer die Materialien des Bus-Aussenmeshes (Tripo/ESWE)
`Material .../tripo_part_N_material missing usage flag Nanite! Default Material
will be used in game.` - Materialien ohne dieses Flag werden auf einem
Nanite-Mesh durch das Default-Material (grau, ohne Textur) ersetzt. Genau das
sieht aus wie "die Texturen der Busse sind weg".

Dieses Skript sagt, WELCHE Seite fehlt: Nanite am Mesh oder das Flag am
Material. Aufruf wie die Import-Skripte:

  UnrealEditor-Cmd.exe WiesbadenReal.uproject -run=pythonscript
      -script=Tools/inspect_bus_nanite.py -stdout -unattended -nopause -nopls
"""
import unreal

MESH = "/Game/Vehicles/Bus/SM_Bus.SM_Bus"
EAL = unreal.EditorAssetLibrary


def log(msg):
    unreal.log("###WBNANITE### %s" % msg)


mesh = EAL.load_asset(MESH)
if mesh is None:
    log("FEHLER: %s nicht ladbar." % MESH)
    raise SystemExit(1)

try:
    nanite = mesh.get_editor_property("nanite_settings")
    log("Mesh %s: Nanite enabled=%s (Fallback %s)"
        % (MESH, nanite.get_editor_property("enabled"),
           nanite.get_editor_property("fallback_percent_triangles")))
except Exception as exc:
    log("Nanite-Einstellung nicht lesbar (%s)" % exc)

mats = mesh.get_editor_property("static_materials")
log("Mesh traegt %d Materialeintraege." % len(mats))
without, withflag = 0, 0
for entry in mats:
    m = entry.get_editor_property("material_interface")
    if m is None:
        continue
    try:
        flag = m.get_editor_property("used_with_nanite")
    except Exception:
        flag = None
    if flag:
        withflag += 1
    else:
        without += 1
        log("OHNE Nanite-Flag: %s" % m.get_path_name())
log("ENDE: %d mit Nanite-Flag, %d ohne." % (withflag, without))
