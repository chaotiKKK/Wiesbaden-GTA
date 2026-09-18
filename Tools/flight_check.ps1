# Flugpruefung WiesbadenReal: startet eine ECHTE Spielsitzung der Stadt
# (Default-Map WiesbadenCity_Alkis15), setzt den Ka-52 per Dev-Befehl in die
# Luft und schreibt die In-Flight-Telemetrie ins Log:
#   Rotor-Drehzahl (Physik vs. tatsaechliche Nabendrehung),
#   Mastachse (Naben- und Blatt-Drehpunkte, Stangen-/Blattachsneigung),
#   Kamera (Modus, Abstand, Blicklage gegen die Rumpflage).
#
# Aufruf:  Tools\flight_check.cmd "WbHeli,WbHeliFly 24" a
#          Tools\flight_check.cmd "WbHeli,WbCam 2,WbHeliFly 12" cockpit
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
    [string]$ExecCmds   = "WbHeli,WbHeliFly 24",
    [string]$Name       = "a",
    # Auf GENUG Messpunkte warten, nicht auf das Demo-Ende: die Stadt kann beim
    # Fliegen streckenweise haengen (WP-Streaming-Hitches) - das ist normal.
    [int]   $MinSeconds = 8,
    [int]   $TimeoutSec = 420
)

$ErrorActionPreference = "Stop"

$Exe  = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
$Proj = Join-Path $Root "WiesbadenReal\WiesbadenReal.uproject"
$Log  = Join-Path $Root "WiesbadenReal\Saved\Logs\wb_flight_$Name.log"

if (-not (Test-Path $Exe))  { Write-Host "ABBRUCH: Editor nicht gefunden: $Exe"; exit 2 }
if (-not (Test-Path $Proj)) { Write-Host "ABBRUCH: Projekt nicht gefunden: $Proj"; exit 2 }

Get-Process UnrealEditor* -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 3
Remove-Item $Log -ErrorAction SilentlyContinue

# Die Quotes um -ExecCmds MUSS dieses Skript setzen (fest verdrahtet): uebergeben
# an Start-Process gehen sie sonst verloren, der Engine-Befehl zerfaellt und es
# laeuft nur das erste Wort (in einem Fall blieb der Editor darum endlos stehen).
$sargs = @("`"$Proj`"", "-game", "-windowed", "-resx=1280", "-resy=720",
           "-nosound", "-ABSLOG=$Log", "-ExecCmds=`"$ExecCmds`"")

$proc = Start-Process -FilePath $Exe -ArgumentList $sargs -PassThru
Write-Host ("Sitzung PID {0}: {1}" -f $proc.Id, $ExecCmds)
Write-Host ("Log: {0}" -f $Log)

$deadline = (Get-Date).AddSeconds($TimeoutSec)
$n = 0
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 5
    if (Test-Path $Log) { $n = @(Select-String -Path $Log -Pattern "WbDev Mast t=").Count }
    if ($n -ge $MinSeconds) { break }
    if (-not (Get-Process -Id $proc.Id -ErrorAction SilentlyContinue)) { break }
}
Write-Host ("{0} Mast-Messpunkte; beende Sitzung." -f $n)

Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
Get-Process UnrealEditor* -ErrorAction SilentlyContinue | Stop-Process -Force
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
exit 0
