@echo off
REM ===========================================================================
REM  Die Engine dieses Projekts - EINE Quelle fuer alle Skripte
REM ===========================================================================
REM  WARUM: Auf diesem Rechner liegen ZWEI Engines 5.8 nebeneinander:
REM
REM      C:\Program Files\Epic Games\UE_5.8          5.8.2, Build.bat 09.09.2026
REM      C:\freebuff\WiesbadenReal_Sicherung\UE_5.8  5.8.1, Build.bat 11.08.2026
REM
REM  Beide heissen "UE_5.8", beide existieren, beide bauen - aber sie tragen
REM  verschiedene PATCH-Staende. Das Projekt-Intermediate haelt den Shared-PCH
REM  genau einer davon. Mischt man sie, stirbt der Build in einem ENGINE-Header
REM  (GenericPlatform.h: C2953 "SelectIntPointerType" bereits definiert) - das
REM  sieht nach kaputtem Engine-Quelltext aus und ist keiner.
REM
REM  Genau das ist am 21.09.2026 passiert: die Release-Pipeline leitete ihren
REM  Engine-Pfad aus dem Projektordner ab und erwischte die Kopie, waehrend
REM  Gate-1-Skript und Starter die installierte benutzten. Eine
REM  Test-Path-Pruefung faellt darauf NICHT herein - die alte Kopie ist ja da.
REM  Ein Pfad, der existiert und trotzdem falsch ist, faellt nur dem Vergleich
REM  auf. Darum steht er hier einmal, und Tools\pruefe_engine.py haelt alle
REM  anderen dagegen.
REM
REM  Aufruf aus einem Skript (Wurzel oder Tools\):
REM      call "%~dp0Tools\engine.cmd"    bzw.  call "%~dp0engine.cmd"
REM  Danach stehen bereit:
REM      %WB_ENGINE%      C:\Program Files\Epic Games\UE_5.8
REM      %WB_BUILD_BAT%   ...\Engine\Build\BatchFiles\Build.bat
REM      %WB_EDITOR%      ...\Engine\Binaries\Win64\UnrealEditor.exe
REM      %WB_EDITOR_CMD%  ...\Engine\Binaries\Win64\UnrealEditor-Cmd.exe
REM      %WB_RUNUAT%      ...\Engine\Build\BatchFiles\RunUAT.bat
REM
REM  WB_ENGINE als Umgebungsvariable behaelt Vorrang - wer bewusst gegen eine
REM  andere Engine baut, sagt es ausdruecklich und bekommt sie.
REM ===========================================================================
setlocal

set "WB_KANON=C:\Program Files\Epic Games\UE_5.8"
if defined WB_ENGINE (set "WB_GEWAEHLT=%WB_ENGINE%") else (set "WB_GEWAEHLT=%WB_KANON%")

if not exist "%WB_GEWAEHLT%\Engine\Build\BatchFiles\Build.bat" (
    echo [engine.cmd] FEHLER: keine Engine unter "%WB_GEWAEHLT%".
    endlocal & exit /b 1
)
if not exist "%WB_GEWAEHLT%\Engine\Build\Build.version" (
    echo [engine.cmd] FEHLER: "%WB_GEWAEHLT%" traegt keine Build.version.
    endlocal & exit /b 1
)

endlocal & (
    set "WB_ENGINE=%WB_GEWAEHLT%"
    set "WB_BUILD_BAT=%WB_GEWAEHLT%\Engine\Build\BatchFiles\Build.bat"
    set "WB_EDITOR=%WB_GEWAEHLT%\Engine\Binaries\Win64\UnrealEditor.exe"
    set "WB_EDITOR_CMD=%WB_GEWAEHLT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
    set "WB_RUNUAT=%WB_GEWAEHLT%\Engine\Build\BatchFiles\RunUAT.bat"
)
exit /b 0
