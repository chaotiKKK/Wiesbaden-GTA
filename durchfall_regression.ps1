<#
  Durchfall-Regressions-Check.

  Faehrt den Durchfall-Test (Streaming-Boden-Waechter) auf einer Stadt-Karte und
  SCHLAEGT ALARM (Exit-Code 1), sobald es eine zusammenhaengende Bodenluecke > 0 m
  gibt - also eine Stelle, an der ein Wagen bei Tempo durch eine noch ungeladene
  Zelle fiele. Bei sauberem Boden Exit-Code 0.

  Gedacht als wiederkehrender Check NACH JEDEM STADT-BUILD:
    - Mit -Bake wird zuerst neu gebacken (rebuild_city.py) und danach automatisch
      der Durchfall-Check auf der frisch gebackenen Karte gefahren. Diesen Aufruf
      statt des blossen rebake-Skripts verwenden -> jeder Build prueft sich selbst.
    - Ohne -Bake nur der Check auf einer bestehenden Karte.
  Der Exit-Code macht ihn in jeder Automatik (Wrapper, Loop, Scheduler, kuenftiges
  CI) als Fehler sichtbar.

  Beispiele:
    powershell -File durchfall_regression.ps1
    powershell -File durchfall_regression.ps1 -Map <andere Karte>
    powershell -File durchfall_regression.ps1 -Bake -TargetMap <neue Karte>
#>
param(
  [string]$Map        = (& "$PSScriptRoot\Tools\karte.ps1"),   # Pruefkarte: die Default-Karte
  [switch]$Bake,                                   # vorher neu backen
  [string]$SourceMap  = (& "$PSScriptRoot\Tools\karte.ps1"),   # Bake-Quelle: die aktuelle Stadt
  [string]$TargetMap  = "",                        # Bake-Ziel: MUSS angegeben werden (neue Karte)
  [int]$Speed         = 140,                        # km/h der Pruef-Fahrt
  [int]$DriveStart    = 40,                         # s bis Fahrtbeginn (Karte laden)
  [int]$QuitAfter     = 200,                        # s bis Lauf-Ende
  [switch]$CheckOnly,                               # nur bestehende Durchfall.txt auswerten
  [switch]$ChaosCar                                 # echten Chaos-Wagen fahren (Karosserie-Sturz)
)

$ErrorActionPreference = "Stop"
$Root   = "C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal"
$Editor = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
$Proj   = Join-Path $Root "WiesbadenReal.uproject"
$Durchfall = Join-Path $Root "Saved\Diagnose\Durchfall.txt"

function Alarm([string]$msg) {
  Write-Host ""
  Write-Host "########################################################" -ForegroundColor Red
  Write-Host "## DURCHFALL-ALARM: $msg" -ForegroundColor Red
  Write-Host "########################################################" -ForegroundColor Red
}

