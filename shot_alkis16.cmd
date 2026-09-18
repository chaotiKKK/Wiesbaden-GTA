@echo off
REM Beleg der frisch gebackenen Karte Alkis16: echte Spielsitzung mit
REM Luftaufnahme (Saved\Diagnose\Stadt*.png) und den Laufzeitmarken
REM (Buslinien, Stadt-Inventar) in Saved\Logs\WiesbadenReal.log.
REM
REM WICHTIG - zwei Fallen, die ein Belegbild wertlos machen:
REM   1) NICHT -unattended: ohne Spieler-/GameMode-Start werden die
REM      Laufzeit-Actors nie erzeugt (keine "Liniendatei ... gelesen"-Zeile,
REM      keine Nerobergbahn) - das sieht wie ein Fehler der neuen Karte aus.
REM   2) NICHT -WbScreenshot fuer die Luftaufnahme: dieser Pfad feuert schon
REM      beim Weltstart, also VOR "Bringing World ... up for play" -
REM      das Bild ist dann schwarz (350 KB statt ~1,7 MB) und die
REM      Laufzeit-Actors fehlen im Log. Richtig ist -WbShotWhenReady: erst
REM      wenn IsCityReady() true ist, laeuft ein Settle-Countdown
REM      (-WbShotDelay, Standard 6 s), dann EIN HighResShot; danach beendet
REM      sich die Sitzung von selbst (-WbShotNoQuit verhindert das).
REM
REM Karte als 1. Argument (Standard WiesbadenCity_Alkis16).
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set MAP=%~1
if "%MAP%"=="" set MAP=WiesbadenCity_Alkis16
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\shot_alkis16.log
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" /Game/Maps/%MAP% -game -WbShotWhenReady -WbCamHeight=400 -WbShotDelay=10 -windowed -ResX=1600 -ResY=900 -WbBusLog -stdout -nop4 > "%LOG%" 2>&1
exit /b %ERRORLEVEL%
