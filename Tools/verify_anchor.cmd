@echo off
setlocal
rem NUR LESENDER Prueflauf des gespeicherten Verankerungszustands.
rem Speichert nichts. Ergebnis: Saved\Diagnose\anchor_verify.txt
rem
rem DREI SACHEN, DIE DIESES SKRIPT VORHERS STILL ERFOLGLOS GEMACHT HAT:
rem
rem 1. ABSOLUTER SKRIPTPFAD. "-script=Tools/verify_anchor_state.py" loest die
rem    Engine gegen Engine\Binaries\Win64 auf und meldet nur "Could not load
rem    Python file" (Exit 127, keine Ergebnisdatei). Nur ein vollstaendiger
rem    Pfad laeuft - siehe SCRIPT weiter unten.
rem
rem 2. ALTES ERGEBNIS LOESCHEN. Das Skript schreibt die Datei am ENDE. Bricht
rem    es vorher ab, liegt der Lauf von gestern noch da und sieht aus wie ein
rem    Ergebnis. Genau das war der wertlose Alkis24-Stand, der wochenlang als
rem    Messung galt. Deshalb: vorher weg, und am Ende MUSS eine neue Datei da
rem    sein, sonst ist der Lauf fehlgeschlagen.
rem
rem 3. ECHTER FEHLSCHLAG. Die Engine beendet sich bei Skriptfehlern mit 127
rem    (gemessen: fehlende Datei UND Absturz im Skript, beide mit "Python
rem    script executed with errors" im Log). Der alte Aufruf meldete trotzdem
rem    Erfolg, weil niemand nach der Ergebnisdatei sah. Dieses Skript prueft
rem    beides und gibt bei Fehlschlag eine ungewoehnliche Exit-Code.
rem
rem Bauform: Sprungmarken statt "if ( ... )"-Bloecken. In einem Klammerblock
rem beendet das ERSTE ungeschuetzte ")" den Block - auch eines, das in einem
rem Text steht. Ein "echo ... errors." am Ende einer Zeile darin reisst den
rem ganzen Block auf ("'.' kann syntaktisch ... nicht verarbeitet werden").
rem
rem Karte: die Standardkarte aus Config\DefaultEngine.ini (Tools\karte.py).
rem NICHT per WB_MAP aus Git Bash setzen - dort wird /Game/... zu
rem C:\Program Files\Git\Game\... umgeschrieben, und der Lauf misst eine leere
rem Ebene. Fuer eine andere Karte das ARGUMENT benutzen - die ini
rem umzuschalten ist der Umweg, der eine laufende Messung ueberschreibt:
rem
rem   Tools\verify_anchor.cmd              -> Standardkarte (siehe oben)
rem   Tools\verify_anchor.cmd Alkis31      -> /Game/Maps/WiesbadenCity_Alkis31
rem   Tools\verify_anchor.cmd /Game/Maps/X -> genau diese Karte
rem
rem Der Name wird HIER zum Paketpfad gebaut und als WB_MAP an das Skript
rem gereicht. So entsteht "/Game/..." nur in dieser Datei und nie auf der
rem Kommandozeile von Git Bash, die es umschreiben wuerde. Jede gemessene
rem Karte bekommt ihr eigenes Ergebnis - zwei Messungen sollen nebeneinander
rem stehen koennen, nicht sich gegenseitig ersetzen:
rem   Saved\Diagnose\anchor_verify.txt           (Standardkarte)
rem   Saved\Diagnose\anchor_verify_<Karte>.txt  (mit Argument)
rem
rem Ergebnis: Saved\Diagnose\anchor_verify.txt (neu je Lauf)
rem Log:      Saved\Logs\verify_anchor_<HIMMMS>.log
rem Exit 0 = gueltige Messung, 2 = Skript fehlt, 3 = Engine ohne Ergebnis,
rem 4 = keine Ergebnisdatei, 5 = falsche/leere Karte, 6 = keine Messzeile,
rem 7 = LEERE Komponenten am Kartenursprung (der World-Partition-Bruch).
rem Gemessen: ein Kartenname, den es nicht gibt ("Alkis99"), endet auf 3
rem und NICHT auf 5 - das Skript bricht mit RuntimeError "Karte nicht
rem geladen" ab und schreibt nichts, also greift 4/5 gar nicht erst. Die
rem Ursache steht als Traceback im Log. 5 ist fuer eine Ergebnisdatei,
rem die zwar da ist, aber keine geladene Karte nennt.

set "PROJ=%~dp0.."
set "UE=C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
set "SCRIPT=%PROJ%\Tools\verify_anchor_state.py"

rem -- 0. Messkarte: Argument, sonst die Standardkarte -----------------------
set "WBMAP=%~1"
if "%WBMAP%"=="" goto :ohne_argument
rem Ein blosser Name wie "Alkis31" wird zum vollstaendigen Paketpfad. Der
rem findstr-Test ist die einzige Stelle, an der "/Game/" in dieser Datei
rem entsteht - auf der Kommandozeile wuerde Git Bash es umschreiben.
echo %WBMAP%| findstr /C:"/Game/" > nul
if not errorlevel 1 goto :argument_ist_pfad
set "WBMAP=/Game/Maps/WiesbadenCity_%WBMAP%"
:argument_ist_pfad
rem Der Namenszusatz (%%~nxf ohne Ordner und ohne .umap) sorgt dafuer, dass
rem zwei Karten zwei Dateien bekommen. Im Kommentar muss %% stehen:
for %%f in ("%WBMAP%") do set "WBSUFFIX=_%%~nxf"
goto :argument_geprueft
:ohne_argument
set "WBMAP="
set "WBSUFFIX="
:argument_geprueft
if not "%WBMAP%"=="" set "WB_MAP=%WBMAP%"
rem Zweite Umgebungsvariable: derselbe Namenszusatz fuer das Skript.
rem Ohne dieses WB_SUFFIX schreibt das Skript nach anchor_verify.txt,
rem waehrend die Batch-Datei unter dem Kartennamen sucht - der Lauf
rem meldet dann "keine Ergebnisdatei", obwohl gemessen wurde.
if not "%WBMAP%"=="" set "WB_SUFFIX=%WBSUFFIX%"

set "ERGEBNIS=%PROJ%\Saved\Diagnose\anchor_verify%WBSUFFIX%.txt"
rem Eigenes Log je Lauf (HIMMMS), nicht ein fester Name: haelt sich ein
rem haengender Prozess an verify_anchor.log fest, blockiert er den naechsten
rem Lauf an der Umleitung ("Der Prozess kann nicht auf die Datei zugreifen")
rem und man glaubt, die Messung sei gescheitert. Genau das ist die Fehler-
rem klasse, die dieses Skript abstellen soll - sie darf nicht im Werkzeug
rem selbst stecken.
set "STAMP=%TIME:~0,2%%TIME:~3,2%%TIME:~6,2%"
set "LOG=%PROJ%\Saved\Logs\verify_anchor_%STAMP%.log"

rem -- 1. Vorbereiten: Skript da? altes Ergebnis weg? -------------------------
if not exist "%SCRIPT%" goto :kein_skript
if exist "%ERGEBNIS%" del /q "%ERGEBNIS%"
rem DEM WEGSEIN WIRD NICHT GLAUBT, ES WIRD GEPRUEFT. "del" meldet einen
rem gesperrten Pfad und laeuft trotzdem weiter - nachgemessen am 27.09.2026
rem an einer Datei mit offenem Handle (FileShare::None): "Der Prozess kann
rem nicht auf die Datei zugreifen, da sie von einem anderen Prozess verwen-
rem det wird", Errorlevel 1, Ablauf geht weiter. Genau dann wertet Schritt 3
rem die ERGEBNISDATEI DES LETZTEN LAUFS aus und meldet "0 leere Komponen-
rem ten am Kartenursprung" fuer einen Lauf, der gar nicht gemessen hat. Das
rem ist genau der Befund, vor dem dieses Skript steht, und der Stillfall
rem waere schlimmer als ein Fehlschlag: er gibt den Auftrag zum Weitermach-
rem en. Also hier abbrechen. (Nebenbei gemessen: das Read-only-Flag blockiert
rem "del /q" nicht - der offene Handle ist der Fall.)
rem (Nach dem Loeschen kann die Datei nur noch von DIESEM Lauf stammen - ein
rem Frische-Messen waere in cmd nur Theater.)
if not exist "%ERGEBNIS%" goto :ergebnis_weg
echo.
echo ABBRUCH: die alte Ergebnisdatei laesst sich nicht loeschen:
echo   "%ERGEBNIS%"
echo   Jede Auswertung waere dann die des LETZTEN Laufs, nicht diese Messung -
echo   also gar keine. Bitte den haengenden Prozess beenden
echo   (Tools\cleanup_unreal_processes.cmd) und erneut starten.
exit /b 8

:ergebnis_weg

rem -- 2. Lauf ----------------------------------------------------------------
"%UE%" "%PROJ%\WiesbadenReal.uproject" -run=pythonscript -script="%SCRIPT%" -unattended -nop4 -nosplash -nullrhi > "%LOG%" 2>&1
set "WB=%ERRORLEVEL%"

rem -- 3. Pruefen. MASSGEBLICH IST DIE ERGEBNISDATEI, nicht der Exit-Code:
rem       die Engine stuerzt beim Herunterfahren gelegentlich ab
rem       (gemessen: UnrealEditor-MegascansPlugin.dll in dllmain_crt_process_
rem       detach), nachdem die Messung laengst geschrieben ist. Ein gueltiges
rem       Ergebnis wird deshalb nicht weggeworfen - aber der Absturz wird
rem       laut gemeldet. Umgekehrt gilt ohne Ergebnis IMMER Fehler.
if exist "%ERGEBNIS%" goto :ergebnis_pruefen
if not "%WB%"=="0" goto :engine_fehler
goto :kein_ergebnis

:ergebnis_pruefen
findstr /C:"Karte geladen: /Game/Maps/" "%ERGEBNIS%" >nul
if errorlevel 1 goto :falsche_karte

findstr /C:"LEERE Komponenten:" "%ERGEBNIS%" >nul
if errorlevel 1 goto :keine_messung

rem -- 3a. SCHWELLE: null leere Komponenten am Kartenursprung ---------------
rem Die Zeile lautet "LEERE Komponenten: 23799 insgesamt, davon 0 am
rem Kartenursprung (...)"; das sechste Feld ist die Zahl am Ursprung. OHNE
rem diese Pruefung waere das Skript eine Messung ohne Aussage - die Datei im
rem Baum nennt 23 799 leere Komponenten und 0 am Ursprung, und beides sieht
rem gleich gruen aus. Genau die zweite Zahl ist der World-Partition-Bruch, an
rem dem die Verankerung erkannt wird (0 = geheilt).
rem
rem Bewusst als Vergleich mit der Ziffer "0" und ohne numerische Rechnung:
rem dass die Zeile ueberhaupt existiert, ist oben geprueft, ein fruehestes
rem leeres Feld ist also auch ein Befund - und kein Anlass fuer eine
rem Arithmetik, die im Klammerblock zerbrechen koennte.
for /f "tokens=6" %%A in ('findstr /C:"LEERE Komponenten:" "%ERGEBNIS%"') do set "WBAMORIGIN=%%A"
if not "%WBAMORIGIN%"=="0" goto :leere_am_ursprung

if not "%WB%"=="0" goto :messung_mit_abweichung

rem -- 4. Befund auf den Bildschirm ------------------------------------------
echo Werkzeug: %~nx0
if not "%WBMAP%"=="" echo Messkarte:  %WBMAP%
findstr /C:"Karte geladen:" "%ERGEBNIS%"
findstr /C:"Zell-Actors" "%ERGEBNIS%"
findstr /C:"LEERE Komponenten:" "%ERGEBNIS%"
echo Ergebnis: %ERGEBNIS%
echo Log:      %LOG%
exit /b 0

rem -- Sprungmarken ------------------------------------------------------------
:messung_mit_abweichung
rem Gueltiges Ergebnis, ungewoehnlicher Exit-Code. Beides sagen, nicht das eine
rem verschweigen und das andere melden.
echo Werkzeug: %~nx0
if not "%WBMAP%"=="" echo Messkarte:  %WBMAP%
findstr /C:"Karte geladen:" "%ERGEBNIS%"
findstr /C:"Zell-Actors" "%ERGEBNIS%"
findstr /C:"LEERE Komponenten:" "%ERGEBNIS%"
echo Ergebnis: %ERGEBNIS%
echo Log:      %LOG%
echo.
echo WARNUNG: Die Engine endete mit Exit %WB% - die Messung oben ist dennoch
echo   gueltig. Typischer Fall: Absturz beim Herunterfahren (Megascans-Plugin),
echo   nachdem das Ergebnis geschrieben war. Fuer die reine Messung zaehlt die
echo   Datei; wenn der Absturz stoert, im Log nach "Fatal error" suchen.
exit /b 0

:kein_skript
echo FEHLER: Skript fehlt - %SCRIPT%
exit /b 2

:engine_fehler
echo FEHLER: Engine endete mit Exit %WB% und hat NICHTS geschrieben.
echo   Gemessene Exit-Codes: 127, wenn die Engine das Skript nicht findet,
echo   -1, wenn das Skript selbst abbrach (beides mit "Python script
echo   executed with errors" im Log, dort steht auch der Grund).
echo   Log: %LOG%
rem -- DIAGNOSE des stillen Engine-Tods (nach dem 29.09.2026):
rem dieser Zweig heisst "Exit -1 und hat NICHTS geschrieben" - ohne
rem Logzeile blieb unklar, WARUM die Engine starb. Gemessen: Gate 5
rem starb zweimal so, je ~4 s nach Start, jeweils WAEHREND parallel
rem eine fremde Engine lief (ein Cook, ein interaktiver Editor -
rem beide ohne Gate-Worktree-Pfad, fuer den Worktree-Filter unsicht-
rem bar). Diese Zeilen machen den Moment sichtbar: die letzten
rem Logzeilen nennen die Stelle, der Engine-Snapshot die Begleit-
rem umstaende. Jede Zeile traegt das Merkmal WB-DIAGNOSE: - der
rem Gate-Bericht zeigt sie, ein Retry kann sie gezielt suchen.
echo WB-DIAGNOSE: stiller Engine-Tod - letzte Logzeilen (Engine-Log in UTC):
powershell -NoProfile -Command "if (Test-Path -LiteralPath '%LOG%') { Get-Content -LiteralPath '%LOG%' -Tail 3 | ForEach-Object { 'WB-DIAGNOSE:   ' + $_ } } else { 'WB-DIAGNOSE:   Logdatei fehlt: %LOG%' }"
echo WB-DIAGNOSE: laufende Engines jetzt:
powershell -NoProfile -Command "$e = Get-CimInstance Win32_Process -Filter \"Name='UnrealEditor.exe' or Name='UnrealEditor-Cmd.exe'\" | Select-Object ProcessId,CommandLine; if ($e) { $e | ForEach-Object { if ($_.CommandLine) { 'WB-DIAGNOSE:   PID ' + $_.ProcessId + ': ' + $_.CommandLine.Substring(0, [Math]::Min(160, $_.CommandLine.Length)) } else { 'WB-DIAGNOSE:   PID ' + $_.ProcessId + ': (ohne Commandline)' } } } else { 'WB-DIAGNOSE:   KEINE Engine laeuft mehr - der Tod kam nicht (mehr) von einer parallelen Engine.' }"
exit /b 3

:kein_ergebnis
echo FEHLER: keine Ergebnisdatei unter %ERGEBNIS%
echo   Der Lauf hat nichts geschrieben - meist ist die Karte nicht geladen.
echo   Log: %LOG%
exit /b 4

:falsche_karte
echo FEHLER: die Ergebnisdatei nennt keine geladene Karte - Kopf:
findstr /C:"Karte geladen:" "%ERGEBNIS%"
echo   Ein umgeschriebener Pfad bedeutet: es wurde eine leere Ebene gemessen.
echo   Log: %LOG%
exit /b 5

:keine_messung
echo FEHLER: die Ergebnisdatei enthaelt keine Zeile "LEERE Komponenten:".
echo   Log: %LOG%
exit /b 6


:leere_am_ursprung
echo FEHLER: %WBAMORIGIN% LEERE Komponenten liegen am Kartenursprung.
findstr /C:"LEERE Komponenten:" "%ERGEBNIS%"
echo   Das ist der World-Partition-Bruch, den die Verankerung behebt: eine
echo   leere Komponente (keine Sections, kein Mesh, keine Instanzen) bekommt
echo   Punkt-Bounds an ihrem eigenen Ort, der Chunk-Actor steht auf (0,0,0),
echo   und ohne StreamingAnchor wandert die Komponente damit an den
echo   Kartenursprung. 0 ist der gesunde Wert, 20 404 waren es am 25.09.2026.
echo   Befund: Saved\Diagnose\anchor_verify.txt
echo   Log:    %LOG%
exit /b 7
