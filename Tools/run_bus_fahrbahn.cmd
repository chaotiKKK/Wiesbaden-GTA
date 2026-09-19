@echo off
rem Beleglauf: Sitzen die Busse auf der FAHRBAHN? (Audit + Wendezeit + Bilder)
rem
rem -WbBusGroundAudit  misst je 25 m, welche Flaeche den Bus traegt (Fahrbahn-
rem   Kollision der Stadt vs. Landscape) UND den Abstand zur Fahrbahn des
rem   Strassennetzes -> Saved\Diagnose\bus_ground_audit_line<ref>.txt
rem -WbBusClock=2500  stellt Wagen 601 fuer die ganze Aufnahme an den fernen
rem   Endpunkt (Halt 39, Mainz-Gonsenheim Wildpark) -> Log zeigt
rem   "VERWEILT noch <n> s von 600 s (Endpunkt)".
rem -WbShotWhenReady -WbShotPoseFile  macht die Belegbilder aus der Naehe.
rem
rem Aufruf: Tools\run_bus_fahrbahn.cmd [Karte] [QuitAfter]
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\shot_fahrbahn.log
set POSES=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Saved\Diagnose\poses_busfahrbahn\fahrbahn.txt
set MAP=%~1
REM  Karte NICHT fest verdrahten - sie kommt aus
REM  Config\DefaultEngine.ini (siehe Tools\karte.cmd).
call "%~dp0karte.cmd"
if "%MAP%"=="" set MAP=%WB_MAP%
set QUIT=%~2
if "%QUIT%"=="" set QUIT=300
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" /Game/Maps/%MAP% -game -WbBusLog -WbBusGroundAudit -WbBusClock=2500 -WbShotWhenReady -WbShotPoseFile="%POSES%" -WbShotDelay=12 -WbPoseSettle=8 -WbQuitAfter=%QUIT% -windowed -ResX=1600 -ResY=900 -stdout -unattended -nop4 > "%LOG%" 2>&1
exit /b %ERRORLEVEL%
