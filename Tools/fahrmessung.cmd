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
REM
REM Sperre: wer sie beim Start SCHON hielt (Push-Gate 7: der Hook haelt sie
REM ab Gate 0), gibt sie am Ende auch nicht frei - sonst liefen die Gates
REM danach ohne Sperre, und ein paralleler Lauf koennte dazwischen starten.
REM Exit: 0 gefahren, 1 Editor-Fehler, 2 nicht gestartet (Sperre/Platte).
setlocal
set "SPERRTEXT=%TEMP%\wb_fahrmessung_sperre_%RANDOM%.txt"
call "%~dp0engine_run_lock.cmd" -Modus Start -Name fahrmessung > "%SPERRTEXT%" 2>&1
set SPERRRC=%ERRORLEVEL%
type "%SPERRTEXT%"
set SCHON_GEHALTEN=0
findstr /c:"bereits gehalten" "%SPERRTEXT%" > nul && set SCHON_GEHALTEN=1
del "%SPERRTEXT%" > nul 2>&1
if not "%SPERRRC%"=="0" goto nicht_gestartet

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
if not exist "%WURZEL%\Saved\Logs" mkdir "%WURZEL%\Saved\Logs"

"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" -game -WbKeinIntro -WbGoto=-180086,899031 -WbFahrTelemetrie %MANOEVER% -WbQuitAfter=%QUIT% -windowed -ResX=1280 -ResY=720 -unattended -nop4 -ExecCmds="WbCam 1,WbDrive %DAUER%" -abslog="%LOG%" > "%LOG%.out" 2>&1
set RC=%ERRORLEVEL%
echo EXITCODE %RC% >> "%LOG%.out"
if "%SCHON_GEHALTEN%"=="0" call "%~dp0engine_run_lock.cmd" -Modus Freigeben > nul
endlocal & set RC=%RC%
if not "%RC%"=="0" exit /b 1
exit /b 0

:nicht_gestartet
endlocal
exit /b 2
