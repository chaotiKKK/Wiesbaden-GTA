@echo off
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\diag_run.log
"C:\freebuff\WiesbadenReal_Sicherung\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" /Game/Maps/WiesbadenCity_Alkis -game -WbScreenshot -windowed -ResX=1280 -ResY=720 -stdout -unattended -nop4 > "%LOG%" 2>&1
exit /b %ERRORLEVEL%
