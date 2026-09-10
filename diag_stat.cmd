@echo off
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" /Game/Maps/WiesbadenCity_Alkis -game -WbScreenshot -windowed -ResX=1280 -ResY=720 -ExecCmds="stat unit" -stdout -unattended -nop4 > C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\diag_stat.log 2>&1
exit /b %ERRORLEVEL%
