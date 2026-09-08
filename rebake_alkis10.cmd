@echo off
REM Koordinierter Re-Bake nach Alkis10: feinere Strassen-Terrainfolge
REM (max_segment_length 220) + Baeume aus echten OSM-Punkten (bUseOsmTrees) +
REM Gebaeude-Override Platter Strasse 140/144/146 (im BuildingGenerator).
REM Quelle Alkis3 (WorldBuilder + Terrain), Ziel Alkis10 (Alkis4 bleibt live).
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\rebake_alkis10.log
set WB_SOURCE_MAP=/Game/Maps/WiesbadenCity_Alkis3
set WB_TARGET_MAP=/Game/Maps/WiesbadenCity_Alkis10
set WB_MAX_SEGMENT_CM=220
set WB_USE_OSM_TREES=1
"C:\freebuff\WiesbadenReal_Sicherung\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" -ExecCmds="py C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Tools\rebuild_city.py" -unattended -nosplash -nop4 > "%LOG%" 2>&1
exit /b %ERRORLEVEL%
