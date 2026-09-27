@echo off
rem Kombi-Lauf Slice 3: Innenausbau-Nachweise in EINER Engine-Runde.
rem   - Figurprobe Treppe + Aufzugsprobe  (Regression gegen die Moeblierung)
rem   - Nacht-Posen Innenraum durch die Glasfassade (Beleuchtung/Tueren/Moebel)
rem Beides zusammen, weil der Engine-Run-Lock vom Push-Waechter- Daemon des
rem anderen Threads fast durchgehend belegt ist - EINE kurze Runde statt zwei.
rem -WbShotWhenReady + -WbShotNoQuit + -WbQuitAfter ist die dokumentierte
rem Kombi fuer "Serie schiessen, aber nicht beenden" (AGENTS.md Medien).
setlocal
call "%~dp0engine_run_lock.cmd" -Modus Start -Name run_innen_nacht
if errorlevel 1 exit /b 1

set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Saved\Logs\wb_nacht_innen.log
set FULLLOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Saved\Logs\wb_nacht_innen_full.log
set POSES=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Saved\Diagnose\poses_innen_nacht.txt
set ABLEITER=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Tools\sebbo_standort.py

set ZIEL=
for /f "usebackq delims=" %%Z in (`python "%ABLEITER%"`) do set ZIEL=%%Z
if "%ZIEL%"=="" (
  echo FEHLER: Turmkoordinate liess sich nicht aus SebboHqSite.h ableiten.
  call "%~dp0engine_run_lock.cmd" -Modus Freigeben
  exit /b 1
)
echo Kombi-Lauf Nacht-Innen: Ziel %ZIEL%, Abbruch nach 420 s

"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" /Game/Maps/WiesbadenCity_Alkis31 -game -WbGoto=%ZIEL% -WbZuFuss=20 -WbLiftProbe "-WbFigurProbe=Treppe" -WbShotWhenReady "-WbShotPoseFile=%POSES%" -WbShotNoQuit -WbPoseSettle=4 -WbShotScale=1 -WbTime=22 -WbQuitAfter=420 -windowed -ResX=1920 -ResY=1080 -stdout -abslog="%FULLLOG%" -unattended -nop4 > "%LOG%" 2>&1
echo EXITCODE %ERRORLEVEL% >> "%LOG%"
call "%~dp0engine_run_lock.cmd" -Modus Freigeben
endlocal
