# Die aktuelle Stadt-Karte - EINE Quelle fuer alle Skripte.
#
# Gelesen wird GameDefaultMap aus Config\DefaultEngine.ini. Das ist die Karte,
# die das Spiel ohne Argument startet; alles andere waere eine zweite Wahrheit.
#
# Benutzung in einem PowerShell-Skript:
#     $Map = & "$PSScriptRoot\karte.ps1"
# oder als Parameter-Vorgabe:
#     [string]$Map = (& "$PSScriptRoot\karte.ps1")
#
# Gegenstuecke: Tools\karte.cmd (Batch), Tools\karte.py (Python).

[CmdletBinding()]
param(
    [string]$Ini = (Join-Path (Split-Path -Parent $PSScriptRoot) 'Config\DefaultEngine.ini')
)

if (-not (Test-Path $Ini)) {
    throw "karte.ps1: $Ini nicht gefunden."
}

$zeile = Select-String -Path $Ini -Pattern '^GameDefaultMap\s*=\s*(.+)$' | Select-Object -First 1
if (-not $zeile) {
    throw "karte.ps1: GameDefaultMap steht nicht in $Ini."
}

# "/Game/Maps/WiesbadenCity_Alkis17.WiesbadenCity_Alkis17" -> kurzer Name
$roh = $zeile.Matches[0].Groups[1].Value.Trim()
$kurz = ($roh -split '/')[-1]
$kurz = ($kurz -split '\.')[0]

if ([string]::IsNullOrWhiteSpace($kurz)) {
    throw "karte.ps1: Kartenname aus '$roh' nicht lesbar."
}

$kurz
