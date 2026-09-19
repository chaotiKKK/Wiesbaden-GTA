@echo off
REM Verifikations-Shot der Relief-Fassaden auf der gebackenen Alkis4-Karte.
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\shot_relief.log
REM  Karte NICHT fest verdrahten - sie kommt aus
REM  Config\DefaultEngine.ini (siehe Tools\karte.cmd).
call "%~dp0Tools\karte.cmd"
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" %WB_MAP_PFAD% -game -windowed -ResX=1920 -ResY=1080 -WbShotWhenReady -WbShotScale=2 -stdout -unattended -nop4 > "%LOG%" 2>&1
exit /b %ERRORLEVEL%
