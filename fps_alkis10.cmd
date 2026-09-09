@echo off
REM Alkis10 Framerate am Spielerstart (Zentrum), stat unit HUD sichtbar.
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\fps_alkis10.log
"C:\freebuff\WiesbadenReal_Sicherung\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" /Game/Maps/WiesbadenCity_Alkis10 -game -windowed -ResX=1280 -ResY=720 -WbShot=20 -WbQuitAfter=28 -ExecCmds="stat unit" -stdout -unattended -nop4 > "%LOG%" 2>&1
exit /b %ERRORLEVEL%
