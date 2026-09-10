@echo off
rem Verankert die leeren Komponenten aller Zell-Actors der gebackenen Karte
rem und speichert die Karte neu (Re-Bake ohne Neubau, s. Tools/anchor_chunk_bounds.py).
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject" -run=pythonscript -script="C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Tools\anchor_chunk_bounds.py" -unattended -nop4 -nosplash -nullrhi > "C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\anchor_bounds.log" 2>&1
exit /b %ERRORLEVEL%
