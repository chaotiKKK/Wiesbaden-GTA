@echo off
call "%~dp0cleanup_unreal_processes.cmd"
if errorlevel 1 exit /b 1
rem Kopfloser Boden-Audit: dieselbe Fahrbahn-Messung wie -WbBusGroundAudit im
rem Spielbetrieb, aber OHNE Fenster (-nullrhi). Zwei Gruende:
rem   * ein Spiel-Fenster im Hintergrund wird von der Engine gedrosselt; die
rem     Spielzeit lief dadurch 10x langsamer als die Wanduhr (gemessen), und
rem   * ein Fensterlauf wurde nach ~1 min mit "ViewportClosed" beendet.
rem Die Messung selbst braucht kein Bild: Stuerze, Streaming und Terrain sind
rem CPU-seitig. Ergebnis: Saved\Diagnose\bus_ground_audit_line<ref>.txt mit den
rem Stellen der groessten Spur/Boden-Abweichung.
rem
rem Aufruf: Tools\run_bus_audit.cmd [Karte] [Spielsekunden] [Logdatei]
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set MAP=%~1
REM  Karte NICHT fest verdrahten - sie kommt aus
REM  Config\DefaultEngine.ini (siehe Tools\karte.cmd).
call "%~dp0karte.cmd"
if "%MAP%"=="" set MAP=%WB_MAP%
set QUIT=%~2
if "%QUIT%"=="" set QUIT=2400
set LOG=%~3
if "%LOG%"=="" set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\audit_bus.log
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" /Game/Maps/%MAP% -game -nullrhi -NoSound -WbBusLog -WbBusLogWagon=601 -WbBusGroundAudit -WbZuFuss=0 -WbQuitAfter=%QUIT% -unattended -nop4 -stdout > "%LOG%" 2>&1
exit /b %ERRORLEVEL%
