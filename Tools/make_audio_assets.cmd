@echo off
REM Erzeugt die Audio-Assets an EINER Stelle (analog zu den anderen Wrappern):
REM   /Game/Audio/Mix/ATT_* , SBX_Reverb, SFXP_Reverb, CON_WbSfx
REM   /Game/Audio/Meta/MS_Amb* , MS_Step* , MS_EngineBoxer
REM Das Commandlet selbst (WbAudioAssets) dokumentiert die Asset-Liste.
REM Ergebnis-Log: Saved/Logs/make_audio_assets.log (Build) + Saved/Logs/WiesbadenReal.log (Commandlet).
REM Beweiszeile: "WbAudioAssets fertig: N/M Pakete gespeichert, 0 Baustein(e) nicht gebaut."
REM - die Baustein-Zahl steht seit 28.09.2026 dabei, weil ein NICHT gebautes
REM Asset gar kein Paket anlegt und "12/12" sonst auch bei vier fehlenden
REM MetaSounds wie ein voller Erfolg aussah (MS_Step_* waren genau so weg).
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Saved\Logs\make_audio_assets.log

REM Der Lock kommt VOR Build und Commandlet (wie in den anderen Wrappern): der
REM Editor-Target-Build schreibt die DLL, die ein fremder Editorlauf geladen
REM haelt. -Modus Start wartet seit 28.09.2026 auf einen belegten Lock (Vorgabe
REM 30 min) und nimmt die Prozessbereinigung mit.
call "%~dp0engine_run_lock.cmd" -Modus Start -Name make_audio_assets
if errorlevel 1 exit /b 1

echo === Build startet: %date% %time% === > "%LOG%"

REM 1) Editor-Target kompilieren (das Commandlet ist C++-Code).
call "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" WiesbadenRealEditor Win64 Development -project="%PROJ%" -waitmutex >> "%LOG%" 2>&1
if errorlevel 1 (
  echo === BUILD FEHLGESCHLAGEN: %date% %time% === >> "%LOG%"
  exit /b 1
)
echo === Build OK: %date% %time% === >> "%LOG%"

REM 2) Commandlet ausfuehren.
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%PROJ%" -run=WbAudioAssets -unattended -nop4 -nosplash -NoP4 >> "%LOG%" 2>&1
echo === Commandlet fertig (Code %ERRORLEVEL%): %date% %time% === >> "%LOG%"
exit /b %ERRORLEVEL%
