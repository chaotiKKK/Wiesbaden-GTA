@echo off
REM Stadt neu bauen, damit die Fahrbahn-Freihaltung greift.
REM
REM Die Baumplatzierung entsteht im PIPELINE-Lauf, nicht zur Laufzeit - ohne
REM Neubau stehen die Baeume weiter auf der Strasse.
REM
REM VOLLER EDITOR, nicht UnrealEditor-Cmd mit -nullrhi.
REM
REM Der erste Anlauf lief als Commandlet und starb nach 22 Minuten an
REM "Ran out of memory ... Die Auslagerungsdatei ist zu klein" bei 42 GiB
REM virtuell - waehrend des Kachelns, 19 Minuten NACH der Asset-Streuung.
REM Saemtliche erfolgreichen Bauten in Saved/BuildHistory/CityBuilds.csv sind
REM mit "Editor" verzeichnet; der Commandlet-Weg ist fuer diese Stadtgroesse
REM nachweislich nicht durchgekommen.
REM
REM Ziel ist eine NEUE Karte: die laufende Alkis2 belegt 12 GB in rund 2.000
REM Actor-Paketen, ein misslungener Neubau darueber waere nicht rueckgaengig
REM zu machen. Umgeschaltet wird erst nach dem Abgleich.
set PROJ=C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\WiesbadenReal.uproject
set WB_TARGET_MAP=/Game/Maps/WiesbadenCity_Alkis3
set WB_SOURCE_MAP=/Game/Maps/WiesbadenCity_Alkis3
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJ%" -ExecCmds="py C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Tools\build_city.py" -unattended -nosplash -nop4 > C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\rebuild_baumfrei.log 2>&1
exit /b %ERRORLEVEL%
