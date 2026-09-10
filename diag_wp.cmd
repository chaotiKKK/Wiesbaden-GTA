@echo off
rem Streaming-Diagnose auf der gebackenen Karte.
rem
rem Karte Alkis (seit dem Neubau entfernt) -> Alkis4. Ohne
rem LoadingRange-Override: Der Messwert "jenseits 2 km" ist nur gegen den
rem Vorgabe-Radius von 2000 m aussagekraeftig (AGENTS.md-Kriterium: 602 -> ~0);
rem mit einem hoeheren Override laden Chunks bei 2-4 km Abstand legitim und
rem verfaelschen die Kennzahl.
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\diag_wp.log
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" /Game/Maps/WiesbadenCity_Alkis4 -game -WbScreenshot -WbAerial=400 -windowed -ResX=1280 -ResY=720 -stdout -unattended -nop4 > "%LOG%" 2>&1
exit /b %ERRORLEVEL%
