@echo off
rem Das Heli-Paar aufnehmen: der neue Ka-52 als Spielerheli und das ALTE
rem Landmarken-Modell daneben als Standstueck.
rem
rem Das Spiel startet am Steuer eines Autos. Die beiden Helikopter stehen in
rem Strassenflucht 12 m bzw. 12 m + Standabstand (rund 23 m) vor dem Wagen -
rem die Fahrerkamera zeigt damit beide. -WbZuFuss laesst den Fahrer vorher
rem aussteigen, damit das Bild aus Augenhoehe kommt (Kamerahoehe des Autos
rem verdeckt die Kufen).
rem
rem Aufruf: shot_heli_paar.cmd [Karte] [Bild bei s] [Ende bei s] [Pose...]
rem   %1 Karte (Standard WiesbadenCity_Alkis15 = neuerster Vollbau)
rem   %2 -WbShot=<s>      Bildzeitpunkt
rem   %3 -WbQuitAfter=<s> Ende des Laufs
rem   %4.. zusaetzliche Schalter, z. B. eine feste Kamera:
rem        -WbAerial=8 -WbAtX=<cm> -WbAtY=<cm> -WbYaw=0 -WbPitch=-15
rem      Die Koordinaten des Paares stehen im Log des ersten Laufs
rem      ("Helikopter abgesetzt: ... bei (x, y, z)" / "Alter Helikopter steht
rem      ... bei (x, y, z)") - gemessen, nicht geschaetzt.
rem
rem Ergebnis: Saved\Diagnose\Messstelle00000.png + Saved\Logs\WiesbadenReal.log
rem (dort die beiden Absetz-Zeilen mit Abstand und Rotorkreisen).

set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\shot_heli_paar.log
set MAP=%~1
if "%MAP%"=="" set MAP=WiesbadenCity_Alkis15
set SHOT=%~2
if "%SHOT%"=="" set SHOT=20
set QUIT=%~3
if "%QUIT%"=="" set QUIT=30
rem Zusatzschalter ab %4 einsammeln. NICHT "%4 ... %10" schreiben: in einer
rem Batchdatei ist %10 nichts anderes als %1 gefolgt von einer Null - der
rem Kartenschalter landete damit ein zweites Mal (und falsch) in der Zeile.
set REST=
shift
shift
shift
:rest
if "%~1"=="" goto :launch
set REST=%REST% %1
shift
goto :rest

:launch

"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" /Game/Maps/%MAP% -game -WbZuFuss=4 -WbShot=%SHOT% -WbQuitAfter=%QUIT% %REST% -windowed -ResX=1920 -ResY=1080 -stdout -unattended -nop4 > "%LOG%" 2>&1
exit /b %ERRORLEVEL%
