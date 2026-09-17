@echo off
cd /d "%~dp0.."
"C:\freebuff\WiesbadenReal_Sicherung\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" ^
  "C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject" ^
  -run=pythonscript -script="C:/freebuff/WiesbadenReal_Sicherung/WiesbadenReal/Tools/check_ka52_mats.py" ^
  -stdout -unattended -nopause -nosplash > Tools\check_ka52_mats_log.txt 2>&1
echo DONE >> Tools\check_ka52_mats_log.txt
