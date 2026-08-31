@echo off
set PROJ=C:\Users\ssonn\aivideo\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\Users\ssonn\aivideo\WiesbadenReal\diag_70.log
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" /Game/Maps/WiesbadenCity_Alkis -game -WbScreenshot -WbAerial=70 -windowed -ResX=1280 -ResY=720 -stdout -unattended -nop4 > "%LOG%" 2>&1
exit /b %ERRORLEVEL%
