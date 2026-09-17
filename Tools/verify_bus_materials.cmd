@echo off
REM Belegt den Bus-Materialfix (Tools/verify_bus_materials.py): Elternmaterial,
REM Nanite-Flag, BaseColor-Eingang und die Texturen der 41 Instanzen.
REM
REM Ergebnis steht NICHT im cmd-Strom (unreal.log aus pythonscript-Commandlets
REM erreicht ihn in diesem Projekt nicht), sondern in
REM   Saved\Diagnose\bus_material_check.txt
setlocal
set ROOT=%~dp0..
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" ^
  "%ROOT%\WiesbadenReal.uproject" ^
  -run=pythonscript -script="%ROOT:\=/%/Tools/verify_bus_materials.py" ^
  -stdout -unattended -nopause -nosplash -nop4 > "%ROOT%\Saved\Logs\wb_verify_bus_materials.log" 2>&1
exit /b %ERRORLEVEL%
