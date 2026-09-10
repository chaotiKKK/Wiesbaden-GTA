@echo off
REM Fassaden neu verdrahtet und Kaefer-Material repariert.
REM
REM Reihenfolge: Grundmaterial samt Instanzen (import_materials), dann die
REM Stadtmaterialien (build_materials), zuletzt der Kaefer - dessen Skript
REM schliesst den Editor selbst.
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set T=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Tools
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\fix_materials.log
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" -ExecCmds="py %T%\import_materials.py, py %T%\build_materials.py, py %T%\fix_beetle_material.py" -unattended -nosplash -nop4 > "%LOG%" 2>&1
exit /b %ERRORLEVEL%
