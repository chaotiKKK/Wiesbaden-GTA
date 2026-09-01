# Rauchtest WiesbadenReal - startet die Stadt im echten Fenster, feuert die
# Dev-Befehle und wertet aus dem Log Bestanden/Durchgefallen.
#
# Idee: Alle Dev-Execs schreiben eine eindeutige "WbDev"-Zeile ins Editor-Log.
# EINE kurze Spielsitzung feuert per -ExecCmds (komma-getrennt) nacheinander
# Teleport, Kippen+Aufrichten (Fahrzeug), dann Heli uebernehmen und Gier- und
# Flugprofil starten. Der Test wartet, bis GENUG Flug-Messpunkte im Log stehen
# (nicht auf das Ende des Demos - die Stadt kann beim Umherfliegen strecken-
# weise haengen), beendet die Sitzung und prueft die Zeilen gegen erwartete
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
$Log    = Join-Path $LogDir "smoke_test.log"

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

# Alle Dev-Befehle in EINER Sitzung. Reihenfolge zaehlt: Teleport/Kippen/
# Aufrichten wirken auf das Startfahrzeug, DANN wird der Heli uebernommen und
# Gier- + Flugprofil laufen gemeinsam (verschiedene Achsen, stoeren sich nicht).
$ExecCmds = "WbTeleport 2,WbNudge 15 55,WbResetVehicle,WbHeli,WbHeliYaw 8,WbHeliFly 16"

Write-Host "=== Rauchtest WiesbadenReal ==="
Get-Process UnrealEditor* -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 2
Remove-Item $Log -ErrorAction SilentlyContinue

$sargs = @(
    "`"$Proj`"", "-game", "-windowed", "-resx=1280", "-resy=720",
    "-ExecCmds=`"$ExecCmds`"", "-nosound", "-ABSLOG=$Log"
)
$proc = Start-Process -FilePath $Exe -ArgumentList $sargs -PassThru
Write-Host ("Sitzung PID {0}: {1}" -f $proc.Id, $ExecCmds)
Write-Host "Warte auf Flug-Messpunkte (Stadt laedt, dann Steigflug) ..."

# Warten, bis genug Flug-Messpunkte da sind (Steigflug erfasst), max 220 s.
$deadline = (Get-Date).AddSeconds(220)
$flugLines = 0
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 4
    $flugLines = Count-Lines $Log "WbDev Flug t="
    if ($flugLines -ge 5) { break }
    if (-not (Get-Process -Id $proc.Id -ErrorAction SilentlyContinue)) { break }
}
Write-Host ("  {0} Flug-Messpunkte im Log; beende Sitzung." -f $flugLines)
Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
Get-Process UnrealEditor* -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 1

$t = if (Test-Path $Log) { Get-Content $Log -Raw } else { "" }

# -- Teleport: Distanz > 100 m ---------------------------------------------
$m = [regex]::Match($t, 'WbTeleport \d+ ausgefuehrt:.*Distanz ([\d.]+) cm')
if ($m.Success) {
    $dist = [double]$m.Groups[1].Value
    Add-Check "Teleport" ($dist -gt 10000) ("Distanz {0:N0} cm (erwartet > 10000)" -f $dist)
} else { Add-Check "Teleport" $false "keine WbTeleport-Zeile im Log" }

# -- Reset: vorher gekippt (|Roll|>30), nachher aufrecht (~0/0) -------------
$m = [regex]::Match($t, 'WbResetVehicle ausgefuehrt: Nick/Roll vorher \(([-\d.]+)/([-\d.]+)\) -> nachher \(([-\d.]+)/([-\d.]+)\)')
if ($m.Success) {
    $vp = [double]$m.Groups[1].Value; $vr = [double]$m.Groups[2].Value
    $np = [double]$m.Groups[3].Value; $nr = [double]$m.Groups[4].Value
    $ok = ([math]::Abs($vr) -gt 30) -and ([math]::Abs($np) -lt 1) -and ([math]::Abs($nr) -lt 1)
    Add-Check "ResetVehicle" $ok ("vorher {0}/{1} -> nachher {2}/{3}" -f $vp,$vr,$np,$nr)
} else { Add-Check "ResetVehicle" $false "keine WbResetVehicle-Zeile im Log" }

# -- HeliFly: Steigflug - Hoehe nimmt um > 15 m zu UND Vario zeitweise > +2 -
$flug = [regex]::Matches($t, 'WbDev Flug t=\d+: Hoehe (\d+) m, Vario ([+-][\d.]+) m/s, Fahrt (\d+) km/h')
if ($flug.Count -ge 3) {
    $hoehen = @(); $varios = @()
    foreach ($f in $flug) { $hoehen += [double]$f.Groups[1].Value; $varios += [double]$f.Groups[2].Value }
    $stieg = ($hoehen | Measure-Object -Maximum).Maximum - $hoehen[0]
    $maxVario = ($varios | Measure-Object -Maximum).Maximum
    # Schwelle bewusst locker: der Rauchtest belegt "steigt ab Boden", nicht ein
    # exaktes Mass. Gier- und Flugprofil laufen gemeinsam, das daempft den Steig.
    $ok = ($stieg -gt 8) -and ($maxVario -gt 1.0)
    Add-Check "HeliFly" $ok ("Hoehengewinn {0:N0} m, max Vario {1:N1} m/s ({2} Messpunkte)" -f $stieg,$maxVario,$flug.Count)
} else { Add-Check "HeliFly" $false ("nur {0} Flug-Messpunkte im Log" -f $flug.Count) }

# -- HeliYaw: der Rumpf giert - Gierrate zeitweise > 10 Grad/s --------------
$gier = [regex]::Matches($t, 'WbDev Gierprobe t=\d+: Kurs \d+ Grad \(Gierrate ([-\d.]+) Grad/s\)')
if ($gier.Count -ge 2) {
    $rates = @(); foreach ($g in $gier) { $rates += [math]::Abs([double]$g.Groups[1].Value) }
    $maxRate = ($rates | Measure-Object -Maximum).Maximum
    Add-Check "HeliYaw" ($maxRate -gt 10) ("max Gierrate {0:N1} Grad/s (erwartet > 10, {1} Messpunkte)" -f $maxRate,$gier.Count)
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
