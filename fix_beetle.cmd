@echo off
REM Kaefer-Grundmaterial neu bauen (Abtasttyp-Fehler im gekauften Asset).
REM Ein Skript je Editorstart - die Dreierkette in einem ExecCmds ist am
REM 28.08. nach dem ersten Glied eingefroren und stand vier Stunden bei
REM Frame 0.
set PROJ=C:\Users\ssonn\aivideo\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\Users\ssonn\aivideo\WiesbadenReal\fix_beetle.log
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" -ExecCmds="py C:\Users\ssonn\aivideo\WiesbadenReal\Tools\fix_beetle_material.py" -unattended -nosplash -nop4 > "%LOG%" 2>&1
exit /b %ERRORLEVEL%
