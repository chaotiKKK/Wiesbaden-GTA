# Setzt eine FESTE 96-GB-Auslagerungsdatei auf C: (initial = max = 98304 MB).
# Reihenfolge wichtig: ZUERST Auto-Verwaltung abschalten (das setzt PagingFiles
# auf "0 0" = systemverwaltet), DANACH die feste Groesse schreiben - sonst wuerde
# Schritt 1 die feste Groesse wieder ueberschreiben. Wirkt nach Neustart.
# Muss ELEVATED laufen (HKLM + Win32_ComputerSystem).
$mm = 'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\Memory Management'
try {
    Set-CimInstance -InputObject (Get-CimInstance Win32_ComputerSystem) -Property @{ AutomaticManagedPagefile = $false } -ErrorAction Stop
    Write-Output 'auto-manage: disabled (CIM)'
} catch {
    Write-Output ('auto-manage disable via CIM fehlgeschlagen: ' + $_.Exception.Message)
}
Set-ItemProperty -Path $mm -Name 'PagingFiles' -Value @('C:\pagefile.sys 98304 98304') -Type MultiString
Write-Output ('PagingFiles=' + ((Get-ItemProperty -Path $mm -Name 'PagingFiles').PagingFiles -join '|'))
Write-Output ('AutoManaged=' + (Get-CimInstance Win32_ComputerSystem).AutomaticManagedPagefile)
