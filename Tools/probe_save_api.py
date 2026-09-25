"""Listet die speichernden Methoden der Editor-Klassen auf (API-Sonde).

Anlass: Weder EditorActorSubsystem.save_actor noch
EditorLoadingAndSavingUtils.save_actor existieren in UE 5.8 - der
Anker-Lauf brach mit AttributeError ab. Statt weiter zu raten, schreibt
dieser Lauf alle Attribute mit 'save' im Namen der naheliegenden Klassen
nach Saved/Diagnose/api_probe.txt.

Aufruf (Ergebnis in Saved/Diagnose/api_probe.txt):
  UnrealEditor-Cmd.exe <uproject> -run=pythonscript \
      -script=Tools/probe_save_api.py
"""

import os

import unreal

RESULT_FILE = os.path.join(
    unreal.Paths.project_saved_dir(), "Diagnose", "api_probe.txt")

_lines = []

for class_name in (
        "EditorActorSubsystem",
        "EditorLoadingAndSavingUtils",
        "LevelEditorSubsystem",
        "EditorLevelLibrary",
        "EditorLevelUtils",
        "EditorAssetLibrary",
        "EditorActorSubsystem",
        "WorldPartitionBlueprintLibrary",
        "Object",
):
    cls = getattr(unreal, class_name, None)
    if cls is None:
        _lines.append("%s: nicht vorhanden" % class_name)
        continue
    names = sorted(a for a in dir(cls) if "save" in a.lower())
    _lines.append("%s: %s" % (class_name, ", ".join(names) if names else "-"))

os.makedirs(os.path.dirname(RESULT_FILE), exist_ok=True)
with open(RESULT_FILE, "w", encoding="utf-8") as f:
    f.write("\n".join(_lines) + "\n")
