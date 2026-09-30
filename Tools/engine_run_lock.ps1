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
#   * ABER verwaist heisst "der Prozessbaum ist LEER", nicht nur "der Treiber
#     ist tot". Stirbt der Besitzer, waehrend seine Kinder weiterarbeiten,
#     lautet der Zustand NACHKOMMEN und die Sperre gilt als BELEGT.
#     GEMESSEN am 28.09.2026 um 03:36: die Sperrdatei gehoerte zur toten PID
#     38032 (Label push_gate), waehrend im Hauptbaum dotnet.exe und vier cl.exe
#     kompilierten. Ohne diese Unterscheidung uebernimmt der naechste Lauf in
#     einen beschaeftigten Baum: zwei Builds in einem Verzeichnis (LNK1104,
#     weil der andere die Modul-DLL offenhaelt - genau so passiert) und ein
#     cleanup, das den noch arbeitenden Editor als Rest erschlaegt.
#     Sobald der letzte Nachkomme endet, ist der Zustand wieder verwaist: die
#     Sperre heilt sich selbst, es wird kein neuer Zustand gespeichert.
#   * Gehoert der Besitzer zur eigenen Prozesskette (Vater/Sohn desselben Laufs,
#     z. B. Gate -> smoke_test -> Cleanup), ist die Sperre EIGEN: der Lauf darf
#     beenden. Reentrant, sonst wuerde sich das Gate selbst blockieren.
#   * PID-Wiederverwendung: neben dem PID wird die Startzeit gespeichert und
#     beim Pruefen verglichen - ein recycelter PID gilt nicht als Besitzer.
#
# MODI
#   Start     Lock nehmen und danach die Prozessbereinigung ausfuehren. Das ist
#             der Aufruf fuer die Batch-Wrapper (ersetzt den cleanup-Aufruf 1:1).
#             SEIT 28.09.2026 WARTET START AUF EINEN BELEGTEN LOCK, statt ihn
#             sofort als Exit 3 zu melden (-StartWarteSekunden, Vorgabe 1800 s,
#             eine Meldung je Minute). Die Wrapper bringen keine eigene Frist
#             mit - ein fremder Lauf gibt die Sperre aber mit seinem Ende von
#             selbst wieder frei, warten ist also aussichtsreich. Ein
#             AUSDRUECKLICH gesetztes -WarteSekunden gewinnt (0 = sofort).
#   Nehmen    nur den Lock holen (PowerShell-Wrapper, die mehrere Sitzungen
#             fahren und den Lock ueber den ganzen Lauf halten). Wartet NUR so
#             lange, wie -WarteSekunden es sagt (Vorgabe 0): motor_sperre in
#             gate_worktree.py bringt hier seine EIGENE Frist mit und wartet
#             selbst - ein zweites Warten verdoppelte sie still.
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
#   SEIT 28.09.2026 WARTET -Modus START AUF PLATZ, statt sofort abzubrechen
#   (-PlattenWarteSekunden, Vorgabe 1800 s): ein Raeumlauf oder ein endender
#   Cook gibt Platz frei, ein Abbruch kostet dagegen den ganzen Lauf.
#   -Modus Nehmen wartet NICHT - motor_sperre (gate_worktree.py) faehrt ihn
#   und bringt seine eigene Frist mit. Die Begruendung steht in Teste-Plate().
#
# VORREINIGUNG DER CACHES (30.09.2026)
#   Unter -PlattenReinigungsGrenze Prozent frei (Vorgabe 20 = die Meldegrenze
#   des Waechters) raeumt der Start ZUERST die reproduzierbaren Caches der
#   Klasse `cache` und nimmt ERST DANACH den Lock. Das ist die dritte Antwort
#   des Gates neben Warten und Abbrechen - und die einzige, die selbst Platz
#   schafft (gemessen 29.09.2026: 11 GB, die den naechsten Push retteten).
#   Angefasst werden NUR Caches: abgeleitete Kopien, die sich beim naechsten
#   Cook neu rechnen. Die Klasse `ausgabe` kostet Bauzeit und die Klasse
#   `dev-builds` traegt die Beweise des fremden Threads - beide entscheidet
#   kein Start, der nur zu wenig Platz hat (docs/plattenstrategie.md).
#   Besonderheiten: die Reinigung laeuft EINMAL je Lauf, nicht in der
#   Warteschleife; -PlattenTrotz und -PlattenGrenze 0 schalten sie mit ab;
#   -PlattenReinigungsGrenze 0 schaltet nur sie ab; ein fehlender Waechter,
#   fehlendes python oder ein Abbruch des Raeumlaufs ist FAIL-OPEN (eine
#   Zeile im Log, das Gate laeuft unveraendert weiter). Und: eine ATTRAPPEN-
#   Messung (-PlattenTestGiga/-PlattenTestReihe) loescht nie die echte Platte.
#   -Modus Status und -Modus Freigeben raeumen NIE - sie fassen die Platte
#   nicht einmal an (Begruendung unten im switch).
#
# Exit: 0 = frei/eigen/verwaist/erfolgreich, 3 = Lock durch fremden Lauf belegt
#         - auch NACHKOMMEN: der Besitzer ist tot, seine Kindprozesse arbeiten
#         aber weiter; dann wird weder uebernommen noch etwas beendet,
#       4 = Start abgebrochen, weil zu wenig Plattenplatz frei ist.
[CmdletBinding()]
param(
    [ValidateSet("Start", "Nehmen", "Freigeben", "Status")]
    [string]$Modus = "Status",
    # Kurzname des Laufs - steht in der Meldung, die ein wartender Lauf sieht.
    [string]$Name = "unbekannt",
    # Sekunden, die ein belegter Lock erwartet wird (0 = sofort abbrechen).
    #
    # FUER -Modus NEHMEN BLEIBT DIE NULL DIE VORGABE - das ist eine Falle, kein
    # Versehen: der Primaeraufrufer ist motor_sperre (gate_worktree.py), und der
    # uebergibt -WarteSekunden NICHT. Er verlaesst sich auf diese Null, bricht
    # selbst ab und wartet in eigenen 15-s-Takten bis zu SEINER Frist
    # (WB_GATE_LOCK_WARTEN, sonst 3600 s). Eine Vorgabe groesser 0 hier
    # verdoppelte still die Frist jedes Push-Laufs.
    [int]$WarteSekunden = 0,
    # WIE LANGE -Modus START AUF EINEN BELEGTEN LOCK WARTET.
    #
    # WARUM DIE FRIST HIER STEHT UND NICHT IM AUFRUFER: Starts Aufrufer sind
    # die .cmd-Wrapper (31 Stueck), und die bringen keine Frist mit. Sie haben
    # den belegten Lock bisher sofort als Exit 3 gemeldet bekommen und mussten
    # von Hand neu gestartet werden - obwohl ein zu Ende gehender fremder Lauf
    # die Sperre mit seinem Prozess von selbst freigibt. Das ist dieselbe
    # Ueberlegung wie beim Platten-Gate: gewartet wird nur, wo sich die Lage
    # AENDERN kann, und nur auf dem Weg, den der Aufrufer nicht selbst
    # absichern kann.
    #
    # Wirkt nur, solange -WarteSekunden NICHT ausdruecklich gesetzt wurde.
    # Ein ausdrueckliches -WarteSekunden 0 holt das alte Verhalten zurueck.
    [int]$StartWarteSekunden = 1800,
    # Abweichender Lock-Pfad. Leer = %LOCALAPPDATA%\WiesbadenReal\Locks\engine_run.lock
    # (MASCHINENweit, nicht pro Projekt: zwei Sitzungen teilen sich die Engine).
    # Ein gesetzter Pfad gilt als PROBE: die Vorreinigung raeumt dann nur mit
    # ausdruecklichem -PlattenReinigerSkript (die Selbsttests fahren so gegen
    # einen Test-Lock bei echter Platte - und eine Probe loescht nichts).
    [string]$LockPfad = "",
    # Nur fuer Start: Ziele der Bereinigung zeigen, nichts beenden.
    [switch]$DryRun,
    # Nur fuer Freigeben: auch den Lock eines noch lebenden fremden Laufs nehmen.
    [switch]$Gewalt,
    # Abbrechgrenze in Prozent frei fuer Start/Nehmen. 0 schaltet das Gate ab.
    # (0 schaltet das ganze Platten-Gate ab - samt Vorreinigung.)
    [double]$PlattenGrenze = 10.0,
    # Notausgang: auch bei zu wenig Platz starten. Fuer den Fall, dass die
    # Platte wirklich voll ist und man den Editor braucht, um aufzuräumen.
    [switch]$PlattenTrotz,
    # WIE LANGE EIN START AUF PLATZ WARTET, statt sofort abzubrechen.
    #
    # GEMESSEN am 28.09.2026: die Python-Suiten eines Push-Laufs kippten, weil
    # Exit 4 wie ein endgueltiger Fehler gelesen wurde - dieselbe Platte hatte
    # zwanzig Minuten spaeter wieder Platz. Ein Raeumlauf oder ein endender
    # Cook heilt das; ein Abbruch verliert stattdessen die ganze Arbeit.
    # 0 = sofort abbrechen (das Verhalten vor dem 28.09.2026).
    #
    # Nur -Modus Start wartet (Begruendung in Teste-Plate()), und nur auf eine
    # Messung, die sich AENDERN kann - ein fester -PlattenTestGiga heilt nie.
    [int]$PlattenWarteSekunden = 1800,
    # VOR DEM LOCK: unter dieser Grenze in Prozent frei werden die
    # reproduzierbaren Caches geraeumt (Vorgabe 20.0 = GRENZE_PROZENT des
    # Waechters, die Meldegrenze). 0 schaltet die Vorreinigung ab; mit
    # -PlattenGrenze 0 oder -PlattenTrotz ist sie ohnehin aus.
    #
    # WARUM 20 UND NICHT 10: die 10 % sind die Notbremse, nicht der
    # Arbeitspunkt. Zwischen 10 und 20 liegt die Zone, in der der Waechter
    # meldet - dort ist Raeumen billig und Warten teuer. Und wer erst bei
    # 10 % raeumt, raeumt im Abbruchfall, also zu spaet.
    [double]$PlattenReinigungsGrenze = 20.0,
    # Nur fuer den Selbsttest: anderes Raeum-Skript als
    # Tools\platten_waechter.py (Vorgabe aus $PSScriptRoot). In einer
    # Attrappen-Messung wird NUR mit gesetztem Ersatzskript geraeumt - eine
    # erfundene Plattenzahl darf keine echten Dateien loeschen.
    [string]$PlattenReinigerSkript = "",
    # Nur fuer den Selbsttest: freier Platz in GB fest vorgeben, statt das
    # Dateisystem zu fragen. Ohne diesen Schalter misst der Test die echte
    # Platte des Rechners - und ein Gruen waere dann Zufall.
    #
    # GEMESSEN am 27.09.2026: -1 ist hier KEIN "nicht messbar", sondern der
    # Wert, der "nicht gesetzt" bedeutet - der Selbsttest mit -PlattenTestGiga
    # -1 hat darum die echte Platte gemessen und 34 % gemeldet. Fuer den
    # nicht messbaren Fall gibt es deshalb einen eigenen Schalter.
    [double]$PlattenTestGiga = -1,
    # Nur fuer den Selbsttest: freier Platz in GB als REIHE (Komma getrennt),
    # je Messung der naechste Wert, der letzte gilt weiter. Damit ist ein
    # HEILENDER Platz pruefbar (0.5,400) - ein fester Wert kann das nicht, und
    # genau deshalb wartet das Gate auf einen festen Wert nie.
    [string]$PlattenTestReihe = "",
    # Nur fuer den Selbsttest: so tun, als koenne der Platz nicht gelesen
    # werden (fehlendes Laufwerk, keine Rechte).
    [switch]$PlattenTestNichtMessbar
)

