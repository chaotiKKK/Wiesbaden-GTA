@echo off
REM Einmal-Probe der StaticMesh-Box-API (Tools\probe_box_api.py).
REM Ergebnis: Saved\Diagnose\probe_box_api.txt
call "%~dp0engine_run_lock.cmd" -Modus Start -Name probe_box_api
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
set LOG=%~dp0..\Saved\Logs\wb_probe_box_api.log
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" ^
  "C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject" ^
  -run=pythonscript -script="C:/freebuff/WiesbadenReal_Sicherung/WiesbadenReal/Tools/probe_box_api.py" ^
  -stdout -unattended -nopause -nosplash -nop4 > "%LOG%" 2>&1
echo EXITCODE %ERRORLEVEL% >> "%LOG%"
exit /b 0
