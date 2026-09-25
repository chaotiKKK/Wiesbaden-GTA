"""Rendert den MS_EngineBoxer-Graphen offline nach und analysiert den Mix.

1:1-Nachbau des MetaSound-Graphen aus
``Source/WiesbadenReal/Audio/WbAudioAssetsCommandlet.cpp`` (BuildEngineSource):
Zuendpuls (Sine, Rpm * 0.0333 Hz, FiringGain), Ansaugung (Pink-Noise -> Einpol-
Tiefpass -> * Throttle), Auspuff (Pink -> TP -> * Exhaust Gain), Rollen (Pink ->
TP -> * Roll Gain), Hupe (Sine 65 Hz -> * Horn). Summe, Tor = EngineRunning.

Knoten-Semantik, ueberprueft im UE-5.8-Quelltext:
- "Noise" (MetasoundNoiseGenerator.cpp) erzeugt standardmaessig PINK-Noise
  (Vertex-Default ENoiseType::Pink), Ausgang ueber die Filtervorschrift aus
  SignalProcessing/Private/Noise.cpp (CCRMA-Design, a =
  [1, -2.494956002, 2.017265875, -0.522189400], b =
  [0.049922035, -0.095993537, 0.050612699, -0.004408786]).
- "One-Pole Low Pass Filter" = Audio::FInterpolatedLPF: y = (1-b1)*x + b1*y1
  mit b1 = exp(-2*pi*fc/fs) (InterpolatedOnePole.cpp).
- "Sine" = Sinus mit Amplitude 1.

Rausch-Layer werden ueber die Frequenzgang-Schaltung gerendert (weißes
Rauschen x |H_pink| x |H_onepole|) - fuer stationaere Rauschanteile
statistisch identisch zur zeitreihenweisen IIR-Kette, aber exakt und schnell.

Aufruf:
  python Tools/render_engineboxer.py alt      # Werte aus WbAudioAssetsCommandlet.cpp
  python Tools/render_engineboxer.py neu      # MIX_NEU unten (nach der Justage)
  python Tools/render_engineboxer.py analyse  # nur Messwerte beider Saetze

Ausgabe: .planning/audio-overhaul/mix/<satz>_<zustand>.wav (+ Layer-Solos),
Messwerte auf stdout.
"""
import os
import sys
import wave

import numpy as np

SR = 48000
OUT_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                       '..', '.planning', 'audio-overhaul', 'mix')
OUT_DIR = os.path.normpath(OUT_DIR)

# ---------------------------------------------------------------------------
# Mix-Werte. "alt" = die Default-Werte der Graph-Knoten in
# WbAudioAssetsCommandlet.cpp (BuildEngineSource, Stand 24.09.2026).
# ---------------------------------------------------------------------------
MIX_ALT = {
    'firing_gain': 0.5,        # Graph.Input "FiringGain"
    'intake_cutoff': 500.0,    # "Intake Cutoff Hz"
    'intake_throttle': 1.0,    # skaliert zur Laufzeit mit Throttle (hier: Faktor 1 = volle Last)
    'exhaust_cutoff': 140.0,   # "Exhaust Cutoff Hz"
    'exhaust_gain': 0.35,      # "Exhaust Gain"
    'roll_cutoff': 900.0,      # "Roll Cutoff Hz"
    'roll_gain': 0.25,         # "Roll Gain"
}

# "neu" = Justage 24.09.2026 (Begruendung siehe findings.md): die Rausch-Layer
# lagen 25-31 dB unter dem Zündpuls - der Mix war faktisch ein reiner Sinus.
# Ziele (Zustand fahrt): firing ~-10.5 dB, intake (halb Gas) ~-21, exhaust ~-22,
# roll ~-25. Zusaetzlich bekam die Ansaugung einen festen Layer-Gain (wie alle
# anderen Layer auch; sie war der einzige Layer ohne Balance-Wert).
MIX_NEU = {
    'firing_gain': 0.42,       # 0.50 -> 0.42: Zündpuls als Fundament, nicht als Solist
    'intake_cutoff': 1000.0,   # 500 -> 1000: Ansaug-Rauschen hoerbar nach oben geoeffnet
    'intake_gain': 4.0,        # NEU: fester Layer-Gain vor dem Throttle-Multiplier
    'exhaust_cutoff': 230.0,   # 140 -> 230: mehr Koerper/Knarzen im Auspuff
    'exhaust_gain': 1.8,       # 0.35 -> 1.8
    'roll_cutoff': 650.0,      # 900 -> 650: Reifenlauf dunkler, laesst der Ansaugung Platz
    'roll_gain': 1.2,          # 0.25 -> 1.2
}

# Betriebszustaende wie in den audio_drive-Laeufen (WbDrive-Tour): Leerlauf,
# Fahrt mit halbem Gas, Vollgas.
ZUSTAENDE = {
    'leerlauf': [(900.0, 0.0), (900.0, 0.0)],
    'fahrt':    [(1600.0, 0.5), (3000.0, 0.5)],
    'vollgas':  [(1500.0, 1.0), (5000.0, 1.0)],
}
DAUER_S = 6.0


# ---------------------------------------------------------------------------
# Signalkette
# ---------------------------------------------------------------------------

