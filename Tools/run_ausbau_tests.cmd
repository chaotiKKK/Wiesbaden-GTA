@echo off
setlocal
for %%I in ("%~dp0..") do set "WBPROJ=%%~fI"
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%WBPROJ%\WiesbadenReal.uproject" -ExecCmds="Automation RunTests WiesbadenReal.GIS.RoadNetwork.SupplementaryPaint+WiesbadenReal.GIS.RegionAssets.ObstacleClearance+WiesbadenReal.Audio.Ambience.Zones; Quit" -unattended -nop4 -nullrhi -NoSound -ABSLOG="%WBPROJ%\Saved\Logs\wb_test_ausbau_stadt.log" > "%WBPROJ%\Saved\Logs\wb_test_ausbau_stadt.out" 2>&1
exit /b %ERRORLEVEL%