$ErrorActionPreference = "Stop"
$ExitBelegt = 3
$ExitPlatte = 4

# Zaehler fuer -PlattenTestReihe (Selbsttest): je Messung der naechste Wert.
$script:PlattenReiheIdx = 0

# Wurde -WarteSekunden AUSDRUECKLICH mitgegeben? Die Abfrage gehoert hier oben
# hin: in einer Funktion meint $PSBoundParameters die Parameter DER FUNKTION -
# ein Aufruf dort waere still immer falsch ('nicht gesetzt'), und Start wartete
# nie auf einen ausdruecklich verlangten Abbruch.
$script:WarteSekundenGesetzt = $PSBoundParameters.ContainsKey("WarteSekunden")

# Wurde -LockPfad AUSDRUECKLICH mitgegeben? Dieselbe Ueberlegung wie oben, nur
# schaerfer: weiter unten heisst die AUFGELOESTE Sperrdatei genauso
# (`$lockPfad = Get-LockDatei $LockPfad`). PowerShell vergleicht Variablennamen
# OHNE Ruecksicht auf Gross- und Kleinschreibung - der Parameter ist nach dem
# Aufloesen also IMMER gesetzt (GEMESSEN am 30.09.2026: die Probe-Regel griff
# dadurch auch im echten Lauf, und die Vorreinigung fiel lautlos aus). Fuer
# Raeume-Caches zaehlt deshalb dieser Schnappschuss von VORHER.
$script:LockPfadGesetzt = ($PSBoundParameters.ContainsKey("LockPfad") -and [bool]$LockPfad)

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
    # Die Kette: je Eintrag "PID|Startzeit". Fehlt sie (Sperrdatei einer
    # aelteren Fassung), bleibt das Feld leer und es gilt weiter die Einzel-PID.
    $kette = @()
    if ($felder.ContainsKey("Kette") -and $felder["Kette"]) { $kette = @($felder["Kette"] -split ";") }
    return @{ Pid = $pidWert; Start = $felder["OwnerStart"]; Label = $label; Seit = $seit; Rechner = $rechner; Kette = $kette }
}

