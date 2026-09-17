"""Importiert die vorgerenderten Linie-6-Halteansagen (Tools/announce_wav/A_NN.wav)
als USoundWave-Assets nach /Game/Audio/Bus/Announce/A_NN und speichert sie.

Aufruf (voller Editor, unbeaufsichtigt):
  UnrealEditor.exe WiesbadenReal.uproject
      -ExecCmds="py Tools/import_bus_announcements.py" -unattended -nosplash -nop4
"""
import os

import unreal

PROJ = unreal.Paths.project_dir().replace("\\", "/").rstrip("/")
RAW = PROJ + "/Tools/announce_wav"
DEST = "/Game/Audio/Bus/Announce"

ATH = unreal.AssetToolsHelpers.get_asset_tools()


def log(msg):
    unreal.log("###WBANSAGE### %s" % msg)


def finish(code):
    if unreal.SystemLibrary.get_command_line().find("-unattended") >= 0:
        unreal.SystemLibrary.quit_editor()
    raise SystemExit(code)


tasks = []
for i in range(0, 40):
    wav = os.path.join(RAW, "A_%02d.wav" % i)
    if not os.path.exists(wav):
        continue
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", wav)
    t.set_editor_property("destination_path", DEST)
    t.set_editor_property("destination_name", "A_%02d" % i)
    t.set_editor_property("automated", True)
    t.set_editor_property("replace_existing", True)
    t.set_editor_property("save", True)
    tasks.append(t)

if not tasks:
    log("ABBRUCH: keine WAVs in %s" % RAW)
    finish(1)

log("Importiere %d Ansage-WAVs nach %s ..." % (len(tasks), DEST))
ATH.import_asset_tasks(tasks)

# Erfolg pruefen + speichern.
ok = 0
for i in range(0, len(tasks)):
    obj = "%s/A_%02d" % (DEST, i)
    if unreal.EditorAssetLibrary.does_asset_exist(obj):
        unreal.EditorAssetLibrary.save_asset(obj, only_if_is_dirty=False)
        ok += 1
    else:
        log("FEHLT nach Import: %s" % obj)

log("FERTIG: %d/%d Ansage-Assets importiert und gespeichert." % (ok, len(tasks)))
finish(0 if ok == len(tasks) else 1)
