@echo off
REM Kontroll-Bake Alkis20 aus Alkis16 (NICHT live). Traegt den aktuellen Stand
REM der Pipeline: per-Gebaeude-Dachdeckung (Vertexfarbe R) + neue per-Gebaeude-
REM Ton-Variation (Vertexfarbe G, RoofToneByte) + Service-Einspur-Fix. Alkis16
REM bleibt Default (WB_LIVE_SCHALTEN NICHT gesetzt), damit ein misslungener Bau
REM die gespielte Stadt nicht ueberschreibt.
REM
REM Datenquellen EXPLIZIT (Abbruch statt stillem Default), identisch zu Alkis17.
REM Fertig: neue Zeile in Saved/BuildHistory/CityBuilds.csv (Alkis20) bzw.
REM "FERTIG - Karte ... liegt vor." im Log. Dauer ~11 min.
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\rebake_alkis20.log
set WB_SOURCE_MAP=/Game/Maps/WiesbadenCity_Alkis16
set WB_TARGET_MAP=/Game/Maps/WiesbadenCity_Alkis20
set WB_MAX_SEGMENT_CM=220
set WB_USE_OSM_TREES=1
set WB_OSM_FILE=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Data\Raw\OSM\wiesbaden.osm.forest.json
set WB_ALKIS_FILE=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Data\Raw\ALKIS\wiesbaden.alkis.lod2.json
set WB_DEM_FILE=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Data\Raw\DEM\wiesbaden_dgm1.asc
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" -ExecCmds="py C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Tools\rebuild_city.py" -unattended -nosplash -nop4 > "%LOG%" 2>&1
exit /b %ERRORLEVEL%
