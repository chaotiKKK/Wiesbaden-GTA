@echo off
set PROJ=C:\Users\ssonn\aivideo\WiesbadenReal\WiesbadenReal.uproject
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%PROJ%" -run=pythonscript -script="C:\Users\ssonn\AppData\Local\Temp\claude\C--Users-ssonn-aivideo\da4fbff6-0b9e-46f8-ab5f-174f72d12999\scratchpad\build_alkis.py" -unattended -nop4 -nullrhi -stdout > C:\Users\ssonn\aivideo\WiesbadenReal\rebuild_map.log 2>&1
exit /b %ERRORLEVEL%
