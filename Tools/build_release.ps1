# Release-Pipeline WiesbadenReal - lokale Qualitaets-Gates vor der Paketierung.
#
# Warum lokal statt GitHub Actions: ein UE-5.8-Build braucht die Engine und viel
# Rechenzeit; ein gehosteter Runner ist unpraktikabel. Die Kernidee des CI/CD
# bleibt aber: NICHTS wird paketiert, das nicht kompiliert, alle Unit-Tests und
# den Rauchtest bestanden hat. Kein Gate ist ueberspringbar.
#
# "Shift left": die billigen Gates zuerst. Ein Compile-Fehler faellt nach ~2 min,
# nicht erst nach dem stundenlangen Cook. Erst wenn ALLES gruen ist, laeuft die
# teure Paketierung, danach wird die Desktop-Verknuepfung auf das frische Paket
# gezogen.
#
#   Gate 1  Kompilieren   (Build.bat WiesbadenRealEditor Development)
#   Gate 2  Unit-Tests    (Automation RunTests WiesbadenReal -> 0 Fehler)
#   Gate 3  Rauchtest     (Tools\smoke_test.ps1 -> Exit 0)
#   ----- ab hier nur bei komplett gruenem Ergebnis -----
#   Schritt 4  Paketieren (package_game.cmd -> Saved\Package\Windows\...)
#   Schritt 5  Verknuepfung 'Wiesbaden Real (Paket)' auf das neue Paket ziehen
#
# Vor dem Ueberschreiben wird das bisherige Paket nach Saved\Package_previous
# gesichert, damit eine schlechte Release umkehrbar ist.
#
# Aufruf:  Tools\build_release.cmd            (voll, inkl. Paket - dauert Stunden)
#          Tools\build_release.cmd -GatesOnly (nur Gates 0-3, ~10-15 min, fuer vor
#                                              dem Commit; ueberspringt NUR das
#                                              Paket, KEIN Qualitaets-Gate)
#          Tools\build_release.cmd -Rollback  (Sekunden: aktuelles <-> vorheriges
#                                              Paket tauschen, Verknuepfung folgt)
# Exit 0 = paketiert (bzw. Gates gruen bei -GatesOnly / Rollback ok), sonst Exit 1.

# CmdletBinding: unbekannte Flags (z. B. Tippfehler '-GateOnly' statt '-GatesOnly')
# werden abgewiesen statt still ignoriert - sonst laeuft versehentlich der VOLLE,
# stundenlange Release-Build durch (im Playtest so beobachtet).
[CmdletBinding()]
param(
    # Leer = Ordner ueber dem Projekt (siehe unten, damit das Gate im Worktree dessen Stand prueft).
    [string]$Root = "",
    [string]$EngineRoot = "C:\Program Files\Epic Games\UE_5.8",
    [switch]$GatesOnly,
    [switch]$Rollback
)

$ErrorActionPreference = "Stop"
# Ordner UEBER dem Projekt aus dem Ort dieses Skripts (Tools\ im Projekt) - im
# RUMPF bestimmt, nicht als Parameter-Vorgabe: mit [CmdletBinding()] ist
# $PSScriptRoot dort unter PowerShell 5.1 LEER (gemessen 25.09.2026 im
# Gate-Worktree: "Split-Path: leere Zeichenfolge").
if (-not $Root) { $Root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent }


# INSTALLIERTE Engine, NICHT die Kopie unter $Root.
#
# GEMESSEN am 21.09.2026: die Pipeline baute gegen
# C:\freebuff\WiesbadenReal_Sicherung\UE_5.8 (Kopie vom 11.08.2026), waehrend
# Tools\build_gate1.cmd, Wiesbaden_spielen.cmd und die Desktop-Verknuepfung
# die installierte Engine (07.09.2026) benutzen. Das Projekt-Intermediate
# traegt deren Shared-PCH; der Build starb darum in einem ENGINE-Header
# (GenericPlatform.h: C2953 "SelectIntPointerType" bereits definiert) - das
# sieht nach kaputtem Engine-Quelltext aus und ist keiner.
#
# Die Vorhandenseins-Pruefung weiter unten schlug NICHT an: die alte Kopie
# existiert ja. Ein Pfad, der da ist und trotzdem falsch ist, faellt keiner
# Test-Path-Pruefung auf - nur dem Vergleich mit dem, was sonst baut.
$Engine  = Join-Path $EngineRoot "Engine"
$BuildBat = Join-Path $Engine "Build\BatchFiles\Build.bat"
$CmdExe   = Join-Path $Engine "Binaries\Win64\UnrealEditor-Cmd.exe"
$ProjDir  = Join-Path $Root "WiesbadenReal"
$Proj     = Join-Path $ProjDir "WiesbadenReal.uproject"
$LogDir   = Join-Path $ProjDir "Saved\Logs"
$PackageDir     = Join-Path $ProjDir "Saved\Package"
$PrevPackageDir = Join-Path $ProjDir "Saved\Package_previous"
$PackageExe = Join-Path $PackageDir "Windows\WiesbadenReal.exe"
$LinkPath = Join-Path ([Environment]::GetFolderPath("Desktop")) "Wiesbaden Real (Paket).lnk"

