# Doku-Drift-Pruefung: WbDev-Referenz gegen Quellcode.
#
# Stellt sicher, dass die Referenz (docs/reference/wbdev-konsolenbefehle.md) mit
# dem Code synchron bleibt - OHNE Editor, rein statisch und deterministisch:
#
#   1) BEFEHLS-DECKUNG (beidseitig): jeder `UFUNCTION(Exec)`-WbDev-Befehl in
#      IRGENDEINEM Header unter Source/ ist in der Referenz dokumentiert
#      (## WbXxx-Abschnitt), und die Referenz erfindet keinen Befehl, den es
#      nicht gibt. Alle Header, weil die Exec-Kette auch HUD und Pawn erreicht
#      (WbOption/WbOptionen sitzen bewusst im HUD) - nur den PlayerController
#      zu lesen meldete sie faelschlich als PHANTOM.
#   2) LOG-DRIFT (Code -> Doku): jede feste Textpassage jeder `UE_LOG(... "WbDev:
#      ...")`-Zeile im PlayerController taucht woertlich in der Referenz auf.
#      Wird eine Log-Meldung umformuliert, ohne die Referenz nachzuziehen, faellt
#      die Passage weg -> Fehler. Formatplatzhalter (%d, %.0f, %s, ...) werden vor
#      dem Vergleich entfernt, damit die Koordinaten-Paraphrasen der Doku
#      ("(x,y,z)") nicht stoeren; verglichen wird nur der feste Wortlaut.
#
# Aufruf:  powershell -File Tools\check_wbdev_docs.ps1   (Exit 0 = synchron)
param(
    [string]$Root = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = "Stop"

$Header = Join-Path $Root "Source\WiesbadenReal\Core\WiesbadenPlayerController.h"
$Cpp    = Join-Path $Root "Source\WiesbadenReal\Core\WiesbadenPlayerController.cpp"
$Ref    = Join-Path $Root "docs\reference\wbdev-konsolenbefehle.md"

foreach ($f in @($Header, $Cpp, $Ref)) {
    if (-not (Test-Path $f)) { Write-Output "ABBRUCH: Datei fehlt: $f"; exit 2 }
}

# Alle Header: Exec-Befehle koennen auf jedem Glied der Exec-Kette sitzen.
$headerText = (Get-ChildItem (Join-Path $Root "Source") -Recurse -Filter *.h |
    ForEach-Object { Get-Content $_.FullName -Raw }) -join "`n"
$cppText    = Get-Content $Cpp -Raw
$refText    = Get-Content $Ref -Raw
# Referenz auf eine leerraum-normalisierte Zeile ziehen: so trifft ein Textstueck
# auch dann, wenn es in der Doku ueber einen Zeilenumbruch laeuft.
$refNorm    = ($refText -replace '\s+', ' ')

$issues = New-Object System.Collections.ArrayList

# --- 1) Befehls-Deckung -----------------------------------------------------
# Exec-Befehle: `void WbXxx(` unmittelbar hinter einer UFUNCTION(Exec)-Zeile.
$execCmds = @()
$hLines = $headerText -split "`r?`n"
for ($i = 0; $i -lt $hLines.Count; $i++) {
    if ($hLines[$i] -match 'void\s+(Wb\w+)\s*\(') {
        $name = $Matches[1]
        # Vorherige nicht-leere Zeile muss UFUNCTION(Exec) sein.
        $j = $i - 1
        while ($j -ge 0 -and $hLines[$j].Trim() -eq "") { $j-- }
        if ($j -ge 0 -and $hLines[$j] -match 'UFUNCTION\(Exec\)') {
            $execCmds += $name
        }
    }
}
$execCmds = $execCmds | Sort-Object -Unique

# Dokumentierte Befehle: `## WbXxx`-Ueberschriften.
$docCmds = @()
foreach ($m in [regex]::Matches($refText, '(?m)^##\s+(Wb\w+)\s*$')) {
    $docCmds += $m.Groups[1].Value
}
$docCmds = $docCmds | Sort-Object -Unique

foreach ($c in $execCmds) {
    if ($docCmds -notcontains $c) {
        [void]$issues.Add("FEHLT: Befehl '$c' ist UFUNCTION(Exec), aber nicht in der Referenz dokumentiert.")
    }
}
foreach ($c in $docCmds) {
    if ($execCmds -notcontains $c) {
        [void]$issues.Add("PHANTOM: Referenz dokumentiert '$c', aber es gibt keinen UFUNCTION(Exec) dieses Namens.")
    }
}

# --- 2) Log-Drift (Code -> Doku) --------------------------------------------
# Alle TEXT("WbDev: ...")-Literale aus dem Cpp ziehen (auch mehrzeilig).
$specRegex = '%[-+ #0]*[0-9]*\.?[0-9]*[dfsuxeEgG]'
foreach ($m in [regex]::Matches($cppText, 'TEXT\("(WbDev:[^"]*)"\)')) {
    $literal = $m.Groups[1].Value
    # An Formatplatzhaltern zerlegen -> nur die festen Wortlaut-Stuecke bleiben.
    foreach ($frag in [regex]::Split($literal, $specRegex)) {
        $t = ($frag -replace '\s+', ' ').Trim()
        # Nur aussagekraeftige Stuecke pruefen (Buchstaben, >= 4 Zeichen);
        # reine Satzzeichen/Trenner wie "," oder ") (" ueberspringen.
        if ($t.Length -ge 4 -and $t -match '[A-Za-z]') {
            if (-not $refNorm.Contains($t)) {
                [void]$issues.Add("DRIFT: Log-Wortlaut '$t' (aus einer WbDev-Meldung) fehlt in der Referenz.")
            }
        }
    }
}

# --- Bericht ----------------------------------------------------------------
if ($issues.Count -eq 0) {
    Write-Output ("OK: {0} Exec-Befehle dokumentiert, alle WbDev-Log-Zeilen in der Referenz gedeckt." -f $execCmds.Count)
    exit 0
} else {
    foreach ($i in $issues) { Write-Output $i }
    Write-Output ("DOKU-DRIFT: {0} Abweichung(en) zwischen Referenz und Code." -f $issues.Count)
    exit 1
}
