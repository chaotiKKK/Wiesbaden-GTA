@echo off
REM Die drei Ankunftswege des SebboTower in der ECHTEN Stadt abtasten.
REM
REM   run_ankunft_probe.cmd [Karte] [Logname] [Sekunden]
REM     Karte    Kartenname ohne Pfad, Standard WiesbadenCity_Alkis16
REM     Logname  Teil vor ".log", Standard = Kartenname
REM     Sekunden Selbstabbruch, Standard 150
REM
REM   run_ankunft_probe.cmd WiesbadenCity_Alkis17
REM   run_ankunft_probe.cmd WiesbadenCity_Alkis17 nachbake 180
REM
REM WARUM DIE KARTE EIN PARAMETER IST: ohne sie oeffnet das Spiel die
REM Default-Karte. Nach einem Neubake liegt die frische Stadt aber unter einer
REM NEUEN Nummer, waehrend die Default-Karte noch auf der alten steht - die
REM Sonde mass dann die alte Stadt und meldete brav die alten Fehler. Das sah
REM aus, als haette der Bake nichts bewirkt.
REM
REM WARUM DIE KOORDINATE ABGELEITET WIRD: hier stand sie als feste Zahl.
REM Verschiebt jemand den Standort in SebboHqSite.h, faehrt die Sonde weiter
REM an die alte Stelle, findet dort keinen Turm und meldet statt "falscher
REM Ort" ein "Startpunkt steckt im Gelaende". Tools/sebbo_standort.py rechnet
REM sie aus demselben Header, aus dem der Turm seinen Platz nimmt.
REM
REM -WbGoto setzt die Spielfigur an den Turm; ohne sie bleibt die Zelle
REM ungestreamt, der Turm baut nie und die Sonde laeuft nie an.
REM
REM Ergebnis: Saved\Diagnose\ankunftsprobe.json und die Logzeile
REM "Ankunftsprobe:" in Saved\Logs\wb_ankunft_<Logname>.log.
setlocal
set KARTE=%~1
if "%KARTE%"=="" set KARTE=WiesbadenCity_Alkis16
set NAME=%~2
if "%NAME%"=="" set NAME=%KARTE%
set QUIT=%~3
if "%QUIT%"=="" set QUIT=150

set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Saved\Logs\wb_ankunft_%NAME%.log
set ABLEITER=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Tools\sebbo_standort.py

REM Ziel aus SebboHqSite.h ableiten. Schlaegt das fehl, wird NICHT geraten:
REM eine Sonde am falschen Ort misst still Unsinn.
set ZIEL=
for /f "usebackq delims=" %%Z in (`python "%ABLEITER%"`) do set ZIEL=%%Z
if "%ZIEL%"=="" (
  echo FEHLER: Turmkoordinate liess sich nicht aus SebboHqSite.h ableiten.
  exit /b 1
)
echo Karte %KARTE%, Ziel %ZIEL%

"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" /Game/Maps/%KARTE% -game -WbGoto=%ZIEL% -WbAnkunftProbe -WbQuitAfter=%QUIT% -windowed -ResX=1280 -ResY=720 -stdout -unattended -nop4 > "%LOG%" 2>&1
echo EXITCODE %ERRORLEVEL% >> "%LOG%"
endlocal
