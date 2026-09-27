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
#     ACHTUNG, das ist der NORMALZUSTAND nach jedem Lauf: der Besitzer ist der
#     aufrufende cmd.exe, der nach dem Lauf endet. Die Sperrdatei liegt also
#     planmaessig da und ist beim naechsten -Modus Status "verwaist". Das ist
#     kein Fehler und kein fremder Rechner, sondern genau der Entwurf.
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
# PLATTEN-GATE (27.09.2026)
#   Ein Engine-Start bricht ab, wenn die Platte unter der Grenze liegt. Wo
#   dieser Test sitzt und warum ausgerechnet hier - siehe Get-PlateFrei().
#   Dazu kommt -Modus Status / -Modus Freigeben: die lesen und loeschen nur,
#   sie starten nichts. Wer sie sperren wuerde, kann auf einer vollen Platte
#   seinen Lock nicht mehr loesen und sitzt fest.
#
# Exit: 0 = frei/eigen/verwaist/erfolgreich, 3 = Lock durch fremden Lauf belegt,
#       4 = Start abgebrochen, weil zu wenig Plattenplatz frei ist.
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
    [switch]$Gewalt,
    # Abbrechgrenze in Prozent frei fuer Start/Nehmen. 0 schaltet das Gate ab.
    [double]$PlattenGrenze = 10.0,
    # Notausgang: auch bei zu wenig Platz starten. Fuer den Fall, dass die
    # Platte wirklich voll ist und man den Editor braucht, um aufzuräumen.
    [switch]$PlattenTrotz,
    # Nur fuer den Selbsttest: freier Platz in GB fest vorgeben, statt das
    # Dateisystem zu fragen. Ohne diesen Schalter misst der Test die echte
    # Platte des Rechners - und ein Gruen waere dann Zufall.
    #
    # GEMESSEN am 27.09.2026: -1 ist hier KEIN "nicht messbar", sondern der
    # Wert, der "nicht gesetzt" bedeutet - der Selbsttest mit -PlattenTestGiga
    # -1 hat darum die echte Platte gemessen und 34 % gemeldet. Fuer den
    # nicht messbaren Fall gibt es deshalb einen eigenen Schalter.
    [double]$PlattenTestGiga = -1,
    # Nur fuer den Selbsttest: so tun, als koenne der Platz nicht gelesen
    # werden (fehlendes Laufwerk, keine Rechte).
    [switch]$PlattenTestNichtMessbar
)

$ErrorActionPreference = "Stop"
$ExitBelegt = 3
$ExitPlatte = 4

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

