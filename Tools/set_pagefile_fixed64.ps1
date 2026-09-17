# Setzt die Auslagerungsdatei auf C: auf FESTE 64 GB (Initial=Maximum).
#
# Warum: Bei Initial 32 GB / Maximum 64 GB ist die Pagefile im Bake NICHT
# gewachsen - sie blieb bei 32768 MB, und der Bake starb bei 78 GiB Commit
# mit "Auslagerungsdatei ist zu klein". Feste Groesse erzwingt die volle
# Groesse sofort beim Boot -> Commit-Limit ~96 GB, der Bake (~78 GiB Peak)
# passt hinein.
#
# Rueckgaengig: set_pagefile_bake.ps1 (32/64 GB) oder automatische Verwaltung.
$ErrorActionPreference = "Stop"

$cs = Get-CimInstance Win32_ComputerSystem
if ($cs.AutomaticManagedPagefile) {
    Set-CimInstance -InputObject $cs -Property @{AutomaticManagedPagefile = $false}
}

$pf = Get-CimInstance Win32_PageFileSetting | Where-Object { $_.Name -like "C:*" }
if (-not $pf) { Write-Error "Keine Pagefile auf C: gefunden." }
Write-Host ("Vorher: {0}  Initial {1} MB, Maximum {2} MB" -f $pf.Name, $pf.InitialSize, $pf.MaximumSize)

$pf.InitialSize = 65536   # 64 GB, fest
$pf.MaximumSize = 65536
$pf | Set-CimInstance

$pf2 = Get-CimInstance Win32_PageFileSetting | Where-Object { $_.Name -like "C:*" }
Write-Host ("Nachher: {0}  Initial {1} MB, Maximum {2} MB" -f $pf2.Name, $pf2.InitialSize, $pf2.MaximumSize)
Write-Host "FERTIG. Wirksam nach Neustart."
