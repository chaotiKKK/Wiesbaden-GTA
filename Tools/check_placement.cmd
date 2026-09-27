@echo off
REM Platzierungs-Audit auf dem IST-Stand - Zaehlung je Regel + Belegkoordinaten.
REM
REM   check_placement.cmd
REM
REM Der Audit zaehlt, wie oft die sechs Regeln der Spec (2026-09-26-
REM platzierungsregeln-design.md) am heutigen Stand verletzt werden, und
REM aendert am Platzierungsverhalten NICHTS. Er baut nur die DATEN in ein
REM Schmierlevel - die Spielkarte bleibt unberuehrt, es entsteht kein Bake.
REM
REM Ergebnisse in der Projektwurzel:
REM   placement_report.json        Verstoesse je Regel + bis zu 20 Belegstellen
REM   placement_audit_result.txt   Einzeiler fuer die Kette
REM   placement_audit.log          volles Protokoll (py-Ausgaben landen dort)
REM
REM Der Engine-Lock wird genommen UND wieder freigegeben (Nehmen statt Start:
REM dieser Lauf fuehrt keine Prozessbereinigung aus und beendet niemanden).
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set SKRIPT=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Tools\check_placement.py
set LOGDIR=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal
set LOG=%LOGDIR%\placement_audit.log
set RESULT=%LOGDIR%\placement_audit_result.txt

call "%~dp0engine_run_lock.cmd" -Modus Nehmen -Name placement_audit -WarteSekunden 900
if errorlevel 1 (
  echo LOCK BELEGT - Platzierungs-Audit nicht gestartet. > "%RESULT%"
  exit /b 3
)

"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%PROJ%" -run=pythonscript -script="%SKRIPT%" -WbPlacementAudit -unattended -nosplash -nop4 -abslog="%LOG%" > "%LOG%.out" 2>&1
set WBEXIT=%ERRORLEVEL%

call "%~dp0engine_run_lock.cmd" -Modus Freigeben
echo EXITCODE %WBEXIT% > "%RESULT%"
exit /b %WBEXIT%
