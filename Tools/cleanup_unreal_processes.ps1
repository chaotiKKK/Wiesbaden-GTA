[CmdletBinding()]
param(
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

function Get-EngineReste {
    @(
        Get-Process -Name "UnrealEditor*" -ErrorAction SilentlyContinue
        Get-Process -Name "zenserver" -ErrorAction SilentlyContinue
    ) | Where-Object { $_ } | Sort-Object Id -Unique
}

function Stop-EngineReste($Prozesse) {
    foreach ($prozess in @($Prozesse)) {
        Stop-Process -Id $prozess.Id -Force -ErrorAction SilentlyContinue
    }
}

$prozesse = @(Get-EngineReste)
if ($prozesse.Count -gt 0) {
    Write-Host "Prozessbereinigung: UnrealEditor/zenserver-Reste werden beendet."
    foreach ($prozess in $prozesse) {
        Write-Host ("  {0} (PID {1})" -f $prozess.ProcessName, $prozess.Id)
    }
    if (-not $DryRun) { Stop-EngineReste $prozesse }
} else {
    Write-Host "Prozessbereinigung: keine UnrealEditor/zenserver-Reste."
}

if ($DryRun) {
    return
}

# ZenServer behaelt nach einem harten Editorabbruch sonst Port 8558. Die
# Mindestpause ist Teil des Starts: ein frischer Editor darf nicht auf den
# alten Limbo-Server warten.
Start-Sleep -Seconds 3

# Ein UnrealEditor-Cmd aus dem vorigen Gate braucht nach Stop-Process manchmal
# laenger als 3 s, bis der Prozess wirklich aus der Prozessliste verschwindet.
# Genau das war der erste rote Gate-Lauf: der Rest wurde korrekt gekillt, aber
# zu frueh als Fehler gewertet. Ein zweiter Kill mit Wartefrist behebt den
# Shutdown-Race, ohne die 3-Sekunden-Regel aufzugeben.
$rest = @(Get-EngineReste)
if ($rest.Count -gt 0) {
    Write-Host ("Prozessbereinigung: {0} Rest(e) nach 3 s - zweiter Versuch, Wartefrist 10 s." -f $rest.Count)
    Stop-EngineReste $rest
    $frist = (Get-Date).AddSeconds(10)
    while ($rest.Count -gt 0 -and (Get-Date) -lt $frist) {
        Start-Sleep -Seconds 1
        $rest = @(Get-EngineReste)
    }
}

if ($rest.Count -gt 0) {
    $namen = ($rest | ForEach-Object { "$($_.ProcessName)/$($_.Id)" }) -join ", "
    throw "Prozessbereinigung unvollstaendig: $namen"
}
