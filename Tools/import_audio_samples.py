# Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
"""Importiert die WAVs aus Data/Raw/AudioSamples nach /Game/Audio/Samples.

Enthaelt auch die Ambience-Lagen A_Amb* (echte Stadtklang-Aufnahmen,
Tools/fetch_ambience_samples.py) - alle als Dauerlaeufer.

Ergebnis je Clip: ein SoundWave-Asset mit:
  - bLooping = True fuer die Dauerlaeufer (Motoren, Reifen) - siehe CLIPS
  - Kompression der Dauerlaeufer BINK/PCM-frei (Speicherdeckel via
    max_texture_size-Pendant: compression naiv lassen, aber Streaming an)

Verifikation (Daen, Dauer, Loop-Flag) landet in
Saved/Diagnose/audio_samples_import.txt - Python-prints erreichen den
Cmdlet-Stream nicht (bekannter Fallstrick, siehe AGENTS.md).

Aufruf (Editor-Cmdlet, Engine installiert):
  UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=Tools/import_audio_samples.py
"""
import json
import os
import unreal

eal = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()

DST = "/Game/Audio/Samples"
SRC = os.path.join(unreal.Paths.project_dir(), "Data", "Raw", "AudioSamples")

# Zielname -> loop (identisch zu Tools/make_audio_samples.py)
CLIPS = {
    "A_EngineGasolineSmall": True,
    "A_EngineDieselBig": True,
    "A_TireSqueal": True,
    "A_ShotBerettaM12": False,
    "A_ShotRifle": False,
    "A_ShotWinchester": False,
    "A_Explosion": False,
    "A_GunLoad": False,
    "A_CarDoor": False,
    "A_CarStart": False,
    "A_Servo": False,
    "A_PedestrianBurst": False,
    "A_PedestrianHit": False,
    "A_PedestrianHitHeavy": False,
    # Stadtklang-Lagen der Audio-Zonen (Tools/fetch_ambience_samples.py,
    # echte Field-Recordings, alle Dauerlaeufer):
    "A_AmbTraffic": True,
    "A_AmbCrowd": True,
    "A_AmbIndustry": True,
    "A_AmbBirds": True,
    "A_AmbWind": True,
    "A_AmbNight": True,
    "A_AmbChildren": True,
}

report_path = os.path.join(unreal.Paths.project_saved_dir(), "Diagnose", "audio_samples_import.txt")
lines = []

eal.make_directory(DST)
for name, b_loop in sorted(CLIPS.items()):
    wav = os.path.join(SRC, name + ".wav")
    if not os.path.isfile(wav):
        lines.append("%s FEHLT (Datei): %s" % (name, wav))
        continue
    task = unreal.AssetImportTask()
    task.filename = wav
    task.destination_path = DST
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = True
    tools.import_asset_tasks([task])

    asset = eal.load_asset("%s/%s" % (DST, name))
    if asset is None:
        lines.append("%s FEHLER (Import)" % name)
        continue

    def try_set(prop, value):
        """Property setzen, wenn es sie gibt (Asset-Namen aendern sich je
        UE-Version: 'streaming' hiess frueher so, 5.8 'bStreamingPriority'
        nutzt nie set_editor_property direkt)."""
        try:
            asset.set_editor_property(prop, value)
            return prop
        except Exception:
            return None

    set_props = []
    try_set("bLooping", b_loop) and set_props.append("bLooping")
    if b_loop:
        try_set("streaming", True) and set_props.append("streaming")
    eal.save_loaded_asset(asset)

    # Dauer aus der WAV-Datei rechnen (SoundWave hat kein get_duration in
    # 5.8-Python): Bytes / (44100 * 2) = Sekunden (int16 mono).
    try:
        dur = os.path.getsize(wav) / (44100.0 * 2.0) - 44.0 / 44100.0  # Header abziehen
    except OSError:
        dur = -1.0
    lines.append("%s ok: %.2f s, loop=%s (%s)" % (
        name, dur, b_loop, ",".join(set_props) or "ohne Props"))

eal.save_directory(DST, only_if_is_dirty=False, recursive=True)
lines.append("Import fertig: %d Clips" % len(CLIPS))
with open(report_path, "w", encoding="utf-8") as f:
    f.write("\n".join(lines) + "\n")
unreal.log("###AUDIO_SAMPLES### %s" % (" | ".join(lines)))
