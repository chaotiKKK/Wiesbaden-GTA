@echo off
REM ===========================================================================
REM  Windows-Aufgabenplanung: Plattenwaechter taeglich melden lassen
REM ===========================================================================
REM Legt die Aufgabe "WB Plattenwaechter" an: taeglich eine Meldung ueber
REM den Plattenplatz, nichts loeschen. Startet sie sofort zur Kontrolle.
REM
REM GEMESSEN 27.09.2026: schtasks /create und /run laufen OHne Windows-
REM Anmeldung durch (CREATE_EXIT=0, RUN_EXIT=0). Die Aufgabe laeuft im
REM Anmeldemodus "Interaktiv/Hintergrund" - sie startet also nur, wenn der
REM Rechner mit diesem Benutzer angemeldet ist. Das ist gewollt: sie
REM braucht kein Kennwort und kein SYSTEM-Konto, und sie fasst die Platte
REM nur an, wenn ohnehin jemand arbeitet.
REM
REM Aufruf:  cmd //c "Tools\platten_waechter_eintragen.cmd"
REM           cmd //c "Tools\platten_waechter_eintragen.cmd" -Entfernen
REM
REM Ohne -Entfernen wird ein vorhandener Eintrag ueberschrieben (/f).
REM
REM "Starten in" (das Arbeitsverzeichnis) setzt schtasks nicht - dafuer gibt es
REM keine Option. GEMESSEN 27.09.2026: /query zeigt "Starten in: Nicht
REM zutreffend". stoert das nicht: platten_waechter.cmd leitet die
REM Projektwurzel selbst aus "%~dp0.." ab und schreibt absolut. Der Weg
REM waere nur fuer Aufgaben, die einen relativen Pfad benutzen.
REM ===========================================================================
setlocal

set "NAME=WB Plattenwaechter"
set "PROJ=%~dp0.."
for %%I in ("%PROJ%") do set "PROJ=%%~fI"
set "WRAPPER=%PROJ%\Tools\platten_waechter.cmd"

if not exist "%WRAPPER%" (
    echo FEHLER: %WRAPPER% nicht gefunden.
    exit /b 1
)

if /i "%~1"=="-Entfernen" goto entfernen

REM OHNE --immer, und das ist der Punkt: GEMESSEN 27.09.2026, der erste
REM Entwurf trug --immer - dann gibt es nie den Rueckgabecode 3, und die
REM WARNUNG-Zeile in platten_waechter.cmd (und damit der sichtbare Hinweis)
REM kann nie greifen. Ohne --immer schweigt der Waechter bei gesundem
REM Rechner mit einer Zeile und meldet sich nur bei Platznot.
REM
REM Wer den Bericht auch bei gesundem Rechner sehen will, haengt --immer an:
REM   schtasks /create /tn "%NAME%" /tr "cmd.exe /c \"%WRAPPER%\" --immer" ^
REM     /sc daily /st 08:30 /f
schtasks /create /tn "%NAME%" ^
  /tr "cmd.exe /c \"%WRAPPER%\"" ^
  /sc daily /st 08:30 /f
if errorlevel 1 (
    echo FEHLER: schtasks /create ist gescheitert.
    exit /b 1
)

echo.
echo Angelegt: taeglich 08:30, Startverzeichnis %PROJ%
echo Startet sofort zur Kontrolle ...
schtasks /run /tn "%NAME%"
if errorlevel 1 (
    echo FEHLER: schtasks /run ist gescheitert.
    exit /b 1
)
echo.
echo Jeder Lauf schreibt Saved\Diagnose\plattenbericht.txt
echo (im Normalfall eine Zeile), bei Platznot zusaetzlich ausfuehrlich
echo Saved\Diagnose\plattenbericht_warnung.txt plus eine Toast-Meldung.
exit /b 0

:entfernen
schtasks /delete /tn "%NAME%" /f
if errorlevel 1 (
    echo FEHLER: schtasks /delete ist gescheitert.
    exit /b 1
)
echo Entfernt.
exit /b 0
