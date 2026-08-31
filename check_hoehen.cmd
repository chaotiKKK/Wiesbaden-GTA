@echo off
REM Hoehenpruefung ueber das gesamte Stadtgebiet.
REM
REM Baut Strassen und Gelaende neu (Gebaeude und Ausstattung bleiben aus, das
REM spart den Grossteil der Bauzeit) und vergleicht dann ALLE 22.227
REM Kreuzungen mit der Gelaendehoehe an derselben Stelle.
REM
REM Ergebnis: hoehen_report.json
set PROJ=C:\Users\ssonn\aivideo\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\Users\ssonn\aivideo\WiesbadenReal\check_hoehen.log
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" -ExecCmds="py C:\Users\ssonn\aivideo\WiesbadenReal\Tools\check_heights.py" -unattended -nosplash -nop4 > "%LOG%" 2>&1
exit /b %ERRORLEVEL%
