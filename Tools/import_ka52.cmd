@echo off
cd /d "%~dp0.."
"C:\freebuff\WiesbadenReal_Sicherung\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" ^
  "C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject" ^
  -run=pythonscript -script="C:/freebuff/WiesbadenReal_Sicherung/WiesbadenReal/Tools/import_ka52.py" ^
  -stdout -unattended -nopause -nosplash > Tools\import_ka52_log.txt 2>&1
echo DONE >> Tools\import_ka52_log.txt
