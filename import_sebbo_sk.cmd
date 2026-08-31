@echo off
REM Animierte Sebbo-Spielfigur importieren (Skelett + drei Bewegungen).
set PROJ=C:\Users\ssonn\aivideo\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\Users\ssonn\aivideo\WiesbadenReal\import_sebbo_sk.log
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" -ExecCmds="py C:\Users\ssonn\aivideo\WiesbadenReal\Tools\import_sebbo_skeletal.py" -unattended -nosplash -nop4 > "%LOG%" 2>&1
exit /b %ERRORLEVEL%
