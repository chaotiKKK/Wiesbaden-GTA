@echo off
REM ===========================================================================
REM  Stadt-Inhalt holen (WiesbadenCity_Alkis16) - fuer frische Klone
REM ===========================================================================
REM  Content\__ExternalActors__\ und Content\Generated\ sind per .gitignore
REM  ausgeschlossen (2,5 GB je Stadt, groesste Einzeldatei 1,21 GB). Dieses
REM  Skript laedt das gepackte Release-Asset (0,95 GB), prueft sha256 und
REM  entpackt es in die Projektwurzel. Details: docs\reference\stadtinhalt-holen.md
REM
REM  Aufruf:  fetch_city_content.cmd          holen und entpacken
REM           fetch_city_content.cmd --check  nur pruefen, was schon da ist
REM ===========================================================================
python "%~dp0fetch_city_content.py" %*
exit /b %ERRORLEVEL%
