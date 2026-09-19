@echo off
REM ===========================================================================
REM  Die aktuelle Stadt-Karte - EINE Quelle fuer alle Skripte
REM ===========================================================================
REM  Gelesen wird GameDefaultMap aus Config\DefaultEngine.ini. Das ist die
REM  Karte, die das Spiel ohne Argument startet; alles andere waere eine
REM  zweite Wahrheit.
REM
REM  WARUM: Die Foto-, Mess- und Diagnose-Skripte trugen ihre Karte fest im
REM  Text - nach drei Bakes zeigten sie auf Alkis3, Alkis4 und Alkis15,
REM  waehrend die Stadt laengst Alkis17 war. Ein Messlauf auf einer toten
REM  Karte sieht aus wie ein Messlauf; er misst nur etwas anderes.
REM
REM  Aufruf aus einem Skript (Wurzel oder Tools\):
REM      call "%~dp0Tools\karte.cmd"        bzw.  call "%~dp0karte.cmd"
REM  Danach stehen bereit:
REM      %WB_MAP%        WiesbadenCity_Alkis17
REM      %WB_MAP_PFAD%   /Game/Maps/WiesbadenCity_Alkis17
REM ===========================================================================
setlocal
set "WB_INI=%~dp0..\Config\DefaultEngine.ini"
if not exist "%WB_INI%" (
    echo [karte.cmd] FEHLER: %WB_INI% nicht gefunden.
    endlocal & exit /b 1
)

set "WB_ROH="
for /f "tokens=2 delims==" %%A in ('findstr /b /c:"GameDefaultMap=" "%WB_INI%"') do set "WB_ROH=%%A"
if "%WB_ROH%"=="" (
    echo [karte.cmd] FEHLER: GameDefaultMap steht nicht in %WB_INI%.
    endlocal & exit /b 1
)

REM  Aus "/Game/Maps/WiesbadenCity_Alkis17.WiesbadenCity_Alkis17" den kurzen
REM  Namen ziehen: alles bis einschliesslich /Maps/ abschneiden, dann am
REM  ersten Punkt trennen.
set "WB_KURZ=%WB_ROH:*/Maps/=%"
for /f "tokens=1 delims=." %%B in ("%WB_KURZ%") do set "WB_KURZ=%%B"

endlocal & set "WB_MAP=%WB_KURZ%" & set "WB_MAP_PFAD=/Game/Maps/%WB_KURZ%"
exit /b 0
