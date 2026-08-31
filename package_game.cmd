@echo off
REM Spiel als Development-Paket bauen und dorthin legen, wohin die
REM Desktop-Verknuepfung "Wiesbaden starten" zeigt:
REM   Saved\Package\Windows\WiesbadenReal.exe
REM
REM Das liegende Paket war vom 27.08. - ohne Baeume, ohne PBR-Texturen,
REM ohne Sebbo, und mit den Materialien, die unter SM6 gar nicht
REM uebersetzten.
set PROJ=C:\Users\ssonn\aivideo\WiesbadenReal\WiesbadenReal.uproject
set OUT=C:\Users\ssonn\aivideo\WiesbadenReal\Saved\Package
set LOG=C:\Users\ssonn\aivideo\WiesbadenReal\package_game.log
call "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun -project="%PROJ%" -platform=Win64 -clientconfig=Development -build -cook -stage -pak -package -archive -archivedirectory="%OUT%" -unattended -noP4 > "%LOG%" 2>&1
exit /b %ERRORLEVEL%
