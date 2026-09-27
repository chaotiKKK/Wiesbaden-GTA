# Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#
# Geteilte Helfer fuer BELEGdateien: Dateien, aus denen spaeter ein Messwert
# oder ein Gate-Ergebnis gelesen wird. Dot-sourcen, nicht aufrufen:
#
#   . (Join-Path $PSScriptRoot "beleg.ps1")
#   Remove-Beleg $Log              # loeschen und NACHPRUEFEN, sonst Abbruch
#   if (-not (Test-Frisch $Log $LaufStart)) { ... }   # ist das wirklich dieser Lauf?
#
# WARUM ES DAS GIBT - die Fehlerklasse, die dreimal still gruenes Gate
# erzeugt hat:
#
#   Remove-Item $Log -ErrorAction SilentlyContinue
#   ... 3 Minuten spaeter wird dieselbe Datei gelesen und behauptet, das
#   Ergebnis des aktuellen Laufs zu sein.
#
# SilentlyContinue schluckt jeden Loeschfehler. Im Projekt sind zwei davon
# NACHGEMESSEN (27.09.2026): das Read-only-Flag am Ordner-Eintrag und der
# offene Handle eines haengenden Editors. Beides fuehrt dazu, dass die Datei
# liegen bleibt - und dass die Auswertung genau deren ZAHLEN als das Ergebnis
# des neuen Laufs meldet. Ein Gate, das nichts gemessen hat, sieht aus wie
# ein Gate, das bestanden hat. Das ist schlimmer als ein Fehlschlag, weil es
# den Auftrag zum Weitermachen gibt.
#
# Die Regel dieses Werkzeugs: DEM WEGSEIN WIRD NICHT GLAUBT, ES WIRD GEPRUEFT.
# Zweite Regel: nicht nur loeschen, sondern die FRISCHE messen - eine Datei,
# die nach dem Sitzungsbeginn geschrieben wurde, ist ein Beleg; eine, die
# vorher geschrieben wurde, ist es nicht, egal ob das Loeschen geklappt hat.
#
# Dieselbe Regel in Python steht in Tools\beleg.py.

# Loescht eine Belegdatei (auch ein Verzeichnis) und prueft danach NACH.
# Ein nicht loeschbarer Beleg ist kein Anlass zum Weitermachen: die
# Auswertung waere sonst die des letzten Laufs. Also Abbruch mit Wegweiser
# statt weiterzuarbeiten. Exit 3 - eindeutig getrennt von den Gate-Exitcodes.
function Remove-Beleg([string]$Pfad) {
    if (-not $Pfad) { return }
    if (-not (Test-Path -LiteralPath $Pfad)) { return }

    $name = Split-Path $Pfad -Leaf
    $istOrdner = (Test-Path -LiteralPath $Pfad -PathType Container)
    if ($istOrdner) {
        Remove-Item -LiteralPath $Pfad -Recurse -Force -ErrorAction SilentlyContinue
    } else {
        Remove-Item -LiteralPath $Pfad -Force -ErrorAction SilentlyContinue
    }

    if (Test-Path -LiteralPath $Pfad) {
        # Read-only steckt am Ordner-Eintrag, nicht am Inhalt.
        try { (Get-Item -LiteralPath $Pfad -Force).IsReadOnly = $false } catch { }
        if ($istOrdner) {
            Remove-Item -LiteralPath $Pfad -Recurse -Force -ErrorAction SilentlyContinue
        } else {
            Remove-Item -LiteralPath $Pfad -Force -ErrorAction SilentlyContinue
        }
    }

    if (Test-Path -LiteralPath $Pfad) {
        Write-Host ""
        Write-Host ("ABBRUCH: der Beleg {0} laesst sich nicht loeschen." -f $name)
        Write-Host "  Er ist entweder schreibgeschuetzt oder von einem haengenden"
        Write-Host "  Prozess offen gehalten (dann blockiert auch -ABSLOG= auf ihn)."
        Write-Host ("  Pfad: {0}" -f $Pfad)
        Write-Host "  Lauf: Tools\cleanup_unreal_processes.cmd, dann erneut."
        exit 3
    }
}

# Ist der Beleg ein Ergebnis DIESES Laufs? $Ab ist der Zeitpunkt, zu dem die
# Sitzung gestartet wurde (Get-Date VOR dem Editor-Aufruf).
#
# Der Vergleich ist grosszuegig (-2 s), weil Dateizeitstempel je nach
# Dateisystem nur sekundengenau sind. Zu grosszuegig darf er nicht werden:
# eine alte Datei, die zufaellig im selben Sekundenfenster geschrieben wurde,
# gibt es nicht - die alte Log ist Minuten alt.
function Test-Frisch([string]$Pfad, [datetime]$Ab) {
    if (-not (Test-Path -LiteralPath $Pfad)) { return $false }
    try {
        $geschrieben = (Get-Item -LiteralPath $Pfad -Force).LastWriteTime
    } catch {
        return $false
    }
    if (-not $Ab) { return $true }
    return $geschrieben -ge $Ab.AddSeconds(-2)
}

# Wegweiser fuer den Fall, dass Test-Frisch false sagt. Wird von den
# aufrufenden Skripten ausgegeben, damit die Meldung nicht ins Leere zeigt.
function Write-BelegHinweis([string]$Pfad) {
    if (-not (Test-Path -LiteralPath $Pfad)) { return }
    try {
        $zeit = (Get-Item -LiteralPath $Pfad -Force).LastWriteTime
        Write-Host ("       Der Beleg ist vom {0:yyyy-MM-dd HH:mm:ss} - er ist" -f $zeit)
        Write-Host "       AELTER als dieser Lauf. Wahrscheinlich hat er nicht"
        Write-Host "       angefaengt, oder ein haengender Prozess haelt die alte Datei."
    } catch {
        Write-Host "       (Alter des Belegs nicht lesbar.)"
    }
}
