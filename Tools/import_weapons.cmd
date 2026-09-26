@echo off
REM Importiert die acht Waffen-Meshes (Tools/Blender/build_weapons.py ->
REM Content\Data\Raw\Waffen\SM_Waffe_*.fbx) nach /Game/Waffen/Meshes.
REM
REM Die Engine MUSS die installierte sein (C:\Program Files\Epic Games\UE_5.8):
REM das Modul ist gegen deren PCH gebaut (C2953/C2011 sonst, siehe build_gate1.cmd).
REM Motorsperre wie bei den Laeufen: zwei Engines nebeneinander sind im Projekt
REM als Absturz dokumentiert.
REM
REM Ergebnis im Log: je Waffe eine "###WAFFENIMP###"-Zeile, am Ende
REM "###WAFFENIMP### ENDE ok=<n>/8" und "EXITCODE". ok < 8 = Ursache steht
REM darueber (meist Massabweichung = UnitScaleFactor).
call "%~dp0engine_run_lock.cmd" -Modus Start -Name import_weapons
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
set LOG=%~dp0..\Saved\Logs\wb_import_weapons.log
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" ^
  "C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject" ^
  -run=pythonscript -script="C:/freebuff/WiesbadenReal_Sicherung/WiesbadenReal/Tools/import_weapons.py" ^
  -stdout -unattended -nopause -nosplash -nop4 > "%LOG%" 2>&1
echo EXITCODE %ERRORLEVEL% >> "%LOG%"
exit /b 0
