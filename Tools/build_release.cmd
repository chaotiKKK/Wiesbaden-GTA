@echo off
REM Release-Pipeline: Kompilieren -> Unit-Tests -> Rauchtest -> (nur bei gruen)
REM Paketieren -> Desktop-Verknuepfung erneuern. Kein Gate ist ueberspringbar.
REM
REM   Tools\build_release.cmd            voller Lauf inkl. Paketierung (Stunden)
REM   Tools\build_release.cmd -GatesOnly nur Gates 0-3 (~10-15 min, vor dem Commit)
REM
REM Exit 0 = Erfolg, sonst 1 (ein Gate rot -> kein Paket).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_release.ps1" %*
exit /b %ERRORLEVEL%
