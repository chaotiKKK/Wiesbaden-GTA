@echo off
REM Importiert die Innenraum-Texturen des Fahrgast-Busses und baut je Flaechenart
REM ein Material (Textur x Vertexfarbe) nach /Game/Vehicles/Bus/Interior
REM (Tools/import_bus_interior.py).
REM
REM Die PNGs macht vorher Tools/make_bus_interior_textures.py (Pillow, ohne Engine).
REM Die Engine MUSS die installierte sein (C:\Program Files\Epic Games\UE_5.8):
REM das Modul ist gegen deren PCH gebaut, die Kopie unter C:\freebuff\...\UE_5.8
REM ist eine andere Version (C2953/C2011, siehe build_gate1.cmd).
REM
REM Ergebnis im Log: je Material eine Zeile "###WBINT### T_... 512x512 -> M_..." mit
REM der BaseColor-Gegenprobe und am Ende "###WBINT### ENDE ok=<n>/<m>".
REM ok < m heisst: PNG fehlt oder eine Verbindung griff nicht - die Ursache steht
REM in der Zeile darueber (FEHLER ...).
setlocal
set ROOT=%~dp0..
set LOG=%ROOT%\Saved\Logs\wb_import_bus_interior.log
"%ProgramFiles%\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" ^
  "%ROOT%\WiesbadenReal.uproject" ^
  -run=pythonscript -script="%ROOT:\=/%/Tools/import_bus_interior.py" ^
  -stdout -unattended -nopause -nosplash -nop4 > "%LOG%" 2>&1
echo EXITCODE %ERRORLEVEL% >> "%LOG%"
