@echo off
REM Wiesbaden spielen - AKTUELLER Stand, ohne Paketierung.
REM
REM Startet die Stadt WiesbadenCity_Alkis3 (Neubau vom 31.08. mit
REM freigehaltenen Fahrbahnen) direkt im Spielmodus.
REM
REM Warum nicht das Paket unter Saved\Package: das ist vom 30.08. und kennt
REM weder die Kettensaege, das Fuss-HUD noch die neue Stadt. Ein neues Paket
REM zu kochen dauert Stunden - und liefe es waehrend des Spielens, wuerde es
REM die Bildrate verfaelschen, die hier gemessen werden soll.
REM
REM Das Fenster braucht FOKUS: Unreal drosselt Fenster ohne Fokus auf 20
REM Bilder je Sekunde. Nicht wegklicken, sonst misst das Protokoll die
REM Drossel statt des Spiels.
REM
REM Alle 15 Sekunden schreibt das Spiel Bildzeit, Aussetzer und die
REM Aufteilung auf Spiel- und Renderer-Strang nach
REM   Saved\Logs\WiesbadenReal.log
REM
REM Steuerung: F1 blendet die Tastenbelegung ein.
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
start "" "C:\freebuff\WiesbadenReal_Sicherung\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" /Game/Maps/WiesbadenCity_Alkis3 -game -windowed -ResX=1600 -ResY=900 -nop4
