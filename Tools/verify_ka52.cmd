@echo off
call "%~dp0cleanup_unreal_processes.cmd"
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
REM Installierte Engine (5.8.2) - siehe Tools\build_gate1.cmd (PCH-Falle).
"c:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" ^
  "C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject" ^
  -run=pythonscript -script="C:/freebuff/WiesbadenReal_Sicherung/WiesbadenReal/Tools/verify_ka52.py" ^
  -stdout -unattended -nopause -nosplash > Tools\verify_ka52_log.txt 2>&1
echo DONE >> Tools\verify_ka52_log.txt
