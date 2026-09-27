@echo off
REM Voll-Bake Alkis32 aus Alkis16 (NICHT live), 26.09.2026 - Wendeplatten an allen
REM Sackgassen (URoadNetworkGenerator::BuildTurningPlates): gepflastert, Gelaende
REM angeschmiegt, Baeume/Laternen freigehalten; der Verkehr wendet ueber sie.
REM Sonst dieselben Datenquellen und Knoepfe wie Alkis31. Die Default-Karte bleibt
REM unveraendert (WB_LIVE_SCHALTEN NICHT gesetzt).
REM
REM Fertig: neue Zeile in Saved/BuildHistory/CityBuilds.csv (Alkis32) bzw.
REM "FERTIG - Karte ... liegt vor." im Log. Dauer ~20 min.
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Saved\Logs\rebake_alkis32.log
set WB_SOURCE_MAP=/Game/Maps/WiesbadenCity_Alkis16
set WB_TARGET_MAP=/Game/Maps/WiesbadenCity_Alkis32
set WB_MAX_SEGMENT_CM=220
set WB_USE_OSM_TREES=1
set WB_OSM_FILE=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Data\Raw\OSM\wiesbaden.osm.forest.json
set WB_ALKIS_FILE=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Data\Raw\ALKIS\wiesbaden.alkis.lod2.json
set WB_DEM_FILE=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Data\Raw\DEM\wiesbaden_dgm1.asc
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" -ExecCmds="py C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Tools\rebuild_city.py" -unattended -nosplash -nop4 > "%LOG%" 2>&1
exit /b %ERRORLEVEL%
