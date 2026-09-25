@echo off
call "%~dp0engine_run_lock.cmd" -Modus Start -Name dump_alkis27_streets
if errorlevel 1 exit /b 1
REM Strassennetz von Alkis27 auslesen (Saved/Diagnose/Strassenalkis27.csv).
REM Zweck: Der Spieltest braucht eine FAHRBAHNMITTE + Richtung, um den
REM Wagen bewusst auf den Gehweg zu stellen - ohne das faehrt er in der
REM Strassenmitte und kommt an den Passanten auf dem 2,5-m-Geheweg nie
REM nahe genug heran (Sondenradius 110 cm, Front 180 cm). Aus der CSV
REM werden Segment-Mittelpunkt und Kursrichtung gelesen, der Gehweg liegt
REM um Fahrbahn-Halbbreite + Gehweg-Halbbreite daneben.
REM
REM Der Lauf endet selbst (WbQuitAfter) - Log wird vollstaendig geschrieben.
setlocal
set UE=C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set LOG=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Saved\Logs\wb_streetdump_alkis27.log
"%UE%" "%PROJ%" /Game/Maps/WiesbadenCity_Alkis31 -game -WbDumpStreets=C:/freebuff/WiesbadenReal_Sicherung/WiesbadenReal/Saved/Diagnose/Strassenalkis27.csv -WbQuitAfter=22 -windowed -ResX=1280 -ResY=720 -unattended -nop4 -abslog="%LOG%"
endlocal
