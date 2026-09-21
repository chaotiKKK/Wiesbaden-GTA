@echo off
REM ENGINE kommt aus Tools\engine.cmd - kein fester Pfad mehr im Text.
REM Frueher stand hier die Kopie unter C:\freebuff\...\UE_5.8 (5.8.1). Das
REM Modul ist gegen den PCH der installierten Engine (5.8.2) gebaut; mischt
REM man beide, stirbt der Lauf in einem Engine-Header.
cd /d "%~dp0.."
call "%~dp0engine.cmd" || exit /b 1
"%WB_EDITOR_CMD%" ^
  "%~dp0..\WiesbadenReal.uproject" ^
  -run=pythonscript -script="%~dp0check_ka52_mats.py" ^
  -stdout -unattended -nopause -nosplash > Tools\check_ka52_mats_log.txt 2>&1
echo DONE >> Tools\check_ka52_mats_log.txt
