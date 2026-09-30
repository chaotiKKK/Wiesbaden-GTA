@echo off
REM Nachweislauf Geschicklichkeitsparcours: Wiese, WbParcours 1 (Fahrer faehrt),
REM Ergebnis im Log (Parcours-Ergebnis:). Unter der Engine-Sperre.
REM   Tools\parcours_lauf.cmd <Name> [Sekunden] [Fahrer 1=sauber, 2=mit Fehlern] [regen]
REM   regen: Regen-Variante (WbParcours <Fahrer> 1 - es regnet, aufgebaut wird
REM          auf nasser Strasse; Ergebnis "Regen" mit eigenen Medaillengrenzen).
REM Weitere Spielargumente (z. B. -WbClip=...) per Umgebungsvariable WBARGS,
REM Kameramodus per WBCAM (Vorgabe 0 = Folgekamera; 1 = Orbit fuer -WbCamYaw/...).
setlocal
set NAME=%~1
if "%NAME%"=="" set NAME=parcours
set QUIT=%~2
if "%QUIT%"=="" set QUIT=100
set MODUS=%~3
if "%MODUS%"=="" set MODUS=1
set REGEN=0
if /i "%~4"=="regen" set REGEN=1
set CAM=%WBCAM%
if "%CAM%"=="" set CAM=0

call "%~dp0engine_run_lock.cmd" -Modus Start -Name parcours_lauf
if errorlevel 1 goto gesperrt

for %%I in ("%~dp0..") do set "WURZEL=%%~fI"
set PROJ=%WURZEL%\WiesbadenReal.uproject
if not exist "%WURZEL%\Saved\Logs" mkdir "%WURZEL%\Saved\Logs"
set LOG=%WURZEL%\Saved\Logs\wb_parcours_%NAME%.log

"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" -game -WbKeinIntro -WbGoto=-180086,899031 -WbQuitAfter=%QUIT% -windowed -ResX=1280 -ResY=720 -unattended -nop4 %WBARGS% -ExecCmds="WbCam %CAM%,WbParcours %MODUS% %REGEN%" -abslog="%LOG%" > "%LOG%.out" 2>&1
set RC=%ERRORLEVEL%
call "%~dp0engine_run_lock.cmd" -Modus Freigeben > nul
echo Log: %LOG% (Exit %RC%)
endlocal & set RC=%RC%
if not "%RC%"=="0" exit /b 1
exit /b 0

:gesperrt
echo Engine-Sperre belegt - kein Lauf.
endlocal
exit /b 3
