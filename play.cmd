@echo off
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
"C:\freebuff\WiesbadenReal_Sicherung\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" /Game/Maps/WiesbadenCity_Alkis -game -windowed -ResX=1600 -ResY=900 > C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\play.log 2>&1
