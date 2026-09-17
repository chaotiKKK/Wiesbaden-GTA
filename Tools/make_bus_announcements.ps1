# Rendert die Linie-6-Halteansagen mit der deutschen Windows-Stimme "Hedda"
# (de-DE, offline, keine Installation noetig) zu WAV. Texte kommen aus
# announce_text.json (UTF-8, von make_bus_announcements.py) - so stehen KEINE
# Umlaut-Literale in diesem Skript (PowerShell 5.1 laese sie sonst als ANSI falsch).
Add-Type -AssemblyName System.Speech

$textFile = Join-Path $PSScriptRoot "announce_text.json"
$outDir   = Join-Path $PSScriptRoot "announce_wav"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$items = Get-Content -Raw -Encoding UTF8 $textFile | ConvertFrom-Json

$synth = New-Object System.Speech.Synthesis.SpeechSynthesizer
try { $synth.SelectVoice("Microsoft Hedda Desktop") }
catch { Write-Host "Hedda nicht gefunden - nutze Standardstimme"; }
$synth.Rate = -1   # etwas langsamer -> klarer verstaendlich

# 22050 Hz, 16 Bit, Mono: schlank und sauber fuer Sprache, importiert problemlos.
$fmt = New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo(22050, `
    [System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen, `
    [System.Speech.AudioFormat.AudioChannel]::Mono)

foreach ($it in $items) {
    $path = Join-Path $outDir $it.file
    $synth.SetOutputToWaveFile($path, $fmt)
    $synth.Speak([string]$it.text)
    Write-Host ("{0}  <-  {1}" -f $it.file, $it.name)
}
$synth.SetOutputToNull()
$synth.Dispose()
Write-Host ("Fertig: {0} WAV-Dateien in {1}" -f $items.Count, $outDir)
