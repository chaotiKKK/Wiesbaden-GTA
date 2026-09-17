@echo off
REM Setzt fehlende Material-Usage-Flags (Nanite, InstancedStaticMeshes), die UE
REM sonst durch das Default-Material ersetzt - graue Fahrzeuge/Gleise trotz
REM vorhandener Texturen. Quelle der Liste ist das Spiel-Log der letzten Sitzung.
REM
REM WICHTIG: ein -run=pythonscript-Commandlet ueberschreibt
REM Saved\Logs\WiesbadenReal.log schon beim Start mit seinem eigenen Log - die
REM Warnzeilen waeren also schon weg, bevor das Skript liest. Darum erst eine
REM Kopie, dann -ABSLOG, damit der Commandlet-Strom das Spiel-Log nicht anfasst.
REM
REM   Tools\fix_material_flags.cmd            (Quelle: Kopie des Spiel-Logs)
REM   Tools\make_material_flags_list.cmd      (nur ein Spiel-Lauf, der die Liste fuellt)
REM Ergebnis: Saved\Diagnose\material_usage_flags.txt
setlocal
set ROOT=%~dp0..
set SRC=%ROOT%\Saved\Logs\WiesbadenReal.log
set COPY=%ROOT%\Saved\Logs\material_flags_source.log
if exist "%SRC%" copy /Y "%SRC%" "%COPY%" > nul
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" ^
  "%ROOT%\WiesbadenReal.uproject" ^
  -run=pythonscript -script="%ROOT:\=/%/Tools/fix_material_usage_flags.py" ^
  -stdout -unattended -nopause -nosplash -nop4 ^
  -ABSLOG="%ROOT%\Saved\Logs\wb_fix_material_flags.log" > "%ROOT%\Saved\Logs\wb_fix_material_flags.out" 2>&1
exit /b %ERRORLEVEL%