# Desktop-Verknuepfung auf das aktuelle Paket zeigen lassen (Release UND Rollback).
function Set-PackageShortcut {
    $ws = New-Object -ComObject WScript.Shell
    $sc = $ws.CreateShortcut($LinkPath)
    $sc.TargetPath = $PackageExe
    $sc.WorkingDirectory = Split-Path $PackageExe -Parent
    $sc.Description = "Wiesbaden Real - gebackenes Paket (getestete Release-Build)"
    $sc.Save()
}

# ---- Rollback: auf das vorherige Paket zurueck (kein Bauen, Sekunden) -----
# Deployment umkehrbar machen: eine rote Release faellt sonst nicht zurueck. Der
# Tausch aktuell<->vorher laesst sich mit erneutem -Rollback wieder vorrollen.
if ($Rollback) {
    Write-Host "======== Rollback: vorheriges Paket wiederherstellen ========"
    if (-not (Test-Path (Join-Path $PrevPackageDir "Windows\WiesbadenReal.exe"))) {
        Write-Host "ABBRUCH: kein vorheriges Paket unter $PrevPackageDir - nichts zum Zurueckrollen."
        exit 1
    }
    $swapTmp = Join-Path $ProjDir "Saved\Package_swap"
    # CRASH-SICHER: Package_swap NICHT loeschen. Existiert es beim Start, wurde ein
    # frueherer Tausch unterbrochen - es haelt dann ein fertiges (stundenlang
    # gebautes) Paket. Loeschen wuerde es unwiderruflich vernichten. Stattdessen den
    # unterbrochenen Tausch abschliessen und beenden.
    if (Test-Path $swapTmp) {
        Write-Host "Unterbrochener Tausch erkannt (Package_swap vorhanden) - schliesse ihn ab (kein Datenverlust)."
        if (-not (Test-Path $PackageDir) -and (Test-Path $PrevPackageDir)) {
            Move-Item $PrevPackageDir $PackageDir -Force
        }
        if (Test-Path $swapTmp) { Move-Item $swapTmp $PrevPackageDir -Force }
        Set-PackageShortcut
        Write-Host "Wiederhergestellt: Rollback aus unterbrochenem Zustand abgeschlossen."
        exit 0
    }
    if (Test-Path $PackageDir) { Move-Item $PackageDir $swapTmp -Force }
    Move-Item $PrevPackageDir $PackageDir -Force
    if (Test-Path $swapTmp)    { Move-Item $swapTmp $PrevPackageDir -Force }
    Set-PackageShortcut
    Write-Host "Rollback fertig: '$LinkPath' zeigt jetzt auf das vorherige Paket."
    Write-Host "(Das zurueckgerollte Paket liegt nun unter Package_previous - erneutes -Rollback rollt wieder vor.)"
    exit 0
}

foreach ($p in @($BuildBat, $CmdExe, $Proj)) {
    if (-not (Test-Path $p)) { Write-Host "ABBRUCH: fehlt - $p"; exit 2 }
}
if (-not (Test-Path $LogDir)) { New-Item -ItemType Directory -Force -Path $LogDir | Out-Null }

$Start = Get-Date