# Lebende Nachkommen eines (vermutlich toten) Besitzers.
#
# WOZU: Besitzer ist der TREIBER eines Laufs. Stirbt er - Sitzung beendet,
# abgebrochen, abgestuerzt -, arbeiten seine Kinder oft weiter. Ohne diese
# Abfrage meldet Get-LockZustand "verwaist", der naechste Lauf uebernimmt, und
# dann laufen zwei Builds in einem Baum bzw. cleanup_unreal_processes.ps1
# erschlaegt den noch arbeitenden Editor. Der Befund ist gemessen, nicht
# angenommen: 28.09.2026, tote Besitzer-PID, im Hauptbaum kompilierte dotnet.exe
# mit vier cl.exe weiter.
#
# Nur im Zweig "Besitzer tot" aufrufen, nie im Normalfall: die Abfrage zieht die
# ganze Prozesstabelle und hat im 2-s-Takt der Warteschleife nichts zu suchen.
#
# PID-WIEDERVERWENDUNG: der gefaehrlichste Fall. Eine Waisenbeziehung steht
# nur als ZAHL in der Kindliste - dass die PID frueher unserem Besitzer gehoerte
# und heute einem fremden Prozess, ist daran nicht zu sehen. Zwei Mittel:
#   * $ErstAb schneidet die ERSTE Ebene: ist die Besitzer-PID nachweislich neu
#     vergeben (Startzeit passt nicht), kann ein Kind, das erst nach dem Start
#     des neuen Inhabers entstanden ist, nicht von unserem toten Besitzer
#     stammen. Ohne diesen Schnitt sieht jeder neue PID-Inhaber mit Kindern wie
#     eine lebende fremde Sitzung aus.
#   * Bleibt eine Waisenbeziehung mehrdeutig (PID neu vergeben UND wieder
#     gestorben), wird sie weiter als Nachkomme gezaehlt. Das ist die SICHEre
#     Richtung - es wird nie laufende Arbeit zerstoert -, es heilt sich mit dem
#     Prozessbaum, und beide Notausgaenge bleiben: -Modus Freigeben -Gewalt und
#     cleanup_unreal_processes.cmd -SperreIgnorieren. Aus der Prozesstabelle
#     ist dieser Rest nicht entscheidbar, das ist keine Luecke in der Abfrage.
function Get-Nachkommen([int]$OwnerPid, [int]$MaxTiefe = 16, $ErstAb = $null) {
    if ($OwnerPid -le 0) { return @() }
    $alle = @(Get-CimInstance Win32_Process -ErrorAction SilentlyContinue)
    if ($alle.Count -eq 0) { return @() }
    $kinder = @{}
    foreach ($p in $alle) {
        $vater = [int]$p.ParentProcessId
        if (-not $kinder.ContainsKey($vater)) { $kinder[$vater] = @() }
        $kinder[$vater] += $p
    }
    # Breitensuche ueber die Kindverknuepfungen. $gesehen verhindert, dass eine
    # wiederverwendete PID eine Schleife erzeugt.
    $gefunden = @()
    $gesehen = @{}
    $stufe = @($OwnerPid)
    for ($t = 0; $t -lt $MaxTiefe -and $stufe.Count -gt 0; $t++) {
        $naechste = @()
        foreach ($vater in $stufe) {
            if ($gesehen.ContainsKey($vater)) { continue }
            $gesehen[$vater] = $true
            if (-not $kinder.ContainsKey($vater)) { continue }
            foreach ($kind in $kinder[$vater]) {
                # Recycle-Schnitt, nur auf der ERSTEN Ebene: ein Kind des neuen
                # PID-Inhabers entstand nach dessen Start, ein Kind unseres
                # toten Besitzers davor.
                if ($t -eq 0 -and $null -ne $ErstAb) {
                    $kindStart = $null
                    try {
                        $kindStart = (Get-Process -Id ([int]$kind.ProcessId) -ErrorAction Stop).StartTime
                    } catch { $kindStart = $null }
                    if ($null -ne $kindStart -and $kindStart -ge $ErstAb) { continue }
                }
                $gefunden += $kind
                $naechste += [int]$kind.ProcessId
            }
        }
        $stufe = $naechste
    }
    return $gefunden
}

