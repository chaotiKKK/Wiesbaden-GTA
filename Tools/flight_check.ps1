# Flugpruefung WiesbadenReal: startet eine ECHTE Spielsitzung der Stadt
# (Default-Map aus Config\DefaultEngine.ini), setzt den Ka-52 per Dev-Befehl in die
# Luft und schreibt die In-Flight-Telemetrie ins Log:
#   Rotor-Drehzahl (Physik vs. tatsaechliche Nabendrehung),
#   Mastachse (Naben- und Blatt-Drehpunkte, Stangen-/Blattachsneigung),
#   Kamera (Modus, Abstand, Blicklage gegen die Rumpflage).
#
# Aufruf:  Tools\flight_check.cmd "WbHeli,WbHeliFly" a
#          Tools\flight_check.cmd "WbHeli,WbHeliFly" cockpit
#
# ACHTUNG, Dev-Befehle OHNE Argument aufrufen. Die Engine kann ueber
# -ExecCmds keins uebergeben; am 26.09.2026 an der Engine gemessen, drei
# Schreibweisen: "WbHeliFly 24" -> "Bad or missing property 'Sekunden'",
# "WbHeliFly=24" -> keine Fehlermeldung und KEINE Wirkung, "WbHeliFly
# Sekunden=24" -> dieselbe Fehlermeldung (CallFunctionByNameWithArguments
# sucht ein Objekt-Property, keinen Funktionsparameter). "WbHeliFly 24" sah
# richtig aus, tat aber nichts: die Flugtelemetrie lief nie an, und dieses
# Skript meldete danach trotzdem Erfolg. Die Dauer kommt jetzt aus der CVar
# wb.Sekunden (Vorgabe 24 s). "WbCam 2" ist aus demselben Grund wirkungslos.
#
# Engine-/Build-Paarung: gebaut wird mit der INSTALLIERTEN Engine
# (Tools\build_gate1.cmd), die Sitzung startet deshalb mit DEMSELBEN
# UnrealEditor.exe. Das Projekt-Intermediate traegt ein shared PCH aus genau
# diesem Baum - die freebuff-Kopie scheitert daran (siehe build_gate1.cmd).
#
# Fenster statt -nullrhi: Streaming und Kamera verhalten sich im echten Fenster
# wie im Spiel; der Rauchtest faehrt aus demselben Grund windowed.
param(
    [string]$Root       = "C:\freebuff\WiesbadenReal_Sicherung",
    [string]$ExecCmds   = "WbHeli,WbHeliFly",
    [string]$Name       = "a",
    # Auf GENUG Messpunkte warten, nicht auf das Demo-Ende: die Stadt kann beim
    # Fliegen streckenweise haengen (WP-Streaming-Hitches) - das ist normal.
    [int]   $MinSeconds = 8,
    [int]   $TimeoutSec = 420
)

$ErrorActionPreference = "Stop"

# Remove-Beleg / Test-Frisch: loeschen und danach NACHPRUEFEN, und beim Lesen
# messen, OB die Datei zu diesem Lauf gehoert (Tools\beleg.ps1).
. (Join-Path $PSScriptRoot "beleg.ps1")

