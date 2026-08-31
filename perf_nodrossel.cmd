@echo off
REM Versuch, die Fensterdrossel abzuschalten.
REM
REM Unreal begrenzt Fenster OHNE FOKUS auf 20 Bilder/s. Genau diesen Deckel
REM haben alle bisherigen Diagnoselaeufe gemessen - die Grafikkarte lag in
REM JEDER Messung bei 49-51 ms, egal was in der Szene stand.
REM
REM t.MaxFPS 0 hebt eine Begrenzung auf; ob sie auch die Fokusdrossel
REM erwischt, muss die Zahl zeigen. Bleibt sie bei 50 ms, greift ein anderer
REM Mechanismus und headless ist endgueltig nicht messbar.
set PROJ=C:\Users\ssonn\aivideo\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\Users\ssonn\aivideo\WiesbadenReal\perf_nodrossel.log
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" /Game/Maps/WiesbadenCity_Alkis3 -game -windowed -ResX=1600 -ResY=900 -ExecCmds="t.MaxFPS 0, t.IdleWhenNotForeground 0, r.Streaming.FramesForFullUpdate 1" -WbQuitAfter=150 -stdout -unattended -nop4 > "%LOG%" 2>&1
exit /b %ERRORLEVEL%
