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

$prozesse = @(Get-EngineReste)
if ($prozesse.Count -gt 0) {
    Write-Host "Prozessbereinigung: UnrealEditor/zenserver-Reste werden beendet."
    foreach ($prozess in $prozesse) {
        Write-Host ("  {0} (PID {1})" -f $prozess.ProcessName, $prozess.Id)
        if (-not $DryRun) {
            Stop-Process -Id $prozess.Id -Force -ErrorAction SilentlyContinue
        }
    }
} else {
    Write-Host "Prozessbereinigung: keine UnrealEditor/zenserver-Reste."
}

if ($DryRun) {
    return
}

# ZenServer behaelt nach einem harten Editorabbruch sonst Port 8558. Die
# Pause ist Teil des Starts: ein frischer Editor darf nicht auf den alten
# Limbo-Server warten.
Start-Sleep -Seconds 3

$rest = @(Get-EngineReste)
if ($rest.Count -gt 0) {
    $namen = ($rest | ForEach-Object { "$($_.ProcessName)/$($_.Id)" }) -join ", "
    throw "Prozessbereinigung unvollstaendig: $namen"
}
