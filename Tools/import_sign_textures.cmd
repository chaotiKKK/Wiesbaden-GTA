@echo off
REM Importiert die Schild-PNGs als UE-Texturen unter /Game/Textures/TrafficSigns.
REM   Tools\import_sign_textures.cmd          -> fehlende Assets anlegen
REM   set SIGN_IMPORT_MODE=info  & Tools\...  -> nur Einstellungen anzeigen
REM   set SIGN_IMPORT_MODE=alle  & Tools\...  -> alle PNGs neu importieren
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set EDCMD=C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Saved\Logs\wb_signimport.log
echo === Schild-Import Start %date% %time% === > "%LOG%"
"%EDCMD%" "%PROJ%" -run=pythonscript -script="C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Tools\import_sign_textures.py" -unattended -nop4 -stdout >> "%LOG%" 2>&1
echo __EXIT=%ERRORLEVEL%__ >> "%LOG%"
