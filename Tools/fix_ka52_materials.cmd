@echo off
cd /d "%~dp0.."
REM Installierte Engine (5.8.2). Die Plattenkopie C:\freebuff\...\UE_5.8 darf
REM NICHT dafuer benutzt werden: gemeinsames PCH im Projekt-Intermediate ->
REM Typneudefinitionen im Engine-Header (siehe Tools\build_gate1.cmd).
"c:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" ^
  "C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject" ^
  -run=pythonscript -script="C:/freebuff/WiesbadenReal_Sicherung/WiesbadenReal/Tools/fix_ka52_materials.py" ^
  -stdout -unattended -nopause -nosplash > Tools\fix_ka52_materials_log.txt 2>&1
echo DONE >> Tools\fix_ka52_materials_log.txt