def pink_response(n, sr):
    """|H(f)| der Pink-Noise-Kette aus SignalProcessing/Private/Noise.cpp."""
    a = [1.0, -2.494956002, 2.017265875, -0.522189400]
    b = [0.049922035, -0.095993537, 0.050612699, -0.004408786]
    f = np.fft.rfftfreq(n, 1.0 / sr)
    z = np.exp(-2j * np.pi * f / sr)
    num = sum(bk * z ** k for k, bk in enumerate(b))
    den = sum(ak * z ** k for k, ak in enumerate(a))
    return np.abs(num / den)


def onepole_lp_response(n, sr, cutoff_hz):
    """|H(f)| von Audio::FInterpolatedLPF: y=(1-b1)x+b1*y1, b1=exp(-2*pi*fc/fs)."""
    b1 = np.exp(-2.0 * np.pi * cutoff_hz / sr)
    f = np.fft.rfftfreq(n, 1.0 / sr)
    z = np.exp(-2j * np.pi * f / sr)
    return np.abs((1.0 - b1) / (1.0 - b1 * z))


def noise_layer(n, sr, cutoff_hz, seed):
    """Pink-Noise durch Einpol-Tiefpass (Frequenzgang-Schaltung)."""
    rng = np.random.default_rng(seed)
    white = rng.uniform(-1.0, 1.0, n)
    spec = np.fft.rfft(white)
    spec *= pink_response(n, sr) * onepole_lp_response(n, sr, cutoff_hz)
    return np.fft.irfft(spec, n)


def sine_layer(freqs_hz, sr):
    """Sinus mit fortlaufender Phase (wie der Sine-Knoten, Amplitude 1)."""
    phase = np.cumsum(freqs_hz) / sr
    return np.sin(2.0 * np.pi * phase)


def render(mix, zustand, solo=None, horn=0.0):
    """Rendert einen Betriebszustand; solo = Name eines Layers fuer Solo-Hoerprobe."""
    rpm0, thr0 = ZUSTAENDE[zustand][0]
    rpm1, thr1 = ZUSTAENDE[zustand][1]
    n = int(DAUER_S * SR)
    t = np.linspace(0.0, 1.0, n)
    rpm = rpm0 + (rpm1 - rpm0) * t
    thr = thr0 + (thr1 - thr0) * t

    firing = sine_layer(rpm * 0.0333, SR) * mix['firing_gain']
    intake = (noise_layer(n, SR, mix['intake_cutoff'], 1234)
              * mix.get('intake_gain', mix.get('intake_throttle', 1.0)) * thr)
    exhaust = noise_layer(n, SR, mix['exhaust_cutoff'], 5678) * mix['exhaust_gain']
    roll = noise_layer(n, SR, mix['roll_cutoff'], 9012) * mix['roll_gain']
    horn_sig = sine_layer(np.full(n, 65.0), SR) * horn

    layer = {'firing': firing, 'intake': intake, 'exhaust': exhaust,
             'roll': roll, 'horn': horn_sig}
    if solo is not None:
        out = layer[solo].copy()
    else:
        out = sum(layer.values())
    return out, layer


def write_wav(path, sig):
    sig = np.clip(sig, -1.0, 1.0)
    pcm = (sig * 32767.0).astype('<i2')
    with wave.open(path, 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(pcm.tobytes())


# ---------------------------------------------------------------------------
# Messung
# ---------------------------------------------------------------------------

BANDS = [(20, 100), (100, 500), (500, 2000), (2000, 8000)]


def db(x):
    return 20.0 * np.log10(max(x, 1e-9))


def analyse(name, sig):
    n = len(sig)
    rms = float(np.sqrt(np.mean(sig ** 2)))
    spec = np.abs(np.fft.rfft(sig))
    f = np.fft.rfftfreq(n, 1.0 / SR)
    band_txt = []
    for lo, hi in BANDS:
        m = (f >= lo) & (f < hi)
        e = float(np.sqrt(np.mean(spec[m] ** 2))) if m.any() else 0.0
        band_txt.append(f"{lo}-{hi}Hz {db(e):6.1f} dB")
    cent = float((f * spec).sum() / max(spec.sum(), 1e-9))
    print(f"  {name:22s} RMS {db(rms):6.1f} dB  Schwerpunkt {cent:5.0f} Hz  |  "
          + "  ".join(band_txt))
    return rms


def main():
    satz = sys.argv[1] if len(sys.argv) > 1 else 'alt'
    mix = MIX_ALT if satz == 'alt' else MIX_NEU
    os.makedirs(OUT_DIR, exist_ok=True)

    if satz != 'analyse':
        for zustand in ZUSTAENDE:
            out, layer = render(mix, zustand)
            write_wav(os.path.join(OUT_DIR, f'{satz}_{zustand}.wav'), out)
            print(f"{OUT_DIR}/{satz}_{zustand}.wav")
        # Layer-Solos nur fuer "fahrt" - dort hoert man die Balance.
        _, layer = render(mix, 'fahrt')
        for lname, lsig in layer.items():
            if lname == 'horn':
                continue
            write_wav(os.path.join(OUT_DIR, f'{satz}_fahrt_solo_{lname}.wav'), lsig)

    print(f"\nMesswerte ({'alt = Ist-Mix aus WbAudioAssetsCommandlet.cpp' if satz == 'alt' else satz}):")
    for zustand in ZUSTAENDE:
        out, layer = render(mix, zustand)
        print(f" {zustand}:")
        analyse('SUMME', out)
        for lname, lsig in layer.items():
            if lname != 'horn':
                analyse(lname, lsig)


if __name__ == '__main__':
    main()
