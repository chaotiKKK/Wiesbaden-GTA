# Sichert alles Einmalige auf den USB-Stick, um auf einem anderen Rechner
# weiterzuarbeiten.
#
# Wichtig: Das Projekt steht NICHT unter Versionsverwaltung. Diese Kopie ist
# damit die einzige Sicherung - entsprechend wird lieber etwas zu viel
# mitgenommen als zu wenig.
#
# Ausgelassen wird nur, was der neue Rechner selbst wieder erzeugt:
# Intermediate, Binaries, Saved, DerivedDataCache und die vorkompilierten
# Header. Zusammen sind das ueber 25 GB reine Kopierzeit ohne Gegenwert.
#
# Aufruf:
#   powershell -ExecutionPolicy Bypass -File Tools\kopiere_auf_stick.ps1 -Ziel E:\
#   powershell ... -Ziel E:\ -OhneRueckfall   (Rueckfallkarte weglassen)
#   powershell ... -Ziel D:\ -MitEngine       (UE 5.8 mitkopieren, 29,9 GB)
#   powershell ... -Ziel E:\ -NurPruefen      (nur rechnen, nichts kopieren)

param(
    [string]$Ziel = 'E:\',
    [switch]$OhneRueckfall,
    # Rueckfallkarte ausdruecklich benennen (Vorgabe: keine). Ein fester
    # Name stand hier zwei Bakes lang falsch.
    [string]$Rueckfall = "",
    [switch]$MitEngine,
    [switch]$NurPruefen
)

$ErrorActionPreference = 'Stop'

# Laufende Summe des Trockenlaufs.
$script:ProbeSumme = 0.0

$Wurzel  = 'C:\Users\ssonn\aivideo'
$Projekt = Join-Path $Wurzel 'WiesbadenReal'
$Sicherung = Join-Path $Ziel 'WiesbadenReal_Sicherung'
$Protokoll = Join-Path $Sicherung 'kopie.log'

# Was der Zielrechner neu erzeugt - kostet nur Zeit.
$AusOrdner = @('Intermediate', 'Binaries', 'Saved', 'DerivedDataCache',
               'node_modules', '.vs', '.git')
$AusDateien = @('*.pch', '*.tmp', '*.obj', '*.pdb', '*.ilk', '*.sdf')

function Zeige($text) { Write-Host $text }

function Groesse($pfad) {
    if (-not (Test-Path $pfad)) { return 0 }
    $s = Get-ChildItem $pfad -Recurse -File -ErrorAction SilentlyContinue |
         Measure-Object -Property Length -Sum
    return $s.Sum
}

