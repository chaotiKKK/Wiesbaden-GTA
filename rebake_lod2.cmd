@echo off
REM Gemeinsamer LoD2-Re-Bake: amtliche Gebaeudehoehen + Dachformen (CityGML,
REM injiziert in wiesbaden.alkis.lod2.json) + DGM1-Gelaende (wiesbaden_dgm1.asc).
REM WB_ALKIS_FILE/WB_DEM_FILE werden in rebuild_city.py set-and-verify gesetzt
REM (bricht ab, falls ein Override nicht greift). Quelle Alkis3, Ziel Alkis10.
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\rebake_lod2.log
set WB_SOURCE_MAP=/Game/Maps/WiesbadenCity_Alkis3
set WB_TARGET_MAP=/Game/Maps/WiesbadenCity_Alkis10
set WB_MAX_SEGMENT_CM=220
set WB_USE_OSM_TREES=1
set WB_ALKIS_FILE=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Data\Raw\ALKIS\wiesbaden.alkis.lod2.json
set WB_DEM_FILE=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Data\Raw\DEM\wiesbaden_dgm1.asc
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" -ExecCmds="py C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Tools\rebuild_city.py" -unattended -nosplash -nop4 > "%LOG%" 2>&1
exit /b %ERRORLEVEL%
