# Stellt die Auslagerungsdatei auf SYSTEMVERWALTET um.
#
# Warum: Der Stadt-Bake erreicht Commit-Peaks von 78-95+ GB. Feste Groessen
# (32/32, 32/64, 64/64) deckten den Peak nicht ab: die Pagefile wuchs bei
# fester InitialSize nie ueber das Initialmass hinaus, der Bake starb
# reprozierbar an Commit-OOM ("Auslagerungsdatei ist zu klein").
# Systemverwaltet waechst die Pagefile bedarfsgesteuert (bis ~3x RAM) und
# vergibt Commit sofort, sobald sie wachsen kann.
#
# Rueckgaengig: Tools/set_pagefile_fixed64.ps1 (64/64 GB).
$ErrorActionPreference = "Stop"

$cs = Get-CimInstance Win32_ComputerSystem
if (-not $cs.AutomaticManagedPagefile) {
    Write-Host "Automatische Pagefile-Verwaltung aktivieren ..."
    Set-CimInstance -InputObject $cs -Property @{AutomaticManagedPagefile = $true}
}

# Explizite Eintraege entfernen, sonst bleibt der feste Wert wirksam.
$existing = Get-CimInstance Win32_PageFileSetting -ErrorAction SilentlyContinue
foreach ($pf in $existing) {
    Write-Host ("Entferne festen Eintrag: {0}" -f $pf.Name)
    Remove-CimInstance -InputObject $pf
}

Write-Host ("AutomaticManagedPagefile: {0}" -f
    (Get-CimInstance Win32_ComputerSystem).AutomaticManagedPagefile)
Write-Host "FERTIG. Wirksam nach Neustart."
