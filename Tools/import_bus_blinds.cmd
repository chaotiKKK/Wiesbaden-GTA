@echo off
REM Importiert die Zielschild-Texturen/-Materialien aller Linien nach
REM /Game/Vehicles/Bus/Blind (Tools/import_bus_blinds.py).
REM
REM Die PNGs macht vorher Tools/make_bus_blind_textures.py (Pillow, ohne Engine).
REM Die Engine MUSS die installierte sein (C:\Program Files\Epic Games\UE_5.8):
REM das Modul ist gegen deren PCH gebaut, die Kopie unter C:\freebuff\...\UE_5.8
REM ist eine andere Version (C2953/C2011, siehe build_gate1.cmd).
REM
REM Ergebnis im Log: je Schild eine Zeile "###BLIND### T_... + M_..." und am Ende
REM "###BLIND### ENDE ok=<n>/<m>". ok < m heisst: PNG fehlt (Ursache steht
REM darueber) - dann Tools/make_bus_blind_textures.py laufen lassen.
cd /d "%~dp0.."
set LOG=%~dp0..\Saved\Logs\wb_import_bus_blinds.log
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" ^
  "C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject" ^
  -run=pythonscript -script="C:/freebuff/WiesbadenReal_Sicherung/WiesbadenReal/Tools/import_bus_blinds.py" ^
  -stdout -unattended -nopause -nosplash -nop4 > "%LOG%" 2>&1
echo EXITCODE %ERRORLEVEL% >> "%LOG%"
