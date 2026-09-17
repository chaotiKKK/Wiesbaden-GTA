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
"c:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" WiesbadenRealEditor Win64 Development -project="C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject" -waitmutex > "C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Saved\Logs\wb_build_gate1.log" 2>&1
echo EXITCODE %ERRORLEVEL% >> "C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Saved\Logs\wb_build_gate1.log"
