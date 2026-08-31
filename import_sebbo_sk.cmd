@echo off
REM Animierte Sebbo-Spielfigur importieren (Skelett + drei Bewegungen).
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\import_sebbo_sk.log
"C:\freebuff\WiesbadenReal_Sicherung\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" -ExecCmds="py C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Tools\import_sebbo_skeletal.py" -unattended -nosplash -nop4 > "%LOG%" 2>&1
exit /b %ERRORLEVEL%
