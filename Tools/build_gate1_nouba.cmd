@echo off
REM Diagnose-Lauf: UBA (Unreal Build Accelerator) aus, damit kein
REM zwischengespeichertes PCH/Objekt wieder eingespielt wird. Log getrennt
REM von build_gate1.cmd.
REM
REM ENGINE kommt aus Tools\engine.cmd und steht hier NICHT mehr im Text.
REM Die frueher hier eingetragene Kopie unter C:\freebuff\...\UE_5.8 war
REM einmal richtig - der alte Kommentar warnte sogar vor dem
REM "Program-Files-Zeitalter". Inzwischen ist es umgekehrt: installiert ist
REM 5.8.2, die Kopie 5.8.1. Ein fest eingetragener Pfad kann solche
REM Umkehrungen nicht mitmachen; ein Aufruf schon.
call "%~dp0engine.cmd" || exit /b 1
set "WB_PROJ=%~dp0..\WiesbadenReal.uproject"
set "WB_LOG=%~dp0..\Saved\Logs\wb_build_nouba.log"
"%WB_BUILD_BAT%" WiesbadenRealEditor Win64 Development -project="%WB_PROJ%" -NoUBA -waitmutex > "%WB_LOG%" 2>&1
echo EXITCODE %ERRORLEVEL% >> "%WB_LOG%"
