@echo off
REM Kompletter Stadt-Neubake mit ALLEN derzeitigen Daten in eine NEUE Karte.
REM
REM Quelle ist die zuletzt gebackene Karte (Alkis15) - von ihr uebernimmt
REM rebuild_city.py die abgestimmten WorldBuilder-Einstellungen; das Ziel
REM Alkis16 bleibt frei, damit ein misslungener Bau die gespielte Stadt nicht
REM ueberschreibt (dieselbe Regel wie bei jedem Bake vorher).
REM
REM Alle Datenquellen werden EXPLIZIT gesetzt (set-and-verify im Skript, es
REM bricht ab statt still mit einem Default weiterzubauen):
REM   OSM   wiesbaden.osm.forest.json - Original + nachgeholte Wald-RELATIONen
REM         als synthetische Ways (Tools/fetch_osm_forest_relations.py); ohne
REM         sie bleibt der Neroberg kahl, weil die Overpass-Abfrage nur Ways
REM         holte und Stadtwald/Neroberg in OSM Multipolygone sind.
REM   ALKIS wiesbaden.alkis.lod2.json - amtliche Gebaeudehoehen + Dachformen.
REM   DEM   wiesbaden_dgm1.asc - amtliches Gelaende (setzt import_dem auf True,
REM         der Quell-Actor kann False geerbt haben).
REM   Baeume: WB_USE_OSM_TREES=1 (OSM-Baumknoten statt Zufallsstreuung).
REM
REM VOLLER Editor, nicht UnrealEditor-Cmd mit -nullrhi: der Commandlet-Weg ist
REM an 42 GiB virtuellem Speicher gescheitert (siehe rebuild_baumfrei.cmd).
REM
REM Fertig erkennt man den Lauf an der neuen Zeile in
REM Saved/BuildHistory/CityBuilds.csv (MapPath /Game/Maps/WiesbadenCity_Alkis16)
REM bzw. an "FERTIG - Karte ... liegt vor." im Log. Dauer ~11 min.
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\rebake_alkis16.log
set WB_SOURCE_MAP=/Game/Maps/WiesbadenCity_Alkis15
set WB_TARGET_MAP=/Game/Maps/WiesbadenCity_Alkis16
set WB_MAX_SEGMENT_CM=220
set WB_USE_OSM_TREES=1
set WB_OSM_FILE=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Data\Raw\OSM\wiesbaden.osm.forest.json
set WB_ALKIS_FILE=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Data\Raw\ALKIS\wiesbaden.alkis.lod2.json
set WB_DEM_FILE=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Data\Raw\DEM\wiesbaden_dgm1.asc
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" -ExecCmds="py C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Tools\rebuild_city.py" -unattended -nosplash -nop4 > "%LOG%" 2>&1
exit /b %ERRORLEVEL%
