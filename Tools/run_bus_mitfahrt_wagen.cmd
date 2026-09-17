@echo off
rem Beleglauf MITFAHRT auf einem BESTIMMTEN Wagen (-WbBusRideWagon), ohne
rem Fenster: die Automatik setzt den Fahrgast neben Wagen 601/301 und steigt
rem ein, sobald der Wagen haelt. Umlauf-Protokoll und Mitfahrt-Zeilen landen
rem im selben Lauf, damit sich "Mitfahrt aktiv" eindeutig auf die Wagen-Nummer
rem beziehen laesst.
rem
rem Aufruf: Tools\run_bus_mitfahrt_wagen.cmd [Wagen] [Karte] [Spielsekunden] [Log]
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set WAGON=%~1
if "%WAGON%"=="" set WAGON=601
set MAP=%~2
if "%MAP%"=="" set MAP=WiesbadenCity_Alkis15
set QUIT=%~3
if "%QUIT%"=="" set QUIT=280
set LOG=%~4
if "%LOG%"=="" set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\mitfahrt_wagen.log
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" /Game/Maps/%MAP% -game -nullrhi -NoSound -WbBusLog -WbBusLogWagon=%WAGON% -WbBusRide=15 -WbBusRideWagon=%WAGON% -WbBusRideExit=200 -WbZuFuss=8 -WbBusGroundAudit -WbQuitAfter=%QUIT% -unattended -nop4 -stdout > "%LOG%" 2>&1
exit /b %ERRORLEVEL%
