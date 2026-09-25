@echo off
REM Voll-Bake Alkis27 - MERGE aus Alkis25 plus die Alkis26-Datenidee, 25.09.2026.
REM
REM Warum ein neuer Bake statt einem Merge der Kartendateien:
REM Eine gebackene Karte ist eine FUNKTION aus (Quellkarte, Daten, Code). Die
REM beiden Karten Alkis25/26 unterscheiden sich in genau einem Datenpunkt -
REM der ALKIS-Schnitt "LuisenForum ueber der Schwalbacher Strasse"
REM (Tools/alkis_ueberbauungen.py, 25.09. 13:13, Datenstempel 13:13). Der
REM Alkis25-Bake startete 13:01 und las die Datei VOR dem Schnitt, der
REM Alkis26-Bake (12:46) ist beim Speichern abgestuerzt und hat keine
REM Historiezeile in Saved/BuildHistory/CityBuilds.csv. Ein Merge zweier
REM Payloads waere Handarbeit an 1,9 GB External-Actor-Paketen ohne
REM Gewissheit; ein Bake aus der besten Quelle mit den aktuellen Daten
REM liefert die Vereinigung deterministisch.
REM
REM Quelle = Alkis25 (juengste VOLLSTAENDIGE Karte, 25.09. 13:19, danach mit
REM dem Anker-Fix verankert und im frischen Prozess geprueft: 20 404 leere
REM Komponenten, 0 am Kartenursprung).
REM Ziel   = Alkis27 (NEU). Kein bestehender Bake wird angefasst: weder die
REM gespielte Stadt (Alkis22) noch Alkis25 oder das kaputte Alkis26.
REM
REM Daten: DGM1 (WGS84-lon/lat, Tools/fetch_dgm1_asc.py), OSM-Wald, LoD2 inkl.
REM Ueberbauungs-Schnitt, WB_MAX_SEGMENT_CM=220 wie in allen Bakes ab 22.
REM WB_LIVE_SCHALTEN ist NICHT gesetzt: die Default-Karte wird erst von Hand
REM nach dem GameTest umgestellt, damit ein misslungener Bau die gespielte
REM Stadt nicht ueberschreibt.
REM
REM Fertig: neue Zeile in Saved/BuildHistory/CityBuilds.csv (Alkis27) bzw.
REM "###WBSTADT### FERTIG" im Log. Dauer ~19 min. Engine-Log: Saved/Logs/
REM WiesbadenReal.log (nicht der Redirect - der bleibt gepuffert/leer).
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Saved\Logs\rebake_alkis27.log
set WB_SOURCE_MAP=/Game/Maps/WiesbadenCity_Alkis25
set WB_TARGET_MAP=/Game/Maps/WiesbadenCity_Alkis27
set WB_MAX_SEGMENT_CM=220
set WB_USE_OSM_TREES=1
set WB_OSM_FILE=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Data\Raw\OSM\wiesbaden.osm.forest.json
set WB_ALKIS_FILE=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Data\Raw\ALKIS\wiesbaden.alkis.lod2.json
set WB_DEM_FILE=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Data\Raw\DEM\wiesbaden_dgm1.asc
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" -ExecCmds="py C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Tools\rebuild_city.py" -unattended -nosplash -nop4 > "%LOG%" 2>&1
exit /b %ERRORLEVEL%
