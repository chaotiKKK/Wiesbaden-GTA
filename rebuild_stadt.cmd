@echo off
REM Kompletter Stadtneubau in eine NEUE Karte (WiesbadenCity_Alkis4).
REM
REM Die bisherige Karte bleibt unangetastet, bis der Neubau beurteilt ist.
REM VOLLER Editor: der Kommandlet-Weg ist an 42 GiB virtuellem Speicher
REM gescheitert.
REM
REM Ergebnis: Content/Maps/WiesbadenCity_Alkis4.umap + hoehen_report.json
set PROJ=C:\Users\ssonn\aivideo\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\Users\ssonn\aivideo\WiesbadenReal\rebuild_stadt.log
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" -ExecCmds="py C:\Users\ssonn\aivideo\WiesbadenReal\Tools\rebuild_city.py" -unattended -nosplash -nop4 > "%LOG%" 2>&1
exit /b %ERRORLEVEL%
