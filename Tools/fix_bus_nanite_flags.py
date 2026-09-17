"""Setzt das Nanite-Flag auf den Materialien des Bus-Aussenmeshes.

BEFUND (Tools/inspect_bus_nanite.py): `/Game/Vehicles/Bus/SM_Bus` hat Nanite
aktiviert, aber ALLE 41 Materialien des Meshes (Tripo/ESWE, `tripo_part_N_material`)
tragen `used_with_nanite = False`. Unreal ersetzt ein solches Material auf einem
Nanite-Mesh durch das DEFAULT-Material - der Bus rendert dann grau und ohne jede
Textur. Im Spiel-Log steht dazu je Material:

  LogMaterial: Warning: Material .../tripo_part_3_material missing usage flag
  Nanite! Default Material will be used in game.

Genau das sah aus wie "wenn man einsteigt sind die Texturen der Busse weg": die
Busse haben ihre Texturen verloren, unabhaengig davon, wer gerade mitfaehrt.

Aufruf:
  UnrealEditor-Cmd.exe WiesbadenReal.uproject -run=pythonscript
      -script=Tools/fix_bus_nanite_flags.py -stdout -unattended -nopause -nop4
Danach mit Tools/inspect_bus_nanite.py gegenpruefen ("41 mit Nanite-Flag, 0 ohne").
"""
import unreal

MESH = "/Game/Vehicles/Bus/SM_Bus.SM_Bus"
EAL = unreal.EditorAssetLibrary


def log(msg):
    unreal.log("###WBNANITEFIX### %s" % msg)


mesh = EAL.load_asset(MESH)
if mesh is None:
    log("FEHLER: %s nicht ladbar." % MESH)
    raise SystemExit(1)

def base_material(mat):
    """Bis zum echten UMaterial hochgehen - das Flag gibt es nur dort.
    (Die Bauteile sind MaterialInstanceConstants; der erste Versuch, es an der
    Instanz zu setzen, scheiterte mit "Failed to find property 'used_with_nanite'".)"""
    seen = 0
    while mat is not None and seen < 16:
        if isinstance(mat, unreal.Material):
            return mat
        try:
            mat = mat.get_editor_property("parent")
        except Exception:
            return None
        seen += 1
    return None


bases = {}
for entry in mesh.get_editor_property("static_materials"):
    mat = entry.get_editor_property("material_interface")
    if mat is None:
        continue
    base = base_material(mat)
    if base is None:
        log("OHNE Basismaterial: %s" % mat.get_path_name())
        continue
    bases[base.get_path_name()] = base

log("Mesh nutzt %d verschiedene Basismaterialien." % len(bases))
fixed, already, failed = 0, 0, 0
for path, base in sorted(bases.items()):
    try:
        if base.get_editor_property("used_with_nanite"):
            already += 1
            continue
        base.set_editor_property("used_with_nanite", True)
        EAL.save_loaded_asset(base)
        if base.get_editor_property("used_with_nanite"):
            fixed += 1
            log("Nanite-Flag gesetzt: %s" % path)
        else:
            failed += 1
            log("NICHT gesetzt: %s" % path)
    except Exception as exc:
        failed += 1
        log("FEHLER bei %s: %s" % (path, exc))

log("ENDE: %d gesetzt, %d waren schon gesetzt, %d fehlgeschlagen."
    % (fixed, already, failed))
