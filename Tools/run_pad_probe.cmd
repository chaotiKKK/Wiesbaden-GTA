@echo off
REM Gamepad-Pruef-Lauf: spielt LT/RT/RB/LB als simulierte Pad-Eingabe im
REM echten Spiel ab und belegt die Wirkung im Log.
REM
REM   run_pad_probe.cmd [Logname] [Sekunden]
REM     Logname  Teil vor ".log", Standard = pad
REM     Sekunden Selbstabbruch, Standard 60
REM
REM Warum -WbZuFuss=6: die Probe sitzt auf dem FUSS-Pawn, im Fahrzeug
REM gaelte sie nichts. Die Probe wartet danach noch 8 s Ruhe (Nachladen der
REM Zellen) und hat 12 bewertete Schritte mit zusammen ~10 s Laufzeit.
REM
REM Nachweise im Log (Saved\Logs\wb_pad_<name>.log):
REM   "LT gedrueckt - .. ADS WIRKT"                 (Zielen)
REM   "RT gedrueckt (LT bleibt) - .. FEUER WIRKT"   (Feuern)
REM   "RB gedrueckt (0,6 s) - .. WECHSEL VOR WIRKT" (Waffenwechsel vor)
REM   "LB gedrueckt (0,6 s) - .. WECHSEL ZURUECK WIRKT"
REM   "R3 gedrueckt - .. DUCKEN WIRKT"              (Ducken + Aufstehen)
REM   "A gedrueckt - .. SPRUNG WIRKT"               (Sprung, gemessene Hoehe)
REM   "linker Stick 1 s MIT L3 - .. RENNEN WIRKT"  (Rennen, gemessene Strecke)
REM   "D-Pad hoch bei gezielter Waffe - .. DPAD ZOOMT"
REM   "D-Pad hoch mit Trennwaffe - .. DPAD DREHT DIE SCHNITTEBENE"
REM   "Y gedrueckt - .. ANSICHT WECHSELT"           (Ego/Schulter)
REM   "X gedrueckt - .. EINSTEIGEN WIRKT"           (muss der LETZTE Schritt sein)
REM   "ALLE GAMEPAD-TASTEN WIRKEN (..)"             (Abschluss, sonst NICHT ...)
REM
REM -abslist/-abslog statt nur -stdout: die Projekt-Logzeilen (Log-
REM Verbosität) gehen beim stdout-Umlenk sonst verloren - gemessen am
REM 26.09.2026, es kamen nur die Warning-Zeilen an.
call "%~dp0engine_run_lock.cmd" -Modus Start -Name run_pad_probe
if errorlevel 1 exit /b 1

set NAME=%~1
if "%NAME%"=="" set NAME=pad
set QUIT=%~2
if "%QUIT%"=="" set QUIT=60

set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Saved\Logs\wb_pad_%NAME%.log

"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" -game -WbKeinIntro -WbPadProbe -WbZuFuss=6 -WbQuitAfter=%QUIT% -windowed -ResX=1280 -ResY=720 -stdout -abslog="%LOG%" -unattended -nop4 > "%LOG%.out" 2>&1
echo EXITCODE %ERRORLEVEL% >> "%LOG%"
call "%~dp0engine_run_lock.cmd" -Modus Freigeben
endlocal
