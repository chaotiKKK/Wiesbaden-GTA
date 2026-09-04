@echo off
REM Health-Check WiesbadenReal: schneller CI-Smoke, faehrt NUR die stationaere
REM WbHealth-Gate-Sitzung und kehrt mit Exit-Code aus dem JSON zurueck.
REM   0 = gesund, 1 = ungesund (Warnungen), 2 = Infrastruktur-Fehler.
REM Voraussetzung: Editor ist gebaut (Tools\bau_neuerpc.cmd).
powershell -ExecutionPolicy Bypass -NoProfile -File "%~dp0health_check.ps1"
exit /b %ERRORLEVEL%
