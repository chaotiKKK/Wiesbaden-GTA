@echo off
REM Durchfall-Test: Laedt der World-Partition-Stream die Strassenzellen schnell
REM genug, damit ein Wagen bei Tempo nicht durch eine noch ungeladene Zelle
REM faellt?
REM
REM Ungebackene Karte Alkis3 -> erzwingt den Laufzeit-Stadtbuild (~3 min). Erst
REM danach (WbAutoDriveStart=260) rollt der Pawn gleichmaessig mit 140 km/h nach
REM Osten; die Streaming-Quelle folgt ihm, es entsteht dieselbe Nachladelast wie
REM beim Fahren. Der Durchfall-Waechter im CitySubsystem traced pro Bild senkrecht
REM nach unten: fehlt ueber die ganze Spalte geladene Kollision, ist die Zelle
REM noch nicht gestreamt (ein Wagen fiele dort ins Leere). Ergebnis:
REM   Saved\Diagnose\Durchfall.txt
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\durchfall_test.log
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" /Game/Maps/WiesbadenCity_Alkis3 -game -windowed -ResX=1280 -ResY=720 -WbAutoDrive=140 -WbAutoDriveStart=260 -WbQuitAfter=355 -stdout -unattended -nop4 > "%LOG%" 2>&1
exit /b %ERRORLEVEL%
