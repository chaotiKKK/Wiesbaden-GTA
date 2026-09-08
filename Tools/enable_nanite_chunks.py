"""Aktiviert Nanite auf den bereits gebackenen SICHTBAREN Chunk-Render-Meshes
(SM_Road_*, SM_Building_*) unter /Game/Generated/Chunks.

Zweck: den Nanite-Vorher/Nachher-Vergleich auf DERSELBEN Karte (Alkis8) am
IDENTISCHEN Blickpunkt fahren, ohne die ganze Stadt (~1-2 h) neu zu backen.
Es ist dieselbe Einstellung (NaniteSettings.enabled), die der Baker jetzt im
Code setzt - hier nur nachtraeglich auf die schon vorhandenen Assets angewandt.

Das unsichtbare Kollisions-Mesh (SM_RoadCol_*) rendert nie und bleibt aussen vor.

Aufruf:
  UnrealEditor.exe WiesbadenReal.uproject
      -ExecCmds="py Tools/enable_nanite_chunks.py" -unattended -nosplash
"""

import os

import unreal

CHUNKS = "/Game/Generated/Chunks"
EAL = unreal.EditorAssetLibrary

# WB_NANITE=1 -> aktivieren (Vorgabe), WB_NANITE=0 -> abschalten (Revert).
TARGET = os.environ.get("WB_NANITE", "1") != "0"


def log(msg):
    unreal.log("###NANITE### %s" % msg)


paths = EAL.list_assets(CHUNKS, recursive=True, include_folder=False)
enabled = 0
already = 0
skipped = 0
processed = 0

for p in paths:
    short = p.split("/")[-1].split(".")[0]
    # Nur sichtbare Render-Meshes. SM_RoadCol_* faellt hier automatisch raus
    # (beginnt NICHT mit "SM_Road_": Zeichen 8 ist 'C', nicht '_').
    if not (short.startswith("SM_Road_") or short.startswith("SM_Building_")):
        skipped += 1
        continue

    asset = EAL.load_asset(p)
    if not isinstance(asset, unreal.StaticMesh):
        skipped += 1
        continue

    settings = asset.get_editor_property("nanite_settings")
    if settings.get_editor_property("enabled") == TARGET:
        already += 1
        continue

    settings.set_editor_property("enabled", TARGET)
    asset.set_editor_property("nanite_settings", settings)
    EAL.save_loaded_asset(asset)
    enabled += 1
    processed += 1
    if processed % 100 == 0:
        log("Fortschritt: %d auf Nanite=%s gesetzt ..." % (processed, TARGET))

log("FERTIG: %d auf Nanite=%s gesetzt, %d schon so, %d uebersprungen (von %d Assets)."
    % (enabled, TARGET, already, skipped, len(paths)))

if unreal.SystemLibrary.get_command_line().find("-unattended") >= 0:
    unreal.SystemLibrary.quit_editor()
