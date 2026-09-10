@echo off
REM Baut M_WbFacade_Backstein/Sandstein mit AAA-Textur (Farbe+Normal) + Fenster-Raster neu.
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set EDCMD=C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe
set TOOLS=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Tools
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\relief_facades.log
echo === Relief-Fassaden Start %date% %time% === > "%LOG%"
"%EDCMD%" "%PROJ%" -run=pythonscript -script="%TOOLS%\build_relief_facades.py" -unattended -nop4 -stdout >> "%LOG%" 2>&1
echo __EXIT=%ERRORLEVEL%__ >> "%LOG%"
echo === Relief-Fassaden Ende %date% %time% === >> "%LOG%"
