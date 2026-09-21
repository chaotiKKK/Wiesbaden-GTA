#!/bin/bash
# Sweep der Weltkarten-Zoomstufen: je Zoom ein Screenshot der Karte mit
# -WbShowMap -WbMapZoom=<z> -WbShot=20 (Rezept aus shot_map_1.log, Sep 8).
# -WbShot quit nicht selbst -> nach dem Screenshot extern beenden.
cd /c/freebuff/WiesbadenReal_Sicherung/WiesbadenReal || exit 1

# MSYS-Pfad-Mangling aus: /Game/Maps/... darf NICHT zu C:/Program Files/Git/... werden.
export MSYS_NO_PATHCONV=1

# Engine: die installierte (5.8.2). Die Kopie unter
# C:/freebuff/WiesbadenReal_Sicherung/UE_5.8 ist 5.8.1 und passt nicht
# zum PCH des Moduls - kanonisch steht sie in Tools/engine.py.
UE="C:\\Program Files\\Epic Games\\UE_5.8\\Engine\\Binaries\\Win64\\UnrealEditor.exe"
PROJ="C:\\freebuff\\WiesbadenReal_Sicherung\\WiesbadenReal\\WiesbadenReal.uproject"
SHOT="Saved/Diagnose/Messstelle00000.png"

for Z in 1 2 2.5 3 3.5 4 4.5 5 8; do
  rm -f "$SHOT"
  echo "=== Zoom $Z start $(date +%T) ===" >> sweep_map_zoom.log
  "$UE" "$PROJ" /Game/Maps/WiesbadenCity_Alkis4 -game -WbShowMap -WbMapZoom=$Z -WbShot=20 \
    -windowed -ResX=1600 -ResY=900 -stdout -unattended -nop4 > "sweep_z${Z}.log" 2>&1 &
  PID=$!
  OK=0
  for i in $(seq 1 75); do
    if [ -f "$SHOT" ]; then OK=1; break; fi
    sleep 2
  done
  if [ "$OK" = "1" ]; then
    sleep 3   # Datei fertig schreiben lassen
    cp "$SHOT" "Saved/Diagnose/map_zoom_${Z}.png"
    echo "  Zoom $Z: Screenshot ok $(date +%T)" >> sweep_map_zoom.log
  else
    echo "  Zoom $Z: KEIN Screenshot (Timeout) $(date +%T)" >> sweep_map_zoom.log
  fi
  kill "$PID" 2>/dev/null
  sleep 1
  taskkill //F //IM UnrealEditor.exe > /dev/null 2>&1
  sleep 4
done
echo "=== DONE $(date +%T) ===" >> sweep_map_zoom.log