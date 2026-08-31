@echo off
REM Fassaden einfarbig machen und an DERSELBEN Stelle nachmessen.
REM Die Differenz zur Grundlinie ist der Preis der Fassaden-Shader.
REM
REM Rueckbau: Tools/build_materials.py erneut laufen lassen (run_materials.cmd).
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set WB_FLAT=M_WbBuildingWall,M_WbFacade_Putz,M_WbFacade_Backstein,M_WbFacade_Sandstein,M_WbFacade_Beton,M_WbFacade_Glas,M_WbFacade_Fachwerk
"C:\freebuff\WiesbadenReal_Sicherung\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%PROJ%" -run=pythonscript -script="C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Tools\debug_flatten_materials.py" -unattended -nop4 > C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\perf_flach_setup.log 2>&1
exit /b %ERRORLEVEL%
