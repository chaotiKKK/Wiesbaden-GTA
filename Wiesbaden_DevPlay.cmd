@echo off
REM ===========================================================================
REM  Wiesbaden - DEV-PLAY (Probespielen des aktuellen Stands)
REM ===========================================================================
REM  Startet die gebackene Stadt WiesbadenCity_Alkis3 direkt im Spielmodus mit
REM  dem aktuell gebauten Modul - also mit allem bisher erreichten Inhalt:
REM    - weiches Einblenden von Fahrbahn UND Gebaeuden am Streaming-Rand
REM    - Geschwindigkeits-Vorausladung (Puffer haelt bei Tempo)
REM    - Nerobergbahn-Ensemble + Nerotalbahn (zur Laufzeit auf der Trasse)
REM    - Beleuchtung/Sonnenstand, Verkehr, Fussgaenger
REM
REM  KEIN Packaging: ein gekochtes Paket dauert Stunden und veraltet sofort;
REM  dieser Direktstart spielt immer den AKTUELLEN Code-/Material-Stand.
REM
REM  Das Fenster braucht FOKUS (Unreal drosselt Fenster ohne Fokus auf 20 FPS).
REM  Steuerung einblenden: F1.
REM ===========================================================================
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set EXE=C:\freebuff\WiesbadenReal_Sicherung\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe
start "" "%EXE%" "%PROJ%" /Game/Maps/WiesbadenCity_Alkis3 -game -windowed -ResX=1600 -ResY=900 -nop4
