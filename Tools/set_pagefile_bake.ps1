# Vergroessert die Auslagerungsdatei auf C: fuer den UE-Stadt-Bake
# (der Editor erreicht ~42 GiB Commit; 32 GB RAM + 32 GB Pagefile reichen
# nicht -> "Auslagerungsdatei ist zu klein" mitten im Bake).
# Neu: Initial 32 GB (unveraendert), Maximum 64 GB -> Commit-Limit ~96-128 GB.
# Rueckgaengig: MaximumSize wieder auf 32768 setzen (oder automatisch verwalten).
$ErrorActionPreference = "Stop"

$cs = Get-CimInstance Win32_ComputerSystem
if ($cs.AutomaticManagedPagefile) {
    Write-Host "Automatische Pagefile-Verwaltung aktiv - schalte ab ..."
    Set-CimInstance -InputObject $cs -Property @{AutomaticManagedPagefile = $false}
}

$pf = Get-CimInstance Win32_PageFileSetting | Where-Object { $_.Name -like "C:*" }
if (-not $pf) {
    Write-Error "Keine Pagefile auf C: gefunden."
}
Write-Host ("Vorher: {0}  Initial {1} MB, Maximum {2} MB" -f $pf.Name, $pf.InitialSize, $pf.MaximumSize)

$pf.MaximumSize = 65536   # 64 GB
$pf | Set-CimInstance

$pf2 = Get-CimInstance Win32_PageFileSetting | Where-Object { $_.Name -like "C:*" }
Write-Host ("Nachher: {0}  Initial {1} MB, Maximum {2} MB" -f $pf2.Name, $pf2.InitialSize, $pf2.MaximumSize)
Write-Host "FERTIG. Wirksam nach Neustart (commit-limit steigt auf ~96 GB)."
