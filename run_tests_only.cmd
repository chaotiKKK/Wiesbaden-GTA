@echo off
rem Fuehrt nur die Automation-Tests aus (Build ist warm). Editor-Log nach Saved/Logs.
"C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/Users/ssonn/aivideo/WiesbadenReal/WiesbadenReal.uproject" -ExecCmds="Automation RunTests WiesbadenReal; Quit" -unattended -nop4 -nullrhi -NoSound -NoP4
