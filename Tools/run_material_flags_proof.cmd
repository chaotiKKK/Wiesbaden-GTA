@echo off
REM Gegenprobe zu Tools\fix_material_flags.cmd: ein FENSTER-Lauf (nur der
REM rendert Ka52 und Nerobergbahn; der kopflose Lauf sieht sie nicht) und danach
REM im Log zaehlen:
REM   grep -c "missing usage flag" Saved\Logs\WiesbadenReal.log   -> 0
REM Vor dem Fix standen dort 7 Zeilen (1 x Ka52/Nanite, 6 x Nb/ISM).
REM
REM Aufruf: Tools\run_material_flags_proof.cmd [Karte] [Sekunden]
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\materialflags_proof.log
set MAP=%~1
if "%MAP%"=="" set MAP=WiesbadenCity_Alkis15
set QUIT=%~2
if "%QUIT%"=="" set QUIT=120
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" /Game/Maps/%MAP% -game -WbBusLog -WbQuitAfter=%QUIT% -windowed -ResX=1280 -ResY=720 -stdout -unattended -nop4 > "%LOG%" 2>&1
exit /b %ERRORLEVEL%
