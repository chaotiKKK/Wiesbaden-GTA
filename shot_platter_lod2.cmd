@echo off
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\shot_platter_lod2.log
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" /Game/Maps/WiesbadenCity_Alkis10 -game -WbTeleportTo=-111307,-117424,13000 -WbShotWhenReady -WbCamHeight=320 -WbAtX=-111307 -WbAtY=-117424 -WbPitch=-32 -WbNoLumen -windowed -ResX=1600 -ResY=900 -stdout -unattended -nop4 > "%LOG%" 2>&1
exit /b %ERRORLEVEL%
