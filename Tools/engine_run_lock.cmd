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
REM
REM Exit 0 = Lock gehalten (bzw. frei), Exit 3 = Lock durch fremden Lauf belegt.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0engine_run_lock.ps1" %*
exit /b %ERRORLEVEL%
