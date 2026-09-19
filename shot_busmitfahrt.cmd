@echo off
rem Mitfahrt im Linienbus aufnehmen - ohne Tastendruck.
rem
rem Der Spieler startet am Steuer eines Autos; -WbZuFuss laesst ihn aussteigen,
rem -WbBusRide setzt ihn danach selbst in einen gerade haltenden Bus. Die Kamera
rem der Mitfahrt steht IM Wagen (Cockpit), das Bild zeigt also den eingerichteten
rem Innenraum waehrend der Fahrt - der Beleg dafuer, dass der Fahrgast NICHT mehr
rem in der geschlossenen Aussenhaut sitzt.
rem
rem Aufruf: shot_busmitfahrt.cmd [Karte] [Park-Halt] [Bild bei s] [Einsteigen bei s] [Ende bei s]
rem   %1 Karte (Standard: die Default-Karte aus Config, siehe Tools-karte.cmd)
rem   %2 -WbBusParkStop (>= 0: Busse stehen fest an dieser Halte, -1 = Fahrplan)
rem   %3 -WbShot=<s>         Bildzeitpunkt
rem   %4 -WbBusRide=<s>     Einsteigen; -1 schaltet die Automatik ab
rem   %5 -WbQuitAfter=<s>   Ende des Laufs
rem
rem Ergebnis: Saved\Diagnose\Messstelle00000.png + Saved\Logs\WiesbadenReal.log
rem (dort "Bus: Fahrgast eingestiegen" und "Bus-Innenraum: ... Fahrgastauge ...").
rem
rem Warum ueber eine .cmd: ein direkt aus PowerShell gestarteter Lauf ergab
rem wiederholt einen LEEREN Log und kein Bild (siehe AGENTS.md, "-WbShot").

set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\shot_busmitfahrt.log
set MAP=%~1
REM  Karte NICHT fest verdrahten - sie kommt aus
REM  Config\DefaultEngine.ini (siehe Tools\karte.cmd).
call "%~dp0Tools\karte.cmd"
if "%MAP%"=="" set MAP=%WB_MAP%
set PARK=%~2
if "%PARK%"=="" set PARK=-1
set SHOT=%~3
if "%SHOT%"=="" set SHOT=60
set RIDE=%~4
if "%RIDE%"=="" set RIDE=25
set QUIT=%~5
if "%QUIT%"=="" set QUIT=75

"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" /Game/Maps/%MAP% -game -WbZuFuss=5 -WbBusRide=%RIDE% -WbBusParkStop=%PARK% -WbShot=%SHOT% -WbQuitAfter=%QUIT% -windowed -ResX=1920 -ResY=1080 -stdout -unattended -nop4 > "%LOG%" 2>&1
exit /b %ERRORLEVEL%
