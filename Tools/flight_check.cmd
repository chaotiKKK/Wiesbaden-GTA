@echo off
REM Flugpruefung: echte Spielsitzung mit dem Ka-52 (Details in flight_check.ps1).
REM Aufruf: flight_check.cmd "<ExecCmds>" [Name] [MinSeconds]
REM   Dev-Befehle OHNE Argument, z. B. "WbHeli,WbHeliFly" - die Engine kann
REM   ueber -ExecCmds keins uebergeben (flight_check.ps1, Kopf).
REM
REM Die optionalen Argumente werden nur dann weitergegeben, wenn sie wirklich
REM gesetzt sind. Vorher stand hier "-ExecCmds %1 -Name %2 -MinSeconds %3" -
REM der dokumentierte Zweier-Aufruf (Name ohne MinSekunden) brach mit
REM "Fehlendes Argument fuer den Parameter MinSeconds" ab, weil cmd ein
REM leeres %3 als *leeres Argument* durchreicht und PowerShell dann keinen
REM Wert findet. Genau der Aufruf, den der eigene Kopf dieser Datei nennt.
setlocal
set ARGS=-ExecCmds "%~1"
if not "%~2"=="" set ARGS=%ARGS% -Name "%~2"
if not "%~3"=="" set ARGS=%ARGS% -MinSeconds "%~3"
powershell -ExecutionPolicy Bypass -NoProfile -File "%~dp0flight_check.ps1" %ARGS%
exit /b %ERRORLEVEL%
