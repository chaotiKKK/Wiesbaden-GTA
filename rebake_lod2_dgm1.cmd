@echo off
REM Re-Bake mit den AMTLICHEN Quellen (Stand 2026-09-16):
REM  * WB_ALKIS_FILE -> wiesbaden.alkis.lod2.json (amtliche LoD2-Hoehe +
REM    Dachform + Dachhoehe je Gebaeude, aus der Hessen-CityGML)
REM  * WB_DEM_FILE   -> wiesbaden_dgm1.asc (amtliches DGM1, ALS/LiDAR ~10 m,
REM    ersetzt das grobe SRTM ~30 m)
REM Quelle Alkis3 (WorldBuilder + Terrain), Ziel Alkis11 (Alkis4 bleibt live).
REM Voller Editor: der Kommandlet-Weg scheitert an virtuellem Speicher.
REM WICHTIG: nur EIN Editor-Instanz-Lauf gleichzeitig - der letzte Bake ist
REM mit OOM gestorben (pagefile), keine weiteren RAM-Schwerlasten parallel.
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\rebake_lod2_dgm1.log
set WB_SOURCE_MAP=/Game/Maps/WiesbadenCity_Alkis3
set WB_TARGET_MAP=/Game/Maps/WiesbadenCity_Alkis11
set WB_ALKIS_FILE=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Data\Raw\ALKIS\wiesbaden.alkis.lod2.json
set WB_DEM_FILE=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Data\Raw\DEM\wiesbaden_dgm1.asc
set WB_MAX_SEGMENT_CM=220
set WB_USE_OSM_TREES=1
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" -ExecCmds="py C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Tools\rebuild_city.py" -unattended -nosplash -nop4 > "%LOG%" 2>&1
exit /b %ERRORLEVEL%
