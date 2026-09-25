# Engine-Lock: zwei Laeufe auf derselben Maschine duerfen sich nicht gegenseitig
# den UnrealEditor wegkills. Der Anlass ist ein roter Push-Lauf: waehrend der
# bereits als fehlgeschlagen gemeldete Gate-Lauf (Rauchtest Sitzung 3) weiterlief,
# startete ein zweiter Push, dessen Cleanup genau diesen Editor beendete - der
# zweite Lauf wurde dadurch mitgerissen.
#
# SO FUNKTIONIERT DER LOCK
#   * Eine Sperrdatei unter %LOCALAPPDATA%\WiesbadenReal\Locks\engine_run.lock
#     wird EXKLUSIV angelegt ([System.IO.FileMode]::CreateNew - atomar, wie
#     O_CREAT|O_EXCL) und traegt den PID des aufrufenden Prozesses.
#   * Besitzer ist der ELTERNprozess (die aufrufende cmd.exe bzw. powershell.exe),
#     nicht der kurzlebige PowerShell-Kindprozess: nur so lebt der Lock genau so
#     lange wie der Lauf, der ihn beansprucht - ohne Freigabepflicht.
#   * Ein Besitzer, dessen Prozess nicht (mehr) existiert, gilt als VERWAIST:
#     die Sperre wird uebernommen. Ein abgebrochener Lauf blockiert also nicht.
#   * Gehoert der Besitzer zur eigenen Prozesskette (Vater/Sohn desselben Laufs,
#     z. B. Gate -> smoke_test -> Cleanup), ist die Sperre EIGEN: der Lauf darf
#     beenden. Reentrant, sonst wuerde sich das Gate selbst blockieren.
#   * PID-Wiederverwendung: neben dem PID wird die Startzeit gespeichert und
#     beim Pruefen verglichen - ein recycelter PID gilt nicht als Besitzer.
#
# MODI
#   Start     Lock nehmen und danach die Prozessbereinigung ausfuehren. Das ist
#             der Aufruf fuer die Batch-Wrapper (ersetzt den cleanup-Aufruf 1:1).
#   Nehmen    nur den Lock holen (PowerShell-Wrapper, die mehrere Sitzungen
#             fahren und den Lock ueber den ganzen Lauf halten).
#   Freigeben Lock loeschen; -Gewalt auch bei LEBENDEM fremdem Besitzer.
#   Status    Zustand melden, nichts aendern. Exit 3 = fremder Lauf belegt.
#
# Exit: 0 = frei/eigen/verwaist/erfolgreich, 3 = Lock durch fremden Lauf belegt.
[CmdletBinding()]
param(
    [ValidateSet("Start", "Nehmen", "Freigeben", "Status")]
    [string]$Modus = "Status",
    # Kurzname des Laufs - steht in der Meldung, die ein wartender Lauf sieht.
    [string]$Name = "unbekannt",
    # Sekunden, die ein belegter Lock erwartet wird (0 = sofort abbrechen).
    [int]$WarteSekunden = 0,
    # Abweichender Lock-Pfad. Leer = %LOCALAPPDATA%\WiesbadenReal\Locks\engine_run.lock
    # (MASCHINENweit, nicht pro Projekt: zwei Sitzungen teilen sich die Engine).
    [string]$LockPfad = "",
    # Nur fuer Start: Ziele der Bereinigung zeigen, nichts beenden.
    [switch]$DryRun,
    # Nur fuer Freigeben: auch den Lock eines noch lebenden fremden Laufs nehmen.
    [switch]$Gewalt
)

$ErrorActionPreference = "Stop"
$ExitBelegt = 3

function Get-LockDatei([string]$Pfad) {
    if ($Pfad) { return $Pfad }
    $basis = $env:LOCALAPPDATA
    if (-not $basis) { $basis = $env:TEMP }
    $ordner = Join-Path $basis "WiesbadenReal\Locks"
    if (-not (Test-Path -LiteralPath $ordner)) {
        New-Item -ItemType Directory -Path $ordner -Force | Out-Null
    }
    return (Join-Path $ordner "engine_run.lock")
}

# PID -> Startzeit fuer diesen Prozess und seine Vorfahren (max. 6 Ebenen).
# Damit laesst sich ohne Absprache erkennen, ob der Lock-Besitzer zu diesem
# Lauf gehoert (Vater-/Sohnprozess) oder zu einem fremden.
function Get-ProzessKette([int]$StartPid) {
    $kette = @{}
    $aktuell = $StartPid
    for ($i = 0; $i -lt 6 -and $aktuell -gt 0; $i++) {
        $start = ""
        try {
            $p = Get-Process -Id $aktuell -ErrorAction SilentlyContinue
            if (-not $p) { break }
            $start = $p.StartTime.ToString("o")
        } catch { break }
        $kette[$aktuell] = $start
        $zeile = Get-CimInstance Win32_Process -Filter ("ProcessId={0}" -f $aktuell) -ErrorAction SilentlyContinue
        if (-not $zeile) { break }
        $aktuell = [int]$zeile.ParentProcessId
    }
    return $kette
}

