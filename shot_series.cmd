@echo off
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\shot_series.log
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" /Game/Maps/WiesbadenCity_Alkis4 -game -WbShotWhenReady -WbShotDelay=10 -WbPoseSettle=7 -WbShotPoseFile="C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Saved\Diagnose\poses_test.txt" -windowed -ResX=1600 -ResY=900 -stdout -unattended -nop4 > "%LOG%" 2>&1
exit /b %ERRORLEVEL%
