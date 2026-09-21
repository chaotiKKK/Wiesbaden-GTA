@echo off
REM Die drei Ankunftswege des SebboTower in der ECHTEN Stadt abtasten.
REM
REM   run_ankunft_probe.cmd [Logname] [Sekunden]
REM
REM -WbGoto setzt die Spielfigur an den Turm; ohne sie bleibt die Zelle
REM ungestreamt, der Turm baut nie und die Sonde laeuft nie an (der Actor
REM tickt bis dahin nur auf ResolveGround). Die Koordinate ist der Fusspunkt
REM aus SebboHqSite (50.093950 / 8.224490) im Wiesbaden-Ursprung.
REM
REM Ergebnis: Saved\Diagnose\ankunftsprobe.json und die Logzeile
REM "Ankunftsprobe:" in Saved\Logs\wb_ankunft_<Logname>.log.
set NAME=%~1
if "%NAME%"=="" set NAME=probe
set QUIT=%~2
if "%QUIT%"=="" set QUIT=180
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Saved\Logs\wb_ankunft_%NAME%.log
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" -game -WbGoto=-110983,-128483 -WbAnkunftProbe -WbQuitAfter=%QUIT% -windowed -ResX=1280 -ResY=720 -stdout -unattended -nop4 > "%LOG%" 2>&1
echo EXITCODE %ERRORLEVEL% >> "%LOG%"
