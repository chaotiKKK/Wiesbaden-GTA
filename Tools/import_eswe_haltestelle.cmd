@echo off
REM ESWE-Haltestelle (Wartehalle, Haltemast, DFI) nach Unreal importieren (VOLLER Editor).
REM
REM Der Kommandlet-Weg -run=pythonscript liefert fuer set_material keine
REM gueltige Zuweisung; darum der volle Editor mit -ExecCmds. Das Skript
REM beendet den Editor selbst, kein zusaetzliches Quit anhaengen.
REM
REM Ergebnis steht in import_eswe_haltestelle_result.txt in der Projektwurzel -
REM Python-Ausgaben erreichen den cmd-Strom nicht zuverlaessig.
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set SKRIPT=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Tools\import_eswe_haltestelle.py
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\import_eswe_haltestelle.log
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" -ExecCmds="py exec(open('%SKRIPT:\=/%').read())" -unattended -nosplash -nop4 > "%LOG%" 2>&1
echo EXITCODE %ERRORLEVEL% >> "%LOG%"
