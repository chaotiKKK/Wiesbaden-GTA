@echo off
REM Rauchtest WiesbadenReal: startet die Stadt, feuert die Dev-Befehle und
REM wertet aus dem Log Bestanden/Durchgefallen. Exit 0 = alles gruen.
REM Voraussetzung: Editor ist gebaut (Tools\bau_neuerpc.cmd).
powershell -ExecutionPolicy Bypass -NoProfile -File "%~dp0smoke_test.ps1"
exit /b %ERRORLEVEL%
