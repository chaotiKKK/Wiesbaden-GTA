@echo off
rem Beleglauf "Der Bus-Innenraum hat Texturen": kurzer kopfloser Lauf, der den
rem Bus-Actor seinen Innenraum bauen laesst. Im Log steht dann je Flaechenart
rem eine Zeile der Form
rem   Bus-Innenraum: <n> Kaesten, <m> Ecken in 5 Abschnitten; Auge X=.. Y=.. Z=..
rem   ok T_WbBusIntBoden(512x512) ok T_WbBusIntSitz(512x512) ... 
rem Ein "FEHLT" statt "ok" heisst: Material fehlt -> Tools\make_bus_interior_textures.py
rem und Tools\import_bus_interior.cmd laufen lassen.
rem
rem -WbBusLogWagon=601 schreibt zusaetzlich das 2-s-Protokoll von Wagen 601
rem   (inkl. Spurseite und Fahrbahn-Grundlage) - ein Lauf, alle Belege.
rem
rem Aufruf: Tools\run_bus_interior_proof.cmd [Karte] [Spielsekunden]
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\shot_interior.log
set MAP=%~1
if "%MAP%"=="" set MAP=WiesbadenCity_Alkis15
set QUIT=%~2
if "%QUIT%"=="" set QUIT=45
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" /Game/Maps/%MAP% -game -nullrhi -NoSound -WbBusLog -WbBusLogWagon=601 -WbQuitAfter=%QUIT% -unattended -nop4 -stdout > "%LOG%" 2>&1
exit /b %ERRORLEVEL%
