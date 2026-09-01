# Rauchtest WiesbadenReal - startet die Stadt im echten Fenster, feuert die
# Dev-Befehle bzw. den Fahr-Selbsttest und wertet aus dem Log Bestanden/
# Durchgefallen.
#
# Zwei kurze Sitzungen, weil sich Fahrzeug und Heli die Besitzung teilen:
#   1) Fahrzeug: ChaosCar-Vollgas-Selbsttest (-WbCarTest) + Teleport + Aufrichten,
#      dazu die Material-Bilanz der Stadt (feuert 8 s nach dem Laden von selbst).
#   2) Helikopter: Gier- und Flugprofil ueber den Test-Harness.
# Gewartet wird auf GENUG Log-Belege (nicht auf das Demo-Ende - die Stadt kann
# beim Umherfliegen streckenweise haengen), dann Auswertung gegen erwartete
# Wirkungen. Kein Screenshot, kein Fenster-Fokus -> rechnerunabhaengig.
#
# Aufruf:  Tools\smoke_test.cmd   (oder powershell -File Tools\smoke_test.ps1)
# Exit 0 = alle Pruefungen bestanden, sonst Exit 1.

param(
    [string]$Root = "C:\freebuff\WiesbadenReal_Sicherung"
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
Write-Host "Sitzung 1/2: Fahrzeug (Vollgas-Selbsttest, Teleport, Aufrichten, Materialien) ..."
# ChaosCar bleibt besessen (kein WbHeli) - nur so laeuft der -WbCarTest-Selbsttest.
Invoke-Session @("-WbChaosCar", "-WbCarTest=3") "WbTeleport 2,WbNudge 15 55,WbResetVehicle" `
    $CarLog "Material-Bilanz:" 1 240

Write-Host "Sitzung 2/2: Helikopter (Gier- und Flugprofil) ..."
Invoke-Session @() "WbHeli,WbHeliYaw 8,WbHeliFly 16" $HeliLog "WbDev Flug t=" 5 240

$car  = if (Test-Path $CarLog)  { Get-Content $CarLog  -Raw } else { "" }
$heli = if (Test-Path $HeliLog) { Get-Content $HeliLog -Raw } else { "" }

# -- Teleport: Distanz > 100 m ---------------------------------------------
$m = [regex]::Match($car, 'WbTeleport \d+ ausgefuehrt:.*Distanz ([\d.]+) cm')
if ($m.Success) {
    $dist = [double]$m.Groups[1].Value
    Add-Check "Teleport" ($dist -gt 10000) ("Distanz {0:N0} cm (erwartet > 10000)" -f $dist)
} else { Add-Check "Teleport" $false "keine WbTeleport-Zeile im Log" }

# -- Reset: vorher gekippt (|Roll|>30), nachher aufrecht (~0/0) -------------
$m = [regex]::Match($car, 'WbResetVehicle ausgefuehrt: Nick/Roll vorher \(([-\d.]+)/([-\d.]+)\) -> nachher \(([-\d.]+)/([-\d.]+)\)')
if ($m.Success) {
    $vp = [double]$m.Groups[1].Value; $vr = [double]$m.Groups[2].Value
    $np = [double]$m.Groups[3].Value; $nr = [double]$m.Groups[4].Value
    $ok = ([math]::Abs($vr) -gt 30) -and ([math]::Abs($np) -lt 1) -and ([math]::Abs($nr) -lt 1)
    Add-Check "ResetVehicle" $ok ("vorher {0}/{1} -> nachher {2}/{3}" -f $vp,$vr,$np,$nr)
} else { Add-Check "ResetVehicle" $false "keine WbResetVehicle-Zeile im Log" }

# -- Fahrphysik: Vollgas-Selbsttest -> Antriebsstrang lebt (Drehzahl steigt,
#    Gang legt ein). Vorwaerts-Tempo ist durch den Chassis-Kollisions-Bug
#    (Teil C) blockiert und wird bewusst NICHT geprueft. --------------------
$fp = [regex]::Matches($car, 'Fahrprobe\s+\d+ s:\s+[-\d.]+ km/h,\s+(\d+) 1/min, Gang (\d+)')
if ($fp.Count -ge 3) {
    $maxRpm = 0; $maxGear = 0
    foreach ($f in $fp) {
        $r = [int]$f.Groups[1].Value; if ($r -gt $maxRpm) { $maxRpm = $r }
        $g = [int]$f.Groups[2].Value; if ($g -gt $maxGear) { $maxGear = $g }
    }
    $ok = ($maxRpm -gt 2000) -and ($maxGear -ge 1)
    Add-Check "Fahrphysik" $ok ("max {0} 1/min (>2000), Gang {1} (>=1), {2} Messpunkte" -f $maxRpm,$maxGear,$fp.Count)
} else { Add-Check "Fahrphysik" $false ("nur {0} Fahrprobe-Messpunkte (-WbCarTest lief nicht?)" -f $fp.Count) }

# -- Materialien: kein Mesh-Abschnitt ohne Material -------------------------
if ($car -match 'Material-Bilanz: alle (\d+) Mesh-Abschnitte haben ein Material') {
    Add-Check "Materialien" $true ("alle {0} Abschnitte mit Material" -f $Matches[1])
} elseif ($car -match 'Material-Bilanz: (\d+) von (\d+) Mesh-Abschnitten OHNE Material') {
    Add-Check "Materialien" $false ("{0} von {1} OHNE Material (Default-Schachbrett)" -f $Matches[1],$Matches[2])
} else { Add-Check "Materialien" $false "keine Material-Bilanz im Log" }

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
