# Legt drei Desktop-Verknuepfungen auf ein DATIERTES Development-Paket:
#
#   "Wiesbaden Stadt spielen"    -> Stadt (Karte)
#   "Wiesbaden Heli spielen"     -> startet direkt im Ka-52 (-WbHeliStart)
#   "Wiesbaden BugTank spielen"  -> startet direkt im Kaeferpanzer (-WbBugTank)
#
# Anders als make_play_shortcut.ps1 (Editor im -game-Modus auf die gebackene
# Karte) zeigen diese auf die PAKETIERTE EXE - ein eigenstaendiges Spiel ohne
# Editor-Ordner. Aufruf nach dem Paketieren:
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File Tools\make_package_shortcuts.ps1 -PackageDir "Saved\Package_2026-10-01"
#
# Ohne -Map nimmt sie die Default-Karte (Tools\karte.ps1). Fuer die Abnahme
# einer NEUEN Karte diese explizit geben - die Default-Karte wird erst nach
# der Abnahme umgestellt (WB_LIVE_SCHALTEN), die Verknuepfung darf aber schon
# auf die neue Karte zeigen.
#
# CmdletBinding: ein Tippfehler im -PackageDir-Argument wird abgewiesen statt
# still ignoriert (dieselbe Falle wie '-GateOnly' in build_release.ps1).
#
# Aus einer DATEI aufrufen, weil die Argumente Anfuehrungszeichen enthalten;
# durch die Git-Bash-Schicht gequetscht zerfallen inline uebergebene Zeichen.

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$PackageDir,
    [string]$Map = (& "$PSScriptRoot\karte.ps1"),
    [string]$Root = "C:\freebuff\WiesbadenReal_Sicherung",
    [int]$ResX = 1920,
    [int]$ResY = 1080
)

$ErrorActionPreference = "Stop"

$ProjDir = Join-Path $Root "WiesbadenReal"
if ([System.IO.Path]::IsPathRooted($PackageDir)) {
    $Exe = Join-Path $PackageDir "Windows\WiesbadenReal.exe"
} else {
    $Exe = Join-Path $ProjDir (Join-Path $PackageDir "Windows\WiesbadenReal.exe")
}

# Erst pruefen, dann verknuepfen: eine Verknuepfung auf eine nicht gebaute
# EXE oeffnet gar nichts, und der Fehler faelle erst beim Spielen auf.
$MapFile = Join-Path $ProjDir ("Content\Maps\{0}.umap" -f $Map)
foreach ($p in @($Exe, $MapFile)) {
    if (-not (Test-Path $p)) { throw "fehlt: $p" }
}

$Desktop = [Environment]::GetFolderPath("Desktop")
$Karte = "/Game/Maps/{0}" -f $Map
$ws = New-Object -ComObject WScript.Shell

# Reihenfolge der Platzhalter: {0} Exe, {1} Karte, {2} ResX, {3} ResY.
$Ziele = @(
    @{ Name = "Wiesbaden Stadt spielen";   Args = '"{0}" {1} -windowed -ResX={2} -ResY={3} -nop4' },
    @{ Name = "Wiesbaden Heli spielen";    Args = '"{0}" {1} -WbHeliStart -windowed -ResX={2} -ResY={3} -nop4' },
    @{ Name = "Wiesbaden BugTank spielen"; Args = '"{0}" {1} -WbBugTank -windowed -ResX={2} -ResY={3} -nop4' }
)

foreach ($Z in $Ziele) {
    $lnk = Join-Path $Desktop ($Z.Name + ".lnk")
    $sc = $ws.CreateShortcut($lnk)
    $sc.TargetPath = $Exe
    $sc.Arguments = $Z.Args -f $Exe, $Karte, $ResX, $ResY
    $sc.WorkingDirectory = Split-Path $Exe -Parent
    $sc.Description = "Wiesbaden Real (Development-Paket) - " + $Z.Name
    $sc.Save()

    # Nachpruefen: gespeichert ist, was die COM-Schnittstelle zurueckliest.
    $check = $ws.CreateShortcut($lnk)
    Write-Host ("angelegt : " + $lnk)
    Write-Host ("Ziel     : " + $check.TargetPath)
    Write-Host ("Argumente: " + $check.Arguments)
}
Write-Host ("Karte    : " + $MapFile)