# Sperrdatei lesen. $null = noch keine Sperre. Fehlt die PID (Datei gerade erst
# angelegt, Prozess davor gestorben), zaehlt das als verwaist.
function Read-LockDatei([string]$Pfad) {
    if (-not (Test-Path -LiteralPath $Pfad)) { return $null }
    $roh = ""
    try { $roh = [System.IO.File]::ReadAllText($Pfad) } catch { $roh = "" }
    $felder = @{}
    foreach ($zeile in ($roh -split "`r?`n")) {
        if ($zeile -match "^([A-Za-z]+)=(.*)$") { $felder[$matches[1]] = $matches[2] }
    }
    $pidWert = 0
    if ($felder.ContainsKey("OwnerPid")) { [void][int]::TryParse($felder["OwnerPid"], [ref]$pidWert) }
    $label = ""
    if ($felder.ContainsKey("Label")) { $label = $felder["Label"] }
    $seit = ""
    if ($felder.ContainsKey("TakenAt")) { $seit = $felder["TakenAt"] }
    $rechner = ""
    if ($felder.ContainsKey("Host")) { $rechner = $felder["Host"] }
    return @{ Pid = $pidWert; Start = $felder["OwnerStart"]; Label = $label; Seit = $seit; Rechner = $rechner }
}

# Zustand: Frei | Verwaist | Eigen | Fremd
function Get-LockZustand([string]$Pfad, $Kette) {
    $l = Read-LockDatei $Pfad
    if ($null -eq $l) { return @{ Status = "Frei"; Lock = $null } }
    if ($l.Pid -le 0) { return @{ Status = "Verwaist"; Lock = $l } }
    $p = Get-Process -Id $l.Pid -ErrorAction SilentlyContinue
    if (-not $p) { return @{ Status = "Verwaist"; Lock = $l } }
    $start = ""
    try { $start = $p.StartTime.ToString("o") } catch { return @{ Status = "Verwaist"; Lock = $l } }
    if ($l.Start -and $l.Start -ne $start) { return @{ Status = "Verwaist"; Lock = $l } }
    if ($Kette.ContainsKey($l.Pid) -and $Kette[$l.Pid] -eq $start) { return @{ Status = "Eigen"; Lock = $l } }
    return @{ Status = "Fremd"; Lock = $l }
}

function Get-LockText($Zustand) {
    $l = $Zustand.Lock
    if ($null -eq $l) { return "frei" }
    return ("PID {0}, Label {1}, seit {2}, Rechner {3}" -f `
        $l.Pid, $l.Label, $(if ($l.Seit) { $l.Seit } else { "unbekannt" }), `
        $(if ($l.Rechner) { $l.Rechner } else { "unbekannt" }))
}

function Write-LockInhalt($Strom, [int]$OwnerPid, [string]$Label) {
    $start = ""
    $name = ""
    try {
        $p = Get-Process -Id $OwnerPid -ErrorAction SilentlyContinue
        if ($p) { $start = $p.StartTime.ToString("o"); $name = $p.ProcessName }
    } catch { }
    $zeilen = @(
        "LockVersion=1",
        "OwnerPid=$OwnerPid",
        "OwnerName=$name",
        "OwnerStart=$start",
        "Label=$Label",
        "TakenAt=$((Get-Date).ToString('yyyy-MM-dd HH:mm:ss'))",
        "Host=$env:COMPUTERNAME"
    )
    $bytes = [System.Text.Encoding]::ASCII.GetBytes(($zeilen -join "`r`n"))
    $Strom.Write($bytes, 0, $bytes.Length)
    $Strom.Flush()
}

# Der ELTERNprozess ist der Besitzer: der aufrufende Lauf (cmd.exe bzw.
# powershell.exe), nicht der kurzlebige PowerShell-Kindprozess, der nur die
# Sperre anlegt. Nur so ueberlebt der Lock die Engine-Sitzung.
function Get-Besitzer([string]$Label) {
    $zeile = Get-CimInstance Win32_Process -Filter ("ProcessId={0}" -f $PID) -ErrorAction SilentlyContinue
    if (-not $zeile) { return $PID }
    $elternPid = [int]$zeile.ParentProcessId
    if ($elternPid -le 0) { return $PID }
    Write-Host ("Lock: besetzt fuer Lauf '{0}' (Besitzer PID {1})." -f $Label, $elternPid)
    return $elternPid
}

