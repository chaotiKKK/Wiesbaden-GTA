@echo off
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
"C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" WiesbadenRealEditor Win64 Development -project="%PROJ%" -waitmutex
exit /b %ERRORLEVEL%
