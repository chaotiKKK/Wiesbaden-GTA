@echo off
REM Differenzmessung: was kosten die Fassaden-Shader?
REM
REM Immer DIESELBE Stelle, damit die Zahlen vergleichbar sind: Startposition,
REM Stillstand, Bild bei 120 s, Ende bei 150 s. Ohne feste Stelle misst man
REM die Fahrstrecke, nicht das Material.
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\perf_fassaden_%1.log
REM  Karte NICHT fest verdrahten - sie kommt aus
REM  Config\DefaultEngine.ini (siehe Tools\karte.cmd).
call "%~dp0Tools\karte.cmd"
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" %WB_MAP_PFAD% -game -windowed -ResX=1600 -ResY=900 -WbShot=125 -WbQuitAfter=150 -stdout -unattended -nop4 > "%LOG%" 2>&1
exit /b %ERRORLEVEL%
