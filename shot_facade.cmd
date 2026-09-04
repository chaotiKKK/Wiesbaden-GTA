@echo off
REM Fassaden-Nahaufnahme: Kamera aus 12 m mit der Sonne im Ruecken (Yaw -35) auf
REM eine sonnenbeschienene Hauswand; 2x-HighResShot sobald die Stadt bereit ist.
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\shot_facade.log
"C:\freebuff\WiesbadenReal_Sicherung\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" /Game/Maps/WiesbadenCity_Alkis4 -game -windowed -ResX=1920 -ResY=1080 -WbShotWhenReady -WbShotScale=2 -WbCamHeight=8 -WbYaw=145 -WbPitch=-3 -WbCamForward=34 -WbLookYaw=-35 -stdout -unattended -nop4 > "%LOG%" 2>&1
