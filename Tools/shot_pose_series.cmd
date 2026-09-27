@echo off
rem Pose-Serie rendern: eine Posendatei, viele Ansichten, Ausgabe WbSeries_XXX.png.
rem Aufruf: shot_pose_series.cmd <Posendatei> <Logdatei>
rem WICHTIG: KEIN -WbScreenshot dazu - das wuerde nach 8 s einen eigenen
rem Screenshot ausloesen und den Lauf 3 s spaeter beenden (ScreenshotQuitDelay).
setlocal
REM Lock VOR dem Start. Ohne ihn startet diese Datei einen sichtbaren Editor
REM auch dann, wenn gerade ein Lauf laeuft - am 26.09.2026 ist genau das
REM passiert (zwei UnrealEditor-Prozesse nebeneinander, Projekt-AGENTS nennt
REM das als Absturz). Der Lock ist der eine Ort, an dem "darf ich eine Engine
REM starten" entschieden wird; er wird hier genauso geprueft wie in
REM run_automation_test.cmd.
call "%~dp0engine_run_lock.cmd" -Modus Start -Name shot_pose_series
if errorlevel 1 (
  echo ABBRUCH: Engine-Lock belegt, es wurde KEIN Editor gestartet.
  exit /b 1
)
set UE=C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set POSES=%~1
set LOG=%~2
"%UE%" "%PROJ%" /Game/Maps/WiesbadenCity_Alkis24 -game -WbShotWhenReady "-WbShotPoseFile=%POSES%" -WbPoseSettle=3 -WbShotScale=1 -WbTime=12 -windowed -ResX=1920 -ResY=1080 -unattended -nop4 "-abslog=%LOG%"
call "%~dp0engine_run_lock.cmd" -Modus Freigeben
