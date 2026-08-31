@echo off
rem Fuehrt nur die Automation-Tests aus (Build ist warm). Editor-Log nach Saved/Logs.
"C:/freebuff/WiesbadenReal_Sicherung/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/freebuff/WiesbadenReal_Sicherung/WiesbadenReal/WiesbadenReal.uproject" -ExecCmds="Automation RunTests WiesbadenReal; Quit" -unattended -nop4 -nullrhi -NoSound -NoP4
