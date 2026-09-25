@echo off
call "%~dp0engine_run_lock.cmd" -Modus Start -Name run_automation_test
if errorlevel 1 exit /b 1
REM Automation-Tests headless fahren. Ausgabe nach Saved\Logs\wb_test_<Log>.log -
REM dort steht am Ende die Automation-Zusammenfassung ("TEST COMPLETE. EXIT CODE").
REM
REM   run_heli_model_test.cmd [Filter] [Logname]
REM     Filter  Testpfad-Praefix, Standard WiesbadenReal.Vehicles.HelicopterModell
REM     Logname Teil vor ".log", Standard heli
REM
REM WICHTIG: die Anfuehrungszeichen um -ExecCmds sind PFLICHT. Ohne sie zerlegt
REM die Engine die Zeile am Leerzeichen zu "-ExecCmds=Automation" und fuehrt nur
REM den Befehl "Automation" aus - RunTests laeuft nie, "Quit" kommt nie, der
REM Editor idlet endlos (im Log nachpruefbar an der Zeile "LogInit: Command
REM Line:"). Beim Aufruf per Start-Process -ArgumentList NICHT moeglich (dort
REM werden die Quotes beim Zusammenbauen entfernt) - darum dieses Skript.
set FILTER=%~1
if "%FILTER%"=="" set FILTER=WiesbadenReal.Vehicles.HelicopterModell
set NAME=%~2
if "%NAME%"=="" set NAME=heli
REM
REM Voraussetzung: Gate 1 gebaut (Tools\build_gate1.cmd). Die Engine-Auswahl MUSS
REM die installierte sein: UnrealEditor-Cmd.exe und die Projekt-PCH stammen aus
REM demselben Baum (sonst C2953/C2011, siehe build_gate1.cmd).
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Saved\Logs\wb_test_%NAME%.log
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%PROJ%" -ExecCmds="Automation RunTests %FILTER%; Quit" -unattended -nop4 -nullrhi -NoSound -ABSLOG="%LOG%" > "%LOG%.out" 2>&1
echo EXITCODE %ERRORLEVEL% >> "%LOG%.out"