# --- 1) Optional: Stadt neu backen -----------------------------------------
if ($Bake -and -not $CheckOnly) {
  Write-Host "[Regression] Backe Stadt $SourceMap -> $TargetMap ..." -ForegroundColor Cyan
  $env:WB_SOURCE_MAP = "/Game/Maps/$SourceMap"
  $env:WB_TARGET_MAP = "/Game/Maps/$TargetMap"
  $bakeLog = Join-Path $Root "durchfall_regression_bake.log"
  $p = Start-Process -FilePath $Editor -PassThru -Wait -ArgumentList `
    "`"$Proj`"", "-ExecCmds=`"py $Root\Tools\rebuild_city.py`"", "-unattended", "-nosplash", "-nop4", "-stdout" `
    -RedirectStandardOutput $bakeLog
  if ($p.ExitCode -ne 0) {
    Alarm "Bake fehlgeschlagen (Exit $($p.ExitCode)) - siehe $bakeLog"
    exit 1
  }
  $Map = $TargetMap
  Write-Host "[Regression] Bake fertig." -ForegroundColor Cyan
}

# --- 2) Durchfall-Fahrt auf der Karte --------------------------------------
$driveLog = Join-Path $Root "durchfall_regression_drive.log"
if (-not $CheckOnly) {
  if (Test-Path $Durchfall) { Remove-Item $Durchfall -Force }
  $mode = if ($ChaosCar) { "echter Chaos-Wagen" } else { "Teleport-Pawn" }
  Write-Host "[Regression] Pruef-Fahrt auf $Map ($Speed km/h, $mode) ..." -ForegroundColor Cyan
  $driveArgs = @(
    "`"$Proj`"", "/Game/Maps/$Map", "-game", "-windowed", "-ResX=1280", "-ResY=720",
    "-WbAutoDrive=$Speed", "-WbAutoDriveStart=$DriveStart", "-WbQuitAfter=$QuitAfter",
    "-unattended", "-nop4", "-stdout")
  if ($ChaosCar) { $driveArgs += "-WbChaosCar" }
  $p = Start-Process -FilePath $Editor -PassThru -Wait -ArgumentList $driveArgs -RedirectStandardOutput $driveLog
} else {
  Write-Host "[Regression] -CheckOnly: werte bestehende Durchfall.txt aus." -ForegroundColor Cyan
}

# --- 3) Ergebnis auswerten -------------------------------------------------
if (-not (Test-Path $Durchfall)) {
  Alarm "Kein Durchfall.txt erzeugt - Test lief nicht (Karte '$Map' geladen? Editor-Log: $driveLog)."
  exit 1
}
$txt = Get-Content $Durchfall -Raw
Write-Host "----- Durchfall.txt -----" -ForegroundColor DarkGray
Write-Host $txt

# Bewegte Messpunkte > 0 (sonst ist die Fahrt nie losgerollt -> Test ungueltig).
$points = 0
if ($txt -match 'Bewegte Messpunkte:\s*([0-9]+)') { $points = [int]$Matches[1] }
if ($points -le 0) {
  Alarm "Fahrt ist nicht losgerollt (0 Messpunkte) - Test ungueltig (DriveStart/QuitAfter pruefen)."
  exit 1
}

# Kennzahl: Trace-Modus "Laengste zusammenhaengende Bodenluecke", Wagen-Modus
# "Groesster Karosserie-Hoehensturz" - beide in Metern, beide > 0 = Durchfall.
$gap = -1.0
$metric = "Bodenluecke"
if ($txt -match 'Laengste zusammenhaengende Bodenluecke:\s*([0-9]+([.,][0-9]+)?)\s*m') {
  $gap = [double]::Parse(($Matches[1] -replace ',', '.'), [Globalization.CultureInfo]::InvariantCulture)
}
elseif ($txt -match 'Groesster Karosserie-Hoehensturz:\s*([0-9]+([.,][0-9]+)?)\s*m') {
  $gap = [double]::Parse(($Matches[1] -replace ',', '.'), [Globalization.CultureInfo]::InvariantCulture)
  $metric = "Karosserie-Hoehensturz"
}
if ($gap -lt 0.0) {
  Alarm "Kennzahl (Bodenluecke/Karosserie-Hoehensturz) nicht gefunden - Ergebnisformat geaendert?"
  exit 1
}

if ($txt -match 'UNGUELTIG') {
  Alarm "Test UNGUELTIG - der Wagen hat die Strecke nicht befahren (siehe Durchfall.txt)."
  exit 1
}
if ($gap -gt 0.0 -or $txt -match 'DURCHGEFALLEN') {
  Alarm ("{0} {1:N1} m > 0 - ein Wagen faellt bei Tempo durch eine ungeladene Zelle. Karte: {2}" -f $metric, $gap, $Map)
  exit 1
}

Write-Host ""
Write-Host "[Regression] BESTANDEN - groesste Bodenluecke 0.0 m auf $Map ($points Messpunkte)." -ForegroundColor Green
exit 0
