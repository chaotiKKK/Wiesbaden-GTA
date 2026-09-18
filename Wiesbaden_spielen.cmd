@echo off
REM ===========================================================================
REM  Wiesbaden spielen - Probespielen des AKTUELLEN Stands (ohne Paketierung)
REM ===========================================================================
REM  Startet die gebackene Stadt direkt im Spielmodus mit dem aktuell gebauten
REM  Modul - also mit allem bisher erreichten Inhalt:
REM    - weiches Einblenden von Fahrbahn UND Gebaeuden am Streaming-Rand
REM    - Geschwindigkeits-Vorausladung (Boden-Puffer haelt bei Tempo)
REM    - Nerobergbahn-Ensemble + Nerotalbahn (zur Laufzeit auf der Trasse)
REM    - Beleuchtung/Sonnenstand, Verkehr, Fussgaenger, Fuss-HUD
REM
REM  KEIN Packaging: ein gekochtes Paket dauert Stunden und veraltet sofort;
REM  der Direktstart spielt immer den aktuellen Code-/Material-Stand. (Loest die
REM  frueheren zwei Launcher Wiesbaden_spielen.cmd + Wiesbaden_DevPlay.cmd ab.)
REM
REM  Karte optional als 1. Argument (Standard: WiesbadenCity_Alkis15 = die
REM  Projekt-Standardkarte aus Config\DefaultEngine.ini), z. B.:
REM    Wiesbaden_spielen.cmd WiesbadenCity_Alkis15
REM
REM  Das Fenster braucht FOKUS: Unreal drosselt Fenster ohne Fokus auf 20 FPS.
REM  Steuerung einblenden: F1.
REM  Protokoll (alle 15 s Bildzeit/Aussetzer/Straenge): Saved\Logs\WiesbadenReal.log
REM ===========================================================================
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
REM  INSTALLIERTE Engine, NICHT die freebuff-Kopie: Gate 1 (Tools\build_gate1.cmd)
REM  baut mit dieser, und das Projekt-Intermediate traegt deren shared PCH.
set EXE=C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe
set MAP=%~1
if "%MAP%"=="" set MAP=WiesbadenCity_Alkis15
start "" "%EXE%" "%PROJ%" /Game/Maps/%MAP% -game -windowed -ResX=1600 -ResY=900 -nop4
