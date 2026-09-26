@echo off
REM Importiert die drei Trenn-Stueck-Meshes des Plasmacutters
REM (Tools\Blender\build_cutpieces.py -> Content\Data\Raw\Cutpieces\SM_Cutpiece_*.fbx)
REM nach /Game/Waffen/Cutpieces.
REM
REM Die Engine MUSS die installierte sein (C:\Program Files\Epic Games\UE_5.8):
REM das Modul ist gegen deren PCH gebaut. Motorsperre wie bei den Laeufen:
REM zwei Engines nebeneinander sind im Projekt als Absturz dokumentiert.
REM
REM Ergebnis im Log: je Stueck eine "###CUTIMP###"-Zeile, am Ende
REM "###CUTIMP### ENDE ok=<n>/3" und "EXITCODE". ok < 3 = Ursache steht
REM darueber (meist Massabweichung = UnitScaleFactor).
call "%~dp0engine_run_lock.cmd" -Modus Start -Name import_cutpieces
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
set LOG=%~dp0..\Saved\Logs\wb_import_cutpieces.log
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" ^
  "C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject" ^
  -run=pythonscript -script="C:/freebuff/WiesbadenReal_Sicherung/WiesbadenReal/Tools/import_cutpieces.py" ^
  -stdout -unattended -nopause -nosplash -nop4 > "%LOG%" 2>&1
echo EXITCODE %ERRORLEVEL% >> "%LOG%"
exit /b 0
