@echo off
rem Importiert die fuenf Dach-Assets (FBX + Materialien + Bildtexturen) in das
rem Projekt. Quelle: Data\Raw\SebboTower (gebaut von Tools\Blender\make_sebbo_dach.py).
rem Der Editor beendet sich nach dem Import von selbst (WB_QUIT).
rem Ergebnis-Nachweis: Data\Raw\SebboTower\import_done.txt und die
rem ###WBSDIMP###-Zeilen in Saved\Logs\import_sebbo_dach.log.
setlocal
set WB_QUIT=1
pushd "%~dp0.."
rem ACHTUNG: "py <relativ>" wird gegen den ENGINE-Binärordner aufgeloest, nicht
rem gegen das Projekt - darum hier der absolute Skriptpfad.
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%CD%\WiesbadenReal.uproject" -ExecCmds="py C:/freebuff/WiesbadenReal_Sicherung/WiesbadenReal/Tools/import_sebbo_dach.py" -unattended -nosplash -nop4 -abslog="%CD%\Saved\Logs\import_sebbo_dach.log"
popd
endlocal
