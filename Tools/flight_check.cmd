@echo off
REM Flugpruefung: echte Spielsitzung mit dem Ka-52 (Details in flight_check.ps1).
REM Aufruf: flight_check.cmd "<ExecCmds>" [Name] [MinSeconds]
powershell -ExecutionPolicy Bypass -NoProfile -File "%~dp0flight_check.ps1" -ExecCmds %1 -Name %2 -MinSeconds %3
exit /b %ERRORLEVEL%
