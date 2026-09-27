@echo off
REM Importiert die BigSoundBank-Aufnahmen nach /Game/Audio/Samples.
REM Ergebnis: Saved\Logs\audio_samples_import.log + Saved\Diagnose\audio_samples_import.txt
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Saved\Logs\audio_samples_import.log
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%PROJ%" -run=pythonscript -script="C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Tools\import_audio_samples.py" -unattended -nop4 -nullrhi -nosplash -ABSLOG="%LOG%" > "%LOG%.out" 2>&1
echo EXITCODE %ERRORLEVEL% >> "%LOG%.out"
