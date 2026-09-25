# Faehrt health_check.ps1 mehrfach hintereinander und beweist damit die
# Stabilitaet des Ampel-Verdikts in Saved\health_history.jsonl.
#
# Warum ein Wrapper: health_check.ps1 killt am Ende seinen Editor (den ZenServer-
# OWNER). Der ZenServer geht dann in einen Limbo-Zustand, haelt aber Port 8558 -
# der naechste Editor kann seinen eigenen ZenServer nicht binden und haengt ewig
# im "Waiting for ZenServer to be ready". Deshalb VOR jedem Lauf: Editoren UND
# zenserver hart beenden und WARTEN, bis Port 8558 wirklich frei ist. Dann startet
# jeder Lauf mit frischem ZenServer sauber (~40 s statt Endlos-Hang).
param(
    [int]$Runs = 3,
    [int]$TimeoutSec = 300,
    [int]$MaxWaitSeconds = 45
)
$ErrorActionPreference = "Continue"
$hc = Join-Path $PSScriptRoot "health_check.ps1"

# Engine-Lock fuer die MEHRERE Laeufe (Tools\engine_run_lock.ps1). health_check
# nimmt ihn darunter ebenfalls - reentrant, weil der Besitzer dann ein Vorfahre
# ist. Waere er nicht gehalten, wuerde ein paralleler Gate-/Bake-Lauf den
# Health-Editor mitten in der Sitzung beenden.
& "$PSScriptRoot\engine_run_lock.ps1" -Modus Nehmen -Name health_multi
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

function Wait-PortFree([int]$Port, [int]$MaxSec = 60) {
    for ($w = 0; $w -lt ($MaxSec / 2); $w++) {
        Start-Sleep -Seconds 2
        try { $null = Get-NetTCPConnection -LocalPort $Port -ErrorAction Stop }
        catch { return $true }
    }
    return $false
}

for ($i = 1; $i -le $Runs; $i++) {
    Write-Host ("=== MULTI RUN {0} cleanup {1} ===" -f $i, (Get-Date -Format HH:mm:ss))
    & "$PSScriptRoot\cleanup_unreal_processes.cmd"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    $free = Wait-PortFree 8558 60
    Write-Host ("  Port 8558 frei: {0}" -f $free)

    Write-Host ("=== MULTI RUN {0} start {1} ===" -f $i, (Get-Date -Format HH:mm:ss))
    & $hc -TimeoutSec $TimeoutSec -MaxWaitSeconds $MaxWaitSeconds
    Write-Host ("=== MULTI RUN {0} exit={1} {2} ===" -f $i, $LASTEXITCODE, (Get-Date -Format HH:mm:ss))
}
Write-Host "MULTI FERTIG"
