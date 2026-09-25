@echo off
call "%~dp0engine_run_lock.cmd" -Modus Start -Name run_material_flags_proof
if errorlevel 1 exit /b 1
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
REM  Karte NICHT fest verdrahten - sie kommt aus
REM  Config\DefaultEngine.ini (siehe Tools\karte.cmd).
call "%~dp0karte.cmd"
if "%MAP%"=="" set MAP=%WB_MAP%
set QUIT=%~2
if "%QUIT%"=="" set QUIT=120
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" /Game/Maps/%MAP% -game -WbBusLog -WbQuitAfter=%QUIT% -windowed -ResX=1280 -ResY=720 -stdout -unattended -nop4 > "%LOG%" 2>&1
exit /b %ERRORLEVEL%
