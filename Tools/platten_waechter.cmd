@echo off
REM platten_waechter.cmd [-Immer] [-Trocken] [-Schwelle N] [-Budget N]
REM
REM Plattenbelegung und die groessten Speicherfresser melden. Fuer die
REM Windows-Aufgabenplanung gedacht - der Rechner meldet sich von selbst,
REM sobald der Plattenplatz unter die Grenze faellt.
REM
REM   cmd //c "Tools\platten_waechter.cmd"                nur melden
REM   cmd //c "Tools\platten_waechter.cmd" --immer        immer berichten
REM   cmd //c "Tools\platten_waechter.cmd" --reinigen --trocken   was wuerde weggehen
REM   cmd //c "Tools\platten_waechter.cmd" --schwelle 30  eigene Grenze
REM
REM Die Argumente gehen unveraendert an platten_waechter.py - also MIT den
REM doppelten Bindestrichen. GEMESSEN: die erste Fassung dokumentierte und
REM testete -Immer; argparse meldete "unrecognized arguments" und endete mit
REM Code 2, der Aufgabenplanungseintrag haette den Lauf als "erfolgreich"
REM gebucht, obwohl nichts gemeldet wurde.
REM
REM Das Skript LOESCHT nichts, solange --reinigen nicht dasteht. Ohne
REM --reinigen ist es eine reine Meldung - auch aus der Aufgabenplanung heraus.
REM
REM Fuer einen Eintrag in der Windows-Aufgabenplanung (taeglich):
REM   Programm:  cmd.exe
REM   Argumente: /c "C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Tools\platten_waechter.cmd"
REM   Starten in: C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal
REM Der Bericht landet in Saved\Diagnose\plattenbericht.txt; wer nichts liest,
REM sieht nichts - das ist der Preis einer unattended Pruefung.
REM
REM Exit-Codes (aus platten_waechter.py durchgereicht):
REM   0  genug Platz (oder -Immer/-Reinigen: Bericht gedruckt)
REM   3  Platz UNTER der Grenze - gemeldet, nichts geloescht
setlocal EnableDelayedExpansion

REM Die Projektwurzel aus der Skriptposition, nicht fest verdrahtet - im
REM Push-Gate liegt das Skript im Commit-Worktree.
for %%I in ("%~dp0..") do set "PROJ=%%~fI"
set "SCRIPT=%PROJ%\Tools\platten_waechter.py"

REM Argumente durchreichen, ohne sie in den Pfad zu kleben. GEMESSEN: der
REM erste Wurf baute "pfad\platten_waechter.py -Immer -Schwelle 30" und
REM python suchte diese Datei - der Aufgabenplanungseintrag waere damit
REM stillschweigend wirkungslos gewesen, ohne einen Fehler zu melden.
set "ARGS="
:schleife
if "%~1"=="" goto ende
set "ARG=%~1"
set "ARGS=!ARGS! !ARG!"
shift
goto schleife
:ende

if not exist "%PROJ%\Saved\Diagnose" mkdir "%PROJ%\Saved\Diagnose" 2>nul

python "!SCRIPT!" !ARGS! > "%PROJ%\Saved\Diagnose\plattenbericht.txt" 2>&1
set "RC=%errorlevel%"
type "%PROJ%\Saved\Diagnose\plattenbericht.txt"
if "%RC%"=="3" echo WARNUNG: Plattenplatz unter der Grenze. Bericht: Saved\Diagnose\plattenbericht.txt
exit /b %RC%
