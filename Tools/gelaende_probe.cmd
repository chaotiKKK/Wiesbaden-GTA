@echo off
REM Gelaende gegen Pflaster messen (Tools/gelaende_probe.py). Ergebnis: gelaende_probe_result.txt
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set SKRIPT=C:/freebuff/WiesbadenReal_Sicherung/WiesbadenReal/Tools/gelaende_probe.py
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" -ExecCmds="py exec(open('%SKRIPT%').read())" -unattended -nosplash -nop4 > C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Saved\Logs\gelaende_probe.log 2>&1
