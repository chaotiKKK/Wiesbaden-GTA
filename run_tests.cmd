@echo off
REM WiesbadenReal Build + Automation-Tests (Wrapper fuer Start-Process).
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\build_test.log
set ERR=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\build_test.err

echo === Build startet: %date% %time% === > "%LOG%"

REM 1) Editor-Target kompilieren (UBT) - der erste Lauf erzeugt Binaries.
"C:\freebuff\WiesbadenReal_Sicherung\UE_5.8\Engine\Build\BatchFiles\Build.bat" WiesbadenRealEditor Win64 Development -project="%PROJ%" -waitmutex >> "%LOG%" 2>&1
if errorlevel 1 (
  echo === BUILD FEHLGESCHLAGEN: %date% %time% === >> "%LOG%"
  exit /b 1
)
echo === Build OK: %date% %time% === >> "%LOG%"

REM 2) Automation-Tests headless ausfuehren.
"C:\freebuff\WiesbadenReal_Sicherung\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%PROJ%" -ExecCmds="Automation RunTests WiesbadenReal; Quit" -unattended -nop4 -nullrhi >> "%LOG%" 2>> "%ERR%"
echo === Tests fertig: %date% %time% === >> "%LOG%"
exit /b 0
