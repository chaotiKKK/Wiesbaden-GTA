@echo off
REM Blender-Skript ausfuehren - vorher aufraeumen, alles protokollieren.
REM
REM %1 = Skriptpfad, Rest = Argumente hinter --
REM
REM Zwei Laeufe des Rig-Skripts sind ohne jede Ausgabe in die
REM Zeitueberschreitung gelaufen. Ein haengender Altprozess war es nicht
REM (es lief keiner) - deshalb wird hier die VOLLSTAENDIGE Ausgabe
REM mitgeschrieben statt gefiltert, sonst sieht man den Grund nie.
taskkill /F /IM blender.exe >nul 2>&1
set LOG=C:\Users\ssonn\aivideo\WiesbadenReal\blender_lauf.log
"C:\Program Files\Blender Foundation\Blender 5.2\blender.exe" -b %1 -P %2 -- %3 %4 %5 %6 > "%LOG%" 2>&1
exit /b %ERRORLEVEL%
