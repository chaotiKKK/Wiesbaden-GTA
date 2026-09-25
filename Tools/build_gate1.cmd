@echo off
REM Gate 1: Kompilieren des Editor-Targets.
REM
REM WICHTIG (2026-09-17): Diese Maschine hat ZWEI UE-5.8-Baeume:
REM   C:\Program Files\Epic Games\UE_5.8   <- installierte Engine (07.09.2026)
REM   C:\freebuff\WiesbadenReal_Sicherung\UE_5.8   <- aeltere Kopie (11.08.2026)
REM Beide enthalten bytgleiche Header, aber das SHARED PCH liegt im
REM PROJEKT-Intermediate und traegt die Headerpfade der Engine, mit der es
REM erzeugt wurde. Baut man mit dem anderen Baum, parst MSVC dieselben Header
REM ein zweites Mal und bricht mit
REM     error C2953 "SelectIntPointerType" ... bereits definiert
REM     error C2011 "FGenericPlatformTypes": "struct" Typneudefinition
REM     fatal error C1189: #error: PLATFORM_32BITS should not be defined
REM ab - die "Deklaration"-Notes nennen dann den fremden Engine-Pfad.
REM Deshalb: IMMER die installierte Engine verwenden (wie alle anderen
REM .cmd-Dateien des Projekts). Bei Baumwechsel vorher die PCH-Dateien unter
REM WiesbadenReal\Intermediate\Build\...\*.pch loeschen.
REM
REM Projekt = Ordner ueber Tools\ (nicht fest verdrahtet): so baut das Gate im
REM sauberen Push-Worktree (Tools\gate_worktree.py) SEINEN Code, nicht den des
REM Hauptordners.
for %%I in ("%~dp0..") do set "WBPROJ=%%~fI"
if not exist "%WBPROJ%\Saved\Logs" mkdir "%WBPROJ%\Saved\Logs"
call "c:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" WiesbadenRealEditor Win64 Development -project="%WBPROJ%\WiesbadenReal.uproject" -waitmutex > "%WBPROJ%\Saved\Logs\wb_build_gate1.log" 2>&1
set "WBEXIT=%ERRORLEVEL%"
echo EXITCODE %WBEXIT% >> "%WBPROJ%\Saved\Logs\wb_build_gate1.log"
exit /b %WBEXIT%
