@echo off
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set EDCMD=C:\freebuff\WiesbadenReal_Sicherung\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\aaa_windows.log
echo === AAA-Fenster Start %date% %time% === > "%LOG%"
"%EDCMD%" "%PROJ%" -run=pythonscript -script="C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Tools\windows_on_aaa_facades.py" -unattended -nop4 -stdout >> "%LOG%" 2>&1
echo __EXIT=%ERRORLEVEL%__ >> "%LOG%"
