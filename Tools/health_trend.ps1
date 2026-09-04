# Health-Trend WiesbadenReal - wertet die JSONL-Historie der WbHealth-Gate-Laeufe
# aus und faengt SCHLEICHENDE Regressionen: Kennzahlen, die ueber Laeufe
# einbrechen, obwohl jeder EINZELNE Lauf noch "healthy" sein mag (z. B. immer
# weniger gezeichnete Fussgaenger/Fahrzeuge - ein langsames Absterben, das das
# binaere healthy-Flag nicht sieht, solange der Wert > 0 bleibt).
#
# Vergleicht den JUENGSTEN Lauf gegen den Median der vorherigen (bis zu $Window)
# GELADENEN Laeufe. Faellt eine "gezeichnet"-Kennzahl um mehr als $DropPct unter
# diesen Median (und der Median ist aussagekraeftig), gilt das als Regression.
#
#   Exit 0 = kein Einbruch (oder zu wenig Historie fuer ein Urteil)
#   Exit 1 = Einbruch erkannt (Details im Log)
#
# Aufruf:  Tools\health_trend.ps1   (wird auch von health_check.ps1 mitgerufen)

param(
    [string]$Root = "C:\freebuff\WiesbadenReal_Sicherung",
    [string]$HistoryPath = "",
    # Wie viele vorherige Laeufe bilden die Vergleichsbasis (Median).
    [int]   $Window = 10,
    # Ab welchem relativen Einbruch (0.30 = 30 %) unter den Basis-Median gewarnt wird.
    [double]$DropPct = 0.30,
    # Nur werten, wenn der Basis-Median mindestens so gross ist (sonst ist ein
    # "Einbruch" statistisches Rauschen bei kleinen Zahlen).
    [int]   $MinBaseline = 5
)

$ErrorActionPreference = "Stop"
if (-not $HistoryPath) {
    $HistoryPath = Join-Path $Root "WiesbadenReal\Saved\health_history.jsonl"
}

Write-Host "=== Health-Trend (schleichende Regressionen) ==="

if (-not (Test-Path $HistoryPath)) {
    Write-Host "  keine Historie ($HistoryPath) - nichts zu werten."
    exit 0
}

# JSONL einlesen: eine JSON-Zeile je Lauf. Kaputte/leere Zeilen ueberspringen.
$runs = @()
foreach ($line in Get-Content $HistoryPath) {
    $t = $line.Trim()
    if (-not $t) { continue }
    try { $runs += ($t | ConvertFrom-Json) } catch { }
}

# Nur GELADENE Laeufe vergleichen (gleiches mit gleichem) - ein Lauf, der nie
# fertig streamte, haette kuenstlich kleine Zahlen und verzerrte den Median.
$loaded = @($runs | Where-Object { $_.streamingComplete -eq $true })
if ($loaded.Count -lt 2) {
    Write-Host ("  nur {0} geladene(r) Lauf/Laeufe in der Historie - zu wenig fuer einen Trend." -f $loaded.Count)
    exit 0
}

$latest = $loaded[-1]
$prior = @($loaded[0..($loaded.Count - 2)])
if ($prior.Count -gt $Window) { $prior = $prior[($prior.Count - $Window)..($prior.Count - 1)] }

function Get-Median([double[]]$vals) {
    $s = @($vals | Sort-Object)
    $n = $s.Count
    if ($n -eq 0) { return 0.0 }
    if ($n % 2 -eq 1) { return [double]$s[[int](($n - 1) / 2)] }
    return ([double]$s[$n / 2 - 1] + [double]$s[$n / 2]) / 2.0
}

# "Gezeichnet"-Kennzahlen, deren EINBRUCH eine schleichende Regression ist.
$metrics = @(
    @{ Key = "pedestriansDrawn";      Label = "gezeichnete Fussgaenger" },
    @{ Key = "trafficVehiclesVisible"; Label = "gezeichnete Fahrzeuge" }
)

$regression = $false
foreach ($m in $metrics) {
    $key = $m.Key
    $latestVal = [double]$latest.$key
    $baseVals = @($prior | ForEach-Object { [double]$_.$key })
    $median = Get-Median $baseVals

    if ($median -lt $MinBaseline) {
        Write-Host ("  {0}: aktuell {1}, Basis-Median {2:N1} (<{3}) - zu klein fuer ein Urteil." -f `
            $m.Label, $latestVal, $median, $MinBaseline)
        continue
    }

    $threshold = $median * (1.0 - $DropPct)
    if ($latestVal -lt $threshold) {
        $dropPctActual = (1.0 - ($latestVal / $median)) * 100.0
        Write-Host ("  [EINBRUCH] {0}: aktuell {1}, Basis-Median {2:N1} ({3} Laeufe) -> -{4:N0}% (Schwelle -{5:N0}%)." -f `
            $m.Label, $latestVal, $median, $prior.Count, $dropPctActual, ($DropPct * 100))
        $regression = $true
    } else {
        Write-Host ("  {0}: aktuell {1}, Basis-Median {2:N1} - stabil." -f $m.Label, $latestVal, $median)
    }
}

# Perf-Last als Kontext (Anstieg ist eine andere Regressionsart; das harte
# Verdikt sitzt im Report - hier nur der Trend zur Sichtbarkeit).
$instMedian = Get-Median @($prior | ForEach-Object { [double]$_.perfInstances })
Write-Host ("  perf-Instanzen: aktuell {0}, Basis-Median {1:N0} (Kontext)." -f [double]$latest.perfInstances, $instMedian)

Write-Host ""
if ($regression) {
    Write-Host "TREND: schleichende Regression erkannt (siehe [EINBRUCH] oben)."
    exit 1
}
Write-Host "TREND: kein Einbruch - Kennzahlen stabil."
exit 0
