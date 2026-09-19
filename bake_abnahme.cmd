@echo off
REM ===========================================================================
REM  Bake-Abnahme - eine frisch gebackene Karte gegen ihre Vorgaengerin
REM ===========================================================================
REM  Aufruf:
REM      bake_abnahme.cmd <neue Karte> [alte Karte]
REM
REM  Ohne zweites Argument ist die Vorgaengerin die Default-Karte aus
REM  Config\DefaultEngine.ini.
REM
REM  Rueckgabe: 0 = angenommen, 1 = ABGELEHNT, 2 = Lauf gescheitert.
REM
REM  Dauer: rund 12 Minuten. Die Abnahme faehrt beide Karten mehrfach -
REM  Aufwaermrunden, bis die Ladezeit steht, dann einen Messlauf. Der ERSTE
REM  Lauf einer nie gespielten Karte misst den Cache, nicht die Karte
REM  (gemessen: 138 s, dann 84 s, dann 21 s).
REM
REM  Nur die Regeln pruefen, ohne das Spiel zu starten (Sekunden):
REM      python Tools\bake_abnahme.py --selbsttest
REM ===========================================================================
if "%~1"=="" (
    echo Aufruf: bake_abnahme.cmd ^<neue Karte^> [alte Karte]
    exit /b 2
)

if "%~2"=="" (
    python "%~dp0Tools\bake_abnahme.py" --neu %1
) else (
    python "%~dp0Tools\bake_abnahme.py" --neu %1 --alt %2
)
exit /b %ERRORLEVEL%
