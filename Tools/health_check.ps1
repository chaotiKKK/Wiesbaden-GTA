# Health-Check WiesbadenReal - schneller CI-Smoke OHNE Fahr-/Flugprofile.
#
# Faehrt NUR die stationaere WbHealth-Gate-Sitzung: startet die Stadt im
# echten Fenster, feuert WbHealth im Gate-Modus (wartet auf den geladenen
# Zustand - Stadt da, Streaming fertig, Perf-Snapshot erhoben - und schreibt
# dann Saved/Logs/WbHealth.json), parst das JSON und kehrt mit Exit-Code zurueck:
#
#   0 = "healthy": true  (keine evidenz-gewichteten Warnungen)
#   1 = "healthy": false (Warnungen werden aufgelistet)
#   2 = Infrastruktur   (Editor/Projekt fehlt, keine/kaputte JSON, Sitzung tot)
#
# Bewusst OHNE Fahr-/Flug-/Material-/Perf-Grep aus dem vollen Rauchtest - das
# ist der schnelle Puls: laeuft die Stadt und meldet sie sich gesund? Der volle
# Rauchtest (Tools\smoke_test.ps1) bleibt fuer Fahrphysik + Regressionsschranken.
#
# Aufruf:  Tools\health_check.cmd   (oder powershell -File Tools\health_check.ps1)

param(
    [string]$Root = "C:\freebuff\WiesbadenReal_Sicherung",
    # Deckel, wie lange WbHealth im Gate-Modus auf den geladenen Zustand wartet.
    [int]   $MaxWaitSeconds = 30,
    # Harte Obergrenze fuer die ganze Sitzung (Laden + Gate), bevor abgebrochen wird.
    [int]   $TimeoutSec = 180
)

$ErrorActionPreference = "Stop"
$Exe    = Join-Path $Root "UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
$Proj   = Join-Path $Root "WiesbadenReal\WiesbadenReal.uproject"
$LogDir = Join-Path $Root "WiesbadenReal\Saved\Logs"
$Log    = Join-Path $LogDir "health_check.log"
$Json   = Join-Path $LogDir "WbHealth.json"

if (-not (Test-Path $Exe))  { Write-Host "ABBRUCH: Editor nicht gefunden: $Exe"; exit 2 }
if (-not (Test-Path $Proj)) { Write-Host "ABBRUCH: Projekt nicht gefunden: $Proj"; exit 2 }

Write-Host "=== Health-Check WiesbadenReal (nur WbHealth-Gate) ==="

# Laufende Editoren beenden, damit die Sitzung sauber startet.
Get-Process UnrealEditor* -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 2

# Zeitstempel VOR dem Start: nur eine danach geschriebene WbHealth.json zaehlt
# (die alte wird bewusst nicht geloescht, sondern per LastWriteTime abgegrenzt).
$before = Get-Date

# STATIONAER: kein Teleport/Flug - die Kamera bleibt am Spawn, damit das
# WP-Streaming einmal sauber einrastet. WbHealth im Gate-Modus.
$sargs = @("`"$Proj`"", "-game", "-windowed", "-resx=1280", "-resy=720",
    "-nosound", "-ABSLOG=$Log", "-ExecCmds=`"WbHealth $MaxWaitSeconds`"")
$proc = Start-Process -FilePath $Exe -ArgumentList $sargs -PassThru
Write-Host ("  Sitzung PID {0}: WbHealth {1}" -f $proc.Id, $MaxWaitSeconds)

# Auf die FRISCH geschriebene WbHealth.json warten (oder Timeout / Prozess-Ende).
$deadline = (Get-Date).AddSeconds($TimeoutSec)
$fresh = $false
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 4
    if ((Test-Path $Json) -and ((Get-Item $Json).LastWriteTime -gt $before)) { $fresh = $true; break }
    if (-not (Get-Process -Id $proc.Id -ErrorAction SilentlyContinue)) { break }
}

Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
Get-Process UnrealEditor* -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 1

if (-not $fresh) {
    Write-Host "DURCHGEFALLEN: keine frische WbHealth.json geschrieben (Gate nicht erreicht - Absturz/Timeout?)."
    exit 2
}

try {
    $h = Get-Content $Json -Raw | ConvertFrom-Json
} catch {
    Write-Host ("DURCHGEFALLEN: WbHealth.json nicht parsebar: {0}" -f $_.Exception.Message)
    exit 2
}

# Kompakte Kennzahlen fuer den CI-Log (aus dem Report, nicht aus Prosa gegreppt).
$p = $h.perf
Write-Host ("  streamingComplete={0}  ampeln={1}  verkehr={2}/{3}  fussgaenger={4}/{5}  kollision={6}" -f `
    $h.streamingComplete, $h.trafficLightCount, $h.trafficVehiclesVisible, $h.activeVehicles, `
    $h.pedestriansDrawn, $h.pedestriansSimulated, $h.buildingCollisionBodies)
if ($p) {
    Write-Host ("  perf: verdict={0}  komponenten={1}  instanzen={2}  spielMs={3}  abschnitte-ohne-material={4}" -f `
        $p.verdict, $p.primitiveComponents, $p.instances, $p.gameThreadMs, $p.meshSectionsWithoutMaterial)
}

Write-Host ""
if ($h.healthy) {
    Write-Host "GESUND: healthy = true (keine Warnungen)."
    exit 0
} else {
    Write-Host "UNGESUND: healthy = false. Warnungen:"
    foreach ($w in @($h.warnings)) { Write-Host ("  - {0}" -f $w) }
    exit 1
}
