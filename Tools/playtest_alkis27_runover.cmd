@echo off
call "%~dp0engine_run_lock.cmd" -Modus Start -Name playtest_alkis27_runover
if errorlevel 1 exit /b 1
REM Skriptgestuetzter Spieltest auf Alkis27: Passanten ueberfahren, Ton,
REM Fahndungskonto, zwei Bilder. 25.09.2026.
REM
REM Warum ueberhaupt ein Skript und keine Tastatur: Tastatur-Injektion wirkt
REM auf ein D3D-Fenster nicht (AGENTS.md). Die Choreografie laeuft deshalb
REM ueber die vorhandenen Schalter. Der Wagen ist der STANDARD-Kaefer
REM (AWiesbadenCar, der Fahrphysik-Pfad mit der Ueberfahren-Kette), NICHT der
REM Chaos-Wagen: der ist auf dieser Karte dokumentiert defekt (Bauch-Lage,
REM Vollgas 6,6 km/h, Versinken statt Fahren - siehe den Lauf in
REM Saved/Logs/wb_playtest_alkis27_chaos.log). Ein Spieltest, der auf einem
REM nicht fahrenden Wagen basiert, beweist nichts.
REM
REM   -WbGoto=X,Y          auf den GEHWEG stellen, nicht in die Fahrbahnmitte:
REM   -WbGotoYaw=32.4      Platter Strasse, 30 % des laengsten Segments
REM                        (321 m), 4,5 m nach links = Gehwegmitte (6,5 m
REM                        Fahrbahn-Halbbreite 3,25 + 2,5 m Gehweg-Halbbreite
REM                        1,25). Punkt und Kurs stammen aus dem echten
REM                        Netzabzug Tools/dump_alkis27_streets.cmd
REM                        (Saved/Diagnose/Strassenalkis27.csv), nicht geraten -
REM                        in der Fahrbahnmitte bleiben die Passanten auf dem
REM                        2,5-m-Geheweg ausser Reichweite der 110-cm-Sonde.
REM   "WbDrive 34"         Fahrprofil ueber die echte Fahrphysik: 40 % Vollgas
REM                        geradeaus, 20 % Lenksweep rechts, 20 % links, 20 %
REM                        bremsen - die Lenkphasen halten den Wagen auf der Strasse
REM   -WbPedDensity=8      Passanten-Zielzahl x8, Radius 80 m: auf der
REM   -WbPedRadius=80      Fahrtstrecke steht alle paar Meter jemand.
REM   -WbShotWhenReady     drei Kamerabilder, sobald die Karte traegt
REM   -WbShotPoseFile=...  (WbSeries_000..002.png): Posen aus
REM                        Saved/Diagnose/poses_alkis27_runover.txt, also
REM                        kamerafuehrend und NICHT zeitgebunden - ein
REM                        -WbShot=<Sekunden> lag in einem Lauf NACH dem
REM                        Selbstabbruch (das Konto laeuft ab Subsystem-
REM                        Start, die Stadt steht aber erst 30 s spaeter),
REM                        die Pose-Serie haengt nur an der Stadtreife.
REM   -WbQuitAfter=95      Selbstabbruch, damit das Log vollstaendig zu Ende
REM                        geschrieben wird (hartes taskkill verliert Puffer).
REM
REM Belege im Log (Saved/Logs/wb_playtest_alkis27.log):
REM   "Ueberfahren: N Fussgaenger."                     (Treffer)
REM   "Passanten-Treffer: Aufnahme ... gespielt."       (Ton, LogWbCore)
REM   "Fahndung: Stufe N (P Punkte) nach Ereignis ..."  (Fahndungskonto)
REM
REM Aufruf: Tools\playtest_alkis27_runover.cmd
setlocal
set UE=C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Saved\Logs\wb_playtest_alkis27.log
"%UE%" "%PROJ%" /Game/Maps/WiesbadenCity_Alkis31 -game -WbGoto=-196192,-724814 -WbGotoYaw=32.4 -WbPedDensity=8 -WbPedRadius=80 -WbShotWhenReady "-WbShotPoseFile=C:/freebuff/WiesbadenReal_Sicherung/WiesbadenReal/Saved/Diagnose/poses_alkis27_runover.txt" -WbPoseSettle=3 -WbQuitAfter=95 -windowed -ResX=1920 -ResY=1080 -unattended -nop4 -ExecCmds="WbDrive 34" -abslog="%LOG%"
endlocal
