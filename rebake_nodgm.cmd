@echo off
REM Isolations-Re-Bake: LoD2-Hoehen/Dachformen (WB_ALKIS_FILE) BEHALTEN, aber
REM OHNE DGM1 (kein WB_DEM_FILE) -> altes SRTM-DEM vom Quell-Actor. Testet, ob das
REM DGM1 die Ursache fuer nicht-sichtbare Gebaeude/Strassen ist. Quelle Alkis3, Ziel Alkis10.
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\rebake_nodgm.log
set WB_SOURCE_MAP=/Game/Maps/WiesbadenCity_Alkis3
set WB_TARGET_MAP=/Game/Maps/WiesbadenCity_Alkis10
set WB_MAX_SEGMENT_CM=220
set WB_USE_OSM_TREES=1
set WB_ALKIS_FILE=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Data\Raw\ALKIS\wiesbaden.alkis.lod2.json
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" -ExecCmds="py C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Tools\rebuild_city.py" -unattended -nosplash -nop4 > "%LOG%" 2>&1
exit /b %ERRORLEVEL%