# Groesse OHNE das, was ohnehin nicht mitkopiert wird.
#
# Die schlichte Summe waere hier irrefuehrend: Das Projekt misst 52,9 GB, wovon
# nur rund 2 GB mitgehen. Eine Schaetzung, die das verschweigt, laesst den
# Kopierlauf unmoeglich aussehen.
function GefilterteGroesse($pfad, $zusatzAus = @()) {
    if (-not (Test-Path $pfad)) { return 0 }

    $ordnerAus = $AusOrdner + $zusatzAus
    $summe = 0L

    Get-ChildItem $pfad -Recurse -File -ErrorAction SilentlyContinue | ForEach-Object {
        $relativ = $_.FullName.Substring($pfad.Length).TrimStart('\')

        # Liegt die Datei in einem ausgeschlossenen Ordner?
        $teile = $relativ -split '\\'
        foreach ($t in $teile) {
            if ($ordnerAus -contains $t) { return }
        }

        foreach ($muster in $AusDateien) {
            if ($_.Name -like $muster) { return }
        }

        $summe += $_.Length
    }

    return $summe
}

function Kopiere($quelle, $unterordner, $zusatzAus = @()) {
    if (-not (Test-Path $quelle)) {
        Zeige "  UEBERSPRUNGEN (fehlt): $quelle"
        return
    }

    $zielPfad = Join-Path $Sicherung $unterordner
    $xd = @()
    foreach ($o in ($AusOrdner + $zusatzAus)) { $xd += @('/XD', $o) }
    $xf = @()
    foreach ($d in $AusDateien) { $xf += @('/XF', $d) }

    if ($NurPruefen) {
        $gb = (GefilterteGroesse $quelle $zusatzAus) / 1GB
        $script:ProbeSumme += $gb
        Zeige ("  [Probe] {0,8:N2} GB  {1}" -f $gb, $quelle)
        return
    }

    Zeige "  $quelle  ->  $zielPfad"

    # /MIR spiegelt, /R:1 /W:1 haengt nicht an einer defekten Datei fest,
    # /NFL /NDL halten die Ausgabe lesbar, /NP unterdrueckt den Prozentzaehler
    # (der sonst das Protokoll mit Zehntausenden Zeilen flutet).
    $argumente = @($quelle, $zielPfad, '/MIR', '/R:1', '/W:1',
                   '/NFL', '/NDL', '/NP', '/NJH', '/TEE',
                   "/LOG+:$Protokoll") + $xd + $xf

    & robocopy @argumente | Out-Null

    # Robocopy meldet 0-7 als Erfolg; ab 8 ist etwas schiefgegangen.
    if ($LASTEXITCODE -ge 8) {
        Write-Warning "Robocopy meldet Code $LASTEXITCODE fuer $quelle"
    }
}

# -- Platz pruefen ----------------------------------------------------------
$laufwerk = (Get-Item $Ziel).PSDrive
$freiGB = [math]::Round($laufwerk.Free / 1GB, 1)
Zeige "Ziel $Ziel - $freiGB GB frei."

if (-not $NurPruefen) {
    New-Item -ItemType Directory -Path $Sicherung -Force | Out-Null
    "=== Sicherung $(Get-Date -Format 'dd.MM.yyyy HH:mm') ===" |
        Out-File -FilePath $Protokoll -Encoding utf8
}

# -- 1) Projekt ohne die gebackenen Staedte ---------------------------------
#
# __ExternalActors__ wird hier ausgelassen und weiter unten gezielt je Karte
# kopiert: Dort liegen drei gebackene Staedte a 13 GB, und die aelteste davon
# braucht niemand mehr.
Zeige "`n[1/5] Projekt (Quelltext, Werkzeuge, Inhalte, Rohdaten)"
Kopiere $Projekt 'WiesbadenReal' @('__ExternalActors__')

# -- 2) Gebackene Staedte ---------------------------------------------------
Zeige "`n[2/5] Gebackene Stadt"
$actorsQuelle = Join-Path $Projekt 'Content\__ExternalActors__\Maps'
$actorsZiel = 'WiesbadenReal\Content\__ExternalActors__\Maps'

# Die AKTUELLE Stadt - welche das ist, sagt Config\DefaultEngine.ini. Ein
# fester Name hier kopierte nach zwei Bakes die vorletzte Stadt auf den Stick.
$aktuelleKarte = & (Join-Path $PSScriptRoot 'karte.ps1')
Kopiere (Join-Path $actorsQuelle $aktuelleKarte) `
        (Join-Path $actorsZiel $aktuelleKarte)

# Rueckfallkarte: ausdruecklich benennen (-Rueckfall <Karte>). Ein fester
# Name stand hier zwei Bakes lang falsch - eine Rueckfallkarte, die niemand
# mehr spielt, ist kein Rueckfall.
if (-not $OhneRueckfall -and $Rueckfall) {
    Kopiere (Join-Path $actorsQuelle $Rueckfall) `
            (Join-Path $actorsZiel $Rueckfall)
} else {
    Zeige "  Rueckfallkarte auf Wunsch ausgelassen."
}

# -- 3) Blender-Arbeitsdateien ----------------------------------------------
#
# Die Modelle und Texturen ausserhalb des Unreal-Projekts. Sie lassen sich
# nicht wiederherstellen: Aus ihnen sind Sebbo, die Fahrzeuge und die
# Vegetation entstanden.
Zeige "`n[3/5] Blender-Arbeitsdateien"
foreach ($ordner in @('city', 'nature', 'models', 'ws', 'tex', 'sebbo',
                      'props', 'out', 'cap', 'src', 'app', 'patches',
                      'peek', 'blmcp_src')) {
    Kopiere (Join-Path $Wurzel $ordner) "Blender\$ordner"
}

