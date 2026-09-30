@echo off
REM verify_fahrphysik.cmd [-NurPruefen]
REM
REM Gate 7: Messfahrt auf der Wiese (Tools\fahrmessung.cmd gate_fahrphysik)
REM und ihre Kennzahlen gegen die kalibrierten Sollwerte des Kaefers
REM (Tools\verify_fahrphysik.py: 0-60 mph, Radspin, Kurvengrip, Bremsweg,
REM Verzoegerung, gleitende Raeder). Rund 3 Minuten.
REM
REM   -NurPruefen  keine Fahrt, nur das Log des letzten Laufs pruefen
REM
REM Belege: Saved\Logs\wb_fahrmessung_gate_fahrphysik.log (Fahrt),
REM         Saved\Diagnose\fahrphysik_gate.txt (Ergebnis).
REM Die Pruefung selbst testet: python -m unittest Tools.test_verify_fahrphysik
setlocal EnableDelayedExpansion
for %%I in ("%~dp0..") do set "PROJ=%%~fI"
set "LOG=%PROJ%\Saved\Logs\wb_fahrmessung_gate_fahrphysik.log"
set "PY=C:\Python314\python.exe"
if not exist "%PY%" set "PY=python"

REM Jedes "exit /b" steht auf oberster Ebene, erreicht per goto
REM (Tools\test_cmd_exitcode.py).
set "RUNRC=0"
if /i not "%~1"=="-NurPruefen" (
  echo == Fahrphysik-Gate: Messfahrt
  REM Altes Log weg: endet der Editor ohne neues Log, darf das Gate nicht
  REM die gruene Fahrt von gestern bewerten (Review PR #27).
  if exist "!LOG!" del "!LOG!"
  call "%PROJ%\Tools\fahrmessung.cmd" gate_fahrphysik 40
  set "RUNRC=!errorlevel!"
)
if "!RUNRC!"=="2" goto :nicht_gestartet
if not "!RUNRC!"=="0" goto :abgebrochen

echo == Fahrphysik-Gate: Kennzahlen gegen die Sollwerte
"%PY%" "%PROJ%\Tools\verify_fahrphysik.py" "!LOG!"
set "RC=!errorlevel!"
if "!RC!"=="0" goto :gruen
if "!RC!"=="2" goto :nicht_gemessen
echo ROT  Die Fahrphysik liegt ausserhalb der Sollwerte - siehe oben.
exit /b 1

:gruen
echo GRUEN  Fahrphysik im Sollband.
exit /b 0

:nicht_gestartet
echo ROT  Die Messfahrt wurde nicht gestartet (Engine-Sperre belegt oder Platte
echo      unter der Grenze). Gegen das Log eines frueheren Laufs wird bewusst
echo      NICHT geprueft.
exit /b 1

:abgebrochen
echo ROT  Die Messfahrt ist mit Fehlercode !RUNRC! abgebrochen - siehe "!LOG!.out".
exit /b 1

:nicht_gemessen
echo ROT  Nicht gemessen - die Fahrt lieferte keine vollstaendigen Kennzahlen.
exit /b 1