# Toter Besitzer: VERWAIST (Baum leer, Sperre darf uebernommen werden) oder
# NACHKOMMEN (Treiber tot, Kinder arbeiten weiter - nichts uebernehmen, nichts
# beenden). Der Unterschied ist der ganze Punkt dieser Abfrage.
function Get-ToterBesitzerZustand($Lock, $ErstAb = $null, [switch]$Recycled) {
    $nach = @(Get-Nachkommen ([int]$Lock.Pid) 16 $ErstAb)
    if ($nach.Count -gt 0) { return @{ Status = "Nachkommen"; Lock = $Lock; Kinder = $nach } }
    return @{ Status = "Verwaist"; Lock = $Lock; Kinder = @(); Recycled = [bool]$Recycled }
}

# Namen der lebenden Nachkommen fuer die Meldung: der Bediener soll sehen,
# WER da noch arbeitet, nicht nur dass jemand arbeitet.
function Get-KinderText($Zustand) {
    if (-not $Zustand.ContainsKey("Kinder") -or $null -eq $Zustand.Kinder) { return "keine" }
    $kinder = @($Zustand.Kinder | Where-Object { $_ })
    if ($kinder.Count -eq 0) { return "keine" }
    $namen = @($kinder | ForEach-Object { "{0}({1})" -f $_.Name, $_.ProcessId })
    return ("{0}: {1}" -f $kinder.Count, ($namen -join ", "))
}