# Nur Editoren DIESES Projektordners beenden. "Get-Process UnrealEditor* |
# Stop-Process" haette am Ende jeder Sitzung auch die eines fremden Laufs
# abgeschossen - derselbe Fehler wie im roten Push-Lauf vom 25.09.2026, nur
# ohne den Lock davor.
function Stop-ProjectEditors([string]$ProjectFile) {
    $want = $ProjectFile.Replace('/', '\')
    Get-CimInstance Win32_Process -Filter "Name LIKE 'UnrealEditor%'" -ErrorAction SilentlyContinue |
        Where-Object { $_.CommandLine -and $_.CommandLine.Replace('/', '\') -like "*$want*" } |
        ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
}

$Exe  = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
$Proj = Join-Path $Root "WiesbadenReal\WiesbadenReal.uproject"
$Log  = Join-Path $Root "WiesbadenReal\Saved\Logs\wb_flight_$Name.log"

if (-not (Test-Path $Exe))  { Write-Host "ABBRUCH: Editor nicht gefunden: $Exe"; exit 2 }
if (-not (Test-Path $Proj)) { Write-Host "ABBRUCH: Projekt nicht gefunden: $Proj"; exit 2 }

# Engine-Lock fuer die ganze Sitzung: ein paralleler Lauf darf diesen Editor
# nicht beenden (Tools\engine_run_lock.ps1, der Cleanup unten fasst den Lock
# danach als "eigen" und darf aufraeumen).
& "$PSScriptRoot\engine_run_lock.ps1" -Modus Nehmen -Name flight_check
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& "$PSScriptRoot\cleanup_unreal_processes.cmd"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
# Die alte Log muss WEG sein, nicht nur gewollt sein: liegt sie noch da (Read-
# only am Ordner-Eintrag oder offener Handle eines haengenden Editors), zaehlt
# die Schleife unten nach fuenf Sekunden deren Zeilen und meldet "N Mast-
# Messpunkte" von GESTERN - bei lebendem Zaehler bricht sie ab, beendet den
# gerade gestarteten Editor (Zeile "Stop-Process") und sagt Erfolg.
Remove-Beleg $Log
# Ab hier zaehlt nur, was dieser Lauf schreibt. Auch wenn das Loeschen
# geklappt haette: das ist die zweite Haelfte der Regel, und sie faengt den
# Fall, in dem die Log von einem parallel geschriebenen Pfad neu angelegt
# wurde.
$LaufStart = Get-Date

# Die Quotes um -ExecCmds MUESSEN die Engine bekommen, und Start-Process
# nimmt sie weg: es baut aus der Argumentliste eine Befehlszeile und
# entfernt die Quotes wieder. Am 26.09.2026 stand dadurch tatsaechlich
#   LogInit: Command Line: ... -ExecCmds=WbHeli,WbHeliFly 24
# im Log - ohne Quotes. Windows hat die Zeile am LEERZEICHEN in
# "-ExecCmds=WbHeli,WbHeliFly" und "24" getrennt, die Engine rief
# "WbHeliFly": Bad or missing property 'Sekunden' auf, und die Flugphase
# lief nie. Der Lauf wartete danach volle 7 Minuten auf Messpunkte, die nie
# kommen konnten, und meldete Erfolg.
#
# ProcessStartInfo.Arguments geht dagegen unveraendert an CreateProcess; die
# inneren Quotes bleiben erhalten. Deshalb dieser Weg statt Start-Process.
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $Exe
$psi.Arguments = '"' + $Proj + '" -game -windowed -resx=1280 -resy=720 -nosound' +
                 ' -ABSLOG="' + $Log + '" -ExecCmds="' + $ExecCmds + '"'
$psi.UseShellExecute = $false
$proc = [System.Diagnostics.Process]::Start($psi)
Write-Host ("Sitzung PID {0}: {1}" -f $proc.Id, $ExecCmds)
Write-Host ("Log: {0}" -f $Log)

$deadline = (Get-Date).AddSeconds($TimeoutSec)
$n = 0
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 5
    if ((Test-Path $Log) -and (Test-Frisch $Log $LaufStart)) {
        $n = @(Select-String -Path $Log -Pattern "WbDev Mast t=").Count
    }
    if ($n -ge $MinSeconds) { break }
    if (-not (Get-Process -Id $proc.Id -ErrorAction SilentlyContinue)) { break }
}
Write-Host ("{0} Mast-Messpunkte; beende Sitzung." -f $n)

# REIHENFOLGE WICHTIG: erst die Sitzung beenden, dann ueber das Ergebnis
# entscheiden. Ein "exit 1" vor dem Stop-Process laesst den Editor zurueck -
# am 26.09.2026 stand so eine 7-GB-Sitzung zehn Minuten weiter, ohne dass
# jemand sie beendet haette. Ein Messlauf, der scheitert, muss trotzdem
# aufraeumen.
Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
Stop-ProjectEditors $Proj
Start-Sleep -Seconds 1

# Kurzbefund auf der Konsole (volle Zeilen stehen im Log). Auch die Dev-Echos,
# damit ein fehlgeschlagenes WbHeli/WbHeliFly sofort sichtbar ist.
if (Test-Path $Log) {
    $marks = @("WbDev: WbHeli ", "WbDev: WbHeliFly ", "Helikopter abgesetzt")
    foreach ($m in $marks) {
        $hit = Select-String -Path $Log -Pattern $m | Select-Object -First 1
        if ($hit) { Write-Host ("  " + $hit.Line.Trim()) }
    }
    $last = Select-String -Path $Log -Pattern "WbDev (Mast|Kamera) t=" | Select-Object -Last 2
    foreach ($l in $last) { Write-Host ("  " + $l.Line.Trim()) }
}

# Ohne Messpunkte ist die Pruefung ergebnislos, nicht erfolgreich. Vorher
# stand hier bedingungslos "exit 0": ein Lauf, bei dem der Hubschrauber gar
# nicht flog, sah auf der Konsole aus wie ein bestandener Lauf. Ein
# Messwerkzeug, das nichts misst und Erfolg meldet, ist das Schlimmste, was
# ein Werkzeug tun kann.
$Frisch = Test-Frisch $Log $LaufStart
$Gemessen = $false
if ($Frisch) {
    $Gemessen = @(Select-String -Path $Log -Pattern "WbDev Mast t=").Count -ge $MinSeconds
}
if (-not $Gemessen) {
    if (-not $Frisch) {
        Write-Host "FEHLER: die Log gehoert nicht zu diesem Lauf - es wurde nichts gemessen."
        Write-Host ("       Log: {0}" -f $Log)
        Write-BelegHinweis $Log
    } else {
        Write-Host ("FEHLER: nur {0} von mindestens {1} Mast-Messpunkten im Log." -f $n, $MinSeconds)
        Write-Host ("       Log: {0}" -f $Log)
    }
    $verdaechtig = Select-String -Path $Log -Pattern "Bad or missing property" -ErrorAction SilentlyContinue |
                   Select-Object -First 3
    if ($verdaechtig) {
        Write-Host "       Die Engine hat die Dev-Befehle abgewiesen - die Messung kam nie zustande:"
        foreach ($v in $verdaechtig) { Write-Host ("         " + $v.Line.Trim()) }
    }
    exit 1
}
exit 0
