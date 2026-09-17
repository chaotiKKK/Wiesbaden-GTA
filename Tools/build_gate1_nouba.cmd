@echo off
REM Diagnose-Lauf: UBA (Unreal Build Accelerator) aus, damit kein
REM zwischengespeichertes PCH/Objekt aus dem "Program Files"-Zeitalter
REM wieder eingespielt wird. Log getrennt von build_gate1.cmd.
"C:\freebuff\WiesbadenReal_Sicherung\UE_5.8\Engine\Build\BatchFiles\Build.bat" WiesbadenRealEditor Win64 Development -project="C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject" -NoUBA -waitmutex > "C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Saved\Logs\wb_build_nouba.log" 2>&1
echo EXITCODE %ERRORLEVEL% >> "C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Saved\Logs\wb_build_nouba.log"
