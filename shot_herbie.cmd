@echo off
REM Spiellauf im Auto: Herbie-Lackierung pruefen (Bild bei 45 s).
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\shot_herbie.log
REM  Karte NICHT fest verdrahten - sie kommt aus
REM  Config\DefaultEngine.ini (siehe Tools\karte.cmd).
call "%~dp0Tools\karte.cmd"
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" %WB_MAP_PFAD% -game -windowed -ResX=1600 -ResY=900 -WbShot=45 -WbQuitAfter=55 -stdout -unattended -nop4 > "%LOG%" 2>&1
exit /b %ERRORLEVEL%
