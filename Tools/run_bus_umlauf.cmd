@echo off
call "%~dp0engine_run_lock.cmd" -Modus Start -Name run_bus_umlauf
if errorlevel 1 exit /b 1
rem Beleglauf "Umlauf": ein Wagen faehrt seine Linie durch, mit 2-s-Protokoll.
rem
rem -WbBusLogWagon=601  je 2 s eine Zeile mit Bogenlaenge, Unterkante,
rem   Fahrbahn- UND Gelaendehoehe, Zustand (faehrt/VERWEILT noch n s von 600 s),
rem   Richtung und Mitfahrt -> der Beleg fuer den ganzen Umlauf.
rem -WbBusLog          zusaetzlich die Kurzzeile JEDES Wagens (alle Linien
rem   fahren durchgehend) - so ist auch belegt, dass die uebrigen Wagen laufen.
rem -WbBusGroundAudit  Boden/Fahrbahn-Messung je 25 m auf beiden Linien ->
rem   Saved\Diagnose\bus_ground_audit_line<ref>.txt, inkl. der Stellen mit der
rem   groessten Spur/Boden-Abweichung und dem Ort jeder Fehlmessung.
rem -WbBusRide/-WbBusRideExit  Mitfahrt: nach n1 s in den naechsten haltenden Bus
rem   (Wagennummer steht im Log), nach n2 s wieder aussteigen.
rem -WbShot=n          ein Bild AUS dem fahrenden Bus n s nach dem Start.
rem -WbQuitAfter=n     Ende nach n s Spielzeit (ein Umlauf Linie 6 dauert 104 min,
rem   6500 s deckt ihn mit Reserve ab).
rem
rem Aufruf: Tools\run_bus_umlauf.cmd [Karte] [QuitAfter] [Ride] [RideExit] [Shot]
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\shot_umlauf.log
set MAP=%~1
REM  Karte NICHT fest verdrahten - sie kommt aus
REM  Config\DefaultEngine.ini (siehe Tools\karte.cmd).
call "%~dp0karte.cmd"
if "%MAP%"=="" set MAP=%WB_MAP%
set QUIT=%~2
if "%QUIT%"=="" set QUIT=6500
set RIDE=%~3
if "%RIDE%"=="" set RIDE=900
set RIDEEXIT=%~4
if "%RIDEEXIT%"=="" set RIDEEXIT=1300
set SHOT=%~5
if "%SHOT%"=="" set SHOT=1000
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" /Game/Maps/%MAP% -game -WbBusLog -WbBusLogWagon=601 -WbBusGroundAudit -WbBusRide=%RIDE% -WbBusRideExit=%RIDEEXIT% -WbZuFuss=8 -WbShot=%SHOT% -WbQuitAfter=%QUIT% -windowed -ResX=1600 -ResY=900 -stdout -unattended -nop4 > "%LOG%" 2>&1
exit /b %ERRORLEVEL%
