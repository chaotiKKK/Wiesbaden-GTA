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
REM
REM PLATTEN-GATE: -Modus Start und -Modus Nehmen brechen ab, wenn weniger als
REM 10 Prozent der Projektplatte frei sind (Exit 4). Der Editor- oder
REM Cook-Start wuerde auf so voller Platte mitten im Lauf abbrechen und
REM unfertige Pakete hinterlassen. -Modus Status und -Modus Freigeben
REM pruefen die Platte NICHT - sie starten nichts, und auf einer vollen Platte
REM muss man seinen Lock noch loskoennen. Notausgaenge: -PlattenTrotz
REM (trotzdem starten) und -PlattenGrenze 0 (Gate aus).
REM
REM Exit 0 = Lock gehalten (bzw. frei), Exit 3 = Lock durch fremden Lauf belegt,
REM       Exit 4 = Start abgebrochen, zu wenig Plattenplatz.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0engine_run_lock.ps1" %*
exit /b %ERRORLEVEL%
