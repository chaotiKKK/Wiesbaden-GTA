@echo off
REM ===========================================================================
REM  Klangnachweis Ambience-Samples: kurzer Spiel-Lauf MIT TON.
REM
REM  Beweiszeilen im Log (Saved\Logs\wb_ambience_probe.log):
REM    "Ambience-Lage A_Amb<Name> als echte Aufnahme aktiv."  (je aktive Lage)
REM  plus "Messlauf beendet"/"Log file closed" fuer das saubere Ende.
REM  Karte aus Config\DefaultEngine.ini (Tools\karte.cmd), feste Tageszeit,
REM  damit Voegel/Nacht-Lagen deterministisch gewichtet sind.
REM ===========================================================================
setlocal
for %%I in ("%~dp0..") do set "WBPROJ=%%~fI"
set "LOG=%WBPROJ%\Saved\Logs\wb_ambience_probe.log"
call "%~dp0karte.cmd"

"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%WBPROJ%\WiesbadenReal.uproject" %WB_MAP_PFAD% -game -WbTime=13 -WbQuitAfter=45 -WbWatchdogStallSec=90 -WbWatchdogExitSec=90 -windowed -ResX=1280 -ResY=720 -unattended -nop4 "-ABSLOG=%LOG%"
exit /b %ERRORLEVEL%
