@echo off
cd /d "%~dp0.."
REM Installierte Engine (siehe Tools\build_gate1.cmd: niemals die beiden
REM UE-5.8-Baeume mischen, sonst PCH-Typneudefinitionen).
"c:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" ^
  "C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject" ^
  -run=pythonscript -script="C:/freebuff/WiesbadenReal_Sicherung/WiesbadenReal/Tools/verify_ka52_actor.py" ^
  -stdout -unattended -nopause -nosplash > Tools\verify_ka52_actor_log.txt 2>&1
echo DONE >> Tools\verify_ka52_actor_log.txt
