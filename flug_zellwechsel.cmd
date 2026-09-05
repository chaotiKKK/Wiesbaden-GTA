@echo off
REM Zellwechsel-Ruckler-Test: Beseitigt das Lade-Budget den Streaming-Ruckler
REM beim Zellwechsel wirklich?
REM
REM Ungebackene Karte Alkis3 -> erzwingt den Laufzeit-Stadtbuild (~3 min). Erst
REM danach (WbAutoDriveStart=260) rast der Pawn bodennah mit 250 km/h nach Osten.
REM Bodennah ist HAERTER als Fliegen: der Streaming-Radius ist am Boden eng
REM (900 m), Zellen kommen spaeter rein -> maximale Zellwechsel-Last. Gemessen
REM wird fokus-unabhaengig:
REM   - 15-s-Bildzeit-Logger (Mittel/schlechteste ms, Aussetzer > 50 ms)
REM   - "ProcessLoadedPackages took Xms" = der Nachlade-Stall auf dem Spielstrang
REM   - stat unit im Reiseflug-Screenshot (WbShot=300)
REM Alles in Saved\Logs\WiesbadenReal.log; Shot in Saved\Diagnose\Messstelle*.
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\flug_zellwechsel.log
"C:\freebuff\WiesbadenReal_Sicherung\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" /Game/Maps/WiesbadenCity_Alkis3 -game -windowed -ResX=1280 -ResY=720 -WbAutoDrive=250 -WbAutoDriveStart=260 -WbShot=300 -WbQuitAfter=325 -ExecCmds="stat unit" -stdout -unattended -nop4 > "%LOG%" 2>&1
exit /b %ERRORLEVEL%