function Sperre-Nehmen([string]$Pfad, [string]$Label, [int]$WarteSekunden) {
    $frist = (Get-Date).AddSeconds([Math]::Max($WarteSekunden, 0))
    $uebernahmen = 0
    while ($true) {
        $zustand = Get-LockZustand $Pfad (Get-ProzessKette $PID)
        if ($zustand.Status -eq "Eigen") {
            Write-Host ("Lock: von diesem Lauf bereits gehalten: {0}." -f (Get-LockText $zustand))
            return 0
        }
        if ($zustand.Status -eq "Fremd") {
            if ((Get-Date) -ge $frist) {
                Write-Host ("Lock: BELEGT durch einen anderen Lauf - {0}." -f (Get-LockText $zustand))
                Write-Host "Lock: dieser Lauf startet NICHT. Ein Beenden des Editors wuerde den"
                Write-Host "Lock: fremden Lauf mitten in der Sitzung zerstoeren (der Fehler aus dem"
                Write-Host "Lock: roten Push-Lauf vom 25.09.2026). Auf ihn warten, oder ihn bewusst"
                Write-Host "Lock: abbrechen (Fenster schliessen / Strg+C) - danach ist die Sperre"
                Write-Host "Lock: verwaist und wird automatisch uebernommen. Notausgaenge, beide"
                Write-Host "Lock: bewusst: Tools\engine_run_lock.cmd -Modus Freigeben -Gewalt und"
                Write-Host "Lock: Tools\cleanup_unreal_processes.cmd -SperreIgnorieren."
                return $ExitBelegt
            }
            Write-Host ("Lock: belegt durch {0} - warte auf Freigabe ..." -f (Get-LockText $zustand))
            Start-Sleep -Seconds 2
            continue
        }
        if ($zustand.Status -eq "Verwaist") {
            if ($uebernahmen -ge 3) {
                Write-Host ("Lock: weckert nicht auf - {0} (zurueckgegeben: {1}). Abbruch." -f `
                    (Get-LockText $zustand), $Pfad)
                return $ExitBelegt
            }
            $uebernahmen++
            Write-Host ("Lock: verwaist ({0}) - Sperre wird uebernommen." -f (Get-LockText $zustand))
            Remove-Item -LiteralPath $Pfad -Force -ErrorAction SilentlyContinue
        }
        # CreateNew schlaegt genau dann fehl, wenn die Datei schon existiert:
        # die Anlage selbst ist atomar, zwei Laeufe kommen nicht beide durch.
        try {
            $strom = [System.IO.File]::Open($Pfad, [System.IO.FileMode]::CreateNew, `
                [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
        } catch [System.IO.IOException] {
            continue
        }
        try {
            Write-LockInhalt $strom (Get-Besitzer $Label) $Label
        } finally {
            $strom.Dispose()
        }
        Write-Host ("Lock: gehalten - {0}." -f (Get-LockText (Get-LockZustand $Pfad (Get-ProzessKette $PID))))
        return 0
    }
}

function Sperre-Freigeben([string]$Pfad, [bool]$Gewalt) {
    $zustand = Get-LockZustand $Pfad (Get-ProzessKette $PID)
    if ($zustand.Status -eq "Frei") {
        Write-Host "Lock: nichts freizugeben."
        return 0
    }
    if ($zustand.Status -eq "Fremd" -and -not $Gewalt) {
        Write-Host ("Lock: fremder Lauf haelt ihn weiter - {0}. Nichts freigegeben." -f (Get-LockText $zustand))
        return $ExitBelegt
    }
    Remove-Item -LiteralPath $Pfad -Force -ErrorAction SilentlyContinue
    Write-Host ("Lock: freigegeben (war {0})." -f (Get-LockText $zustand))
    return 0
}

function Sperre-Status([string]$Pfad) {
    $zustand = Get-LockZustand $Pfad (Get-ProzessKette $PID)
    switch ($zustand.Status) {
        "Frei"     { Write-Host ("Lock: frei ({0})." -f $Pfad) }
        "Verwaist" { Write-Host ("Lock: verwaist - {0} lebt nicht mehr, Sperre wird beim naechsten Lauf uebernommen." -f (Get-LockText $zustand)) }
        "Eigen"    { Write-Host ("Lock: von diesem Lauf gehalten - {0}." -f (Get-LockText $zustand)) }
        "Fremd"    { Write-Host ("Lock: BELEGT durch einen anderen Lauf - {0}." -f (Get-LockText $zustand)) }
    }
    if ($zustand.Status -eq "Fremd") { return $ExitBelegt }
    return 0
}

$lockPfad = Get-LockDatei $LockPfad
switch ($Modus) {
    "Status"    { exit (Sperre-Status $lockPfad) }
    "Nehmen"    { exit (Sperre-Nehmen $lockPfad $Name $WarteSekunden) }
    "Freigeben" { exit (Sperre-Freigeben $lockPfad ([bool]$Gewalt)) }
    "Start" {
        $rc = Sperre-Nehmen $lockPfad $Name $WarteSekunden
        if ($rc -ne 0) { exit $rc }
        if ($DryRun) {
            & "$PSScriptRoot\cleanup_unreal_processes.ps1" -DryRun -LockPfad $lockPfad
        } else {
            & "$PSScriptRoot\cleanup_unreal_processes.ps1" -LockPfad $lockPfad
        }
        exit $LASTEXITCODE
    }
}
