@echo off
rem Streaming-Diagnose auf der gebackenen Karte.
rem
rem Die Karte kommt aus Tools\karte.cmd - hier stand frueher Alkis4, das ist
rem seit dem Karten-Aufraeumen ebenfalls geloescht. Ohne
rem LoadingRange-Override: Der Messwert "jenseits 2 km" ist nur gegen den
rem Vorgabe-Radius von 2000 m aussagekraeftig (AGENTS.md-Kriterium: 602 -> ~0);
rem mit einem hoeheren Override laden Chunks bei 2-4 km Abstand legitim und
rem verfaelschen die Kennzahl.
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\diag_wp.log
REM  Karte NICHT fest verdrahten - sie kommt aus
REM  Config\DefaultEngine.ini (siehe Tools\karte.cmd).
call "%~dp0Tools\karte.cmd"
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" %WB_MAP_PFAD% -game -WbScreenshot -WbAerial=400 -windowed -ResX=1280 -ResY=720 -stdout -unattended -nop4 > "%LOG%" 2>&1
exit /b %ERRORLEVEL%
