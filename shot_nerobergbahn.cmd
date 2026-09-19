@echo off
rem Ansichten der Nerobergbahn auf der gebackenen Karte.
rem
rem Aufruf:  shot_nerobergbahn.cmd [Posendatei] [Startverzoegerung] [Ballast] [Kurbel]
rem   %3  -WbBallast=<0..1> Anfangs-Fuellstand (Standard 0,75). Zwei Laeufe mit
rem       verschiedenem Fuellstand und gleicher Pose zeigen die Bahn des
rem       Schwimmers im Schauglas.
rem   %4  -WbKurbel=<Sekunden> oeffnet den Wasserschieber (-1 = zu).
rem   %1  Posendatei in Saved\Diagnose (Standard: poses_nerobergbahn.txt,
rem       die Trassen-/Querschnittsansichten)
rem   %2  -WbShotDelay in Sekunden (Standard 10) - steuert ueber die Fahrzeit
rem       auch, wo der Wagen steht: 7,3 km/h = 2,17 m/s, und die Posen stehen
rem       im Abstand von -WbPoseSettle (7 s) nacheinander.
rem
rem Fertige Bilder landen als Saved\Diagnose\WbSeries_<Nr>.png (Reihenfolge
rem der Posen).

set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\shot_nerobergbahn.log
set POSE=%~1
if "%POSE%"=="" set POSE=poses_nerobergbahn.txt
set DELAY=%~2
if "%DELAY%"=="" set DELAY=10
set BALLAST=%~3
if "%BALLAST%"=="" set BALLAST=0.75
set KURBEL=%~4
if "%KURBEL%"=="" set KURBEL=-1

REM  Karte NICHT fest verdrahten - sie kommt aus
REM  Config\DefaultEngine.ini (siehe Tools\karte.cmd).
call "%~dp0Tools\karte.cmd"
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" %WB_MAP_PFAD% -game -WbShotWhenReady -WbShotDelay=%DELAY% -WbPoseSettle=7 -WbBallast=%BALLAST% -WbKurbel=%KURBEL% -WbShotPoseFile="C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Saved\Diagnose\%POSE%" -windowed -ResX=1920 -ResY=1080 -stdout -unattended -nop4 > "%LOG%" 2>&1
exit /b %ERRORLEVEL%
