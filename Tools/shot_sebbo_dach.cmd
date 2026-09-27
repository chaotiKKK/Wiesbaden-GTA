@echo off
rem Nachweis-Aufnahme der Dachaufbauten des SebboTower: Schild mit Wortmarke
rem und AG-Logo, Magazinstaender mit Titelbild, Topfpflanzen am Dachrand.
rem Quelle der Posen: Saved\Diagnose\poses_sebbo_dach\dach.txt
rem Aufruf: shot_sebbo_dach.cmd [Posendatei] [Logdatei]
rem Eigenes Skript, weil shot_pose_series.cmd auf die leere Karte Alkis24 zeigt
rem und shot_innen_nacht.cmd fremde WIP ist. Wie dort: Lock VOR dem Start und
rem Freigabe danach - der Lock ist der eine Ort, an dem "darf ich eine Engine
rem starten" entschieden wird.
setlocal
call "%~dp0engine_run_lock.cmd" -Modus Start -Name shot_sebbo_dach
if errorlevel 1 (
  echo ABBRUCH: Engine-Lock belegt, es wurde KEIN Editor gestartet.
  exit /b 1
)
set UE=C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set POSES=%~1
set LOG=%~2
if "%POSES%"=="" set POSES=%CD%\Saved\Diagnose\poses_sebbo_dach\dach.txt
if "%LOG%"=="" set LOG=%CD%\Saved\Logs\shot_sebbo_dach.log
rem -WbShotNoQuit: die Serie hat fuenf Bilder, ein Screenshot wuerde den Lauf
rem nach dem ersten beenden. -WbQuitAfter ist DABEIT zwingend: ohne beendet
rem -WbShotNoQuit das Spiel gar nicht mehr, der Editor laeuft endlos weiter,
rem der Wrapper kommt nie zum Freigeben und der Lock bleibt liegen (genau
rem das ist am 27.09.2026 passiert, 14 Minuten ueber die Aufnahme hinaus).
rem 150 s reichen fuer fuenf Posen mit 4 s Settle.
"%UE%" "%PROJ%" /Game/Maps/WiesbadenCity_Alkis31 -game -WbShotWhenReady -WbShotNoQuit -WbQuitAfter=150 "-WbShotPoseFile=%POSES%" -WbPoseSettle=4 -WbShotScale=1 -WbTime=13 -windowed -ResX=1920 -ResY=1080 -unattended -nop4 "-abslog=%LOG%"
call "%~dp0engine_run_lock.cmd" -Modus Freigeben