if (-not $NurPruefen) {
    # Lose Dateien direkt in aivideo - sebbo_rig.blend liegt dort.
    #
    # Der Pfad MUSS auf \* enden: -Include greift ohne Wildcard im Pfad (oder
    # ohne -Recurse) gar nicht und liefert stillschweigend nichts.
    $loseZiel = Join-Path $Sicherung 'Blender'
    New-Item -ItemType Directory -Path $loseZiel -Force | Out-Null
    Get-ChildItem "$Wurzel\*" -File `
        -Include '*.blend', '*.blend1', '*.py', '*.js', '*.html' `
        -ErrorAction SilentlyContinue |
        Copy-Item -Destination $loseZiel -Force
}

# -- 4) Weitere Quellen -----------------------------------------------------
Zeige "`n[4/5] Weitere Quellen"
Kopiere 'C:\Users\ssonn\Downloads\vw-beetle-1969' 'Quellen\vw-beetle-1969'

# wbnracing ist ein EIGENES Unreal-Projekt. 15 der 18 GB sind Bauabfall -
# vorkompilierte Header, .tmp, .obj. Die Ausschlussliste oben raeumt das weg.
Kopiere 'C:\Users\ssonn\Downloads\wbnracing' 'Quellen\wbnracing'

# -- 4b) Unreal Engine ------------------------------------------------------
#
# Die Engine wird VOLLSTAENDIG kopiert - ohne jeden Ausschluss.
#
# Das ist kein Versehen: Bei einem Projekt sind Binaries und Intermediate
# Abfall, bei einer fertigen Engine sind sie genau das, was sie ausmacht.
# UnrealEditor.exe liegt in Engine\Binaries\Win64. Wer hier dieselbe Ausschlussliste
# anlegt wie beim Projekt, kopiert eine Engine, die nicht startet.
#
# Auf dem neuen Rechner registrieren mit:
#   Engine\Binaries\Win64\UnrealVersionSelector.exe
# Danach laufen Build.bat, der Editor und die .uproject-Verknuepfung. Der
# Epic Launcher fuehrt sie nicht als installiert - zum Arbeiten braucht es
# ihn aber nicht.
if ($MitEngine) {
    Zeige "`n[4b] Unreal Engine 5.8 (vollstaendig, ohne Ausschluesse)"
    $engine = 'C:\Program Files\Epic Games\UE_5.8'

    if (-not (Test-Path $engine)) {
        Zeige "  UEBERSPRUNGEN (fehlt): $engine"
    }
    elseif ($NurPruefen) {
        $gb = (Groesse $engine) / 1GB
        $script:ProbeSumme += $gb
        Zeige ("  [Probe] {0,8:N2} GB  {1}" -f $gb, $engine)
    }
    else {
        $engineZiel = Join-Path $Sicherung 'UE_5.8'
        Zeige "  $engine  ->  $engineZiel"
        & robocopy $engine $engineZiel /MIR /R:1 /W:1 /NFL /NDL /NP /NJH /TEE `
            "/LOG+:$Protokoll" | Out-Null
        if ($LASTEXITCODE -ge 8) {
            Write-Warning "Robocopy meldet Code $LASTEXITCODE fuer die Engine"
        }
    }
}

# -- 5) Gedaechtnis und Arbeitsskripte --------------------------------------
Zeige "`n[5/5] Gedaechtnis und Arbeitsskripte"
Kopiere 'C:\Users\ssonn\.claude\projects\C--Users-ssonn-aivideo\memory' `
        'Claude\memory'

$kratz = 'C:\Users\ssonn\AppData\Local\Temp\claude\C--Users-ssonn-aivideo\da4fbff6-0b9e-46f8-ab5f-174f72d12999\scratchpad'
Kopiere $kratz 'Claude\arbeitsskripte'

# -- Ergebnis ---------------------------------------------------------------
if ($NurPruefen) {
    Zeige ("`nProbe gesamt: {0:N2} GB - auf {1} sind {2} GB frei." -f `
        $script:ProbeSumme, $Ziel, $freiGB)
}

if (-not $NurPruefen) {
    $kopiert = Groesse $Sicherung
    Zeige ("`nFERTIG. {0:N2} GB auf {1} geschrieben." -f ($kopiert / 1GB), $Sicherung)
    Zeige "Protokoll: $Protokoll"
}
