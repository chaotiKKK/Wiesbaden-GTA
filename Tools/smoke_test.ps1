# Rauchtest WiesbadenReal - startet die Stadt im echten Fenster, feuert die
# Dev-Befehle bzw. das Fahrprofil und wertet aus dem Log Bestanden/
# Durchgefallen.
#
# Zwei kurze Sitzungen, weil sich Fahrzeug und Heli die Besitzung teilen:
#   1) Fahrzeug: Fahrprofil (WbDrive - Vollgas + Lenk-Sweep ueber den Test-
#      Harness) weist Laengsdynamik UND Lenkung des Standard-Kaefers nach,
#      dazu Material-Bilanz UND Perf-Regression (Spiel-Strang-Zeit + Last-
#      Inventar) aus dem 8-s-Diagnoseblock (feuert 8 s nach dem Laden von selbst).
#   2) Helikopter: Teleport + Aufrichten (am Fahrzeug), dann Gier- und Flugprofil.
# Gewartet wird auf GENUG Log-Belege (nicht auf das Demo-Ende - die Stadt kann
# beim Umherfliegen streckenweise haengen), dann Auswertung gegen erwartete
# Wirkungen. Kein Screenshot, kein Fenster-Fokus -> rechnerunabhaengig.
#
# Aufruf:  Tools\smoke_test.cmd   (oder powershell -File Tools\smoke_test.ps1)
# Exit 0 = alle Pruefungen bestanden, sonst Exit 1.

param(
    [string]$Root = "C:\freebuff\WiesbadenReal_Sicherung",
    # Perf-Regression-Schranken (aus dem 8-s-Diagnoseblock am Boden). BEWUSST ueber
    # der aktuellen Grundlast, damit der Check heute besteht und erst bei einer
    # echten Verschlechterung durchfaellt - jetzt aber ENG an der Ist-Last, um den
    # WP-Streaming-Fix (hoehenadaptiver Radius, 1a8f34c) festzunageln.
    # Ist-Werte am Boden nach dem Fix: Spiel ~13-16 ms, ~8.832 Komponenten,
    # ~565k Instanzen (vorher 6000-m-Regime: ~110-163 ms / ~19.700 / ~1,07 Mio.).
    # Ein Rueckfall Richtung altem Streaming-Verhalten reisst diese Schranken.
    [double]$MaxSpielMs        = 45,
    [int]   $MaxPrimComponents = 13000,
    [int]   $MaxInstances      = 800000
)

$ErrorActionPreference = "Stop"
$Exe    = Join-Path $Root "UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
$Proj   = Join-Path $Root "WiesbadenReal\WiesbadenReal.uproject"
$LogDir = Join-Path $Root "WiesbadenReal\Saved\Logs"
$CarLog  = Join-Path $LogDir "smoke_car.log"
$HeliLog = Join-Path $LogDir "smoke_heli.log"

if (-not (Test-Path $Exe))  { Write-Host "ABBRUCH: Editor nicht gefunden: $Exe"; exit 2 }
if (-not (Test-Path $Proj)) { Write-Host "ABBRUCH: Projekt nicht gefunden: $Proj"; exit 2 }

$Checks = New-Object System.Collections.ArrayList
function Add-Check([string]$Name, [bool]$Ok, [string]$Detail) {
    [void]$Checks.Add([pscustomobject]@{ Name = $Name; Ok = $Ok; Detail = $Detail })
}
function Count-Lines([string]$File, [string]$Pattern) {
    if (-not (Test-Path $File)) { return 0 }
    return @(Select-String -Path $File -Pattern $Pattern).Count
}

