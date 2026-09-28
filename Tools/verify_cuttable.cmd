@echo off
REM verify_cuttable.cmd [Logname] [-NurPruefen]
REM
REM Gate fuer die Plasmacutter-Bildfolge: faehrt den Lauf (-WbCutShots) und
REM prueft danach Log UND Pixel mit Tools\verify_cuttable.py.
REM
REM   cmd //c "Tools\verify_cuttable.cmd"                  Lauf + Pruefung
REM   cmd //c "Tools\verify_cuttable.cmd schnitt_neu"     eigener Logname
REM   cmd //c "Tools\verify_cuttable.cmd -NurPruefen"     nur Log+Bilder
REM   cmd //c "Tools\verify_cuttable.cmd schnitt -NurPruefen"
REM
REM -NurPruefen braucht keine Engine und keine Sperre: es prueft einen
REM vorhandenen Lauf. Damit laesst sich das Gate nach einem Abbruch erneut
REM anwenden, ohne die Stadt erneut aufzubauen.
REM
REM Das Gate vergleicht Log UND Pixel. Allein die Log-Zeilen zu pruefen
REM reicht nicht: alle drei Winkel koennen 80 Grad daneben zeigen und
REM trotzdem vier "Bild gespeichert"-Zeilen ins Log schreiben.
REM
REM Das Gate selbst prueft man getrennt - ohne Engine und ohne Sperre,
REM es braucht aber echte Belege in Saved\Diagnose:
REM   python -m unittest Tools.test_verify_cuttable_gate -v
REM Es baut die acht Fehlerfaelle nach und verlangt, dass das Gate bei
REM jedem ROT wird. 21 gruene Pruefungen allein sagen nichts darueber, ob
REM die Pruefungen etwas koennen. Im Commit-Worktree ueberspringt dieser
REM Test (Saved ist nicht versioniert) - dort faehrt nur dieser Lauf.
REM
REM Belege: Saved\Logs\wb_test_verify_cuttable.log (Gate),
REM         Saved\Logs\wb_cut_<name>.log (Lauf),
REM         Saved\Diagnose\schnitt_uebersicht.html (Kontaktbogen)
REM Der Lauf dauert rund 40 s (Stadt aufbauen, vier Kamerawechsel).

REM EnableDelayedExpansion ist Pflicht, nicht Kosmetik: %errorlevel% und
REM %RUNRC% INNERHALB eines if-Blocks werden vor der Ausfuehrung des
REM Blocks expandiert und sind dann immer leer. Genau so hat die erste
REM Fassung jeden erfolgreichen Lauf als "abgebrochen" gemeldet, weil
REM "%RUNRC%" niemals "0" war.
setlocal EnableDelayedExpansion

REM Die Projektwurzel aus der Skriptposition, nicht fest verdrahtet - im
REM Push-Gate liegt das Skript im Commit-Worktree und muss dort messen,
REM nicht im Hauptprojekt (siehe run_cut_shots.cmd).
for %%I in ("%~dp0..") do set "PROJ=%%~fI"
set "NAME=schnitt"
set "NURPRUEF=0"
if /i "%~1"=="-NurPruefen" (
  set "NURPRUEF=1"
) else (
  if not "%~1"=="" set "NAME=%~1"
  if /i "%~2"=="-NurPruefen" set "NURPRUEF=1"
)

REM Der Lauf legt wb_cut_<name>.log an - das Prefix gehoert zu
REM run_cut_shots.cmd, hier nur <name> einsetzen. Sonst sucht das Gate
REM nach wb_<name>.log und meldet "Log fehlt", obwohl der Lauf da war.
set "LOG=%PROJ%\Saved\Logs\wb_cut_%NAME%.log"
set "GATELOG=%PROJ%\Saved\Logs\wb_test_verify_cuttable.log"
set "PY=C:\Python314\python.exe"
if not exist "%PY%" set "PY=python"

REM JEDES "exit /b" STEHT AUF OBERSTER EBENE, erreicht per goto.
REM GEMESSEN am 28.09.2026: ein "exit /b 1" in einem INNEREN Block, hinter
REM dem im umschliessenden Block noch ein Befehl folgt, kommt beim Aufrufer
REM von "cmd /c" als 0 an. So war dieser Block gebaut: das Push-Gate meldete
REM Gate 4 nach 0 s gruen, obwohl der Lauf gar nicht gestartet war (Platte
REM an der 10-Prozent-Grenze) und "ROT" auf dem Schirm stand - ohne ein
REM neues Bild. Abgesichert in Tools\test_cmd_exitcode.py.
set "RUNRC=0"
if "!NURPRUEF!"=="0" (
  echo == Plasmacutter-Gate: Lauf fahren
  echo    Log: !LOG!
  call "%PROJ%\Tools\run_cut_shots.cmd" !NAME!
  set "RUNRC=!errorlevel!"
) else (
  echo == Plasmacutter-Gate: nur pruefen, kein Lauf
)
if "!RUNRC!"=="2" goto :nicht_gestartet
if not "!RUNRC!"=="0" goto :abgebrochen

echo.
echo == Plasmacutter-Gate: Log und Bilder pruefen
cd /d "%PROJ%"
"%PY%" Tools\verify_cuttable.py "!LOG!" > "!GATELOG!" 2>&1
set "RC=!errorlevel!"
type "!GATELOG!"
echo.
echo (Gate vollstaendig in !GATELOG!)

if not "!RC!"=="0" goto :durchgefallen
echo.
echo GRUEN  Bildfolge belegt. Log: !LOG! / !GATELOG!
exit /b 0

:nicht_gestartet
echo ROT  Der Lauf wurde nicht gestartet - die Engine-Sperre war belegt oder
echo      die Platte liegt unter der Grenze (Tools\engine_run_lock.ps1).
echo      Es sind keine neuen Bilder entstanden; die Pruefung laeuft
echo      absichtlich NICHT gegen die Bilder des letzten Laufes,
echo      sonst wuerde sie deren Glueh-Kante als Beleg ausgeben.
exit /b 1

:abgebrochen
echo ROT  Der Lauf ist mit Fehlercode !RUNRC! abgebrochen - die
echo      Editor-Meldung steht in "!LOG!.out".
exit /b 1

:durchgefallen
echo.
echo ROT  Das Gate ist durchgefallen ^(Exit !RC!^). Log: !GATELOG!
exit /b 1
