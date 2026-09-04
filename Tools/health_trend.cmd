@echo off
REM Health-Trend WiesbadenReal: wertet die JSONL-Historie der WbHealth-Gate-Laeufe
REM aus und faengt schleichende Regressionen (einbrechende gezeichnete
REM Fussgaenger/Fahrzeuge ueber Laeufe).
REM   0 = kein Einbruch (oder zu wenig Historie), 1 = Einbruch erkannt.
REM Wird auch von health_check.cmd nach jedem Gate-Lauf mitgerufen.
powershell -ExecutionPolicy Bypass -NoProfile -File "%~dp0health_trend.ps1"
exit /b %ERRORLEVEL%
