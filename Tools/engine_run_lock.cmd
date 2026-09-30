@echo off
REM Engine-Lock gegen parallele Laeufe (Tools\engine_run_lock.ps1). Nimmt die
REM Sperre fuer DIESEN Lauf und raeumt danach die Prozessreste - der Normalfall
REM ersetzt den Aufruf von cleanup_unreal_processes.cmd in den Wrappern.
REM
REM   engine_run_lock.cmd -Modus Start -Name rebake_alkis25
REM   engine_run_lock.cmd -Modus Nehmen -Name smoke_test -WarteSekunden 60
REM   engine_run_lock.cmd -Modus Freigeben [-Gewalt]
REM   engine_run_lock.cmd -Modus Status
REM   engine_run_lock.cmd -Modus Start -Name test -DryRun -LockPfad C:\...\test.lock
REM   engine_run_lock.cmd -Modus Start -Name test -PlattenTrotz
REM   engine_run_lock.cmd -Modus Start -Name test -PlattenGrenze 0
REM   engine_run_lock.cmd -Modus Start -Name test -PlattenWarteSekunden 0
REM   engine_run_lock.cmd -Modus Start -Name test -StartWarteSekunden 120
REM   engine_run_lock.cmd -Modus Start -Name test -WarteSekunden 0
REM
REM PLATTEN-GATE: -Modus Start und -Modus Nehmen brechen ab, wenn weniger als
REM 10 Prozent der Projektplatte frei sind (Exit 4). Der Editor- oder
REM Cook-Start wuerde auf so voller Platte mitten im Lauf abbrechen und
REM unfertige Pakete hinterlassen. -Modus Status und -Modus Freigeben
REM pruefen die Platte NICHT - sie starten nichts, und auf einer vollen Platte
REM muss man seinen Lock noch loskoennen. Notausgaenge: -PlattenTrotz
REM (trotzdem starten) und -PlattenGrenze 0 (Gate aus).
REM
REM VORREINIGUNG SEIT 30.09.2026: unter -PlattenReinigungsGrenze Prozent frei
REM (Vorgabe 20, die Meldegrenze des Waechters) raeumt der Start ZUERST die
REM reproduzierbaren Caches der Klasse 'cache' (Zen-DDC, Shader-DDC, npm/pip/
REM gradle - Tools\platten_waechter.py --reinigen) und nimmt ERST DANACH den
REM Lock. Die Caches sind abgeleitete Kopien und rechnen sich beim naechsten
REM Cook neu, kosten also nur Rechenzeit. NICHT angefasst werden die
REM Ausgabe-Klasse (kostet Bauzeit) und die dev-builds (Beweise des fremden
REM Threads); ein fehlender Waechter ist fail-open (eine Zeile im Log).
REM Abschalten: -PlattenReinigungsGrenze 0 - mit -PlattenGrenze 0 oder
REM -PlattenTrotz ist sie ohnehin aus, und -Modus Status/Freigeben raeumen nie.
REM
REM SEIT 28.09.2026 WARTET -Modus START AUF PLATZ (Vorgabe 30 min, eine
REM Meldung je Minute, Abbruch erst, wenn die Platte bis zur Frist zu voll
REM bleibt). Ein Raeumlauf oder ein endender Cook gibt Platz frei, ein Abbruch
REM kostet dagegen den ganzen Lauf. -PlattenWarteSekunden 0 stellt das alte
REM Verhalten wieder her. -Modus NEHMEN wartet NICHT: motor_sperre in
REM gate_worktree.py faehrt ihn und bringt seine eigene Frist mit - ein
REM zweites Warten hier wuerde deren Frist verdoppeln.
REM
REM SEIT 28.09.2026 WARTET -Modus START AUF EINEN BELEGTEN LOCK (Vorgabe
REM 30 min, eine Meldung je Minute, Exit 3 erst, wenn er bis zur Frist belegt
REM bleibt). Die .cmd-Wrapper bringen keine eigene Frist mit, und ein endender
REM fremder Lauf gibt die Sperre von selbst frei - ein sofortiges Exit 3 kostete
REM dagegen den ganzen Lauf und ein zweites Anwerfen von Hand. Die Frist stellt
REM -StartWarteSekunden ein, ein ausdrueckliches -WarteSekunden 0 schaltet das
REM Warten ab.
REM -Modus NEHMEN wartet NICHT (Vorgabe -WarteSekunden 0): motor_sperre in
REM gate_worktree.py faehrt ihn und bringt seine eigene Frist mit - ein zweites
REM Warten hier wuerde deren Frist verdoppeln.
REM
REM Exit 0 = Lock gehalten (bzw. frei), Exit 3 = Lock durch fremden Lauf belegt,
REM       Exit 4 = Start abgebrochen, zu wenig Plattenplatz.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0engine_run_lock.ps1" %*
exit /b %ERRORLEVEL%
