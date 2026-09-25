@echo off
call "%~dp0cleanup_unreal_processes.cmd"
if errorlevel 1 exit /b 1
rem Beleglauf Mitfahrt mit einem Wagen der neuen Nummernkreise (601+/301+):
rem -WbZuFuss=8     Spieler steigt nach 8 s aus dem Auto (nur zu Fuss einsteigbar)
rem -WbBusRide=18  die Automatik setzt ihn nach 18 s neben den gewaehlten Bus und
rem                 steigt beim naechsten Halt ein; das Log nennt Linie + WAGEN-Nummer
rem -WbBusRideWagon=<Nr>  WELCHER Wagen (ohne die Nummer nimmt die Automatik den
rem                 ersten HALTENDEN Bus, also einen beliebigen Wagen)
rem -WbBusRideExit=150   nach 150 s Mitfahrt automatisch wieder aussteigen
rem -WbShot=<Sekunden> Bild AUS dem fahrenden Bus (Fahrgastkamera) ->
rem                 Saved\Diagnose\Messstelle*.png. Achtung: der Schuss muss NACH
rem                 dem Einsteigen liegen (das Einsteigen wartet auf den naechsten
rem                 Halt des Wagens), sonst zeigt das Bild den Fussgaenger neben dem
rem                 Bus. Das Log nennt die Einsteigezeit ("Mitfahrt Linie .. Wagen ..").
rem
rem ACHTUNG: ein Fensterlauf im Hintergrund wird gedrosselt (gemessen 10x langsam);
rem fuer Messungen ohne Bild Tools\run_bus_audit.cmd (kopflos) nehmen.
rem
rem Aufruf: Tools\run_bus_mitfahrt.cmd [Wagen] [Karte] [QuitAfter] [ShotSekunden]
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\shot_mitfahrt2.log
set WAGON=%~1
if "%WAGON%"=="" set WAGON=601
set MAP=%~2
REM  Karte NICHT fest verdrahten - sie kommt aus
REM  Config\DefaultEngine.ini (siehe Tools\karte.cmd).
call "%~dp0karte.cmd"
if "%MAP%"=="" set MAP=%WB_MAP%
set QUIT=%~3
if "%QUIT%"=="" set QUIT=260
set SHOT=%~4
if "%SHOT%"=="" set SHOT=75
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" /Game/Maps/%MAP% -game -WbBusLog -WbBusLogWagon=%WAGON% -WbBusRide=18 -WbBusRideWagon=%WAGON% -WbBusRideExit=150 -WbZuFuss=8 -WbShot=%SHOT% -WbQuitAfter=%QUIT% -windowed -ResX=1600 -ResY=900 -stdout -unattended -nop4 > "%LOG%" 2>&1
exit /b %ERRORLEVEL%
