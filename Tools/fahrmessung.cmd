@echo off
REM fahrmessung.cmd [Name] [Fahrsekunden] [kurve]
REM
REM   kurve   Bremsphase mit gehaltener Linkslenkung (-WbDriveKurvenbremsung):
REM           Vollbremsung mitten in der Kurve - zeigt, ob der Wagen beim
REM           Bremsen lenkbar bleibt.
REM
REM Messfahrt fuer Fahrphysik-Vergleiche (Vorher/Nachher) auf offenem Feld.
REM Der Spielerkaefer faehrt das WbDrive-Profil (40 % Vollgas geradeaus, 20 %
REM Lenken rechts 0,6, 20 % links, 20 % Vollbremsung) und schreibt mit
REM -WbFahrTelemetrie zehn Messzeilen je Sekunde ins Log. Auswertung:
REM   python Tools\fahrmessung_auswerten.py Saved\Logs\wb_fahrmessung_<Name>.log
REM
REM Ort: die Wiese Grabenstrasse/Schulstrasse (-WbGoto=-180086,899031) - vom
REM Garagenhof aus faehrt das blinde Profil nach wenigen Sekunden gegen eine
REM Wand (AGENTS.md). Karte = Standardkarte aus Config/DefaultEngine.ini.
REM Die Projektwurzel kommt aus der Skriptposition: im Worktree misst das
REM Skript den Worktree-Build, nicht den Hauptordner.
call "%~dp0engine_run_lock.cmd" -Modus Start -Name fahrmessung
if errorlevel 1 exit /b 2

setlocal
set NAME=%~1
if "%NAME%"=="" set NAME=messung
set DAUER=%~2
if "%DAUER%"=="" set DAUER=40
set /a QUIT=%DAUER%+80
set MANOEVER=
if /i "%~3"=="kurve" set MANOEVER=-WbDriveKurvenbremsung

for %%I in ("%~dp0..") do set "WURZEL=%%~fI"
set PROJ=%WURZEL%\WiesbadenReal.uproject
set LOG=%WURZEL%\Saved\Logs\wb_fahrmessung_%NAME%.log

"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" -game -WbKeinIntro -WbGoto=-180086,899031 -WbFahrTelemetrie %MANOEVER% -WbQuitAfter=%QUIT% -windowed -ResX=1280 -ResY=720 -unattended -nop4 -ExecCmds="WbCam 1,WbDrive %DAUER%" -abslog="%LOG%" > "%LOG%.out" 2>&1
set RC=%ERRORLEVEL%
echo EXITCODE %RC% >> "%LOG%.out"
call "%~dp0engine_run_lock.cmd" -Modus Freigeben > nul
endlocal & set RC=%RC%
if not "%RC%"=="0" exit /b 1
exit /b 0
