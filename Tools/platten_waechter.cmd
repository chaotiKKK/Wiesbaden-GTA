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
REM Zwei Dateien, nicht eine:
REM   plattenbericht.txt        der JETZIGE Stand (im Normalfall eine Zeile,
REM                              weil nichts zu melden ist)
REM   plattenbericht_warnung.txt  die letzte Detailmeldung einer Platznot
REM Jede Ausfuehrung ueberschreibt plattenbericht.txt, der Warnfall kopiert
REM vorher seinen Detailbericht weg - sonst waere der Nachweis nach einem
REM einzigen gesunden Tag verloren.
REM
REM LOESCHPROTOKOLL: JEDER Loeschpfad hinterlaesst eine Zeile in
REM Saved\Diagnose\loeschprotokoll.jsonl (angehaengt, JSONL) mit der
REM Begruendung aus der Kandidatenliste und der wirklich freigewordenen
REM Groesse - zweimal je Pfad: "absicht" VOR dem Eingriff, "ergebnis" danach.
REM Laesst sich das Protokoll nicht schreiben, wird GARNICHTS geloescht und
REM der Pfad wandert nach "abgewiesen". Ein Loeschen ohne Protokoll waere ein
REM Eingriff, den man spaeter nicht mehr erklaeren kann.
REM
REM Exit-Codes (aus platten_waechter.py durchgereicht):
REM   0  genug Platz (oder -Immer/-Reinigen: Bericht gedruckt)
REM   3  Platz UNTER der Grenze - gemeldet, nichts geloescht
setlocal EnableDelayedExpansion

REM TEXT kommt in der env des powershell-KINDprozesses an, nicht in diesem.
REM Deshalb wird er VOR dem Aufruf gesetzt - sonst zeigte der Toast einen
REM leeren Text.

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

if "%RC%"=="3" (
    echo WARNUNG: Plattenplatz unter der Grenze. Bericht: Saved\Diagnose\plattenbericht.txt
    REM GEMESSEN 27.09.2026: der Normalfall schreibt nur eine Zeile in
    REM plattenbericht.txt und UEBERSCHREIBT damit den Detailbericht (80 Bytes
    REM statt 2506). Ohne diese Kopie waere der Nachweis einer Platznot nach
    REM dem naechsten gesunden Tag verschwunden - und genau dann will man ihn
    REM lesen. Die Detailberichte bleiben deshalb nebeneinander stehen.
    copy /y "%PROJ%\Saved\Diagnose\plattenbericht.txt" "%PROJ%\Saved\Diagnose\plattenbericht_warnung.txt" >nul
    REM Die Benachrichtigung ist der Grund, warum es diese Aufgabe gibt: eine
    REM Datei, die niemand oeffnet, ist keine Meldung.
    REM
    REM GEMESSEN 27.09.2026, zwei Fehlannahmen ausgeraeumt:
    REM  1. System.Windows.Forms.MessageBox BLOCKIERT. Die Aufgabe haette
    REM     damit endlos als "laeuft" in der Aufgabenplanung gestanden, bis
    REM     jemand auf OK klickt - bei einem unbeaufsichtigten Platten-
    REM     waechter genau das Falsche. Ersetzt durch eine Toast-Meldung.
    REM  2. msg.exe ist auf diesem Rechner NICHT vorhanden (10.0.26200); ein
    REM     Aufruf endet mit "kann nicht gefunden werden".
    REM Der Toast kommt ueber die Windows-COM-Schnittstelle, ist asynchron und
    REM laeuft nach; BurntToast waere eine Abhaengigkeit, die man erst
    REM installieren muesste. Scheitert er (alte Oberflaeche), bleibt die
    REM Datei - deshalb steht der Pfad oben in der echo-Zeile.
    REM
    REM GEMESSEN 27.09.2026, dritter Fund: $t.GetElementsByTagName('text')
    REM liefert eine LIVE-Sammlung. Das erste AppendChild aendert sie, der
    REM naechste Zugriff darauf bricht mit "Die Sammlung wurde geaendert" ab -
    REM und weil der ganze Block in einem catch steckt, waere die Warnung
    REM lautlos ausgefallen. Deshalb das @(...): es kopiert vorher in ein
    REM Array. Gegenprobe: gleicher Aufruf ohne @ -> TOAST_FEHLER, mit @ ->
    REM TOAST_OK.
    set "TEXT="
    for /f "usebackq delims=" %%Z in ("%PROJ%\Saved\Diagnose\plattenbericht.txt") do (
        if not defined TEXT set "TEXT=%%Z"
    )
    powershell -NoProfile -ExecutionPolicy Bypass -Command ^
      "try { [Windows.UI.Notifications.ToastNotificationManager,Windows.UI.Notifications,ContentType=WindowsRuntime] | Out-Null; $t=[Windows.UI.Notifications.ToastNotificationManager]::GetTemplateContent([Windows.UI.Notifications.ToastTemplateType]::ToastText02); $n=@($t.GetElementsByTagName('text')); $n[0].AppendChild($t.CreateTextNode('Plattenplatz unter der Grenze')) | Out-Null; $n[1].AppendChild($t.CreateTextNode($env:TEXT)) | Out-Null; [Windows.UI.Notifications.ToastNotificationManager]::CreateToastNotifier('WiesbadenReal').Show([Windows.UI.Notifications.ToastNotification]::new($t)) } catch { }" 2>nul
)

exit /b %RC%