# Zustand: Frei | Verwaist | Nachkommen | Eigen | Fremd
function Get-LockZustand([string]$Pfad, $Kette) {
    $l = Read-LockDatei $Pfad
    if ($null -eq $l) { return @{ Status = "Frei"; Lock = $null } }
    # Ohne PID in der Datei gibt es keinen Baum, den man absuchen koennte.
    if ($l.Pid -le 0) { return @{ Status = "Verwaist"; Lock = $l } }
    $p = Get-Process -Id $l.Pid -ErrorAction SilentlyContinue
    if (-not $p) { return (Get-ToterBesitzerZustand $l) }
    $start = ""
    try { $start = $p.StartTime.ToString("o") } catch { return (Get-ToterBesitzerZustand $l) }
    # Startzeit passt nicht: die PID wurde neu vergeben, der eingetragene
    # Besitzer ist tot. Der lebende Inhaber ist ein ANDERER Prozess - seine
    # nach seinem Start entstandenen Kinder sind nicht unsere Nachkommen
    # (ErstAb-Schnitt). Echte Waisen des toten Besitzers zaehlen weiter, und
    # dass die PID neu vergeben ist, wird gemeldet statt verschwiegen.
    if ($l.Start -and $l.Start -ne $start) {
        return (Get-ToterBesitzerZustand $l -ErstAb $p.StartTime -Recycled)
    }
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

# Die ganze Prozesskette des Besitzers: Besitzer zuerst, dann seine Vorfahren,
# je als "PID|Startzeit".
#
# WOZU: OwnerPid allein ist eine Zahl, die sich wiederverwenden laesst; PID UND
# Startzeit zusammen identifizieren einen Prozess eindeutig. Die Kette macht
# daraus einen belastbaren Besitz-EINTRAG statt einer einzelnen Zahl - sie
# benennt im Streitfall, welcher Lauf gemeint war, und sie ist die Grundlage
# fuer den Recycle-Schnitt in Get-Nachkommen (eine Sperrdatei ohne Kette laeuft
# unveraendert weiter und nutzt weiter die Einzel-PID).
function Get-KettenZeilen([int]$StartPid, [int]$MaxTiefe = 16) {
    $zeilen = @()
    $aktuell = $StartPid
    for ($i = 0; $i -lt $MaxTiefe -and $aktuell -gt 0; $i++) {
        $start = ""
        try {
            $p = Get-Process -Id $aktuell -ErrorAction SilentlyContinue
            if (-not $p) { break }
            $start = $p.StartTime.ToString("o")
        } catch { break }
        $zeilen += ("{0}|{1}" -f $aktuell, $start)
        $zeile = Get-CimInstance Win32_Process -Filter ("ProcessId={0}" -f $aktuell) -ErrorAction SilentlyContinue
        if (-not $zeile) { break }
        $aktuell = [int]$zeile.ParentProcessId
    }
    return $zeilen
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
        "Host=$env:COMPUTERNAME",
        "Kette=$((Get-KettenZeilen $OwnerPid) -join ';')"
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
    $beginn = Get-Date
    $frist = $beginn.AddSeconds([Math]::Max($WarteSekunden, 0))
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
        # NACHKOMMEN verhaelt sich wie Fremd: der Treiber ist tot, aber es
        # arbeitet noch jemand. Warten statt uebernehmen, nichts loeschen - und
        # vor allem die Prozessbereinigung nicht auf einen beschaeftigten Baum
        # loslassen.
        if ($zustand.Status -eq "Fremd" -or $zustand.Status -eq "Nachkommen") {
            # Beim Nachkommen-Fall eine Zeile mehr: "belegt" allein laesst den
            # Bediener einen lebenden fremden Lauf vermuten, und dann sucht er
            # nach einem Fenster, das es nicht gibt. Wer hier wartet, wartet auf
            # Kindprozesse eines toten Treibers.
            if ($zustand.Status -eq "Nachkommen") {
                Write-Host ("Lock: der Besitzer lebt nicht mehr, es arbeitet aber noch: {0}." -f (Get-KinderText $zustand))
            }
            if ((Get-Date) -ge $frist) {
                $verstrichen = [int]((Get-Date) - $beginn).TotalSeconds
                Write-Host ("Lock: BELEGT durch einen anderen Lauf - {0}." -f (Get-LockText $zustand))
                # Nur wenn wirklich gewartet wurde: bei -WarteSekunden 0 (der
                # Weg von motor_sperre) waere die Zeile sinnloses Rauschen.
                if ($verstrichen -ge 1) {
                    Write-Host ("Lock: {0} s auf die Freigabe gewartet - sie blieb bis zur Frist belegt." -f $verstrichen)
                    Write-Host "Lock: sofort abbrechen statt warten: -WarteSekunden 0"
                }
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
            # ATOMAR UEBERNEHMEN: die verwaiste Datei wird WEGBEWEGT, nicht
            # geloescht. Remove-Item + CreateNew war ein Rennfenster, und seit
            # die .cmd-Wrapper auf einen belegten Lock WARTEN (28.09.2026),
            # stehen mehrere Uebernehmer gleichzeitig an derselben Datei.
            # GEMESSEN am 28.09.2026, 20:21:53: mein Kommandolet-Lauf und eine
            # fremde Bugtank-Pruefung uebernahmen im selben Moment; weil der
            # zweite Remove-Item die FRISCHE Sperre des ersten wegraeumte,
            # liefen beide - Editor-Target-Neubau neben fremdem Editorlauf,
            # genau das, was die Sperre verhindern soll.
            # Move-Item auf dieselbe Platte ist atomar: nur EINER holt die
            # alte Datei weg, der andere faellt in den catch und fragt neu ab
            # (dann gehoert die Sperre dem Gewinner).
            $weggeraumt = "{0}.verwaist-{1}" -f $Pfad, [Guid]::NewGuid().ToString("N")
            try {
                Move-Item -LiteralPath $Pfad -Destination $weggeraumt -ErrorAction Stop
            } catch {
                Start-Sleep -Milliseconds 200
                continue
            }
            Remove-Item -LiteralPath $weggeraumt -Force -ErrorAction SilentlyContinue
            if ($uebernahmen -ge 3) {
                Write-Host ("Lock: weckert nicht auf - {0} (zurueckgegeben: {1}). Abbruch." -f `
                    (Get-LockText $zustand), $Pfad)
                return $ExitBelegt
            }
            $uebernahmen++
            Write-Host ("Lock: verwaist ({0}) - Sperre wird uebernommen." -f (Get-LockText $zustand))
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

# GB-Vorgabe (Selbsttest) in Prozent. Die Gesamtheit kommt aus dem echten
# Laufwerk, damit die Prozentzahl im berechenbaren Bereich bleibt.
function Get-ProzentAusGiga([double]$giga) {
    $gdrive = Get-PSDrive -Name ($env:SystemDrive.TrimEnd(':')) -ErrorAction SilentlyContinue
    if (-not $gdrive) { return -1 }
    $gesamt = ($gdrive.Used + $gdrive.Free)
    if ($gesamt -le 0) { return -1 }
    return ($giga * 1GB / $gesamt * 100.0)
}

# Freier Plattenplatz in Prozent, oder -1, wenn nicht lesbar.
function Get-PlateFrei() {
    if ($PlattenTestNichtMessbar) { return -1 }
    if ($PlattenTestReihe) {
        # Selbsttest-REIHE: je Messung der naechste Wert, der letzte gilt weiter.
        # Ein fester Wert kann nicht heilen, eine Reihe schon - deshalb ist sie
        # der einzige Selbsttest-Weg, auf dem das Warten messbar ist.
        $werte = @($PlattenTestReihe -split "," | ForEach-Object { [double]$_.Trim() })
        if ($werte.Count -eq 0) { return -1 }
        $i = [Math]::Min($script:PlattenReiheIdx, $werte.Count - 1)
        $script:PlattenReiheIdx = $script:PlattenReiheIdx + 1
        if ($werte[$i] -lt 0) { return -1 }
        return (Get-ProzentAusGiga $werte[$i])
    }
    if ($PlattenTestGiga -ge 0) {
        return (Get-ProzentAusGiga $PlattenTestGiga)
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

# Reproduzierbare Caches raeumen, BEVOR der Lock genommen wird.
#
# GIBT DEN PLATTENSTAND NACH DEM RAEUMEN ZURUECK (Prozent) oder -1, wenn
# nichts geraeumt wurde. -1 hiesse auch: nicht messbar - dann gilt weiter die
# erste Messung, und das Gate entscheidet wie vorher (warten oder abbrechen).
#
# WARUM DAS HIER STEHT: das Platten-Gate kannte bisher nur zwei Antworten -
# warten oder abbrechen. Der Waechter kann aber selbst Platz schaffen, und die
# Cache-Klasse ist genau dafuer da: Zen-DDC, Shader-DDC, npm/pip/gradle-Caches
# sind abgeleitete Kopien, sie rechnen sich beim naechsten Cook neu
# (docs/plattenstrategie.md). GEMESSEN am 29.09.2026: 10,2 % frei, 0 % Luft bis
# zur Abbruchgrenze - dieselbe Reinigung holte 11 GB und rettete den naechsten
# Push; seit dem 30.09.2026 holt sie dieser Aufruf von selbst.
#
# WARUM VOR DEM LOCK UND NICHT DANACH: das Gate sitzt vor Sperre-Nehmen, und
# genau dort faellt der Abbruch (Exit 4). Nach dem Lock waere ein Abbruch
# ausserdem teurer - er hinterlaesst eine Sperrdatei, die erst der naechste
# Lauf als verwaist weckt (gemessen 27.09.2026: 1 Datei nach Exit 4).
#
# NUR DIE KLASSE `cache`: `--auch-ausgabe` und `--auch-devbuilds` werden
# bewusst NICHT mitgegeben. Ausgaben kosten Bauzeit (Vollbuild, gemessen
# 104 s -> 181 s), dev-builds sind die Beweise des fremden Threads - beides
# entscheidet kein Start, der nur zu wenig Platz hat. Fuer die Ausgabe gibt es
# die Stufe 2 in vor_dem_commit (unter 14 %, mit Zeitkosten-Warnung im
# Gate-Log), und fuer dev-builds fehlt bis heute die Zustimmung.
#
# FAIL-OPEN: fehlt das Skript, fehlt python oder endet der Raeumlauf mit einem
# Fehler, steht EINE Zeile im Log und das Gate laeuft unveraendert weiter. Ein
# Raeumwerkzeug, das einen Start verhindert, waere schlimmer als die volle
# Platte - dieselbe Haltung wie beim Plattenhinweis in vor_dem_commit.
#
# WARUM EINE PROBE NICHTS LOESCHT: der Selbsttest faehrt Platzreihen
# (0.5,400 = erst zu voll, dann Heilung) UND einen umgeleiteten Lock (-LockPfad,
# Sperrdatei in %TEMP%). Eine erfundene Zahl darf keine echten Dateien anfassen,
# und ein Test-Lock auf einer echten Platte erst recht nicht - sonst raeumt ein
# Testlauf dem Bediener die Caches weg (gemessen: 21 solche Aufrufe in
# test_gate_worktree.py). Geraeumt wird in der Probe deshalb nur mit
# ausdruecklichem -PlattenReinigerSkript: die Selbsttests fahren damit denselben
# Weg, aber mit einem Ersatz-Waechter, der nichts loescht.
function Raeume-Caches([double]$Frei) {
    if ($PlattenReinigungsGrenze -le 0) { return -1 }
    if ($Frei -lt 0 -or $Frei -ge $PlattenReinigungsGrenze) { return -1 }

    # EINE PROBE LOESCHT NICHTS - und Probe ist zweierlei:
    #   * die ATTRAPPEN-Messung (-PlattenTestGiga/-PlattenTestReihe): eine
    #     erfundene Plattenzahl darf keine echten Dateien anfassen;
    #   * der UMGELETETE LOCK (-LockPfad): die Suite faehrt dann gegen eine
    #     Sperrdatei in %TEMP%, waehrend die Platte des Rechners die ECHTE
    #     bleibt. GEMESSEN am 30.09.2026: genau diese Mischung steht in
    #     test_gate_worktree.py (21 Aufrufe mit -LockPfad), und ohne diese
    #     Regel raeumte ein Testlauf dem Bediener die Caches weg.
    # Geraeumt wird in der Probe nur mit ausdruecklichem -PlattenReinigerSkript
    # - so fahren die Selbsttests den echten Weg mit einem Ersatz-Waechter, der
    # nichts loescht.
    $probe = ($PlattenTestGiga -ge 0) -or [bool]$PlattenTestReihe -or $script:LockPfadGesetzt
    if ($probe -and -not $PlattenReinigerSkript) {
        Write-Host (("Lock: nur {0:N1} % frei (Vorreinigung ab {1:N1} %): eine Probe loescht nichts (Attrappen-Messung oder umgeleiteter Lock; dafuer gibt es -PlattenReinigerSkript).") -f $Frei, $PlattenReinigungsGrenze)
        return -1
    }

    $skript = $PlattenReinigerSkript
    if (-not $skript) { $skript = Join-Path $PSScriptRoot "platten_waechter.py" }
    Write-Host (("Lock: nur {0:N1} % frei (Vorreinigung ab {1:N1} %) - die reproduzierbaren Caches werden geraeumt, BEVOR der Lock genommen wird.") -f $Frei, $PlattenReinigungsGrenze)
    if (-not (Test-Path -LiteralPath $skript)) {
        Write-Host ("Lock: Vorreinigung nicht ausgefuehrt - {0} fehlt (fail-open)." -f $skript)
        return -1
    }

    $beginn = Get-Date
    $roh = @()
    # $ErrorActionPreference = "Stop" steht GANZ OBEN in diesem Skript, und eine
    # native Ausgabe auf stderr WIRFT darunter - auch mit 2>&1 (GEMESSEN
    # 30.09.2026: try/catch faengt sie ab, die Meldung ist dann leer). Der
    # Waechter schreibt seinen Bericht nach stdout, eine einzelne Warnzeile
    # darf aber nicht die ganze Vorreinigung als "nicht ausgefuehrt" ausfallen
    # lassen. Deshalb ausdruecklich "Continue" - nur fuer diesen Aufruf.
    $alt = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        # Nur --reinigen, ohne --auch-ausgabe/--auch-devbuilds: die Vorgabe des
        # Waechters ist die Klasse `cache`, und die soll es bleiben.
        $roh = @(& python $skript --reinigen 2>&1)
        $code = $LASTEXITCODE
    } catch {
        Write-Host ("Lock: Vorreinigung nicht ausgefuehrt ({0}) - fail-open." -f $_.Exception.Message)
        return -1
    } finally {
        $ErrorActionPreference = $alt
    }
    $gedauert = [int]((Get-Date) - $beginn).TotalSeconds

    # Aus dem langen Bericht des Waechters nur die Zeilen, die den Eingriff
    # belegen - und die Zeile, die sagt, was der naechste Cook bezahlt.
    foreach ($zeile in @($roh | Where-Object { $_ -match "^(GELOESCHT|zusammen|Protokoll)" })) {
        Write-Host ("Lock:   {0}" -f ([string]$zeile).Trim())
    }
    if ($code -ne 0) {
        Write-Host ("Lock: Vorreinigung endete mit Code {0} - das Gate laeuft unveraendert weiter. Letzte Zeilen:" -f $code)
        foreach ($zeile in @($roh | Select-Object -Last 3)) {
            Write-Host ("Lock:   {0}" -f ([string]$zeile).Trim())
        }
    }

    $nachher = Get-PlateFrei
    if ($nachher -lt 0) {
        Write-Host "Lock: Plattenplatz nach der Vorreinigung nicht messbar - es gilt die erste Messung."
        return -1
    }
    Write-Host (("Lock: nach der Vorreinigung {0:N1} % frei (vorher {1:N1} %, {2} s, Klasse 'cache' - der naechste Cook rechnet sie neu).") -f $nachher, $Frei, $gedauert)
    return $nachher
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
#
# DIE DRITTE ANTWORT (30.09.2026): unter der Meldegrenze (20 %) wird zuerst
# GERAEUMT (Raeume-Caches, nur die Cache-Klasse), und erst wenn das nicht
# reicht, geht es in die Warteschleife bzw. in den Abbruch. Warten und Abbrechen
# sind Antworten auf eine Lage, die man nicht aendert - Raeumen ist die, die
# die Lage aendert. Die Reihenfolge ist Absicht: erst raeumen, dann messen,
# dann warten. Ein Abbruch, der zwei Minuten vorher noch Platz gehabt haette,
# ist der teuerste Fall von allen.
function Teste-Plate([int]$ExitCode) {
    if ($PlattenGrenze -le 0) { return $ExitCode }
    if ($PlattenTrotz) {
        Write-Host "Lock: Platten-Gate uebergangen (-PlattenTrotz)."
        return $ExitCode
    }

    # WARTEN STATT ABBRECHEN (28.09.2026) - die Regel in einem Satz:
    # gewartet wird nur, wenn sich die Messung ueberhaupt AENDERN kann.
    #   * -PlattenTestGiga ist ein FESTER Wert: warten waere sinnlos, und der
    #     Selbsttest haenge an einer Zahl, die nie heilt.
    #   * -PlattenTestReihe und die echte Platte aendern sich: Raeumlaeufe und
    #     endende Cooks geben Platz frei, ein Abbruch kostet den ganzen Lauf.
    #   * Nur -Modus Start wartet. motor_sperre (gate_worktree.py) faehrt
    #     -Modus Nehmen und bringt seine EIGENE Frist mit - ein zweites Warten
    #     hier wuerde deren Frist verdoppeln (und ihre Attrappen-Uhr entwerten).
    $darfWarten = ($Modus -eq "Start") -and ($PlattenWarteSekunden -gt 0) -and ($PlattenTestGiga -lt 0 -or [bool]$PlattenTestReihe)
    $beginn = Get-Date
    $frist = $beginn.AddSeconds([Math]::Max(0, $PlattenWarteSekunden))
    $naechsteMeldung = $beginn
    # EINMAL je Lauf, nicht je Durchlauf: die Warteschleife misst im 15-s-Takt,
    # und ein Raeumlauf je Takt wuerde den Waechter selbst zum Fresser machen.
    # Die Reinigung steht VOR dem Gruen-Test der ersten Messung (Begruendung
    # dort) - sonst liefe sie erst unter der Abbruchgrenze, also zu spaet.
    $vorbereinigt = $false

    while ($true) {
        $frei = Get-PlateFrei
        if ($frei -lt 0) {
            # Nicht messbar heisst NICHT voll. Ein Gate, das im Zweifel blockiert,
            # ist ein Gate, das irgendwann den Rechner anhaelt - und ein
            # nicht lesbares Laufwerk ist eher ein Rechteproblem als ein Platzproblem.
            Write-Host "Lock: Plattenplatz nicht messbar - Start laeuft."
            return $ExitCode
        }
        # ZUERST RAEUMEN - UND ZWAR VOR DEM GRUEN-TEST (30.09.2026).
        #
        # Die Reihenfolge ist die halbe Wirkung: die Meldegrenze (20 %) liegt
        # UEBER der Abbruchgrenze (10 %). Stuende das Raeumen hinter dem
        # Gruen-Test, liefe es erst unter 10 % - also genau in der Lage, in der
        # der Start schon faellt. So raeumt der Start in der Zone, in der noch
        # Platz zu holen ist, und das Gruen danach nennt den echten Stand.
        #
        # Der Rueckgabewert ist der neue Plattenstand oder -1 (nichts geraeumt
        # / nicht messbar); bei -1 bleibt es bei der gemessenen Zahl, und der
        # Weg ist derselbe wie vorher - ein fehlendes Raeumwerkzeug aendert am
        # Gate nichts.
        if (-not $vorbereinigt) {
            $vorbereinigt = $true
            $nachher = Raeume-Caches $frei
            if ($nachher -ge 0) { $frei = $nachher }
        }

        if ($frei -ge $PlattenGrenze) {
            $gewartet = [int]((Get-Date) - $beginn).TotalSeconds
            if ($darfWarten -and $gewartet -ge 1) {
                Write-Host (("Lock: Platten-Gate gruen ({0:N1} % frei, Grenze {1} %) nach {2} s Wartezeit.") -f $frei, $PlattenGrenze, $gewartet)
            } else {
                Write-Host (("Lock: Platten-Gate gruen ({0:N1} % frei, Grenze {1} %).") -f $frei, $PlattenGrenze)
            }
            return $ExitCode
        }

        $rest = ($frist - (Get-Date)).TotalSeconds
        if ((-not $darfWarten) -or $rest -le 0) { break }

        # GEDROSSELT: die erste Meldung sofort, danach hoechstens eine je Minute.
        # Wer eine Warteschleife sieht, will wissen, OB es Fortschritt gibt.
        if ((Get-Date) -ge $naechsteMeldung) {
            Write-Host (("Lock: ABBRUCH - nur {0:N1} % frei, der Start braucht {1} %: warte auf Plattenplatz (hoechstens noch {2:N1} min).") -f $frei, $PlattenGrenze, ($rest / 60.0))
            $naechsteMeldung = (Get-Date).AddSeconds(60.0)
        }
        # Nie laenger als 15 s schlafen und nie ueber die Frist hinaus.
        Start-Sleep -Seconds ([int][Math]::Min(15, [Math]::Max(1, [Math]::Ceiling($rest))))
    }

    # Hierher kommt nur, wer zu voll geblieben ist: ohne Warten oder bis zur Frist.
    $gewartet = [int]((Get-Date) - $beginn).TotalSeconds
    Write-Host ""
    Write-Host (("Lock: ABBRUCH - nur {0:N1} % frei, der Start braucht {1} %.") -f $frei, $PlattenGrenze)
    if ($darfWarten -and $gewartet -ge 1) {
        Write-Host (("Lock: {0} s auf Plattenplatz gewartet - die Platte blieb bis zur Frist zu voll.") -f $gewartet)
    }
    Write-Host "Lock: ein Editor- oder Cook-Start auf dieser Platte bricht mitten im"
    Write-Host "Lock: Lauf ab und hinterlaesst unfertige Pakete. Der Waechter zeigt die"
    Write-Host "Lock: groessten Fresser:  Tools\platten_waechter.cmd --reinigen --trocken"
    Write-Host "Lock: Notausgang, wenn der Editor zum Aufraeumen gebraucht wird:"
    Write-Host "Lock:   engine_run_lock.cmd -Modus Start -Name <lauf> -PlattenTrotz"
    Write-Host "Lock:   engine_run_lock.cmd -Modus Start -Name <lauf> -PlattenWarteSekunden 0"
    Write-Host ""
    return $ExitPlatte
}

function Sperre-Freigeben([string]$Pfad, [bool]$Gewalt) {
    $zustand = Get-LockZustand $Pfad (Get-ProzessKette $PID)
    if ($zustand.Status -eq "Frei") {
        Write-Host "Lock: nichts freizugeben."
        return 0
    }
    if (($zustand.Status -eq "Fremd" -or $zustand.Status -eq "Nachkommen") -and -not $Gewalt) {
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
        "Verwaist" {
            $zusatz = ""
            if ($zustand.ContainsKey("Recycled") -and $zustand.Recycled) {
                $zusatz = " Die PID ist inzwischen an einen anderen Prozess vergeben; seine Kinder zaehlen nicht als Nachkommen des Laufs."
            }
            Write-Host (("Lock: verwaist - der Prozess des Laufs lebt nicht mehr ({0}).{1}") -f (Get-LockText $zustand), $zusatz)
        }
        "Eigen"    { Write-Host ("Lock: von diesem Lauf gehalten - {0}." -f (Get-LockText $zustand)) }
        # Der Besitzer ist tot, aber sein Baum arbeitet weiter. Das darf NICHT
        # als verwaist gelesen werden: cleanup_unreal_processes.ps1 beendet bei
        # Exit 0 alle UnrealEditor-Prozesse - hier waere das ein Uebergriff auf
        # laufende Arbeit. Deshalb nennt der Satz die Prozesse beim Namen.
        "Nachkommen" { Write-Host ("Lock: BELEGT - der Besitzer lebt nicht mehr, seine Kindprozesse arbeiten aber weiter ({0}; Nachkommen {1}). Uebernommen wird erst, wenn dieser Prozessbaum leer ist - Notausgang: -Modus Freigeben -Gewalt." -f (Get-LockText $zustand), (Get-KinderText $zustand)) }
        "Fremd"    { Write-Host ("Lock: BELEGT durch einen anderen Lauf - {0}." -f (Get-LockText $zustand)) }
    }
    if ($zustand.Status -eq "Fremd" -or $zustand.Status -eq "Nachkommen") { return $ExitBelegt }
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
        # WARTEN AUF EINEN BELEGTEN LOCK (28.09.2026): die .cmd-Wrapper bringen
        # keine Frist mit, also bringt das Skript sie mit - aber nur, wenn der
        # Aufrufer nicht selbst eine gesetzt hat. -WarteSekunden 0 heisst
        # weiterhin "sofort abbrechen".
        $warteLock = $StartWarteSekunden
        if ($script:WarteSekundenGesetzt) { $warteLock = $WarteSekunden }
        $rc = Sperre-Nehmen $lockPfad $Name $warteLock
        if ($rc -ne 0) { exit $rc }
        if ($DryRun) {
            & "$PSScriptRoot\cleanup_unreal_processes.ps1" -DryRun -LockPfad $lockPfad
        } else {
            & "$PSScriptRoot\cleanup_unreal_processes.ps1" -LockPfad $lockPfad
        }
        exit $LASTEXITCODE
    }
}
