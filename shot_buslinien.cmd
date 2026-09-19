@echo off
rem Belegbilder der beiden OEPNV-Linien: Linie 6 (Nordfriedhof <-> Mainz-Gonsenheim
rem Wildpark) und Linie 3 (Nordfriedhof <-> Biebrich Rheinufer).
rem
rem Der Lauf faehrt eine POSEN-SERIE ab (Saved\Diagnose\poses_buslinien\linien.txt)
rem und legt je Pose ein Bild ab: Saved\Diagnose\WbSeries_000.png (Startpunkt
rem beider Linien), _001 (gemeinsamer Korridor Welfenstrasse), _002
rem (Kastel/Brueckenkopf), _003 (Endstation Wildpark in Mainz).
rem
rem -WbBusLog schreibt alle 2 s je Bus "Linie <ref>: Dienstzeit ..., n Wagen im
rem Umlauf" plus Zeile je Wagen mit Bogenlaenge und SICHTBAR/VERWEILT - nur damit
rem ist belegt, dass die festen Wagen wirklich durchfahren.
rem
rem Ergebnis: Saved\Diagnose\WbSeries_*.png + Saved\Logs\WiesbadenReal.log
rem
rem Aufruf: shot_buslinien.cmd [Karte]

set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\shot_buslinien.log
set POSES=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Saved\Diagnose\poses_buslinien\linien.txt
set MAP=%~1
REM  Karte NICHT fest verdrahten - sie kommt aus
REM  Config\DefaultEngine.ini (siehe Tools\karte.cmd).
call "%~dp0Tools\karte.cmd"
if "%MAP%"=="" set MAP=%WB_MAP%

"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" /Game/Maps/%MAP% -game -WbBusLog -WbShotWhenReady -WbShotPoseFile="%POSES%" -WbShotDelay=12 -WbPoseSettle=8 -WbQuitAfter=300 -windowed -ResX=1920 -ResY=1080 -stdout -unattended -nop4 > "%LOG%" 2>&1
exit /b %ERRORLEVEL%
