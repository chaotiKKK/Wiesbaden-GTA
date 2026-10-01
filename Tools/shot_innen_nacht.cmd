@echo off
rem Nacht-Posen des SebboTower-Innenausbaus: Beleuchtung, Tueren und
rem Moeblierung durch die Glasfassade. Wie shot_pose_series.cmd, aber
rem Spielzeit 22 Uhr (Innenlicht an) und Karte WiesbadenCity_Alkis31.
rem Aufruf: shot_innen_nacht.cmd <Posendatei> <Logdatei> [Stunde]
rem   Stunde = Spielzeit fuer Tag-Posen (z.B. 12), Standard 22 (Nacht, Innenlicht an).
setlocal
call "%~dp0engine_run_lock.cmd" -Modus Start -Name shot_innen_nacht
if errorlevel 1 (
  echo ABBRUCH: Engine-Lock belegt, es wurde KEIN Editor gestartet.
  exit /b 1
)
set UE=C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set POSES=%~1
set LOG=%~2
set STUNDE=%~3
if "%STUNDE%"=="" set STUNDE=22
"%UE%" "%PROJ%" /Game/Maps/WiesbadenCity_Alkis31 -game -WbShotWhenReady "-WbShotPoseFile=%POSES%" -WbPoseSettle=4 -WbShotScale=1 -WbTime=%STUNDE% -windowed -ResX=1920 -ResY=1080 -unattended -nop4 "-abslog=%LOG%"
call "%~dp0engine_run_lock.cmd" -Modus Freigeben
