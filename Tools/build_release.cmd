@echo off
REM Release-Pipeline: Kompilieren -> Unit-Tests -> Rauchtest -> Paketieren ->
REM BugTank-Development-Abnahme im frischen Paket -> Desktop-Verknuepfung.
REM Bei roter BugTank-Abnahme wird das vorherige Paket wiederhergestellt.
REM
REM   Tools\build_release.cmd            voller Lauf inkl. Paketierung (Stunden)
REM   Tools\build_release.cmd -GatesOnly nur Gates 0-3 (~10-15 min, vor dem Commit)
REM
REM Exit 0 = Erfolg, sonst 1 (ein Gate rot -> kein Paket).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_release.ps1" %*
exit /b %ERRORLEVEL%
