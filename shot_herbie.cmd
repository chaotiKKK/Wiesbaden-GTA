@echo off
REM Spiellauf im Auto: Herbie-Lackierung pruefen (Bild bei 45 s).
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\shot_herbie.log
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" /Game/Maps/WiesbadenCity_Alkis3 -game -windowed -ResX=1600 -ResY=900 -WbShot=45 -WbQuitAfter=55 -stdout -unattended -nop4 > "%LOG%" 2>&1
exit /b %ERRORLEVEL%
