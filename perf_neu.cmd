@echo off
REM Leistungsmessung unter denselben Bedingungen wie die letzte Grundlinie
REM (28.08., 50,1 ms): 1600x900, Autofahrt, Bild bei 430 s, Ende bei 500 s.
REM
REM Die alte Grundlinie ist WERTLOS: Saemtliche Fassaden- und das
REM Figurenmaterial uebersetzten damals nicht (Abtasttyp-Fehler) - gemessen
REM wurde das graue Standardmaterial, nicht die Stadt.
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\perf_neu.log
REM  Karte NICHT fest verdrahten - sie kommt aus
REM  Config\DefaultEngine.ini (siehe Tools\karte.cmd).
call "%~dp0Tools\karte.cmd"
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" %WB_MAP_PFAD% -game -windowed -ResX=1600 -ResY=900 -WbAutoDrive -WbShot=430 -WbQuitAfter=500 -stdout -unattended -nop4 > "%LOG%" 2>&1
exit /b %ERRORLEVEL%
