@echo off
REM Importiert Cockpit-Innenraum und 30-mm-Kanone der Ka-52 (Blender-Pipeline
REM Tools\Blender\build_ka52_cockpit.py -> ka52_cockpit.fbx, ka52_gun_turret.fbx).
REM
REM Der Lauf gilt nur als gelungen, wenn der Import-Selbstbericht
REM Saved\Diagnose\ka52\import_ka52_cockpit_ergebnis.txt "ERGEBNIS: OK" und
REM "beide Meshes da" enthaelt. Prints aus -run=pythonscript erreichen weder
REM Konsole noch Log (AGENTS.md) - der erste Lauf meldete deshalb
REM "Import unvollstaendig", obwohl die Engine 5 s lang gearbeitet hatte.
REM
REM Bauform: Sprungmarken statt Klammerbloecken, Bericht wird vorher geloescht
REM (sonst liegt der alte Erfolg noch da und jeder Lauf gilt als gruen).
cd /d "%~dp0.."
set "LOG=Tools\import_ka52_cockpit_log.txt"
set "RPT=Saved\Diagnose\ka52\import_ka52_cockpit_ergebnis.txt"
if exist "%RPT%" del /q "%RPT%"
if exist "%LOG%" del /q "%LOG%"

call "%~dp0engine.cmd" || exit /b 1
if exist "%RPT%" goto :pruefen

"%WB_EDITOR_CMD%" ^
  "%~dp0..\WiesbadenReal.uproject" ^
  -run=pythonscript -script="%~dp0import_ka52_cockpit.py" ^
  -stdout -unattended -nopause -nosplash > "%LOG%" 2>&1
goto :pruefen

:pruefen
if not exist "%RPT%" goto :kein_bericht
findstr /C:"ERGEBNIS: OK" "%RPT%" > nul
if errorlevel 1 goto :bericht_fehler
findstr /C:"beide Meshes da" "%RPT%" > nul
if errorlevel 1 goto :bericht_fehler
type "%RPT%"
echo OK: Cockpit und Kanone importiert. Bericht: %RPT%
exit /b 0

:bericht_fehler
echo FEHLER: Import unvollstaendig - siehe "%RPT%"
type "%RPT%"
exit /b 2

:kein_bericht
echo FEHLER: kein Bericht geschrieben - der Lauf kam nicht bis zum Skript.
echo Log: %LOG%
exit /b 3
