@echo off
REM Re-Bake der Strassen-Fixes (#1 Gehweg/Bordstein an Mittellinie, #4 Schild-
REM Rotation, #5 Markierungs-Hoehe) + LoD2-Gebaeudehoehen/Daecher + OSM-Baeume,
REM OHNE DGM. Ziel eine FRISCHE Karte Alkis11 - Alkis4 bleibt als Fallback.
REM Ueber CMD (nicht Git Bash): CMD wandelt /Game/... nicht in Windows-Pfade um.
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\rebake_alkis11.log
set WB_SOURCE_MAP=/Game/Maps/WiesbadenCity_Alkis3
set WB_TARGET_MAP=/Game/Maps/WiesbadenCity_Alkis11
set WB_MAX_SEGMENT_CM=220
set WB_USE_OSM_TREES=1
set WB_ALKIS_FILE=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Data\Raw\ALKIS\wiesbaden.alkis.lod2.json
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" -ExecCmds="py C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Tools\rebuild_city.py" -unattended -nosplash -nop4 > "%LOG%" 2>&1
exit /b %ERRORLEVEL%
