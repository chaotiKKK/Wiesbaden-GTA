"""Setzt fehlende Material-Usage-Flags, die UE mit dem Default-Material bestraft.

BEFUND (aus dem Spiel-Log, Zeile fuer Zeile dieselbe Ursache wie beim Bus):
    Material /Game/Vehicles/Ka52/M_Ka52PBR missing usage flag Nanite!
    ... Default Material will be used in game.
    Material /Game/Nerobergbahn/Materials/MI_Nb_NbSchiene missing usage flag
    InstancedStaticMeshes! ...

Ein Nanite-Mesh (Ka52) mit einem Material ohne Nanite-Flag rendert grau, und
eine Instanced-Static-Mesh-Komponente (die Nerobergbahn baut Schienen,
Zahnstangen, Roste und Seil als ISM) mit einem Material ohne ISM-Flag ebenso.
Beides ist KEIN Renderfehler, sondern ein Schalter am Asset.

Quellen fuer die Liste:
  * die Warnzeilen des kopierten Spiel-Logs (Standard), UND
  * die KNOWN-Liste unten - die Warnzeile erscheint nur, wenn das Objekt im Lauf
    auch gerendert wurde (der kopflose Lauf zeigt weder Ka52 noch Nerobergbahn).

Ergebnis: Saved/Diagnose/material_usage_flags.txt (unreal.log() kommt aus einem
Commandlet nicht im cmd-Strom an). Aufruf: Tools\\fix_material_flags.cmd
"""
import os
import re
import unreal

# NICHT Saved/Logs/WiesbadenReal.log direkt: ein -run=pythonscript-Commandlet
# ueberschreibt genau diese Datei schon beim Start mit seinem eigenen Log.
# Der .cmd kopiert das Spiel-Log darum vorher nach material_flags_source.log.
LOG = "C:/freebuff/WiesbadenReal_Sicherung/WiesbadenReal/Saved/Logs/material_flags_source.log"
OUT = "C:/freebuff/WiesbadenReal_Sicherung/WiesbadenReal/Saved/Diagnose/material_usage_flags.txt"

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary

PROP = {
    "Nanite": "used_with_nanite",
    "InstancedStaticMeshes": "used_with_instanced_static_meshes",
}

KNOWN = [
    # Ka52: Nanite-Mesh, Material ohne Nanite-Flag -> grauer Hubschrauber.
    ("/Game/Vehicles/Ka52/M_Ka52PBR", "Nanite"),
]
# Nerobergbahn: die Nb-Materialien werden in InstancedStaticMesh-Komponenten
# verbaut (Schienen, Zahnstangen, Roste, Seil, Schotter).
for _asset in EAL.list_assets("/Game/Nerobergbahn/Materials", recursive=False, include_folder=False):
    if "/MI_Nb_" in _asset:
        KNOWN.append((_asset.split(".")[0], "InstancedStaticMeshes"))

lines = []


def out(msg):
    lines.append(str(msg))


text = ""
if os.path.exists(LOG):
    with open(LOG, "r", errors="ignore") as handle:
        text = handle.read()

# "Material <pfad> missing usage flag <Flag>!" (Pfad enthaelt .Objektname)
found = {}
for path, flag in re.compile(r"Material (\S+) missing usage flag (\w+)!").findall(text):
    if flag in PROP:
        found.setdefault(path.split(".")[0], set()).add(flag)
for path, flag in KNOWN:
    found.setdefault(path, set()).add(flag)

out("Material-Usage-Flags (Log: %s%s)" % (LOG, "" if text else " - FEHLT, nur KNOWN-Liste"))
out("=" * 70)
for path in sorted(found):
    asset = EAL.load_asset(path)
    if asset is None:
        out("NICHT GEFUNDEN: %s" % path)
        continue
    out("--- %s (fehlt: %s)" % (path, ", ".join(sorted(found[path]))))
    targets = [asset]
    try:
        parent = asset.get_editor_property("parent")
    except Exception:
        parent = None
    if parent is not None:
        targets.append(parent)
    for flag in sorted(found[path]):
        prop = PROP[flag]
        for target in targets:
            try:
                if target.get_editor_property(prop):
                    continue
                target.set_editor_property(prop, True)
                after = target.get_editor_property(prop)
                EAL.save_loaded_asset(target)
                out("    %s: %s -> %s" % (target.get_name(), flag, after))
            except Exception as exc:
                out("    %s: %s liess sich NICHT setzen (%s)" % (target.get_name(), prop, exc))
    for target in targets:
        try:
            if isinstance(target, unreal.Material):
                MEL.recompile_material(target)
                EAL.save_loaded_asset(target)
        except Exception as exc:
            out("    %s: Neuuebersetzen fehlgeschlagen (%s)" % (target.get_name(), exc))

out("")
out("Gegenprobe: ein Fenster-Lauf mit denselben Objekten in Sicht darf danach")
out("keine 'missing usage flag'-Zeile mehr schreiben.")

with open(OUT, "w", encoding="utf-8") as handle:
    handle.write("\n".join(lines) + "\n")
