@echo off
REM Fahrt mit Autopilot: Herbie von hinten (Dachstreifen), Ueberflug und
REM Nerobergbahn im Protokoll.
set PROJ=C:\Users\ssonn\aivideo\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\Users\ssonn\aivideo\WiesbadenReal\shot_fahrt.log
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" /Game/Maps/WiesbadenCity_Alkis3 -game -windowed -ResX=1600 -ResY=900 -WbAutoDrive -WbShot=120 -WbQuitAfter=150 -stdout -unattended -nop4 > "%LOG%" 2>&1
exit /b %ERRORLEVEL%
