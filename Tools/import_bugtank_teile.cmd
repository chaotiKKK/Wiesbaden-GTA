@echo off
rem Importiert die 15 statischen Insekt-Teile (Blender/bugtank/export_bugtank_teile.py)
rem als SM_Insekt_<Teil> unter /Game/Vehicles/BugTank. Umweg um den Skelett-Import:
rem Unreal 5.8 setzt die Knochenorientierung eines Blender-Rigs um (Belege
rem Saved/Logs/wb_test_bugtankrig5.log und rig6). Siehe Blender/bugtank/LIESMICH.md.
rem
rem Der Editor beendet sich nach dem Import von selbst (WB_QUIT).
rem Nachweis: die ###WBTPARTS###-Zeilen in Saved\Logs\wb_import_bugtank_teile.log,
rem besonders die Bounds-Zeile je Teil - sie muss die Box des Blender-Exports
rem treffen (sonst hat der Import die Achsen vertauscht).
rem
rem Aufruf:  Tools\import_bugtank_teile.cmd [GLB-Pfad]
setlocal
set WB_QUIT=1
pushd "%~dp0.."
call Tools\engine_run_lock.cmd -Modus Nehmen -Name teileimport -WarteSekunden 0 -PlattenGrenze 0
if errorlevel 1 (
  echo BugTankTeile: Lock nicht bekommen, Abbruch.
  popd
  exit /b 3
)
set WBTGLB=%~1
if "%WBTGLB%"=="" set WBTGLB=C:/freebuff/WiesbadenReal_Sicherung/Blender/bugtank/ausgabe/SM_BugTank_Teile.glb
rem -run=pythonscript statt -ExecCmds: der Import muss als Kommandozeilenlauf
rem laufen, und der Schalter -GLB= steht nicht in sys.argv (siehe Skript).
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%CD%\WiesbadenReal.uproject" -run=pythonscript -script="C:/freebuff/WiesbadenReal_Sicherung/WiesbadenReal/Tools/import_bugtank_teile.py" -GLB="%WBTGLB%" -unattended -nop4 -nullrhi -abslog="%CD%\Saved\Logs\wb_import_bugtank_teile.log"
call Tools\engine_run_lock.cmd -Modus Freigeben
popd
endlocal