# Eine Sitzung fahren: bis GENUG Belege (WaitPattern >= MinCount) im Log stehen,
# dann beenden. ExtraArgs sind zusaetzliche Kommandozeilen-Schalter.
function Invoke-Session([string[]]$ExtraArgs, [string]$ExecCmds, [string]$LogFile,
                        [string]$WaitPattern, [int]$MinCount, [int]$TimeoutSec) {
    Get-Process UnrealEditor* -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Seconds 3
    Remove-Item $LogFile -ErrorAction SilentlyContinue

    $sargs = @("`"$Proj`"", "-game", "-windowed", "-resx=1280", "-resy=720") `
        + $ExtraArgs + @("-nosound", "-ABSLOG=$LogFile")
    if ($ExecCmds) { $sargs += "-ExecCmds=`"$ExecCmds`"" }

    $proc = Start-Process -FilePath $Exe -ArgumentList $sargs -PassThru
    Write-Host ("  Sitzung PID {0}: {1} {2}" -f $proc.Id, ($ExtraArgs -join ' '), $ExecCmds)

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    $n = 0
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Seconds 4
        $n = Count-Lines $LogFile $WaitPattern
        if ($n -ge $MinCount) { break }
        if (-not (Get-Process -Id $proc.Id -ErrorAction SilentlyContinue)) { break }
    }
    Write-Host ("    {0} Treffer fuer '{1}'; beende Sitzung." -f $n, $WaitPattern)
    Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
    Get-Process UnrealEditor* -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Seconds 1
}

Write-Host "=== Rauchtest WiesbadenReal ==="

# -- Doku-Drift: WbDev-Referenz gegen Quellcode (statisch, editor-unabhaengig,
#    Sekunden). Faengt umbenannte/entfernte Befehle und umformulierte Log-Zeilen,
#    bevor die teuren Editor-Sitzungen laufen. --------------------------------
$DocCheck = Join-Path $PSScriptRoot "check_wbdev_docs.ps1"
$docOut = & $DocCheck -Root (Join-Path $Root "WiesbadenReal")
$docOk  = ($LASTEXITCODE -eq 0)
$docDetail = if ($docOk) {
    (@($docOut) | Select-Object -Last 1)
} else {
    (@($docOut) | Where-Object { $_ -match 'FEHLT|PHANTOM|DRIFT' } | Select-Object -First 1)
}
Add-Check "DocDrift" $docOk ("{0}" -f $docDetail)

Write-Host "Sitzung 1/2: Fahrzeug (Fahrprofil WbDrive + Materialien) ..."
# Standard-Kaefer bleibt besessen (kein WbHeli): WbDrive faehrt ihn ueber den
# Test-Harness Vollgas + Lenk-Sweep. Warten auf die Material-Bilanz (~8 s) faengt
# alle Fahr-Messpunkte (WbDrive laeuft 7 s) mit ein.
Invoke-Session @() "WbDrive 7" $CarLog "Material-Bilanz:" 1 240

Write-Host "Sitzung 2/2: Teleport + Aufrichten (Fahrzeug), dann Helikopter ..."
Invoke-Session @() "WbTeleport 2,WbNudge 15 55,WbResetVehicle,WbHeli,WbHeliYaw 8,WbHeliFly 16" `
    $HeliLog "WbDev Flug t=" 5 240

$car  = if (Test-Path $CarLog)  { Get-Content $CarLog  -Raw } else { "" }
$heli = if (Test-Path $HeliLog) { Get-Content $HeliLog -Raw } else { "" }

# -- Teleport: Distanz > 100 m (Sitzung 2, am Fahrzeug vor WbHeli) ---------
$m = [regex]::Match($heli, 'WbTeleport \d+ ausgefuehrt:.*Distanz ([\d.]+) cm')
if ($m.Success) {
    $dist = [double]$m.Groups[1].Value
    Add-Check "Teleport" ($dist -gt 10000) ("Distanz {0:N0} cm (erwartet > 10000)" -f $dist)
} else { Add-Check "Teleport" $false "keine WbTeleport-Zeile im Log" }

# -- Reset: vorher gekippt (|Roll|>30), nachher aufrecht (~0/0) -------------
$m = [regex]::Match($heli, 'WbResetVehicle ausgefuehrt: Nick/Roll vorher \(([-\d.]+)/([-\d.]+)\) -> nachher \(([-\d.]+)/([-\d.]+)\)')
if ($m.Success) {
    $vp = [double]$m.Groups[1].Value; $vr = [double]$m.Groups[2].Value
    $np = [double]$m.Groups[3].Value; $nr = [double]$m.Groups[4].Value
    $ok = ([math]::Abs($vr) -gt 30) -and ([math]::Abs($np) -lt 1) -and ([math]::Abs($nr) -lt 1)
    Add-Check "ResetVehicle" $ok ("vorher {0}/{1} -> nachher {2}/{3}" -f $vp,$vr,$np,$nr)
} else { Add-Check "ResetVehicle" $false "keine WbResetVehicle-Zeile im Log" }

# -- Fahren: WbDrive faehrt den Standard-Kaefer ueber die echte Fahrphysik
#    (Test-Harness, ohne Tastatur) -> Tempo baut auf UND der Lenk-Sweep aendert
#    den Kurs. Beweist Laengsdynamik + Lenkung des Fahrzeugs, das ausgeliefert
#    wird (nicht der belly-gebugte ChaosCar). --------------------------------
$fahrt = [regex]::Matches($car, 'WbDev Fahrt t=\d+: Tempo (\d+) km/h, Kursaenderung ([+-]\d+) Grad, Gang (\d+)')
if ($fahrt.Count -ge 3) {
    $maxTempo = 0; $maxKurs = 0; $maxGear = 0
    foreach ($f in $fahrt) {
        $t = [int]$f.Groups[1].Value; if ($t -gt $maxTempo) { $maxTempo = $t }
        $k = [math]::Abs([int]$f.Groups[2].Value); if ($k -gt $maxKurs) { $maxKurs = $k }
        $g = [int]$f.Groups[3].Value; if ($g -gt $maxGear) { $maxGear = $g }
    }
    $ok = ($maxTempo -gt 20) -and ($maxKurs -gt 15)
    Add-Check "Fahren" $ok ("max Tempo {0} km/h (>20), Kursaenderung {1} Grad (>15), Gang {2}, {3} Messpunkte" -f $maxTempo,$maxKurs,$maxGear,$fahrt.Count)
} else { Add-Check "Fahren" $false ("nur {0} Fahrt-Messpunkte (WbDrive lief nicht?)" -f $fahrt.Count) }

# -- Materialien: kein Mesh-Abschnitt ohne Material -------------------------
if ($car -match 'Material-Bilanz: alle (\d+) Mesh-Abschnitte haben ein Material') {
    Add-Check "Materialien" $true ("alle {0} Abschnitte mit Material" -f $Matches[1])
} elseif ($car -match 'Material-Bilanz: (\d+) von (\d+) Mesh-Abschnitten OHNE Material') {
    Add-Check "Materialien" $false ("{0} von {1} OHNE Material (Default-Schachbrett)" -f $Matches[1],$Matches[2])
} else { Add-Check "Materialien" $false "keine Material-Bilanz im Log" }

# -- Perf-Regression: Spiel-Strang-Zeit + Last-Inventar aus dem 8-s-Diagnose-
#    block (feuert in der Fahrzeug-Sitzung). Faellt durch, wenn ein Wert seine
#    Schranke reisst - faengt Regressionen ab, ohne am heutigen (bekannt hohen)
#    Stand zu scheitern. Fehlt der Block ganz, ebenfalls Fehler (koennte eine
#    Verschlechterung verdecken). --------------------------------------------
$mSpiel = [regex]::Match($car, 'Straenge im Mittel: Spiel ([\d.]+) ms')
$mInv   = [regex]::Match($car, 'Last-Inventar \(Spiel-Strang\): (\d+) Primitive-Komponenten .*? (\d+) Instanz-Komponenten mit (\d+) Instanzen')
if ($mSpiel.Success -and $mInv.Success) {
    $spielMs = [double]$mSpiel.Groups[1].Value
    $primComps = [int]$mInv.Groups[1].Value
    $instances = [int]$mInv.Groups[3].Value
    $okSpiel = $spielMs -le $MaxSpielMs
    $okComps = $primComps -le $MaxPrimComponents
    $okInst  = $instances -le $MaxInstances
    $ok = $okSpiel -and $okComps -and $okInst
    Add-Check "Perf-Regression" $ok ("Spiel {0:N0} ms (<= {1}), {2:N0} Komponenten (<= {3:N0}), {4:N0} Instanzen (<= {5:N0})" -f `
        $spielMs, $MaxSpielMs, $primComps, $MaxPrimComponents, $instances, $MaxInstances)
} else {
    Add-Check "Perf-Regression" $false "kein 8-s-Diagnoseblock (Straenge/Last-Inventar) im Log - Perf nicht pruefbar"
}

# -- HeliFly: Steigflug - Hoehe > 8 m zu UND Vario zeitweise > +1 ----------
$flug = [regex]::Matches($heli, 'WbDev Flug t=\d+: Hoehe (\d+) m, Vario ([+-][\d.]+) m/s, Fahrt (\d+) km/h')
if ($flug.Count -ge 3) {
    $hoehen = @(); $varios = @()
    foreach ($f in $flug) { $hoehen += [double]$f.Groups[1].Value; $varios += [double]$f.Groups[2].Value }
    $stieg = ($hoehen | Measure-Object -Maximum).Maximum - $hoehen[0]
    $maxVario = ($varios | Measure-Object -Maximum).Maximum
    $ok = ($stieg -gt 8) -and ($maxVario -gt 1.0)
    Add-Check "HeliFly" $ok ("Hoehengewinn {0:N0} m, max Vario {1:N1} m/s ({2} Messpunkte)" -f $stieg,$maxVario,$flug.Count)
} else { Add-Check "HeliFly" $false ("nur {0} Flug-Messpunkte im Log" -f $flug.Count) }

# -- HeliYaw: Gierrate zeitweise > 10 Grad/s --------------------------------
$gier = [regex]::Matches($heli, 'WbDev Gierprobe t=\d+: Kurs \d+ Grad \(Gierrate ([-\d.]+) Grad/s\)')
if ($gier.Count -ge 2) {
    $rates = @(); foreach ($g in $gier) { $rates += [math]::Abs([double]$g.Groups[1].Value) }
    $maxRate = ($rates | Measure-Object -Maximum).Maximum
    Add-Check "HeliYaw" ($maxRate -gt 10) ("max Gierrate {0:N1} Grad/s (>10, {1} Messpunkte)" -f $maxRate,$gier.Count)
} else { Add-Check "HeliYaw" $false ("nur {0} Gierprobe-Messpunkte im Log" -f $gier.Count) }

# -- Bericht ---------------------------------------------------------------
Write-Host ""
Write-Host "=== Ergebnis ==="
$fail = 0
foreach ($c in $Checks) {
    $tag = if ($c.Ok) { "BESTANDEN" } else { "DURCHGEFALLEN" }
    if (-not $c.Ok) { $fail++ }
    Write-Host ("  [{0,-13}] {1,-14} {2}" -f $tag, $c.Name, $c.Detail)
}
Write-Host ""
if ($fail -eq 0) {
    Write-Host ("ALLE {0} PRUEFUNGEN BESTANDEN." -f $Checks.Count)
    exit 0
} else {
    Write-Host ("{0} von {1} PRUEFUNGEN DURCHGEFALLEN." -f $fail, $Checks.Count)
    exit 1
}
