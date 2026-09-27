@echo off
call "%~dp0engine_run_lock.cmd" -Modus Start -Name run_innen_probe
if errorlevel 1 exit /b 1
REM Die INNEN-Proben des SebboTower: Aufzugsfahrt und Treppensteig im echten
REM Spiel. Beide Proben laufen IM SELBEN Lauf - der Aufzug faehrt leer waehrend
REM die Figur die Treppe hochgeht, die Nachweise trennen sich nur im Log.
REM
REM   run_innen_probe.cmd [Karte] [Logname] [Sekunden]
REM     Karte    Kartenname ohne Pfad, Standard WiesbadenCity_Alkis31
REM     Logname  Teil vor ".log", Standard = innen
REM     Sekunden Selbstabbruch, Standard 720 (der Treppensteig braucht Minuten)
REM
REM Nachweise im Log:
REM   "Tower-Aufzug-Probe: Etage 14 erreicht ... Tueren offen."   (Aufzug ok)
REM   "WbFigurProbe Treppe: OBEN - alle N Wegpunkte ..."          (Treppe ok)
REM   "WbFigurProbe Treppe: HAENGT ..."                           (Treppe NICHT ok)
REM
REM WARUM -WbGoto: ohne sie bleibt die Zelle ungestreamt, der Turm baut nie
REM und beide Proben laufen nie an (wie in run_ankunft_probe.cmd begruendet).
REM
REM WARUM -WbZuFuss: TickFigurProbe kehrt zurueck, solange der Controller das
REM Fahrzeug besitzt ("erst aussteigen (-WbZuFuss)", WiesbadenGameMode.cpp).
REM Am 27.09. blieb der Spieler im Auto (Tempo 0/Rest 12 min) und die Probe
REM startete nie - der Lauf ohne Nachweis durchlief. Der Ausstieg nach 20 s
REM setzt die Figur nur ab; die Treppe-Probe versetzt sie danach selbst an
REM den Treppenfuss.
REM
REM Bilder der Figurprobe: Saved\Diagnose\figur_treppe_*.png
setlocal
set KARTE=%~1
if "%KARTE%"=="" set KARTE=WiesbadenCity_Alkis31
set NAME=%~2
if "%NAME%"=="" set NAME=innen
set QUIT=%~3
if "%QUIT%"=="" set QUIT=720

set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Saved\Logs\wb_innen_%NAME%.log
REM -abslog sichert die Log-Verbosity-Zeilen ("Start ...", "Wegpunkt N nach",
REM "OBEN"): der -stdout-Umleitung fehlen Projekt-Logzeilen, nur Warnungen
REM kommen durch (AGENTS.md "Unreal/Engine-Fallen").
set FULLLOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Saved\Logs\wb_innen_%NAME%_full.log
set ABLEITER=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Tools\sebbo_standort.py

set ZIEL=
for /f "usebackq delims=" %%Z in (`python "%ABLEITER%"`) do set ZIEL=%%Z
if "%ZIEL%"=="" (
  echo FEHLER: Turmkoordinate liess sich nicht aus SebboHqSite.h ableiten.
  call "%~dp0engine_run_lock.cmd" -Modus Freigeben
  exit /b 1
)
echo Karte %KARTE%, Ziel %ZIEL%, Abbruch nach %QUIT% s

"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" /Game/Maps/%KARTE% -game -WbGoto=%ZIEL% -WbZuFuss=20 -WbLiftProbe "-WbFigurProbe=Treppe" -WbQuitAfter=%QUIT% -windowed -ResX=1280 -ResY=720 -stdout -abslog="%FULLLOG%" -unattended -nop4 > "%LOG%" 2>&1
echo EXITCODE %ERRORLEVEL% >> "%LOG%"
call "%~dp0engine_run_lock.cmd" -Modus Freigeben
endlocal
