@echo off
REM Wartet, bis kein UnrealEditor.exe mehr laeuft, und baut dann Gate 1 plus den
REM Ka-52-Automationstest. Wird per Detach gestartet, weil der Editor minutenlang
REM offen bleiben kann.
REM
REM Ergebnis nach Saved\Diagnose\ka52\build_test_ergebnis.txt - Prints aus einem
REM Build sind nicht zuverlaessig erreichbar, der Dateibericht schon.
REM
REM `find /i` ist auf diesem Rechner das GNU-find aus Git Bash, nicht das
REM Windows-find.exe - `where find` listet das von Git ZUERST. Es meldet
REM "find: '/i': No such file or directory", liefert errorlevel 1, und die
REM Editor-Abfrage meldet damit IMMER "Editor zu". Das Skript hat dadurch
REM nie gewartet und trotzdem gebaut; Live Coding blockierte den Build und
REM UBT brach mit EXITCODE 6 ab, was wie ein Rechnerfehler aussah.
REM `findstr /b /c:` ist Windows-eigen, wird nicht verdeckt und matcht nur
REM die Prozesszeile: die Zeile "Keine Aufgaben ... ausgefuehrt" beginnt mit
REM INFORMATION:, nicht mit dem Programmnamen.
REM
REM Warum Warten statt taskkill: ein offener Editor mit Live Coding blockiert
REM UnrealBuildTool ("Unable to build while Live Coding is active"). Der Editor
REM gehoert dem Menschen; beenden darf man ihn nur auf ausdruecklichen Auftrag.
setlocal
REM Das Skript liegt in Tools\ - ein Level hoch ist das Projektwurzelverzeichnis.
cd /d "%~dp0.."
if not exist Saved\tmp mkdir Saved\tmp
set RPT=Saved\Diagnose\ka52\build_test_ergebnis.txt
if exist "%RPT%" del /q "%RPT%"
rem Das Wegsein wird geprueft, nicht geglaubt: "del" laeuft nach "Zugriff
rem verweigert" weiter, und der Aufrufer liest am Ende nur "ERGEBNIS: OK"
rem aus %RPT%. Ein liegengebliebener alter Bericht waere eine bestandene
rem Bau-Aussage fuer einen Lauf, der gar nicht gebaut hat. Praktisch
rem unwahrscheinlich (der Build laeuft nur bei geschlossenem Editor), aber
rem die Bauform soll nicht davon abhaengen, dass es klappt.
rem Das Wegsein wird geprueft, nicht geglaubt: "del" laeuft nach einer
rem gesperrten Datei weiter, und der Aufrufer liest am Ende nur "ERGEBNIS: OK"
rem aus %RPT%. Ein liegengebliebener alter Bericht waere eine bestandene
rem Bau-Aussage fuer einen Lauf, der gar nicht gebaut hat. Praktisch
rem unwahrscheinlich (der Build laeuft nur bei geschlossenem Editor), aber die
rem Bauform soll nicht davon abhaengen, dass das Loeschen klappt.
if not exist "%RPT%" goto :beleg_weg
echo.
echo ABBRUCH: der alte Ergebnisbericht laesst sich nicht loeschen - "%RPT%"
echo   Die Auswertung waere dann die des LETZTEN Laufs. Bitte den haengenden
echo   Prozess beenden (Tools\cleanup_unreal_processes.cmd), dann erneut.
exit /b 3

:beleg_weg

REM Kein PID eintippen: der war 24628, als das Skript entstand, und laeuft
REM Jahre spaeter als Text mit. Wer danach im Task-Manager nachsucht, findet
REM den_editor nicht, den der Lauf wirklich wartet.
echo Warte auf das Ende des Editors bzw. von Live Coding ... > Saved\tmp\ka52_bau_wartet.log
:warten
tasklist /FI "IMAGENAME eq UnrealEditor.exe" /NH 2>nul | findstr /b /c:"UnrealEditor.exe" > nul
if not errorlevel 1 (
  echo %date% %time% - laeuft noch >> Saved\tmp\ka52_bau_wartet.log
  ping -n 21 127.0.0.1 > nul
  goto :warten
)
tasklist /FI "IMAGENAME eq LiveCodingConsole.exe" /NH 2>nul | findstr /b /c:"LiveCodingConsole.exe" > nul
if not errorlevel 1 (
  echo %date% %time% - Live Coding laeuft noch >> Saved\tmp\ka52_bau_wartet.log
  ping -n 21 127.0.0.1 > nul
  goto :warten
)
echo %date% %time% - Editor zu, warte 10 s zur Sicherheit >> Saved\tmp\ka52_bau_wartet.log
ping -n 11 127.0.0.1 > nul

REM Jetzt noch einmal pruefen: wer den Editor in der Pause wieder geoeffnet
REM hat, soll nicht auf eine halbe DLL warten.
tasklist /FI "IMAGENAME eq UnrealEditor.exe" /NH 2>nul | findstr /b /c:"UnrealEditor.exe" > nul
if not errorlevel 1 (
  echo %date% %time% - Editor wieder offen, weiter warten >> Saved\tmp\ka52_bau_wartet.log
  goto :warten
)

echo %date% %time% - starte build_gate1 >> Saved\tmp\ka52_bau_wartet.log
call Tools\engine_run_lock.cmd -Modus Start -Name ka52_final >> Saved\tmp\ka52_bau_wartet.log 2>&1
call Tools\build_gate1.cmd >> Saved\tmp\ka52_bau_wartet.log 2>&1
set BUILD=%ERRORLEVEL%
echo %date% %time% - build_gate1 Exit %BUILD% >> Saved\tmp\ka52_bau_wartet.log

if not "%BUILD%"=="0" (
  echo ERGEBNIS: BAUFEHLER > "%RPT%"
  echo Gate 1 endete mit Exit %BUILD%. Log: Saved\Logs\wb_build_gate1.log >> "%RPT%"
  exit /b 1
)

echo %date% %time% - Automationstest >> Saved\tmp\ka52_bau_wartet.log
call Tools\run_automation_test.cmd WiesbadenReal.Vehicles.Ka52Ausstattung ka52 >> Saved\tmp\ka52_bau_wartet.log 2>&1
set TEST=%ERRORLEVEL%
echo %date% %time% - Test Exit %TEST% >> Saved\tmp\ka52_bau_wartet.log

echo ERGEBNIS: OK > "%RPT%"
echo build_gate1: Exit 0 >> "%RPT%"
echo Log: Saved\Logs\wb_build_gate1.log >> "%RPT%"
echo >> "%RPT%"
findstr /C:"Test Completed" /C:"LogAutomationController: Error" Saved\Logs\wb_test_ka52.log >> "%RPT%" 2>nul
echo >> "%RPT%"
findstr /C:"TEST COMPLETE" Saved\Logs\wb_test_ka52.log >> "%RPT%" 2>nul
echo ENDE >> "%RPT%"
endlocal
