@echo off
REM Herbie-Lackierung importieren (Texturen + Materialinstanzen).
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\import_herbie.log
"C:\freebuff\WiesbadenReal_Sicherung\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" -ExecCmds="py C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Tools\import_herbie.py" -unattended -nosplash -nop4 > "%LOG%" 2>&1
exit /b %ERRORLEVEL%
