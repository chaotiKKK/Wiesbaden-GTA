@echo off
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set VARIANT=%1
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\shot_facade_%VARIANT%.log
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" /Game/Maps/WiesbadenCity_Alkis4 -game -WbShotWhenReady -WbShotDelay=12 -WbGotoFacade=%VARIANT% -windowed -ResX=1920 -ResY=1080 -stdout -unattended -nop4 > "%LOG%" 2>&1
exit /b %ERRORLEVEL%
