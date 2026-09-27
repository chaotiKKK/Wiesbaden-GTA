"""Rendert den MS_EngineBoxer-Graphen offline nach und analysiert den Mix.

1:1-Nachbau des MetaSound-Graphen aus
``Source/WiesbadenReal/Audio/WbAudioAssetsCommandlet.cpp`` (BuildEngineSource):
Zuendpuls, Ansaugung (Pink-Noise -> Einpol-Tiefpass -> * Throttle), Auspuff
(Pink -> TP -> * Exhaust Gain), Rollen (Pink -> TP -> * Roll Gain), Hupe
(Sine 65 Hz -> * Horn). Summe, Tor = EngineRunning.

Der Zuendpuls ist je Satz verschieden: "alt"/"neu" ein reiner Sinus
(Rpm * 0.0333 Hz * FiringGain), "oberton" das Oberwell-Gemisch der Referenz
(WiesbadenEngineAudio.cpp): sechs gewichtete Harmonische {1..6} der
Zuednfrequenz, Gewichte {1.00, 0.62, 0.38, 0.22, 0.13, 0.07} / 2.42,
Amplitude mit Boxer-Versatz (1 + 0.14 * sin auf halber Zuednfrequenz),
anschliessend weiche Saettigung tanh(x * 1.3) ueber die Motor-Layer
(WaveShaper, Hupe kommt wie in der Referenz danach).

Ab "dynamik" (25.09.2026) tragen der Zündpuls einen Last-Gain
(0.62 + 0.76 * Throttle) und der Roll-Layer die Tempo-Abhaengigkeit aus dem
Graphen (Gain voll ab 50 km/h, Tiefpass 390 + 5.2 * km/h, begrenzt
200..2500 Hz) - beides ueber die Parameter Throttle/SpeedKmh. Die Hupe ist
ab "dynamik" das Nebelhorn der Referenz: 65 Hz + reine Quinte 98 Hz, je
vier abfallende Teiltoene {1, 0.60, 0.32, 0.16} (unnormiert),
tanh((A+B) * 0.42) und ein traege 180 ms einschwingender Tor (InterpTo,
lineare Rampe); vorher ein reiner 65-Hz-Sinus. Ausserdem dreht die
Faerbung des Zuendpulses mit dem Gas (Oberton-Verschiebung: Delta-Stab
mal (2 * Throttle - 1), dunkler im Leerlauf, heller unter Last) und der
Ansaug-Tiefpass oeffnet mit dem Gas (312.5 + 1375 * Throttle Hz,
Clamp 200..3000 - halb Gas exakt die alten 1000 Hz).

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
  python Tools/render_engineboxer.py oberton  # MIX_OBERTON (Endabnahme-Mix)
  python Tools/render_engineboxer.py dynamik  # MIX_DYNAMIK (aktueller Graph)
  python Tools/render_engineboxer.py analyse  # nur Messwerte aller Saetze

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

# "oberton" = Stand 24.09.2026 spaet: der Zuendpuls traegt jetzt das
# Oberwell-Gemisch der Referenz (WiesbadenEngineAudio.cpp) - sechs gewichtete
# Harmonische, Boxer-Versatz auf halber Zuednfrequenz, tanh-Softsaettigung
# ueber die Motor-Layer. Der FiringGain steigt 0.42 -> 0.80: die normierte
# Oberton-Summe misst 5.6 dB leiser als ein reiner Sinus (Gewichte/2.42),
# der Ausgleich haelt die Balance aus der Mix-Abstimmung.
MIX_OBERTON = {
    **MIX_NEU,
    'firing_gain': 0.80,
    'harmonics': True,   # Additive-Synth-Stack + Boxer-Modulation
    'tanh': True,        # WaveShaper Tanh ueber die Motor-Layer
}

# "dynamik" = 25.09.2026, der aktuelle Graph nach der Endabnahme: der
# Zuendpuls traegt einen Last-Gain (0.62 + 0.76 * Throttle), der Roll-Layer
# skaliert mit SpeedKmh (Gain voll ab 50 km/h, Tiefpass 390 + 5.2 * km/h,
# Clamp 200..2500). Auf die freigegebene Endabnahme geeicht: bei halbem Gas
# und 50 km/h (Zustand "fahrt") ist beides exakt 1.0 bzw. die alten 650 Hz -
# dynamik_fahrt ist damit bit-identisch zu oberton_fahrt.
MIX_DYNAMIK = {
    **MIX_OBERTON,
    'last_base': 0.62,         # "Last Base"
    'last_scale': 0.76,        # "Last Scale" (pro Throttle-Einheit)
    'roll_cutoff': 390.0,      # "Roll Cutoff Hz" (Abschneidebasis im Stand)
    'roll_cut_per_kmh': 5.2,   # "Roll Cutoff Per Kmh"
    'roll_speed_scale': 0.02,  # "Roll Speed Scale" (voll ab 50 km/h, Clamp 0..1)
    'horn_stack': True,        # Nebelhorn 65+98 Hz wie WiesbadenEngineAudio.cpp
    'last_shift': True,        # Oberton-Verschiebung: (2 * Throttle - 1) * Delta
    'intake_cutoff': 312.5,    # "Intake Cutoff Hz" (Stand; war 1000 konstant)
    'intake_cut_per_thr': 1375.0,  # "Intake Cutoff Per Throttle" (halb Gas = 1000 Hz)
}

# "sport" = Profil-Studie 25.09.2026, NICHT der Graph-Stand (dynamik).
# Straffere Last-Kennlinie: 0.55 + 1.25 * Throttle statt 0.62 + 0.76 -
# halb Gas liegt 1.4 dB ueber dynamik, der Leerlauf 1.0 dB darunter,
# Vollgas 2.4 dB darueber. Dazu der doppelte Faerbungs-Tilt
# (shift_scale 2.0): noch dunklerer Leerlauf, noch helleres Vollgas -
# die dunkle Seite bleibt gueltig (Kleinstgewicht 0.01/2.42 auf der
# 6. Harmonischen). Uebertrag in den Graph waere ein reiner
# Konstanten-Satz (oder ein zweites Asset MS_EngineBoxerSport).
MIX_SPORT = {
    **MIX_DYNAMIK,
    'last_base': 0.55,         # straffere Kennlinie, steilerer Antritt
    'last_scale': 1.25,
    'shift_scale': 2.0,        # doppelter Faerbungs-Tilt
}

# Betriebszustaende wie in den audio_drive-Laeufen (WbDrive-Tour): Leerlauf,
# Fahrt mit halbem Gas, Vollgas - je (Rpm, Throttle, SpeedKmh) von/nach.
ZUSTAENDE = {
    'leerlauf': [(900.0, 0.0, 0.0), (900.0, 0.0, 0.0)],
    'fahrt':    [(1600.0, 0.5, 50.0), (3000.0, 0.5, 50.0)],
    'vollgas':  [(1500.0, 1.0, 40.0), (5000.0, 1.0, 140.0)],
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


def noise_layer_tv(n, sr, cutoff_arr, seed):
    """Pink-Noise durch EINPOL-Tiefpass mit zeitveraenderlicher Grenzfrequenz
    (wie im Graphen, wo "Roll Cutoff Per Kmh" den Filter fuehrt): der
    Frequenzgang-Trick gilt nur bei konstantem Cutoff."""
    rng = np.random.default_rng(seed)
    white = rng.uniform(-1.0, 1.0, n)
    spec = np.fft.rfft(white) * pink_response(n, sr)
    pink = np.fft.irfft(spec, n)
    b1 = np.exp(-2.0 * np.pi * cutoff_arr / sr)
    out = np.empty(n)
    y = 0.0
    for i in range(n):
        y = (1.0 - b1[i]) * pink[i] + b1[i] * y
        out[i] = y
    return out


def sine_layer(freqs_hz, sr):
    """Sinus mit fortlaufender Phase (wie der Sine-Knoten, Amplitude 1)."""
    phase = np.cumsum(freqs_hz) / sr
    return np.sin(2.0 * np.pi * phase)


def horn_stack(freqs_hz, sr):
    """Oberton-Stab wie in WiesbadenEngineAudio.cpp (Stack-Lambda): vier
    abfallende Teiltoene {1, 0.60, 0.32, 0.16} der Grundfrequenz, bewusst
    NICHT normiert - die Pegelkontrolle kommt aus tanh((A+B)*0.42)."""
    phase = np.cumsum(freqs_hz) / sr
    out = np.zeros(len(freqs_hz))
    for k, w in enumerate([1.00, 0.60, 0.32, 0.16], start=1):
        out += w * np.sin(2.0 * np.pi * k * phase)
    return out


def horn_layer(n, gate, sr):
    """Nebelhorn wie der aktuelle Graph: Staebe bei 65 und 98 Hz, Summe,
    WaveShaper Tanh (Amount 0.42, OutputGain tanh(0.42) = exakt
    tanh(0.42*x)), Tor mit 180-ms-Rampe (InterpTo, linear auf den
    Zielwert), Schlussgain 0.85."""
    if np.isscalar(gate):
        gate = np.full(n, float(gate))
    shaped = np.tanh((horn_stack(np.full(n, 65.0), sr)
                     + horn_stack(np.full(n, 98.0), sr)) * 0.42)
    env = np.empty(n)
    y = 0.0
    step = 1.0 / (0.18 * sr)
    for i in range(n):
        tgt = gate[i]
        if y < tgt:
            y = min(tgt, y + step)
        elif y > tgt:
            y = max(tgt, y - step)
        env[i] = y
    return shaped * env * 0.85


def harmonic_layer(freqs_hz, sr):
    """Oberton-Gemisch wie der Additive-Synth-Knoten:
    summe w_k * sin(2*pi*k*f*t) / 2.42 (Gewichte der Referenz)."""
    phase = np.cumsum(freqs_hz) / sr
    weights = [1.00, 0.62, 0.38, 0.22, 0.13, 0.07]
    out = np.zeros(len(freqs_hz))
    for k, w in enumerate(weights, start=1):
        out += w * np.sin(2.0 * np.pi * k * phase)
    return out / 2.42


def delta_layer(freqs_hz, sr, scale=1.0):
    """Delta der Oberton-Verschiebung (zweiter Additive-Synth-Knoten im
    Graph): GEWICHTETE Abweichung {-0.14, -0.02, +0.03, +0.05, +0.05,
    +0.03} / 2.42 von den Basis-Gewichten - dunkler im Leerlauf, heller
    unter Last. Im Graph stehen die Betraege in Amplitudes, die Vorzeichen
    als Phase 180 Grad. scale staerkt den Tilt (Profil "sport")."""
    phase = np.cumsum(freqs_hz) / sr
    weights = [-0.14 * scale, -0.02 * scale, 0.03 * scale,
               0.05 * scale, 0.05 * scale, 0.03 * scale]
    out = np.zeros(len(freqs_hz))
    for k, w in enumerate(weights, start=1):
        out += w * np.sin(2.0 * np.pi * k * phase)
    return out / 2.42


def render(mix, zustand, solo=None, horn=0.0):
    """Rendert einen Betriebszustand; solo = Name eines Layers fuer Solo-Hoerprobe."""
    (rpm0, thr0, kmh0), (rpm1, thr1, kmh1) = ZUSTAENDE[zustand]
    n = int(DAUER_S * SR)
    t = np.linspace(0.0, 1.0, n)
    rpm = rpm0 + (rpm1 - rpm0) * t
    thr = thr0 + (thr1 - thr0) * t
    kmh = kmh0 + (kmh1 - kmh0) * t

    base_hz = rpm * 0.0333
    if mix.get('harmonics'):
        # Boxer-Versatz: Amplitude wackelt mit der halben Zuednfrequenz.
        boxer = 1.0 + 0.14 * np.sin(2.0 * np.pi * np.cumsum(base_hz * 0.5) / SR)
        tone = harmonic_layer(base_hz, SR)
        if 'last_shift' in mix:
            # Oberton-Verschiebung mit der Last: Ueberlagerung mit
            # (2 * Throttle - 1) - bei halbem Gas exakt 0.0 (bit-identisch
            # zur Endabnahme), im Leerlauf dunkler, unter Last heller.
            tone = tone + delta_layer(base_hz, SR, mix.get('shift_scale', 1.0)) * (2.0 * thr - 1.0)
        firing = tone * boxer
    else:
        firing = sine_layer(base_hz, SR)
    # Last-Gain auf dem Zuendpuls: 0.62 + 0.76 * Throttle (ohne Keys = 1.0,
    # dann bleibt der alte Satz bit-identisch).
    if 'last_base' in mix:
        last = mix['last_base'] + mix['last_scale'] * thr
    else:
        last = 1.0
    firing = firing * mix['firing_gain'] * last
    # Ansaugung: das Filter OEFFNET mit der Last (wie die Referenz-Formel
    # 0.05 + 0.22 * Throttle) - bei konstantem Cutoff faellt der alte
    # Frequenzgang-Pfad an, sonst der zeitvariante wie beim Roll-Layer.
    igain = mix.get('intake_gain', mix.get('intake_throttle', 1.0))
    if 'intake_cut_per_thr' in mix:
        icut = np.clip(mix['intake_cutoff'] + mix['intake_cut_per_thr'] * thr,
                       200.0, 3000.0)
    else:
        icut = np.full(n, mix['intake_cutoff'])
    if np.all(icut == icut[0]):
        intake = noise_layer(n, SR, float(icut[0]), 1234) * igain * thr
    else:
        intake = noise_layer_tv(n, SR, icut, 1234) * igain * thr
    exhaust = noise_layer(n, SR, mix['exhaust_cutoff'], 5678) * mix['exhaust_gain']
    # Rollen: Pegel und Filter fahren mit dem Tempo (Clamp wie im Graphen).
    if 'roll_cut_per_kmh' in mix:
        cutoff = np.clip(mix['roll_cutoff'] + mix['roll_cut_per_kmh'] * kmh,
                         200.0, 2500.0)
    else:
        cutoff = np.full(n, mix['roll_cutoff'])
    if 'roll_speed_scale' in mix:
        speed = np.clip(kmh * mix['roll_speed_scale'], 0.0, 1.0)
    else:
        speed = 1.0
    if np.all(cutoff == cutoff[0]):
        roll = noise_layer(n, SR, float(cutoff[0]), 9012)
    else:
        roll = noise_layer_tv(n, SR, cutoff, 9012)
    roll = roll * mix['roll_gain'] * speed
    if mix.get('horn_stack'):
        horn_sig = horn_layer(n, horn, SR)
    else:
        horn_sig = sine_layer(np.full(n, 65.0), SR) * horn

    layer = {'firing': firing, 'intake': intake, 'exhaust': exhaust,
             'roll': roll, 'horn': horn_sig}
    if solo is not None:
        out = layer[solo].copy()
    else:
        engine = firing + intake + exhaust + roll
        if mix.get('tanh'):
            # WaveShaper Tanh (Amount 1.3, OutputGain tanh(1.3)) rechnet
            # exakt tanh(x * 1.3); die Hupe kommt wie in der Referenz danach.
            engine = np.tanh(engine * 1.3)
        out = engine + horn_sig
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


SAETZE = {'alt': MIX_ALT, 'neu': MIX_NEU, 'oberton': MIX_OBERTON,
          'dynamik': MIX_DYNAMIK, 'sport': MIX_SPORT}


def messen(name, mix):
    print(f"\nMesswerte ({name}):")
    for zustand in ZUSTAENDE:
        out, layer = render(mix, zustand)
        print(f" {zustand}:")
        analyse('SUMME', out)
        for lname, lsig in layer.items():
            if lname != 'horn':
                analyse(lname, lsig)


def main():
    satz = sys.argv[1] if len(sys.argv) > 1 else 'alt'
    os.makedirs(OUT_DIR, exist_ok=True)

    if satz == 'analyse':
        for name in SAETZE:
            messen(name, SAETZE[name])
        return

    mix = SAETZE.get(satz, MIX_NEU)
    for zustand in ZUSTAENDE:
        out, layer = render(mix, zustand)
        write_wav(os.path.join(OUT_DIR, f'{satz}_{zustand}.wav'), out)
        print(f"{OUT_DIR}/{satz}_{zustand}.wav")
    # Hupe: Tor 0.4-2.4 s an, ueber den Leerlauf gemischt - Anstieg und
    # Abklingen sind so hoerbar (180 ms wie im Graphen).
    gate = np.zeros(int(DAUER_S * SR))
    gate[int(0.4 * SR):int(2.4 * SR)] = 1.0
    out, _ = render(mix, 'leerlauf', horn=gate)
    write_wav(os.path.join(OUT_DIR, f'{satz}_hupe.wav'), out)
    print(f"{OUT_DIR}/{satz}_hupe.wav")

    # Layer-Solos nur fuer "fahrt" - dort hoert man die Balance.
    _, layer = render(mix, 'fahrt')
    for lname, lsig in layer.items():
        if lname == 'horn':
            continue
        write_wav(os.path.join(OUT_DIR, f'{satz}_fahrt_solo_{lname}.wav'), lsig)

    messen('alt = Ist-Mix aus WbAudioAssetsCommandlet.cpp'
           if satz == 'alt' else satz, mix)


if __name__ == '__main__':
    main()
