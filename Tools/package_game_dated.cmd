@echo off
REM Development-Paket in eine DATIERTE Ablage bauen - liegende Pakete bleiben
REM unangetastet. package_game.cmd schreibt fest nach Saved\Package (das
REM aktuelle Spiel-Paket, "Wiesbaden Real (Paket)"); dieser Wrapper legt ein
REM eigenstaendiges Paket ab, damit ein Fehlversuch nichts ueberschreibt.
REM
REM Aufruf:  Tools\package_game_dated.cmd [Zielordner]
REM Ohne Argument: Saved\Package_YYYY-MM-DD (heutiges Datum).
REM Ergebnis:   <Zielordner>\Windows\WiesbadenReal.exe
REM
REM Dieselben Flags wie package_game.cmd (-build -cook -stage -pak -package
REM -archive), nur die Ablage ist eine andere - am 01.10.2026 geprueft.
REM
REM -map ist NOETIG (gemessen 01.10.2026): ohne -map kocht der Cook-Commandlet
REM nur die Default-Karte plus die DirectoriesToAlwaysCook - die MapsToCook-
REM Eintraege aus DefaultGame.ini werden von UAT NICHT als -map durchgereicht.
REM Trenner der Kartennamen ist das Pluszeichen.
setlocal
set PROJDIR=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal
set PROJ=%PROJDIR%\WiesbadenReal.uproject
if "%~1"=="" (
    for /f %%I in ('powershell -NoProfile -Command "Get-Date -Format yyyy-MM-dd"') do set OUT=%PROJDIR%\Saved\Package_%%I
) else (
    set OUT=%~1
)
set LOG=%PROJDIR%\package_game_dated.log
echo Ziel: %OUT%> "%LOG%"
echo Start: %DATE% %TIME%>> "%LOG%"
call "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun -project="%PROJ%" -platform=Win64 -clientconfig=Development -map=WiesbadenCity_Alkis31+WiesbadenCity_Alkis32 -build -cook -stage -pak -package -archive -archivedirectory="%OUT%" -unattended -noP4 >> "%LOG%" 2>&1
echo Ende: %DATE% %TIME% Exit %ERRORLEVEL%>> "%LOG%"
exit /b %ERRORLEVEL%
