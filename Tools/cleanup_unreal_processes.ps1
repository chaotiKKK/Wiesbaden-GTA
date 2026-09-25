[CmdletBinding()]
param(
    [switch]$DryRun,
    # Abweichender Lock-Pfad (nur Tests). Leer = maschinenweiter Engine-Lock.
    [string]$LockPfad = "",
    # NOTAUSGANG: auch beenden, wenn ein fremder Lauf den Lock haelt. Nur, wenn
    # dieser Lauf bewusst aufgegeben wird - der andere laeuft dann in einen
    # abrupten Abbruch seiner Sitzung.
    [switch]$SperreIgnorieren
)

$ErrorActionPreference = "Stop"

function Get-EngineReste {
    @(
        Get-Process -Name "UnrealEditor*" -ErrorAction SilentlyContinue
        Get-Process -Name "zenserver" -ErrorAction SilentlyContinue
    ) | Where-Object { $_ } | Sort-Object Id -Unique
}

function Stop-EngineReste($Prozesse) {
    foreach ($prozess in @($Prozesse)) {
        Stop-Process -Id $prozess.Id -Force -ErrorAction SilentlyContinue
    }
}

# -- Engine-Lock -----------------------------------------------------------
# Dieser Cleanup ist bewusst global (er raeumt den ZenServer-Limbo aus fremden
# Restprozessen) - genau deshalb hat er am 25.09.2026 einen bereits als rot
# gemeldeten Gate-Lauf mitgerissen: dessen Rauchtest-Editor wurde beendet, waehrend
# der zweite Push seine Gates fuhr. Solange ein fremder Lauf den Engine-Lock
# haelt, wird deshalb GAR NICHTS beendet und der Lauf bricht verstaendlich ab.
# Gehoert der Lock diesem Lauf (Gate -> Rauchtest -> Cleanup), ist er "eigen" und
# das Beenden bleibt erlaubt.
if (-not $SperreIgnorieren) {
    & "$PSScriptRoot\engine_run_lock.ps1" -Modus Status -LockPfad $LockPfad
    if ($LASTEXITCODE -eq 3) {
        throw ("Engine-Lock ist von einem anderen Lauf belegt (Besitzer siehe Zeile oben). " +
               "Ein Beenden wuerde diesen Lauf mitten in der Sitzung zerstoeren. " +
               "Auf ihn warten: endet er, ist die Sperre verwaist und wird beim " +
               "naechsten Start automatisch uebernommen. Nur mit -SperreIgnorieren " +
               "bewusst darueber hinweg beenden.")
    }
}

$prozesse = @(Get-EngineReste)
if ($prozesse.Count -gt 0) {
    Write-Host "Prozessbereinigung: UnrealEditor/zenserver-Reste werden beendet."
    foreach ($prozess in $prozesse) {
        Write-Host ("  {0} (PID {1})" -f $prozess.ProcessName, $prozess.Id)
    }
    if (-not $DryRun) { Stop-EngineReste $prozesse }
} else {
    Write-Host "Prozessbereinigung: keine UnrealEditor/zenserver-Reste."
}

if ($DryRun) {
    return
}

# ZenServer behaelt nach einem harten Editorabbruch sonst Port 8558. Die
# Mindestpause ist Teil des Starts: ein frischer Editor darf nicht auf den
# alten Limbo-Server warten.
Start-Sleep -Seconds 3

# Ein UnrealEditor-Cmd aus dem vorigen Gate braucht nach Stop-Process manchmal
# laenger als 3 s, bis der Prozess wirklich aus der Prozessliste verschwindet.
# Genau das war der erste rote Gate-Lauf: der Rest wurde korrekt gekillt, aber
# zu frueh als Fehler gewertet. Ein zweiter Kill mit Wartefrist behebt den
# Shutdown-Race, ohne die 3-Sekunden-Regel aufzugeben.
$rest = @(Get-EngineReste)
if ($rest.Count -gt 0) {
    Write-Host ("Prozessbereinigung: {0} Rest(e) nach 3 s - zweiter Versuch, Wartefrist 10 s." -f $rest.Count)
    Stop-EngineReste $rest
    $frist = (Get-Date).AddSeconds(10)
    while ($rest.Count -gt 0 -and (Get-Date) -lt $frist) {
        Start-Sleep -Seconds 1
        $rest = @(Get-EngineReste)
    }
}

if ($rest.Count -gt 0) {
    $namen = ($rest | ForEach-Object { "$($_.ProcessName)/$($_.Id)" }) -join ", "
    throw "Prozessbereinigung unvollstaendig: $namen"
}
