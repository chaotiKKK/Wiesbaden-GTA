@echo off
rem Mitfahrt im Nerobergbahn-Wagen aufnehmen - ohne Tastendruck.
rem
rem Das Spiel startet am Steuer eines Autos; -WbZuFuss laesst den Fahrer nach
rem %FUSS% Sekunden aussteigen, -WbMitfahr setzt ihn danach in Wagen A. Die
rem Kamera der Mitfahrt steht IM Wagen (Cockpit), das Bild zeigt also genau den
rem eingerichteten Innenraum waehrend der Fahrt.
rem
rem Aufruf: shot_mitfahrt.cmd [Verzoegerung in Sekunden] [Kameramodus 0|1|2] [Kurbel bei s] [Logabstand s]
rem   0 = Follow (Wagen von aussen), 2 = Cockpit (Innenraum, Standard)
rem   Kurbel > 0: der Wasserschieber wird nach so vielen Sekunden geoeffnet
rem   (Entwicklungshilfe -WbKurbel), -1 = zu. Fuer die Aufnahme des Schwimmers
rem   im Schauglas: gleiche Verzoegerung, einmal mit und einmal ohne Kurbel -
rem   die Bilder unterscheiden sich nur in Fuellstand und Armstellung.
rem Ergebnis: Saved\Diagnose\WbReadyShot.png (+ .log mit den WbDev-Zeilen)
rem
rem -WbWagenlog schreibt Nadel-, Schwimmer- und Kurbelstellung alle %LOG%
rem Sekunden nach Saved\Logs\WiesbadenReal.log. Ohne diese Zahlen ist die
rem Nadelstellung im Bild nicht pruefbar (das Bild allein zeigt nur, DASS sie
rem steht, nicht WO sie stehen sollte).
rem
rem Beispiel Schwimmerprobe (gleiche Verzoegerung, nur der Schieber anders):
rem   shot_mitfahrt.cmd 45 2 -1 5   -> Fuellstand bleibt, Kurbelarm senkrecht
rem   shot_mitfahrt.cmd 45 2 12 5   -> Fuellstand faellt (75 % -> ~9 %), Arm
rem                                    zieht zum Bediener, beide Bilder
rem                                    unterscheiden sich nur darin

set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\shot_mitfahrt.log
set DELAY=%~1
if "%DELAY%"=="" set DELAY=20
set CAM=%~2
if "%CAM%"=="" set CAM=2
set KURBEL=%~3
if "%KURBEL%"=="" set KURBEL=-1
set WAGENLOG=%~4
if "%WAGENLOG%"=="" set WAGENLOG=5

"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" /Game/Maps/WiesbadenCity_Alkis4 -game -WbShotWhenReady -WbShotDelay=%DELAY% -WbZuFuss=5 -WbMitfahr=9 -WbCamMode=%CAM% -WbKurbel=%KURBEL% -WbWagenlog=%WAGENLOG% -windowed -ResX=1920 -ResY=1080 -stdout -unattended -nop4 > "%LOG%" 2>&1
exit /b %ERRORLEVEL%
