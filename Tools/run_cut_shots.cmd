@echo off
REM Bildfolge des Plasmacutters: stellt ein Trenn-Stueck auf, trennt es und
REM legt drei Blickwinkel auf die gluehende Schnittkante ab.
REM
REM   run_cut_shots.cmd [Logname] [Sekunden]
REM     Logname  Teil vor ".log", Standard = schnitt
REM     Sekunden Selbstabbruch, Standard 60
REM
REM Rueckgabe (bewusst dreistufig - das Gate muss einen belegten Lock von
REM einem kaputten Lauf unterscheiden koennen, beide sind "nichts belegt"):
REM     0  Lauf vollstaendig durch
REM     1  Lauf gestartet, Editor beendet sich mit Fehlercode
REM     2  Lauf wurde NICHT gestartet, die Engine-Sperre war belegt
REM
REM Warum -WbZuFuss=6, obwohl die Kamera frei ist: der Lauf stellt nur die
REM Kamera um, die Figur muss aber zu Fuss stehen - im Fahrzeug sitzt sie
REM im Wagen und das Aufstellen des Trenn-Stuecks geht daneben.
REM
REM Nachweise:
REM   Saved\Diagnose\schnitt_00_vorher.png   ungeschnitten (Vergleich)
REM   Saved\Diagnose\schnitt_01_nah.png     nah an der Glutkante
REM   Saved\Diagnose\schnitt_02_schraeg.png schraeg von oben (Fuge + Glut)
REM   Saved\Diagnose\schnitt_03_weit.png    weit: abgefallenes Stueck + Rest
REM
REM -abslog statt nur -stdout: die Projekt-Logzeilen (Log-Verbositaet)
REM gehen beim stdout-Umlenk sonst verloren (gemessen 26.09.2026).
REM
REM Bekannte, harmlose Meldung auf der Konsole (nicht in den Logs):
REM   "Der Prozess kann nicht auf die Datei zugreifen..."
REM   "Der Verzeichnisname ist ungueltig."
REM Beide kommen aus dem Editorprozess selbst, nicht aus diesem Skript und
REM nicht aus engine_run_lock.cmd (beide Aufrufe isoliert geprueft). Der
REM Lauf ist danach vollstaendig - das entscheidende Kriterium ist die
REM Gate-Ausgabe, nicht die Ruhe auf der Konsole.
call "%~dp0engine_run_lock.cmd" -Modus Start -Name run_cut_shots
if errorlevel 1 exit /b 2

set NAME=%~1
if "%NAME%"=="" set NAME=schnitt
set QUIT=%~2
if "%QUIT%"=="" set QUIT=60

REM Die Projektwurzel aus der Skriptposition, NICHT fest verdrahtet: das
REM Push-Gate fahrt Tools\run_cut_shots.cmd im Commit-Worktree, und ein
REM fest eingetragener Pfad wuerde dort den Lauf im HAUPTprojekt starten -
REM mit den Bildern eines fremden laufenden Agenten als Beleg.
for %%I in ("%~dp0..") do set "WURZEL=%%~fI"
set PROJ=%WURZEL%\WiesbadenReal.uproject
set LOG=%WURZEL%\Saved\Logs\wb_cut_%NAME%.log

"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" -game -WbKeinIntro -WbCutShots -WbZuFuss=6 -WbQuitAfter=%QUIT% -windowed -ResX=1280 -ResY=720 -stdout -abslog="%LOG%" -unattended -nop4 > "%LOG%.out" 2>&1
set RC=%ERRORLEVEL%
echo EXITCODE %RC% >> "%LOG%.out"
call "%~dp0engine_run_lock.cmd" -Modus Freigeben > nul
endlocal & set RC=%RC%
if not "%RC%"=="0" exit /b 1
exit /b 0
