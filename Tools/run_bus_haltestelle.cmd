@echo off
rem Belegbilder "Bus steht an der Halte auf der Fahrbahn": parkt je einen Wagen
rem beider Richtungen an Halt N (-WbBusParkStop) und macht Bilder aus der Naehe.
rem Halt 7 = Stadtstrasse (Bogen 2592 m), Halt 20 = Rheinbrueckenkopf.
rem
rem Aufruf: Tools\run_bus_haltestelle.cmd <Halt> [Karte]
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\shot_haltestelle.log
set POSES=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Saved\Diagnose\poses_busfahrbahn\fahrbahn_bahnsteig.txt
set STOP=%~1
if "%STOP%"=="" set STOP=7
set MAP=%~2
REM  Karte NICHT fest verdrahten - sie kommt aus
REM  Config\DefaultEngine.ini (siehe Tools\karte.cmd).
call "%~dp0karte.cmd"
if "%MAP%"=="" set MAP=%WB_MAP%
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" /Game/Maps/%MAP% -game -WbBusLog -WbBusGroundAudit -WbBusParkStop=%STOP% -WbShotWhenReady -WbShotPoseFile="%POSES%" -WbShotDelay=12 -WbPoseSettle=8 -WbQuitAfter=240 -windowed -ResX=1600 -ResY=900 -stdout -unattended -nop4 > "%LOG%" 2>&1
exit /b %ERRORLEVEL%
