# Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
r"""Laedt die echten Stadtklang-Aufnahmen fuer die Audio-Zonen herunter.

WOZU: Die Klanglagen waren bisher ein Pegel-Mix aus SYNTHETISCHEN
MetaSound-Betten (Noise) - die Zonen haben nur Lautstaerken gemischt
(Saved/Diagnose/ab_bericht.md-bekannte Baustelle "echte Samples fehlen").
Dieses Skript holt echte Field-Recordings von BigSoundBank (Lizenz CC0-
aehnlich: "copy, remix, redistribute, and use commercially worldwide",
bigsoundbank.com/licenses.html - dieselbe Quelle wie die Fahrzeug-/Waffen-
Clips aus Tools/make_audio_samples.py) und normalisiert sie exakt wie dort:

  - mono, 44100 Hz, int16
  - Peak auf -6 dBFS (Dauerlaeufer, wie die Motoren-Clips)

Ziel je Clip: Data/Raw/AudioSamples/<Name>.wav (Import ueber
Tools/import_audio_samples.py nach /Game/Audio/Samples).

Aufruf:  python Tools/fetch_ambience_samples.py
Idempotent: vorhandene Zieldateien werden ueberschrieben; die Herkunft
steht in Data/Raw/AudioSamples/A_Amb_quellen.txt (Beweis, nicht Kommentar).
"""
import os
import subprocess
import sys

import imageio_ffmpeg

FFMPEG = imageio_ffmpeg.get_ffmpeg_exe()
PROJ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DST = os.path.join(PROJ, "Data", "Raw", "AudioSamples")
QUELLEN = os.path.join(DST, "A_Amb_quellen.txt")

# Name -> (BigSoundBank-Sound-Id, Kurzbeschreibung fuer den Zonen-Einsatz)
CLIPS = {
    # Verkehrssummen - ersetzt das synthetische City-Bett (127 s).
    "A_AmbTraffic":  ("0608", "Street and Roads - Verkehrssummen (City/Traffic-Lage)"),
    # Stimmen/Fussgaenger - die Innenstadt-Lage (73 s).
    "A_AmbCrowd":    ("0527", "Small pedestrian street - Menschenmurmeln (Commercial)"),
    # Maschinen/Handwerk - die Industrie-Lage (120 s).
    "A_AmbIndustry": ("0503", "Industry Workshop - Maschinenraum (Industrial)"),
    # Voegel am Morgen - die Gruen-Lage (175 s).
    "A_AmbBirds":    ("0222", "Birds Waking #1 - Vogelchor (Quiet/Tag)"),
    # Wind - bleibt als Grundlage ueberall hoerbar (90 s).
    "A_AmbWind":     ("0595", "Wind - Windbett (alle Zonen)"),
    # Nacht nach Regen - die Nacht-Lage (68 s).
    "A_AmbNight":    ("0621", "Night after rain - Nachtambiente"),
    # Kinder auf der Strasse - die Wohn-Lage (28 s).
    "A_AmbChildren": ("0370", "Children in the Street - Strassenleben (Residential)"),
}

URL = "https://bigsoundbank.com/UPLOAD/bwf-en/%s.wav"
PEAK_DB = -6.0


def hole(name, sound_id, beschreibung):
    ziel = os.path.join(DST, "%s.wav" % name)
    roh = os.path.join(DST, "%s_roh.wav" % name)
    subprocess.run(["curl", "-sL", "--max-time", "180", URL % sound_id, "-o", roh],
                   check=True)
    if not os.path.isfile(roh) or os.path.getsize(roh) < 100000:
        return "%s FEHLT/zu klein (Download %s)" % (name, URL % sound_id)

    # Wie make_audio_samples.py: mono/44,1 kHz/int16, Peak-Norm auf -6 dBFS.
    # Peak messen, dann normieren (zwei Durchlaeufe, einfach und nachvollziehbar).
    mess = subprocess.run(
        [FFMPEG, "-i", roh, "-af", "volumedetect", "-f", "null", "-"],
        capture_output=True, text=True)
    peak = 0.0
    for zeile in mess.stderr.splitlines():
        if "max_volume" in zeile:
            peak = float(zeile.split(":")[-1].split()[0])
    gain = PEAK_DB - peak
    subprocess.run([
        FFMPEG, "-y", "-i", roh,
        "-ac", "1", "-ar", "44100", "-c:a", "pcm_s16le",
        "-af", "volume=%.1fdB" % gain,
        ziel,
    ], check=True, capture_output=True)
    os.remove(roh)

    groesse = os.path.getsize(ziel) // 1024
    dauer = subprocess.run(
        [FFMPEG, "-i", ziel, "-f", "null", "-"],
        capture_output=True, text=True)
    zeit = ""
    for zeile in dauer.stderr.splitlines():
        if "Duration" in zeile:
            zeit = zeile.split("Duration:")[1].split(",")[0].strip()
    return "%s ok (%s KB, %s, Peak %.1f -> %.1f dB)" % (
        name, groesse, zeit, peak, PEAK_DB)


def hauptprogramm():
    os.makedirs(DST, exist_ok=True)
    zeilen = []
    for name, (sound_id, beschreibung) in sorted(CLIPS.items()):
        ergebnis = hole(name, sound_id, beschreibung)
        print(ergebnis)
        zeilen.append("%s | BigSoundBank %s | %s" % (name, sound_id, beschreibung))
    with open(QUELLEN, "w", encoding="utf-8") as f:
        f.write("Herkunft der Ambience-Samples (Lizenz: bigsoundbank.com/licenses.html,\n")
        f.write("CC0-aehnlich, kommerzielle Nutzung ohne Namensnennung erlaubt):\n\n")
        for zeile in zeilen:
            f.write(zeile + "\n")
    print("Quellen dokumentiert: %s" % QUELLEN)
    return 0


if __name__ == "__main__":
    sys.exit(hauptprogramm())
