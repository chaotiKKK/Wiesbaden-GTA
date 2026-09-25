# Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
"""Konvertiert die BigSoundBank-Aufnahmen in Import-fertige WAVs.

WOZU: Der synthetische Motorklang und das Reifenquietschen sind ersetzt
worden durch echte Aufnahmen (Nutzerwunsch 2026-09). Die Quelldateien
liegen verstreut in Downloads, teils als MP3, mit 48 kHz und 24-bit -
Unreal importiert WAV am zuverlaessigsten mono/44,1 kHz/int16. Dieses
Skript normalisiert alles nach Data/Raw/AudioSamples/<Name>.wav:

  - mono (Channels zusammengefasst)
  - 44100 Hz
  - int16, Peak auf -3 dBFS normalisiert (Dauerlaeufer -6 dB)

Die Namen hier sind die Asset-Namen in /Game/Audio/Samples (Import:
Tools/import_audio_samples.py). "loop" markiert Dauerlaeufer (Motoren,
Reifen) - die bekommen im Import bLooping=True.

Aufruf:  python Tools/make_audio_samples.py
Idempotent: vorhandenene Zieldateien werden ueberschrieben.
"""
import os
import subprocess
import sys

import imageio_ffmpeg

FFMPEG = imageio_ffmpeg.get_ffmpeg_exe()
PROJ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = r"C:\Users\HP\Downloads"
DST = os.path.join(PROJ, "Data", "Raw", "AudioSamples")

# Zielname -> (Quelldatei in Downloads, Normalisierungs-Peak dBFS, loop)
CLIPS = {
    "A_EngineGasolineSmall": ("VEHCar_Small gasoline engine (ID 0966)_BigSoundBank.com.wav", -6.0, True),
    "A_EngineDieselBig":     ("VEHCar_Big diesel engine (ID 0967)_BigSoundBank.com.wav", -6.0, True),
    "A_TireSqueal":          ("VEHCar_Handbrake 4 (ID 3510)_BigSoundBank.com.wav", -4.0, True),
    "A_ShotBerettaM12":      ("GUNAuto_Shot beretta m12 9 mm (ID 0437)_BigSoundBank.com.mp3", -3.0, False),
    "A_ShotRifle":           ("GUNShotg_Rifle shot 1 (ID 2853)_BigSoundBank.com.wav", -3.0, False),
    "A_ShotWinchester":      ("GUNShotg_Shot of winchester magnum xtr (ID 0397)_BigSoundBank.com.wav", -3.0, False),
    "A_Explosion":           ("EXPLReal_Explosion 2 (ID 1808)_BigSoundBank.com.wav", -3.0, False),
    "A_GunLoad":             ("GUNMech_Gun loading 2 (ID 1989)_BigSoundBank.com.wav", -6.0, False),
    "A_CarDoor":             ("VEHDoor_Car door 4 (ID 1526)_BigSoundBank.com.wav", -6.0, False),
    "A_CarStart":            ("VEHCar_Car starting and departure (ID 0189)_BigSoundBank.com.wav", -8.0, False),
    "A_Servo":               ("MOTRSrvo_Servomotor 2 90 2 (ID 2920)_BigSoundBank.com.wav", -9.0, False),
    # Passanten-Treffer (Nutzerwunsch 2026-09): die drei Gore-Clips decken den
    # weichen Treffer (Gewicht auf den Fuss, GORESrce), das Zerplatzen unter
    # Rad/Saege und den schweren Treffer mit Sturz (GOREBone) ab. Leichte
    # Clips werden lauter gemischt (Umgebung ist laut).
    "A_PedestrianBurst":     ("GORESrce_Gore zucchini 1 (ID 2582)_BigSoundBank.com.wav", -4.0, False),
    "A_PedestrianHit":       ("GOREBone_Gore pepper 2 (ID 2594)_BigSoundBank.com.wav", -3.0, False),
    "A_PedestrianHitHeavy":  ("GOREBone_Gore pepper 4 (ID 2596)_BigSoundBank.com.wav", -3.0, False),
}


def convert(name, src_name, peak_db, b_loop):
    src = os.path.join(SRC, src_name)
    if not os.path.isfile(src):
        return "%s FEHLT (Quelle): %s" % (name, src_name)
    dst = os.path.join(DST, "%s.wav" % name)
    # loudnorm waere zweistufig; hier reicht ein simpler Peak-Norm über
    # volume=(dBFS): messen mit volumedetect, dann anheben/absenken.
    probe = subprocess.run(
        [FFMPEG, "-hide_banner", "-i", src, "-af", "volumedetect", "-f", "null", "-"],
        capture_output=True, text=True)
    peak = 0.0
    for line in probe.stderr.splitlines():
        if "max_volume" in line:
            try:
                peak = float(line.split("max_volume:")[1].split("dB")[0].strip())
            except ValueError:
                peak = 0.0
    gain = peak_db - peak
    loops = ["-stream_loop", "-1"] if False else []   # Loop-Flag kommt vom Asset, nicht vom File
    cmd = [FFMPEG, "-y", "-hide_banner", "-loglevel", "error"] + loops + [
        "-i", src,
        "-vn", "-ac", "1", "-ar", "44100", "-sample_fmt", "s16",
        "-af", "volume=%.1f dB" % gain,
        "-c:a", "pcm_s16le", dst]
    run = subprocess.run(cmd, capture_output=True, text=True)
    if run.returncode != 0 or not os.path.isfile(dst):
        return "%s FEHLER: %s" % (name, run.stderr.strip()[:200])
    size_kb = os.path.getsize(dst) // 1024
    return "%s ok (%d KB, Peak %+.1f -> %+.1f dB, loop=%s)" % (
        name, size_kb, peak, peak_db, b_loop)


def main():
    os.makedirs(DST, exist_ok=True)
    results = []
    for name, (src_name, peak_db, b_loop) in sorted(CLIPS.items()):
        results.append(convert(name, src_name, peak_db, b_loop))
    report = os.path.join(PROJ, "Saved", "Diagnose", "audio_samples_convert.txt")
    with open(report, "w", encoding="utf-8") as f:
        f.write("\n".join(results) + "\n")
    print("\n".join(results))
    print("Bericht: %s" % report)


if __name__ == "__main__":
    sys.exit(main())
