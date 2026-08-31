@echo off
REM Kompletter Stadtneubau in eine NEUE Karte (WiesbadenCity_Alkis4).
REM
REM Die bisherige Karte bleibt unangetastet, bis der Neubau beurteilt ist.
REM VOLLER Editor: der Kommandlet-Weg ist an 42 GiB virtuellem Speicher
REM gescheitert.
REM
REM Ergebnis: Content/Maps/WiesbadenCity_Alkis4.umap + hoehen_report.json
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\rebuild_stadt.log
"C:\freebuff\WiesbadenReal_Sicherung\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" -ExecCmds="py C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Tools\rebuild_city.py" -unattended -nosplash -nop4 > "%LOG%" 2>&1
exit /b %ERRORLEVEL%
