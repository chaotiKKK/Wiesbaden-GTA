# Rauchtest WiesbadenReal - startet die Stadt im echten Fenster, feuert die
# Dev-Befehle bzw. das Fahrprofil und wertet aus dem Log Bestanden/
# Durchgefallen.
#
# Drei kurze Sitzungen, weil sich Fahrzeug und Heli die Besitzung teilen:
#   1) Fahrzeug: Fahrprofil (WbDrive - Vollgas + Lenk-Sweep ueber den Test-
#      Harness) weist Laengsdynamik UND Lenkung des Standard-Kaefers nach, dazu
#      Perf-Regression (Spiel-Strang-Zeit + Last-Inventar) aus dem 8-s-Diagnose-
#      block (feuert 8 s nach dem Laden von selbst).
#   2) Helikopter: Teleport + Aufrichten (am Fahrzeug), dann Gier- und Flugprofil.
#   3) Gesundheits-Gate: stationaer, WbHealth im Gate-Modus (wartet auf den
#      geladenen Zustand) -> WbHealth.json. Daraus MASCHINENLESBAR ausgewertet
#      (kein Prosa-grep): die Material-Pruefung (perf.meshSectionsWithoutMaterial)
#      und die evidenz-gewichteten Health-Warnungen als je EIGENE Pruefung.
# Gewartet wird auf GENUG Log-Belege (nicht auf das Demo-Ende - die Stadt kann
# beim Umherfliegen streckenweise haengen), dann Auswertung gegen erwartete
# Wirkungen. Kein Screenshot, kein Fenster-Fokus -> rechnerunabhaengig.
#
# Aufruf:  Tools\smoke_test.cmd   (oder powershell -File Tools\smoke_test.ps1)
# Exit 0 = alle Pruefungen bestanden, sonst Exit 1.

param(
    # Leer = Ordner ueber dem Projekt (siehe unten, damit das Gate im Worktree dessen Stand prueft).
    [string]$Root = "",
    # Die INSTALLIERTE Engine, mit der auch Gate 1 baut (Tools\pruefe_engine.py).
    [string]$EngineRoot = "C:\Program Files\Epic Games\UE_5.8",
    # Perf-Regression-Schranken (aus dem 8-s-Diagnoseblock am Boden), die den
    # WP-Streaming-Fix (hoehenadaptiver Radius, 1a8f34c) festnageln.
    # PRIMAeRES Signal = die DETERMINISTISCHEN Zaehler: Komponenten/Instanzen sind
    # lauf-zu-lauf bit-identisch (8.832 / 565.557 gemessen) und eng gesetzt; ein
    # Rueckfall zum 6000-m-Regime (~19.700 / ~1,07 Mio.) reisst sie sofort.
    # Die FRAME-ZEIT ist dagegen LAST-SENSIBEL: ueber Laeufe 13-26 ms gemessen
    # (~2x Varianz je nach Maschinenlast). $MaxSpielMs ist die BASIS-Schranke fuer
    # eine UNBELASTETE Maschine; sie wird zur Laufzeit mit einem gemessenen
    # Lastfaktor hochskaliert (siehe unten), damit Maschinenlast sie nicht
    # faelschlich reisst. Die deterministischen Zaehler bleiben die harte
    # Primaer-Schranke, die eine ECHTE Regression (6000-m-Regime, ~19.700/~1,07 Mio.)
    # last-UNABHAENGIG faengt.
    [double]$MaxSpielMs        = 40,
    [int]   $MaxPrimComponents = 13000,
    [int]   $MaxInstances      = 800000,
    # -- Last-Normierung der Frame-Zeit-Schranke ----------------------------
    # Eine CPU-Mikrobench misst die AKTUELLE Maschinengeschwindigkeit; ihr
    # Verhaeltnis zur Referenz (unbelastet gemessen) ist der Lastfaktor. Er
    # skaliert $MaxSpielMs, gedeckelt auf $MaxLoadFactor - so relaxt der Test unter
    # Last, statt falsch durchzufallen, deckt aber echte Ausreisser weiter ab
    # (absolutes Dach $AbsoluteMaxSpielMs, unabhaengig vom Lastfaktor).
    [double]$CalibRefMs        = 236,   # 3M sqrt-Iterationen auf dieser Maschine, unbelastet (min).
    [int]   $CalibIter         = 3000000,
    [int]   $CalibSamples      = 5,
    [double]$MaxLoadFactor     = 8.0,
    [double]$AbsoluteMaxSpielMs = 250
)

