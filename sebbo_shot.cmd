@echo off
REM Spielerfigur zu Fuss ansehen: nach 40 s aussteigen, bei 52 s ein Bild.
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\sebbo_ingame.log
"C:\freebuff\WiesbadenReal_Sicherung\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" /Game/Maps/WiesbadenCity_Alkis3 -game -windowed -ResX=1600 -ResY=900 -WbZuFuss=40 -WbShot=52 -WbQuitAfter=64 -stdout -unattended -nop4 > "%LOG%" 2>&1
exit /b %ERRORLEVEL%
