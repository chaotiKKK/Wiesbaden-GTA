@echo off
REM Differenzmessung: was kosten die Fassaden-Shader?
REM
REM Immer DIESELBE Stelle, damit die Zahlen vergleichbar sind: Startposition,
REM Stillstand, Bild bei 120 s, Ende bei 150 s. Ohne feste Stelle misst man
REM die Fahrstrecke, nicht das Material.
set PROJ=C:\Users\ssonn\aivideo\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\Users\ssonn\aivideo\WiesbadenReal\perf_fassaden_%1.log
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" /Game/Maps/WiesbadenCity_Alkis3 -game -windowed -ResX=1600 -ResY=900 -WbShot=125 -WbQuitAfter=150 -stdout -unattended -nop4 > "%LOG%" 2>&1
exit /b %ERRORLEVEL%
