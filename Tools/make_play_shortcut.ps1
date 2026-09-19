# Legt die Desktop-Verknuepfung "Wiesbaden aktuell (<Karte>)" auf eine gebackene
# Stadtkarte. OHNE -Map nimmt sie die Default-Karte aus Config\DefaultEngine.ini
# (Tools\karte.ps1) - nach einem Neubau genuegt also ein Aufruf ohne Argument:
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File Tools/make_play_shortcut.ps1
#   powershell ... -File Tools/make_play_shortcut.ps1 -Map <andere Karte>
#
# CmdletBinding: ein Tippfehler im -Map-Argument wird abgewiesen statt STILL
# ignoriert (sonst zeigt die Verknuepfung wieder auf die alte Karte - dieselbe
# Falle wie '-GateOnly' statt '-GatesOnly' in build_release.ps1).
#
# Aus einer DATEI aufgerufen, weil das Argument ein Anfuehrungszeichen enthaelt
# (der uproject-Pfad hat kein Leerzeichen, aber die Form "..." -Argument ist die,
# die die uebrigen Verknuepfungen und .cmd-Dateien verwenden); durch die
# Git-Bash-Schicht gequetscht zerlegt PowerShell solche Zeichen.
#
# Engine: IMMER die Launcher-Installation 5.8.2 (C:\Program Files\Epic Games\UE_5.8),
# weil Intermediate/Binaries des Projekts dagegen gebaut sind. Die Plattenkopie
# C:\freebuff\...\UE_5.8 ist 5.8.1 - siehe AGENTS.md "die beiden UE-5.8-Baeme
# niemals mischen".

[CmdletBinding()]
param(
    # Vorgabe ist die Default-Karte - eine feste Zahl hier zeigte nach jedem
    # Bake auf die vorletzte Stadt.
    [string]$Map = (& "$PSScriptRoot\karte.ps1"),
    [string]$Root = "C:\freebuff\WiesbadenReal_Sicherung",
    [string]$EngineRoot = "C:\Program Files\Epic Games\UE_5.8",
    [int]$ResX = 1920,
    [int]$ResY = 1080
)

$ErrorActionPreference = "Stop"

# NICHT Join-Path $Root "UE_5.8\...": unter $Root liegt die 5.8.1-Kopie, die nur
# als Rueckfall dient. Der falsche Baum faellt hier nicht auf - beide exe-Dateien
# heissen gleich und existieren -, erst beim Spielen.
$Engine = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor.exe"
$ProjDir = Join-Path $Root "WiesbadenReal"
$Proj = Join-Path $ProjDir "WiesbadenReal.uproject"
$MapFile = Join-Path $ProjDir ("Content\Maps\{0}.umap" -f $Map)

# Erst pruefen, dann verknuepfen: eine Verknuepfung auf eine fehlende Karte
# startet den Editor in ein leeres Level - der Fehler faellt dann erst beim
# Spielen auf.
foreach ($p in @($Engine, $Proj, $MapFile)) {
    if (-not (Test-Path $p)) { throw "fehlt: $p" }
}

$lnk = Join-Path ([Environment]::GetFolderPath("Desktop")) ("Wiesbaden aktuell ({0}).lnk" -f $Map.Replace("WiesbadenCity_", ""))

$ws = New-Object -ComObject WScript.Shell
$sc = $ws.CreateShortcut($lnk)
$sc.TargetPath = $Engine
$sc.Arguments = '"{0}" /Game/Maps/{1} -game -windowed -ResX={2} -ResY={3} -nop4' -f $Proj, $Map, $ResX, $ResY
$sc.WorkingDirectory = $ProjDir
$sc.Description = "Wiesbaden City ({0}), gebackene Stadt - Fenster braucht FOKUS (Unreal drosselt ohne Fokus auf 20 FPS)" -f $Map
$sc.Save()

$check = $ws.CreateShortcut($lnk)
Write-Host ("angelegt : " + $lnk)
Write-Host ("Ziel     : " + $check.TargetPath)
Write-Host ("Argumente: " + $check.Arguments)
Write-Host ("Arbeitsv.: " + $check.WorkingDirectory)
Write-Host ("Karte    : " + $MapFile)