# Nur Editoren DIESES Projektordners beenden - nicht jeden auf dem Rechner.
# Frueher traf "Get-Process UnrealEditor* | Stop-Process" auch fremde, laufende
# Arbeit (andere Agenten, offene Editoren); darum wartete der Push-Waechter, bis
# keiner mehr lief. Im Gate-Worktree (Tools\gate_worktree.py) haelt ohnehin nur
# der eigene Editor dessen Binaries fest.
function Stop-ProjectEditors([string]$ProjectFile) {
    $want = $ProjectFile.Replace('/', '\')
    Get-CimInstance Win32_Process -Filter "Name LIKE 'UnrealEditor%'" -ErrorAction SilentlyContinue |
        Where-Object { $_.CommandLine -and $_.CommandLine.Replace('/', '\') -like "*$want*" } |
        ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
}
function Section([int]$Num, [string]$Title) {
    Write-Host ""
    Write-Host ("==== Gate {0}: {1} ====" -f $Num, $Title)
}
# Ein Gate ist rot -> Pipeline sofort abbrechen (fail-fast), mit klarer Ursache.
function Fail([string]$Gate, [string]$Detail, [string]$LogHint) {
    Write-Host ""
    Write-Host ("XXXX PIPELINE GESTOPPT: {0} FEHLGESCHLAGEN" -f $Gate)
    Write-Host ("     {0}" -f $Detail)
    if ($LogHint) { Write-Host ("     Log: {0}" -f $LogHint) }
    Write-Host ("     Kein Paket wird gebaut. Ursache beheben und erneut laufen lassen.")
    exit 1
}

Write-Host "======== Release-Pipeline WiesbadenReal ========"
Write-Host ("Modus: {0}" -f ($(if ($GatesOnly) { "nur Gates 0-3 (-GatesOnly)" } else { "voll inkl. Paketierung" })))

# ---- Gate 0: Engine-Pfade ------------------------------------------------
#
# Sekunden, und ganz vorn: dieses Gate haette den Lauf vom 21.09.2026 gespart.
# Damals zeigte die Pipeline selbst auf die aeltere Engine-Kopie, und der
# Build starb erst zehn Minuten spaeter in einem ENGINE-Header, der aussah,
# als sei der Engine-Quelltext kaputt. Ein falscher Pfad soll hier auffallen,
# nicht im Compiler.
Section 0 "Engine-Pfade (Tools\pruefe_engine.py)"
$EngineCheck = Join-Path $PSScriptRoot "pruefe_engine.py"
if (Test-Path $EngineCheck) {
    $prevEAP0 = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    & python $EngineCheck | ForEach-Object { Write-Host ("  {0}" -f $_) }
    $engineRc = $LASTEXITCODE
    $ErrorActionPreference = $prevEAP0
    if ($engineRc -ne 0) {
        Fail "Gate 0 (Engine-Pfade)" ("Mindestens ein Werkzeug nennt eine andere Engine als {0}." -f $EngineRoot) ""
    }
    Write-Host "  Gate 0 gruen: alle Werkzeuge zeigen auf dieselbe Engine."
} else {
    Write-Host "  uebersprungen: pruefe_engine.py fehlt."
}

# ---- Gate 1: Kompilieren -------------------------------------------------
Section 1 "Kompilieren (WiesbadenRealEditor Win64 Development)"
$BuildLog = Join-Path $LogDir "release_build.log"
Remove-Item $BuildLog -ErrorAction SilentlyContinue
Stop-ProjectEditors $Proj
Start-Sleep -Seconds 2
# WICHTIG (PS 5.1): UBT schreibt routinemaessig auf stderr (auch bei Warnungen).
# Unter $ErrorActionPreference='Stop' wuerde eine ueber 2>&1 gepipte stderr-Zeile als
# terminierender NativeCommandError den Lauf abbrechen, BEVOR der Exit-Code geprueft
# wird - und eine gute Build stuerbe an einer Warnzeile. Fuer den nativen Aufruf auf
# 'Continue' schalten; ueber Erfolg/Misserfolg entscheidet allein $LASTEXITCODE.
$prevEAP = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
& $BuildBat WiesbadenRealEditor Win64 Development -project="$Proj" -waitmutex 2>&1 |
    Tee-Object -FilePath $BuildLog | Select-Object -Last 4
$ErrorActionPreference = $prevEAP
if ($LASTEXITCODE -ne 0) {
    $firstErr = (Select-String -Path $BuildLog -Pattern "error [A-Z]|Error:" -SimpleMatch:$false |
        Select-Object -First 3 | ForEach-Object { $_.Line.Trim() }) -join " | "
    Fail "Gate 1 (Kompilieren)" ("Exit {0}. Erste Fehler: {1}" -f $LASTEXITCODE, $firstErr) $BuildLog
}
Write-Host "  Gate 1 gruen: kompiliert."

# ---- Gate 2: Unit-Tests --------------------------------------------------
Section 2 "Unit-Tests (Automation RunTests WiesbadenReal)"
$TestLog = Join-Path $LogDir "release_tests.log"
Remove-Item $TestLog -ErrorAction SilentlyContinue
# WICHTIG: -ExecCmds MUSS ueber eine .bat mit exakter Quotierung laufen. PowerShell
# (`& exe -ExecCmds="a b; c"` oder Start-Process -ArgumentList) zerlegt den Wert an
# Leerzeichen/Semikolon -> der Cmd startet ohne ExecCmds und schreibt kein Log.
$TestBat = Join-Path $LogDir "release_run_tests.bat"
@"
@echo off
"$CmdExe" "$Proj" -ExecCmds="Automation RunTests WiesbadenReal; Quit" -unattended -nop4 -nullrhi -NoSound -stdout -ABSLOG="$TestLog"
"@ | Set-Content -Path $TestBat -Encoding ASCII
& cmd /c "`"$TestBat`"" | Out-Null
$testText = if (Test-Path $TestLog) { Get-Content $TestLog -Raw } else { "" }
$pass = ([regex]::Matches($testText, "Result=\{Success\}")).Count
$fail = ([regex]::Matches($testText, "Result=\{Fail\}")).Count
$complete = $testText -match "TEST COMPLETE\. EXIT CODE: 0"
Write-Host ("  {0} bestanden, {1} fehlgeschlagen, Abschluss-Marker: {2}" -f $pass, $fail, $(if ($complete) { "ja" } else { "NEIN" }))
if ($fail -gt 0)      { Fail "Gate 2 (Unit-Tests)" ("{0} Test(s) fehlgeschlagen." -f $fail) $TestLog }
if ($pass -lt 1)      { Fail "Gate 2 (Unit-Tests)" "Kein Test lief (Registrierung/Build kaputt?)." $TestLog }
if (-not $complete)   { Fail "Gate 2 (Unit-Tests)" "Kein sauberer Abschluss-Marker (Lauf abgebrochen?)." $TestLog }
Write-Host "  Gate 2 gruen: alle Unit-Tests bestanden."

# ---- Gate 3: Rauchtest ---------------------------------------------------
Section 3 "Rauchtest (Tools\smoke_test.ps1)"
$SmokePs1 = Join-Path $ProjDir "Tools\smoke_test.ps1"
if (-not (Test-Path $SmokePs1)) { Fail "Gate 3 (Rauchtest)" "smoke_test.ps1 fehlt." "" }
& powershell -NoProfile -ExecutionPolicy Bypass -File $SmokePs1 -Root $Root -EngineRoot $EngineRoot
if ($LASTEXITCODE -ne 0) {
    Fail "Gate 3 (Rauchtest)" ("Rauchtest Exit {0} - mindestens eine Pruefung durchgefallen." -f $LASTEXITCODE) `
        (Join-Path $LogDir "smoke_heli.log")
}
Write-Host "  Gate 3 gruen: Rauchtest bestanden."

# ---- Alle Gates gruen ----------------------------------------------------
if ($GatesOnly) {
    Write-Host ""
    Write-Host "======== ALLE GATES GRUEN (Paketierung uebersprungen: -GatesOnly) ========"
    Write-Host ("Dauer: {0:N1} min" -f ((Get-Date) - $Start).TotalMinutes)
    exit 0
}

# ---- Schritt 4: Paketieren ----------------------------------------------
Section 4 "Paketieren (package_game.cmd - dauert lange)"
$PackageCmd = Join-Path $ProjDir "package_game.cmd"
if (-not (Test-Path $PackageCmd)) { Fail "Schritt 4 (Paketieren)" "package_game.cmd fehlt." "" }
# Vorheriges Paket als Rollback-Ziel sichern, BEVOR das neue es ueberschreibt.
if (Test-Path $PackageExe) {
    Remove-Item $PrevPackageDir -Recurse -Force -ErrorAction SilentlyContinue
    Move-Item $PackageDir $PrevPackageDir -Force
    Write-Host "  Vorheriges Paket gesichert -> $PrevPackageDir (fuer 'build_release.cmd -Rollback')."
}
& cmd /c "`"$PackageCmd`""
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $PackageExe)) {
    Fail "Schritt 4 (Paketieren)" ("BuildCookRun Exit {0}, Paket-Exe {1}. Das vorige Paket liegt unter Package_previous - mit 'build_release.cmd -Rollback' in Sekunden wiederherstellen." -f $LASTEXITCODE, $(if (Test-Path $PackageExe) { "vorhanden" } else { "FEHLT" })) `
        (Join-Path $ProjDir "package_game.log")
}
Write-Host "  Paket gebaut: $PackageExe"

# ---- Schritt 5: Desktop-Verknuepfung erneuern ---------------------------
Section 5 "Desktop-Verknuepfung auf das neue Paket ziehen"
Set-PackageShortcut
Write-Host "  Verknuepfung erneuert: $LinkPath -> $PackageExe"

Write-Host ""
Write-Host "======== RELEASE FERTIG: getestet, paketiert, Verknuepfung aktuell ========"
Write-Host ("Dauer gesamt: {0:N1} min" -f ((Get-Date) - $Start).TotalMinutes)
Write-Host "Rueckrollen bei Problemen:  Tools\build_release.cmd -Rollback"
exit 0
