@echo off
REM ===========================================================================
REM  play.cmd - Kurzstart mit Protokoll in play.log
REM ===========================================================================
REM  KEIN /Game-Argument mehr: die Karte kommt aus GameDefaultMap in
REM  Config\DefaultEngine.ini. Hier stand fest "/Game/Maps/WiesbadenCity_Alkis" -
REM  diese Karte gibt es seit Alkis2 nicht mehr. Der Start endete deshalb in
REM  einem englischen Engine-Dialog ("The map specified on the commandline ...
REM  could not be found") und wartete dort ewig auf einen Klick, waehrend die
REM  Ausgabe in play.log verschwand. Ein fester Kartenname veraltet mit jedem
REM  Bake; der Default tut das nicht.
REM
REM  Zum Probespielen ist Wiesbaden_spielen.cmd gedacht (Fenster, Hinweise,
REM  optionaler Kartenname als Argument).
REM ===========================================================================
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" -game -windowed -ResX=1600 -ResY=900 > C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\play.log 2>&1
