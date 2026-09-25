@echo off
call "%~dp0cleanup_unreal_processes.cmd"
if errorlevel 1 exit /b 1
rem Beleglauf "Sitzen die Busse auf der Fahrbahn?" + Wendezeit am fernen Endpunkt.
rem
rem -WbBusGroundAudit misst je 25 m Fahrstrecke, WELCHE Flaeche den Bus traegt
rem   (Fahrbahn-Kollision vs. Landscape) und wie weit die naechste Flaeche
rem   darunter liegt -> Saved\Diagnose\bus_ground_audit_line<ref>.txt
rem -WbBusClock=2500 stellt die Dienstzeit so ein, dass Wagen 601 gerade am
rem   fernen Endpunkt (Mainz-Gonsenheim Wildpark) ankommt und dort seine
rem   10 Minuten Wendezeit absitzt - im Log mit "VERWEILT noch <n> s (Endpunkt)".
rem
rem Aufruf: Tools\run_bus_ground.cmd [Karte] [QuitAfter]
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\shot_ground.log
set MAP=%~1
REM  Karte NICHT fest verdrahten - sie kommt aus
REM  Config\DefaultEngine.ini (siehe Tools\karte.cmd).
call "%~dp0karte.cmd"
if "%MAP%"=="" set MAP=%WB_MAP%
set QUIT=%~2
if "%QUIT%"=="" set QUIT=240
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" /Game/Maps/%MAP% -game -WbBusLog -WbBusGroundAudit -WbBusClock=2500 -WbQuitAfter=%QUIT% -windowed -ResX=1600 -ResY=900 -stdout -unattended -nop4 > "%LOG%" 2>&1
exit /b %ERRORLEVEL%
