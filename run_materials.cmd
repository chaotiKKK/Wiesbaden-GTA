@echo off
"C:\freebuff\WiesbadenReal_Sicherung\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject" -run=pythonscript -script="C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Tools\build_materials.py" -unattended -nop4 > materials.log 2>&1
