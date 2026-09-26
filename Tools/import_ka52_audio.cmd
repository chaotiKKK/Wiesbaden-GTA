@echo off
REM Importiert die vier Ka-52-Flugsounds (Tools\make_ka52_audio.py ->
REM ka52_rotor, ka52_engine, ka52_mg, ka52_wind) nach Content\Audio\Ka52.
REM
REM Der Lauf gilt nur als gelungen, wenn der Selbstbericht
REM Saved\Diagnose\ka52\import_ka52_audio_ergebnis.txt "ERGEBNIS: OK" und
REM "alle vier Flugsounds da" enthaelt. Prints aus -run=pythonscript erreichen
REM weder Konsole noch Log (AGENTS.md).
REM
REM Bauform: Sprungmarken statt Klammerbloecken, Bericht wird vorher geloescht
REM (sonst liegt der alte Erfolg noch da und jeder Lauf gilt als gruen).
cd /d "%~dp0.."
set "LOG=Tools\import_ka52_audio_log.txt"
set "RPT=Saved\Diagnose\ka52\import_ka52_audio_ergebnis.txt"
if exist "%RPT%" del /q "%RPT%"
if exist "%LOG%" del /q "%LOG%"

call "%~dp0engine.cmd" || exit /b 1
if exist "%RPT%" goto :pruefen

"%WB_EDITOR_CMD%" ^
  "%~dp0..\WiesbadenReal.uproject" ^
  -run=pythonscript -script="%~dp0import_ka52_audio.py" ^
  -stdout -unattended -nopause -nosplash > "%LOG%" 2>&1
goto :pruefen

:pruefen
if not exist "%RPT%" goto :kein_bericht
findstr /C:"ERGEBNIS: OK" "%RPT%" > nul
if errorlevel 1 goto :bericht_fehler
findstr /C:"alle vier Flugsounds da" "%RPT%" > nul
if errorlevel 1 goto :bericht_fehler
type "%RPT%"
echo OK: Flugsounds importiert. Bericht: %RPT%
exit /b 0

:bericht_fehler
echo FEHLER: Import unvollstaendig - siehe "%RPT%"
type "%RPT%"
exit /b 2

:kein_bericht
echo FEHLER: kein Bericht geschrieben - der Lauf kam nicht bis zum Skript.
echo Log: %LOG%
exit /b 3
