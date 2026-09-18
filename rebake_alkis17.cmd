@echo off
REM Kompletter Stadt-Neubake in eine NEUE Karte: Alkis17.
REM
REM Quelle ist die gespielte Karte (Alkis16) - von ihr uebernimmt
REM rebuild_city.py die abgestimmten WorldBuilder-Einstellungen; das Ziel
REM Alkis17 bleibt frei, damit ein misslungener Bau die gespielte Stadt nicht
REM ueberschreibt (dieselbe Regel wie bei jedem Bake vorher).
REM
REM Datenquellen identisch zu rebake_alkis16.cmd - EXPLIZIT gesetzt, damit der
REM Lauf abbricht statt still mit einem Default weiterzubauen:
REM   OSM   wiesbaden.osm.forest.json - Original + nachgeholte Wald-RELATIONen
REM   ALKIS wiesbaden.alkis.lod2.json - amtliche Gebaeudehoehen + Dachformen
REM   DEM   wiesbaden_dgm1.asc        - amtliches Gelaende
REM   Baeume: WB_USE_OSM_TREES=1
REM
REM ANMERKUNG zum Anlass: das Wetter-Overlay (Regen/Schnee als Post-Process)
REM braucht KEINEN Bake - Material und C++ wirken zur Laufzeit. Dieser Lauf
REM hebt die Stadt nur auf eine frische Versionsnummer mit dem aktuellen Stand
REM der Pipeline.
REM
REM VOLLER Editor, nicht UnrealEditor-Cmd mit -nullrhi: der Commandlet-Weg ist
REM an 42 GiB virtuellem Speicher gescheitert (siehe rebuild_baumfrei.cmd).
REM
REM Fertig erkennt man den Lauf an der neuen Zeile in
REM Saved/BuildHistory/CityBuilds.csv (MapPath /Game/Maps/WiesbadenCity_Alkis17)
REM bzw. an "FERTIG - Karte ... liegt vor." im Log. Dauer ~11 min.
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\rebake_alkis17.log
set WB_SOURCE_MAP=/Game/Maps/WiesbadenCity_Alkis16
set WB_TARGET_MAP=/Game/Maps/WiesbadenCity_Alkis17
set WB_MAX_SEGMENT_CM=220
set WB_USE_OSM_TREES=1
set WB_OSM_FILE=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Data\Raw\OSM\wiesbaden.osm.forest.json
set WB_ALKIS_FILE=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Data\Raw\ALKIS\wiesbaden.alkis.lod2.json
set WB_DEM_FILE=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Data\Raw\DEM\wiesbaden_dgm1.asc
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" -ExecCmds="py C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Tools\rebuild_city.py" -unattended -nosplash -nop4 > "%LOG%" 2>&1
exit /b %ERRORLEVEL%
