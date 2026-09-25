@echo off
REM Gemeinsamer Startschritt fuer Bake-, Test- und Diagnosewrapper.
REM Optional: -DryRun zeigt nur die Ziele und beendet nichts.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0cleanup_unreal_processes.ps1" %*
exit /b %ERRORLEVEL%