# PID -> Startzeit fuer diesen Prozess und seine Vorfahren (max. 16 Ebenen).
# Damit laesst sich ohne Absprache erkennen, ob der Lock-Besitzer zu diesem
# Lauf gehoert (Vater-/Sohnprozess) oder zu einem fremden.
#
# 16 statt 6: seit der Push-Hook den Lock VOR dem Worktree-Checkout nimmt, ist
# sein Python-Prozess der Besitzer, und die tiefste Abfrage liegt 6 Ebenen
# darunter (Cleanup -> cmd -> smoke_test -> build_release -> cmd ->
# vor_dem_commit -> Hook). Mit 6 Ebenen hielte sich der eigene Lauf fuer fremd.
function Get-ProzessKette([int]$StartPid) {
    $kette = @{}
    $aktuell = $StartPid
    for ($i = 0; $i -lt 16 -and $aktuell -gt 0; $i++) {
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
    # Ausgabe entzerren (27.09.2026). GEMESSEN: die Schleife unten fragt alle
    # 2 s ab und meldete JEDES Mal. Bei -WarteSekunden 900 sind das bis zu 450
    # Zeilen "Lock: belegt durch ... warte auf Freigabe ...", die den
    # eigentlichen Befund (welcher Lauf, wie lange noch) erschlagen.
    # Gesperrt wird die WIEDERHOLUNG, nicht die Information: der erste
    # Eintrag und jeder Wechsel des Besitzers kommen sofort - wer eine
    # Warteschleife sieht, will wissen, OB es Fortschritt gibt.
    # gate_worktree.py macht es seit langem genauso (naechste_meldung).
    $naechsteMeldung = Get-Date
    $letzterBesitzer = ""
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
            $besitzer = Get-LockText $zustand
            $jetzt = Get-Date
            if ($besitzer -ne $letzterBesitzer -or $jetzt -ge $naechsteMeldung) {
                $rest = [Math]::Max(($frist - $jetzt).TotalMinutes, 0)
                Write-Host ("Lock: belegt durch {0} - warte auf Freigabe ({1:N0} min Rest) ..." -f $besitzer, $rest)
                $naechsteMeldung = $jetzt.AddSeconds(60)
                $letzterBesitzer = $besitzer
            }
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

# Freier Plattenplatz in Prozent, oder -1, wenn nicht lesbar.
function Get-PlateFrei() {
    if ($PlattenTestNichtMessbar) { return -1 }
    if ($PlattenTestGiga -ge 0) {
        # Selbsttest: Festwert. Die Gesamtheit wird aus dem echten Laufwerk
        # genommen, damit die Prozentzahl im berechenbaren Bereich bleibt.
        $gdrive = Get-PSDrive -Name ($env:SystemDrive.TrimEnd(':')) -ErrorAction SilentlyContinue
        if (-not $gdrive) { return -1 }
        $gesamt = ($gdrive.Used + $gdrive.Free)
        if ($gesamt -le 0) { return -1 }
        return ($PlattenTestGiga * 1GB / $gesamt * 100.0)
    }
    # Laufwerk des PROJEKTS, nicht das Systemlaufwerk: der Lock ist
    # maschinenweit, das Projekt nicht. Auf einem zweiten Laufwerk waere
    # Systemlaufwerk die falsche Zahl.
    $wurzel = Split-Path -Parent $PSScriptRoot
    try {
        $d = [System.IO.DriveInfo]::new((Split-Path -Qualifier $wurzel))
        if (-not $d.IsReady) { return -1 }
        if ($d.TotalSize -le 0) { return -1 }
        return ($d.AvailableFreeSpace / $d.TotalSize * 100.0)
    } catch {
        return -1
    }
}

# Start abbrechen, wenn zu wenig Platz ist. Gibt Exit 0 zurueck, wenn der Lauf
# starten darf.
#
# WARUM IM LOCK UND NICHT IM COMMIT-HOOK: Tools/vor_dem_commit.py hat den
# Plattenhinweis ausdruecklich NICHT zum Gate gemacht, mit der richtigen
# Begruendung - wer Commits verweigert, weil der Rechner voll ist, umgeht das
# binnen eines Tages mit --no-verify. Fuer den Engine-Start gilt das nicht:
# ein abgebrochener Cook auf voller Platte schreibt unfertige Pakete, und der
# naechste Lauf macht es wieder. Das ist ein Fehler an der ARBEIT, nicht am
# Rechner - dafuer ist ein Gate das richtige Mittel.
#
# ZWEI GRUENZEN, NICHT EINE: der Waechter meldet ab 20 % (platten_waechter.
# py, GRENZE_PROZENT), das Gate bricht erst ab 10 % ab. Zwischen beiden liegt
# die Zone, in der man noch arbeiten kann - dort wird gemeldet, nicht geblockt.
function Teste-Plate([int]$ExitCode) {
    if ($PlattenGrenze -le 0) { return $ExitCode }
    if ($PlattenTrotz) {
        Write-Host "Lock: Platten-Gate uebergangen (-PlattenTrotz)."
        return $ExitCode
    }
    $frei = Get-PlateFrei
    if ($frei -lt 0) {
        # Nicht messbar heisst NICHT voll. Ein Gate, das im Zweifel blockiert,
        # ist ein Gate, das irgendwann den Rechner anhaelt - und ein
        # nicht lesbares Laufwerk ist eher ein Rechteproblem als ein Platzproblem.
        Write-Host "Lock: Plattenplatz nicht messbar - Start laeuft."
        return $ExitCode
    }
    if ($frei -ge $PlattenGrenze) {
        Write-Host ("Lock: Platten-Gate gruen ({0:N1} % frei, Grenze {1} %)." -f $frei, $PlattenGrenze)
        return $ExitCode
    }
    Write-Host ""
    Write-Host ("Lock: ABBRUCH - nur {0:N1} % frei, der Start braucht {1} %." -f $frei, $PlattenGrenze)
    Write-Host "Lock: ein Editor- oder Cook-Start auf dieser Platte bricht mitten im"
    Write-Host "Lock: Lauf ab und hinterlaesst unfertige Pakete. Der Waechter zeigt die"
    Write-Host "Lock: groessten Fresser:  Tools\platten_waechter.cmd --reinigen --trocken"
    Write-Host "Lock: Notausgang, wenn der Editor zum Aufraeumen gebraucht wird:"
    Write-Host "Lock:   engine_run_lock.cmd -Modus Start -Name <lauf> -PlattenTrotz"
    Write-Host ""
    return $ExitPlatte
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
        # DER SATZ WAR FALSCH ZUSAMMENGESETZT (27.09.2026). Er lautete
        # "... {0} lebt nicht mehr" mit Get-LockText als {0} - und der Text
        # endet auf "Rechner OMENBERT". Daraus las sich woertlich
        # "Rechner OMENBERT lebt nicht mehr", obwohl OMENBERT laeuft und der
        # ZURUECKGEBLIEBENE PROZESS stirbt. GEMESSEN mit einer Sperrdatei fuer
        # die tote PID 999999 auf diesem Rechner. Geprueft, ob die Erkennung
        # falsch anschlaegt: nein - mit einer Sperrdatei fuer einen LEBENDEN
        # Prozess meldet derselbe Aufruf "BELEGT durch einen anderen Lauf".
        # Es war allein der Satz. Das Prädikat gehoert deshalb VOR die
        # Angaben, und der Prozess wird ausdruecklich beim Namen benannt.
        "Verwaist" { Write-Host ("Lock: verwaist - der Prozess des Laufs lebt nicht mehr ({0})." -f (Get-LockText $zustand)) }
        "Eigen"    { Write-Host ("Lock: von diesem Lauf gehalten - {0}." -f (Get-LockText $zustand)) }
        "Fremd"    { Write-Host ("Lock: BELEGT durch einen anderen Lauf - {0}." -f (Get-LockText $zustand)) }
    }
    if ($zustand.Status -eq "Fremd") { return $ExitBelegt }
    return 0
}

$lockPfad = Get-LockDatei $LockPfad
switch ($Modus) {
    # Status und Freigeben laufen OHNE Platten-Gate. Beide starten nichts: sie
    # fragen nur ab bzw. loeschen. Ein Gate hier waere die schlimmste denkbare
    # Stelle - auf einer vollen Platte koennte man den eigenen Lock nicht mehr
    # loesen, und die Notausgaenge (Freigeben -Gewalt, cleanup -SperreIgnorieren)
    # waeren ebenfalls blockiert.
    "Status"    { exit (Sperre-Status $lockPfad) }
    "Freigeben" { exit (Sperre-Freigeben $lockPfad ([bool]$Gewalt)) }
    # Nehmen und Start pruefen VOR der Sperre. Wer erst sperrt und dann
    # abbricht, hinterlaesst eine Sperre, die erst der naechste Lauf als
    # verwaist weckt - der erste Abbruch wuerde sich also als "Lock belegt"
    # melden und nicht als Platznot.
    #
    # GEMESSEN am 27.09.2026, der Fehler, den die erste Fassung hatte:
    # `Teste-Plate (Sperre-Nehmen ...)` liest sich richtig, wertet aber BEIDES
    # aus - Sperre-Nehmen laeuft, legt die Datei an, und Teste-Plate
    # verwirft danach nur den Rueckgabewert. Der Abbruch hinterlaesst dann
    # eine Sperrdatei (gemessen: 1 Datei nach Exit 4). Der Test muss deshalb
    # ZWEI Schritte haben: erst pruefen, dann - nur bei 0 - sperren.
    "Nehmen"    {
        $rc = Teste-Plate 0
        if ($rc -ne 0) { exit $rc }
        exit (Sperre-Nehmen $lockPfad $Name $WarteSekunden)
    }
    "Start" {
        $rc = Teste-Plate 0
        if ($rc -ne 0) { exit $rc }
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