$ErrorActionPreference = "Stop"
# Ordner UEBER dem Projekt aus dem Ort dieses Skripts (Tools\ im Projekt) - im
# RUMPF bestimmt, nicht als Parameter-Vorgabe: mit [CmdletBinding()] ist
# $PSScriptRoot dort unter PowerShell 5.1 LEER (gemessen 25.09.2026 im
# Gate-Worktree: "Split-Path: leere Zeichenfolge").
if (-not $Root) { $Root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent }

$Exe    = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor.exe"
$Proj   = Join-Path $Root "WiesbadenReal\WiesbadenReal.uproject"
$LogDir = Join-Path $Root "WiesbadenReal\Saved\Logs"
$CarLog  = Join-Path $LogDir "smoke_car.log"
$HeliLog = Join-Path $LogDir "smoke_heli.log"
$HealthLog  = Join-Path $LogDir "smoke_health.log"
$HealthJson = Join-Path $LogDir "WbHealth.json"

if (-not (Test-Path $Exe))  { Write-Host "ABBRUCH: Editor nicht gefunden: $Exe"; exit 2 }
if (-not (Test-Path $Proj)) { Write-Host "ABBRUCH: Projekt nicht gefunden: $Proj"; exit 2 }

# -- Engine-Lock -------------------------------------------------------------
# Dieser Lauf haelt den Lock ueber alle drei Sitzungen. Ohne ihn wuerde ein
# zweiter Lauf (Push-Gate, Bake, Health-Check) beim Start genau diesen Editor
# beenden - am 25.09.2026 hat das einen bereits als rot gemeldeten Gate-Lauf
# mitgerissen. Freigabe ist nicht noetig: der Lock stirbt mit diesem Prozess,
# ein verwaistes File uebernimmt der naechste Lauf selbst.
& "$PSScriptRoot\engine_run_lock.ps1" -Modus Nehmen -Name smoke_test
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# Zwischen den Sitzungen beenden wir nur Editoren DIESES Projektordners. Am
# Start jeder Sitzung ruft der Wrapper dagegen den globalen Cleanup auf: genau
# das verhindert den ZenServer-Limbo aus einem fremden Restprozess. Im
# Gate-Worktree (Tools\gate_worktree.py) haelt ohnehin nur der eigene Editor
# dessen Binaries fest.
function Stop-ProjectEditors([string]$ProjectFile) {
    $want = $ProjectFile.Replace('/', '\')
    Get-CimInstance Win32_Process -Filter "Name LIKE 'UnrealEditor%'" -ErrorAction SilentlyContinue |
        Where-Object { $_.CommandLine -and $_.CommandLine.Replace('/', '\') -like "*$want*" } |
        ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
}

$Checks = New-Object System.Collections.ArrayList
function Add-Check([string]$Name, [bool]$Ok, [string]$Detail) {
    [void]$Checks.Add([pscustomobject]@{ Name = $Name; Ok = $Ok; Detail = $Detail })
}
function Count-Lines([string]$File, [string]$Pattern) {
    if (-not (Test-Path $File)) { return 0 }
    return @(Select-String -Path $File -Pattern $Pattern).Count
}

# -- BELEGE: eine Datei von gestern ist kein Messergebnis ------------------
#
# GEMESSEN am 27.09.2026: diese Datei loeschte ihre Belege mit
# "Remove-Item -ErrorAction SilentlyContinue" und VERTRAUTE darauf, dass sie
# damit weg sind. SilentlyContinue schluckt jeden Fehler - die Datei blieb
# liegen, sowohl mit Read-only-Flag als auch mit dem offenen Handle eines
# haengenden Editors (beide Faelle nachgemessen). Danach las die Auswertung
# genau diese alte Datei und meldete deren ZAHLEN als das Ergebnis des
# aktuellen Laufs. Das ist dieselbe Fehlerklasse, an der Gate 4 im selben
# Lauf gescheitert ist: die Belege waren da, nur nicht von diesem Lauf.
#
# Warum das hier besonders leicht passiert: Saved/ ist in .gitignore, und der
# Gate-Worktree putzt mit "git clean -fd" OHNE -x. Ignorierte Dateien
# ueberleben den Putz - der Worktree sammelt die Belege ueber beliebig viele
# Push-Laeufe an.
$BelegStart = @{}

# Loescht einen Beleg und NACHPRUEFT, dass er wirklich weg ist. Ein nicht
# loeschbarer Beleg ist kein Anlass zum Weitermachen: die Auswertung waere
# dann die des letzten Laufs, und genau so sieht ein gruenes Gate aus, das
# nichts gemessen hat. Also abbruch mit Wegweiser statt weiterlaufen.
function Remove-Beleg([string]$File) {
    $name = Split-Path $File -Leaf
    if (Test-Path $File) { Remove-Item $File -Force -ErrorAction SilentlyContinue }
    if (Test-Path $File) {
        # Read-only steckt am Ordner-Eintrag, nicht am Inhalt.
        try { (Get-Item $File -Force).IsReadOnly = $false } catch { }
        Remove-Item $File -Force -ErrorAction SilentlyContinue
    }
    if (Test-Path $File) {
        Write-Host ""
        Write-Host ("ABBRUCH: der Beleg {0} laesst sich nicht loeschen." -f $name)
        Write-Host "  Er ist entweder schreibgeschuetzt oder von einem haengenden"
        Write-Host "  Prozess offen gehalten (dann blockiert auch -ABSLOG= auf ihn)."
        Write-Host "  Laeuft ein Fremdlauf oder steht ein Editor noch? Erst auf ihn"
        Write-Host "  warten und dann erneut starten - sonst wuerde der Rauchtest die"
        Write-Host "  ZAHLEN DES FREMdlaufs als Messung dieses Laufs melden."
        exit 1
    }
}

# Stammt die Datei aus dem Lauf, der um $Start begann? Eine Sekunde
# Toleranz: das Dateisystem rundet die Zeitstempel je nach Plattform, und
# ein Log, das Sekundenbruchteile nach dem Start entsteht, darf nicht
# daran scheitern. Gegenlaeufig zur Loeschpruefung - eine Datei, die gar
# nicht geloescht werden konnte, faellt hier ebenfalls durch.
function Test-Frisch([string]$File, $Start) {
    if ($null -eq $Start)  { return $false }
    if (-not (Test-Path $File)) { return $false }
    try   { $zeit = (Get-Item $File).LastWriteTime }
    catch { return $false }
    return ($zeit -ge $Start.AddSeconds(-1))
}

# Misst die AKTUELLE CPU-Geschwindigkeit dieser Maschine ueber eine feste,
# deterministische Last (Median mehrerer Proben glaettet Ausreisser). Steht die
# Maschine unter Last, dauert das laenger - genau das soll die Frame-Zeit-
# Schranke relaxen. Der Median mehrerer Proben ist der "typische" Ist-Zustand.
function Measure-LoadFactor([int]$Iter, [int]$Samples, [double]$RefMs, [double]$MaxFactor) {
    $times = @()
    for ($s = 0; $s -lt $Samples; $s++) {
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        $acc = 0.0
        for ($i = 1; $i -le $Iter; $i++) { $acc += [math]::Sqrt($i) }
        $sw.Stop()
        $times += $sw.Elapsed.TotalMilliseconds
    }
    $median = ($times | Sort-Object)[[int]($times.Count / 2)]
    # Faktor >= 1 (schnellere Maschine als die Referenz bekommt keinen Bonus,
    # sondern die strenge Basis-Schranke) und gedeckelt (kein Freibrief).
    $factor = $median / $RefMs
    if ($factor -lt 1.0) { $factor = 1.0 }
    if ($factor -gt $MaxFactor) { $factor = $MaxFactor }
    return $factor
}

# -- AUSWERTUNG --------------------------------------------------------------
# Die Pruefungen liegen in Funktionen, nicht im Rumpf. Grund ist nicht
# Schoenheit: Test-Frisch beweist nur die AKTUALITAET eines Belegs, nicht
# dessen VOLLSTAENDIGKEIT. Ob der Rauchtest auf eine frueh Gestorbene Sitzung
# noch rot wird, entscheidet sich in genau diesem Code - und der war bisher
# nur mit einem startenden Editor erreichbar. Als Funktionen laesst er sich
# mit abgeschnittenen Belegen fuettern (Tools/test_rauchtest_beleg.py).
function Lese-Beleg([string]$Datei, $Start, [string]$Was) {
    if (Test-Frisch $Datei $Start) { return (Get-Content $Datei -Raw) }
    Add-Check ("Beleg:" + $Was) $false `
        ("{0} ist kein Beleg dieses Laufs (fehlt, oder unveraendert seit dem " +
         "vorigen Lauf stehen geblieben). Die Zahlen waeren die des vorigen " +
         "Laufs - deshalb wird hier nichts ausgewertet.") `
        -f (Split-Path $Datei -Leaf)
    return ""
}

function Pruefe-Helilog([string]$heli) {
    # Teleport: Distanz > 100 m (Sitzung 2, am Fahrzeug vor WbHeli)
    $m = [regex]::Match($heli, 'WbTeleport \d+ ausgefuehrt:.*Distanz ([\d.]+) cm')
    if ($m.Success) {
        $dist = [double]$m.Groups[1].Value
        Add-Check "Teleport" ($dist -gt 10000) ("Distanz {0:N0} cm (erwartet > 10000)" -f $dist)
    } else { Add-Check "Teleport" $false "keine WbTeleport-Zeile im Log" }

    # Reset: vorher gekippt (|Roll|>30), nachher aufrecht (~0/0)
    $m = [regex]::Match($heli, 'WbResetVehicle ausgefuehrt: Nick/Roll vorher \(([-\d.]+)/([-\d.]+)\) -> nachher \(([-\d.]+)/([-\d.]+)\)')
    if ($m.Success) {
        $vp = [double]$m.Groups[1].Value; $vr = [double]$m.Groups[2].Value
        $np = [double]$m.Groups[3].Value; $nr = [double]$m.Groups[4].Value
        $ok = ([math]::Abs($vr) -gt 30) -and ([math]::Abs($np) -lt 1) -and ([math]::Abs($nr) -lt 1)
        Add-Check "ResetVehicle" $ok ("vorher {0}/{1} -> nachher {2}/{3}" -f $vp,$vr,$np,$nr)
    } else { Add-Check "ResetVehicle" $false "keine WbResetVehicle-Zeile im Log" }

    # HeliFly: Steigflug - Hoehe > 8 m zu UND Vario zeitweise > +1
    $flug = [regex]::Matches($heli, 'WbDev Flug t=\d+: Hoehe (\d+) m, Vario ([+-][\d.]+) m/s, Fahrt (\d+) km/h')
    if ($flug.Count -ge 3) {
        $hoehen = @(); $varios = @()
        foreach ($f in $flug) { $hoehen += [double]$f.Groups[1].Value; $varios += [double]$f.Groups[2].Value }
        $stieg = ($hoehen | Measure-Object -Maximum).Maximum - $hoehen[0]
        $maxVario = ($varios | Measure-Object -Maximum).Maximum
        $ok = ($stieg -gt 8) -and ($maxVario -gt 1.0)
        Add-Check "HeliFly" $ok ("Hoehengewinn {0:N0} m, max Vario {1:N1} m/s ({2} Messpunkte)" -f $stieg,$maxVario,$flug.Count)
    } else { Add-Check "HeliFly" $false ("nur {0} Flug-Messpunkte im Log (die Sitzung starb vorher?)" -f $flug.Count) }

    # HeliYaw: Gierrate zeitweise > 10 Grad/s
    $gier = [regex]::Matches($heli, 'WbDev Gierprobe t=\d+: Kurs \d+ Grad \(Gierrate ([-\d.]+) Grad/s\)')
    if ($gier.Count -ge 2) {
        $rates = @(); foreach ($g in $gier) { $rates += [math]::Abs([double]$g.Groups[1].Value) }
        $maxRate = ($rates | Measure-Object -Maximum).Maximum
        Add-Check "HeliYaw" ($maxRate -gt 10) ("max Gierrate {0:N1} Grad/s (>10, {1} Messpunkte)" -f $maxRate,$gier.Count)
    } else { Add-Check "HeliYaw" $false ("nur {0} Gierprobe-Messpunkte im Log (die Sitzung starb vorher?)" -f $gier.Count) }
}

function Pruefe-Fahrlog([string]$car) {
    # Fahren: WbDrive faehrt den Standard-Kaefer ueber die echte Fahrphysik
    # (Test-Harness, ohne Tastatur) -> Tempo baut auf UND der Lenk-Sweep aendert
    # den Kurs. Beweist Laengsdynamik + Lenkung des ausgelieferten Fahrzeugs.
    # Die Fahrt-Zeile traegt seit dem Drehzahl-Flare (a4f276b) auch die Drehzahl;
    # ohne das optionale Feld fand die Pruefung 0 Messpunkte.
    $fahrt = [regex]::Matches($car, 'WbDev Fahrt t=\d+: Tempo (\d+) km/h,(?: Drehzahl \d+ U/min,)? Kursaenderung ([+-]\d+) Grad, Gang (\d+)')
    if ($fahrt.Count -ge 3) {
        $maxTempo = 0; $maxKurs = 0; $maxGear = 0
        foreach ($f in $fahrt) {
            $t = [int]$f.Groups[1].Value; if ($t -gt $maxTempo) { $maxTempo = $t }
            $k = [math]::Abs([int]$f.Groups[2].Value); if ($k -gt $maxKurs) { $maxKurs = $k }
            $g = [int]$f.Groups[3].Value; if ($g -gt $maxGear) { $maxGear = $g }
        }
        $ok = ($maxTempo -gt 20) -and ($maxKurs -gt 15)
        Add-Check "Fahren" $ok ("max Tempo {0} km/h (>20), Kursaenderung {1} Grad (>15), Gang {2}, {3} Messpunkte" -f $maxTempo,$maxKurs,$maxGear,$fahrt.Count)
    } else { Add-Check "Fahren" $false ("nur {0} Fahrt-Messpunkte (WbDrive lief nicht? die Sitzung starb vorher?)" -f $fahrt.Count) }

    # Perf-Regression: Spiel-Strang-Zeit + Last-Inventar aus dem 8-s-Diagnose-
    # block (feuert in der Fahrzeug-Sitzung). Zwei Signale mit verschiedener
    # Natur: die DETERMINISTISCHEN Zaehler (Komponenten/Instanzen) sind die
    # harte, last-unabhaengige Primaer-Schranke; die FRAME-ZEIT wird
    # last-normiert (Basis x gemessener Lastfaktor, gedeckelt + absolutes Dach).
    # Fehlt der Block ganz, ebenfalls Fehler - koennte eine Verschlechterung
    # verdecken, und genau das ist der Fall, wenn die Sitzung vorher stirbt.
    $mSpiel = [regex]::Match($car, 'Straenge im Mittel: Spiel ([\d.]+) ms')
    $mInv   = [regex]::Match($car, 'Last-Inventar \(Spiel-Strang\): (\d+) Primitive-Komponenten .*? (\d+) Instanz-Komponenten mit (\d+) Instanzen')
    if ($mSpiel.Success -and $mInv.Success) {
        $spielMs = [double]$mSpiel.Groups[1].Value
        $primComps = [int]$mInv.Groups[1].Value
        $instances = [int]$mInv.Groups[3].Value

        $okComps = $primComps -le $MaxPrimComponents
        $okInst  = $instances -le $MaxInstances

        $loadFactor = Measure-LoadFactor $CalibIter $CalibSamples $CalibRefMs $MaxLoadFactor
        $effMaxSpiel = [Math]::Min($MaxSpielMs * $loadFactor, $AbsoluteMaxSpielMs)
        $okSpiel = $spielMs -le $effMaxSpiel

        $ok = $okSpiel -and $okComps -and $okInst
        Add-Check "Perf-Regression" $ok ("Spiel {0:N0} ms (<= {1:N0} = {2}x{3:N1}, Dach {4:N0}), {5:N0} Komponenten (<= {6:N0}), {7:N0} Instanzen (<= {8:N0})" -f `
            $spielMs, $effMaxSpiel, $MaxSpielMs, $loadFactor, $AbsoluteMaxSpielMs, `
            $primComps, $MaxPrimComponents, $instances, $MaxInstances)
    } else {
        Add-Check "Perf-Regression" $false "kein 8-s-Diagnoseblock (Straenge/Last-Inventar) im Log - Perf nicht pruefbar"
    }
}

function Pruefe-Health($health, [string]$healthErr) {
    # Materialien: kein Mesh-Abschnitt ohne Material - aus dem WbHealth.json-
    # Report-Feld (perf.meshSectionsWithoutMaterial), NICHT aus der Material-
    # Bilanz-Prosa gegreppt.
    if ($null -ne $health -and $null -ne $health.perf) {
        $noMat = [int]$health.perf.meshSectionsWithoutMaterial
        $total = [int]$health.perf.meshSectionsTotal
        $matDetail = if ($noMat -eq 0) { "alle {0} Abschnitte mit Material" -f $total } `
            else { "{0} von {1} OHNE Material (Default-Schachbrett)" -f $noMat, $total }
        Add-Check "Materialien" ($noMat -eq 0) $matDetail
    } else {
        Add-Check "Materialien" $false ("Material aus WbHealth.json nicht lesbar: {0}" -f $healthErr)
    }

    # Gesundheits-Gate: die evidenz-gewichteten Warnungen aus WbHealth.json als
    # EIGENE Pruefungen (maschinenlesbar, kein Prosa-grep). Gesund -> eine
    # gruene Zeile; ungesund -> je Warnung eine eigene rote Pruefung, benannt
    # nach ihrer Kategorie. So ist auf einen Blick sichtbar, WELCHE Dimension
    # kippt, statt eine Sammel-Zeile.
    if ($null -eq $health) {
        Add-Check "Health-Gate" $false ("WbHealth.json fehlt/unparsebar: {0}" -f $healthErr)
    } elseif ($health.healthy) {
        Add-Check "Health-Gate" $true "healthy: true (keine Warnungen)"
    } else {
        foreach ($w in @($health.warnings)) {
            $cat = ($w -split ':', 2)[0].Trim()
            Add-Check ("Health:" + $cat) $false $w
        }
    }
}

# Eine Sitzung fahren: bis GENUG Belege (WaitPattern >= MinCount) im Log stehen,
# dann beenden. ExtraArgs sind zusaetzliche Kommandozeilen-Schalter.
function Invoke-Session([string[]]$ExtraArgs, [string]$ExecCmds, [string]$LogFile,
                        [string]$WaitPattern, [int]$MinCount, [int]$TimeoutSec) {
    & "$PSScriptRoot\cleanup_unreal_processes.cmd"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    Stop-ProjectEditors $Proj
    Remove-Beleg $LogFile

    $sargs = @("`"$Proj`"", "-game", "-windowed", "-resx=1280", "-resy=720") `
        + $ExtraArgs + @("-nosound", "-ABSLOG=$LogFile")
    if ($ExecCmds) { $sargs += "-ExecCmds=`"$ExecCmds`"" }

    # Ab hier ist der Beleg dieser Sitzung: alles, was die Datei nun bekommt,
    # stammt aus diesem Lauf. Test-Frisch vergleicht später genau dagegen.
    $BelegStart[$LogFile] = Get-Date
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
    Stop-ProjectEditors $Proj
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
# Auf OFFENEM FELD (Wiese Grabenstrasse/Schulstrasse): seit der Kaefer im
# Garagenhof Platter Str. 144 startet (c6c420f), fuhr er dort nach 2 s gegen
# die Hofmauer - 19 km/h, kein Kurs, die Pruefung fiel ohne Physikfehler durch.
Invoke-Session @("-WbGoto=-180086,899031") "WbDrive 7" $CarLog "Material-Bilanz:" 1 240

Write-Host "Sitzung 2/3: Teleport + Aufrichten (Fahrzeug), dann Helikopter ..."
Invoke-Session @() "WbTeleport 2,WbNudge 15 55,WbResetVehicle,WbHeli,WbHeliYaw 8,WbHeliFly 16" `
    $HeliLog "WbDev Flug t=" 5 240

# Sitzung 3: Gesundheits-Gate. STATIONAER (kein Teleport/Flug, damit das
# WP-Streaming einmal sauber einrastet), WbHealth im Gate-Modus (wartet bis zu
# 30 s auf den geladenen Zustand, dann Dump nach WbHealth.json). Auf die
# geschriebene JSON-Zeile im Log warten.
Write-Host "Sitzung 3/3: Gesundheits-Gate (WbHealth nach dem Laden) ..."
Remove-Beleg $HealthJson
$BelegStart[$HealthJson] = Get-Date
Invoke-Session @() "WbHealth 30" $HealthLog "WbDev: WbHealth:" 1 180

# Die Belege LESEN - aber nur, wenn sie aus diesem Lauf stammen. Ein
# liegengebliebener Log von gestern bekaeme hier sonst die Messpunkte
# untergeschoben, und der Warte-Abbruch weiter oben haette den frischen
# Editor nach vier Sekunden gekillt, ohne je gemessen zu haben.
$car  = Lese-Beleg $CarLog  $BelegStart[$CarLog]  "Sitzung 1 (Fahrzeug)"
$heli = Lese-Beleg $HeliLog $BelegStart[$HeliLog] "Sitzung 2 (Heli)"
# Umgekehrt heisst "gruen" jetzt auch: die Belege wurden als die dieses Laufs
# nachgewiesen. Ohne diese Zeile ist nicht unterscheidbar, ob die Pruefungen
# gemessen haben oder eine liegengebliebene Datei gelesen haben.
if (@($Checks | Where-Object { $_.Name -like "Beleg:*" }).Count -eq 0) {
    Add-Check "Beleg" $true ("{0} Logs aus diesem Lauf bestaetigt" -f 2)
}

# WbHealth.json EINMAL parsen (aus der stationaeren Gate-Sitzung 3). Speist das
# Gesundheits-Gate UND die Material-Pruefung MASCHINENLESBAR aus dem Report,
# statt Prosa-Logzeilen zu greppen.
$health = $null
$healthErr = ""
if (Test-Frisch $HealthJson $BelegStart[$HealthJson]) {
    try { $health = Get-Content $HealthJson -Raw | ConvertFrom-Json }
    catch { $healthErr = $_.Exception.Message }
} elseif (Test-Path $HealthJson) {
    # Die Datei liegt da und sieht richtig aus - nur ist sie von gestern.
    # Genau dieser Fall macht aus dem Gesundheits-Gate eine Ampel ohne Lampe:
    # "healthy: true" waeren die Werte des VORRIGEN Durchlaufs.
    $healthErr = ("WbHealth.json ist vom vorigen Lauf uebernommen (unveraendert " +
                  "seit {0:HH:mm:ss}) - das Gesundheits-Gate und die " +
                  "Material-Pruefung wurden nicht aus diesem Lauf gelesen") `
                 -f (Get-Item $HealthJson).LastWriteTime
    Add-Check "Beleg:WbHealth.json" $false $healthErr
} else {
    $healthErr = "keine WbHealth.json geschrieben (WbHealth nicht ausgeloest?)"
}

# Die Pruefungen selbst liegen in Pruefe-Helilog/Pruefe-Fahrlog/Pruefe-Health
# weiter oben - dort koennen sie mit abgeschnittenen Belegen gefuettert werden.
Pruefe-Helilog $heli
Pruefe-Fahrlog $car
Pruefe-Health $health $healthErr

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
