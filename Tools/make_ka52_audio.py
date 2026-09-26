"""Erzeugt die Ka-52-Flugsounds als echte WAV-Dateien.

WARUM ECHTE DATEIEN UND NICHT PROZEDURAL IM TICK: Der prozedurale Pfad
(FWiesbadenHelicopterAudioModel) ist der Rueckfall, in dem man hoert, dass
er keiner ist - eine Saegezahn-Approximation ohne Transienten. Der Rotor
eines Hubschraubers besteht aber im Wesentlichen aus Transienten: jeder
Blattdurchgang ist ein kurzer, sehr breitbandiger Druckimpuls, und genau
dieser Klick macht den Ka-52 ueber 300 m hoerbar. Ein synthetisiertes
Spektrum ohne Transienten klingt nach Rechner.

PHYSIK DER VIER KLANGFAMILIEN:
  Rotor      Der Koaxialrotor hat oben UND unten je drei Blaetter. Jeder
             Blattdurchgang erzeugt Druckimpuls ("Schlag") plus dumpfen
             Stoss. Bei 300 rpm sind das 15 Impulse je Rotor und Sekunde,
             mit halbem Versatz 30 Ereignisse - der doppelte Takt klingt
             nach dem Ka-52, nicht nach einem normalen Hubschrauber.
  Triebwerk  TV3-117: Turbinenpfiff (Kommpressorband um 1,2-5 kHz), dazu
             ein tieferes Laufradbrummen und das Zischen der Ansaugseite.
  MG         2A42, 30 mm: sehr kurzer Knall mit breitem Spektrum, tiefer
             Stoss darunter, dann der metallische Nachhall im Rohr.
  Wind       Fahrtwind gegen Rumpf und Rotorblatt, nimmt mit der
             Geschwindigkeit zu (die Komponente regelt nur die Lautstaerke).

SCHLEIFEN: Alle Dauerklaenge sind exakt eine Sekunde lang und werden im
Spektralbereich so gebaut, dass Anfang und Ende zusammenpassen (zirkulaere
Rauschformung, Ereignisse mit Fenstern auf dem Raster). Ein Loop, der an
der Naht klickt, ist schlimmer als kein Loop.

Die KompONENTEN rechnen mit einer Bezugsdrehzahl (RotorPitch = Rpm / 300,
EnginePitch = Rpm / 600, siehe UpdateAssetAudio). Die Dateien sind deshalb
auf genau diese 300 bzw. 600 rpm gebaut: bei Reiseflug bleibt der Ton
unveraendert, beim Hochlaufen zieht er an.

Aufruf:
  python Tools/make_ka52_audio.py [Content/Data/Raw/Ka52/audio]
"""

import os
import sys

import numpy as np

FS = 44100                 # Hz
DAUER = 1.0                # s - jede Schleife genau eine Sekunde
REF_ROTOR_RPM = 300.0      # Bezugsdrehzahl des Rotorloops
REF_ENGINE_RPM = 600.0     # Bezugsdrehzahl des Triebwerksloops
BLAETTER_OBEN = 3
BLAETTER_UNTEN = 3


# --- Bausteine ------------------------------------------------------------

def periodisches_rauschen(n, verlauf=None, seed=1):
    """Rauschen, das an der Schleifengrenze stetig weiterlaeuft.

    Im Zeitbereich waere jedes Fensterende ein Sprung. Im Spektralbereich
    gibt es keine Enden: das Signal ist per Definition periodisch, und der
    gewünschte Amplitudenverlauf wird direkt auf das Spektrum gelegt.
    """
    rng = np.random.default_rng(seed)
    spek = rng.normal(0.0, 1.0, n // 2 + 1) + 1j * rng.normal(0.0, 1.0, n // 2 + 1)
    if verlauf is not None:
        spek *= verlauf
    zeit = np.fft.irfft(spek, n)
    return zeit / (np.max(np.abs(zeit)) + 1e-12)


def tiefpass_verlauf(n, fs, grenze, ordnung=2.0):
    """Betragsverlauf 1/f^ordnung unterhalb der Grenzfrequenz."""
    f = np.fft.rfftfreq(n, 1.0 / fs)
    v = 1.0 / (1.0 + (f / grenze) ** (2.0 * ordnung))
    return v


def bandpass_verlauf(n, fs, tief, hoch):
    f = np.fft.rfftfreq(n, 1.0 / fs)
    v = 1.0 / (1.0 + (tief / np.maximum(f, 1.0)) ** 4)
    v *= 1.0 / (1.0 + (np.maximum(f, 1.0) / hoch) ** 4)
    return v


def fenster(n):
    return np.hanning(n + 2)[1:-1]


def schlag(n, fs, takte, takt_fn, seed=7):
    """Summe kurzer, gefensterter Ereignisse auf einem Takt.

    Jedes Ereignis belegt genau sein Taktfenster und wird mit einem
    Hannfenster multipliziert, das an beiden Enden auf 0 geht. Damit ist
    die Summe exakt periodisch - kein Knacksen an der Schleifennaht.
    """
    rng = np.random.default_rng(seed)
    out = np.zeros(n)
    pro_takt = n // takte
    for k in range(takte):
        a = k * pro_takt
        b = a + pro_takt
        spur = np.zeros(n)
        spur[a:b] = takt_fn((b - a), fs, rng)
        out += spur
    return out


def gedaempfte_sinus(frequenz, n, fs, tau, phase=0.0):
    t = np.arange(n) / fs
    return np.exp(-t / tau) * np.sin(2.0 * np.pi * frequenz * t + phase)


def spek_nach_rik(f):
    """Frequenz auf ein ganzzahliges Vielfaches von 1 Hz runden.

    Nur so passt ein Spektralbaustein ueber die Sekundenschleife stetig
    zusammen; 5,3 Hz ergaebe am Loop-Ende einen Sprung.
    """
    return max(1, int(round(f)))


def normalisiere(x, ziel_rms_dbfs=-14.0):
    rms = np.sqrt(np.mean(x ** 2)) + 1e-12
    ziel = 10.0 ** (ziel_rms_dbfs / 20.0)
    y = x * (ziel / rms)
    spitze = np.max(np.abs(y))
    if spitze > 0.98:                      # nie ueber 0 dBFS
        y *= 0.98 / spitze
    return y


# --- Die vier Klangfamilien -----------------------------------------------

def rotor():
    """Koaxialrotor: zwei Rotoren zu je drei Blaettern, halber Versatz."""
    n = int(FS * DAUER)
    hub = spek_nach_rik(REF_ROTOR_RPM / 60.0)        # 5 Hz Nabenfrequenz


    # Schlag: dumpfer Stoss (Rumpf) + heller Impuls (Blattkante).
    def ein_schlag(len_spur, fs, rng):
        laenge = min(len_spur, spek_nach_rik(0.028 * fs))
        spur = np.zeros(len_spur)
        stoss = (gedaempfte_sinus(88.0, laenge, fs, 0.011)
                 + 0.45 * gedaempfte_sinus(176.0, laenge, fs, 0.007, 0.4))
        kante = rng.normal(0.0, 1.0, laenge) * np.exp(-np.arange(laenge) / (0.0035 * fs))
        spur[:laenge] = (stoss + 0.55 * kante) * fenster(laenge)
        return spur

    takte = BLAETTER_OBEN * hub * 2
    oben = schlag(n, FS, takte, ein_schlag, seed=11)
    # Der untere Rotor ist eine halbe Nabenperiode versetzt: dieselbe Spur,
    # um die halbe Taktlaenge verschoben und etwas dumpfer.
    versatz = n // (2 * hub)
    unten = np.roll(oben, versatz) * 0.88

    # Grundbrummen des Rotors (Luftstrom durch die Blattspitzen) und der
    # leicht schwebende Tiefton des Koaxials.
    brummen = periodisches_rauschen(n, tiefpass_verlauf(n, FS, 260.0, 1.4), seed=3)
    tiefton = 0.35 * np.sin(2.0 * np.pi * hub * np.arange(n) / FS)

    x = (1.0 * (oben + unten) / np.max(np.abs(oben + unten))
         + 0.55 * brummen + 0.18 * tiefton)
    return normalisiere(x, -13.0)


def triebwerk():
    """TV3-117: Kommpressorpfiff, Laufradbrummen, Ansaugzischen."""
    n = int(FS * DAUER)
    t = np.arange(n) / FS
    welle = spek_nach_rik(REF_ENGINE_RPM / 60.0)        # 10 Hz Wellendrehzahl

    # Pfiff: Harmonische der Wellendrehzahl, mit Abstand - so klingt ein
    # Verdichter, nicht ein Oszillator. 1,2 kHz ist der Grundton des
    # Kompressors, 1,2 kHz * 3 = 3,6 kHz der schneidende Oberton.
    pfiff = np.zeros(n)
    for k, (faktor, anteil) in enumerate(((120, 1.0), (240, 0.55), (360, 0.30),
                                         (600, 0.18), (840, 0.10))):
        pfiff += anteil * np.sin(2.0 * np.pi * faktor * t + k * 0.7)
    pfiff /= np.max(np.abs(pfiff))

    brummen = 0.5 * np.sin(2.0 * np.pi * welle * 2 * t)
    zischen = 0.45 * periodisches_rauschen(n, bandpass_verlauf(n, FS, 900.0, 5200.0), seed=5)

    x = 0.55 * pfiff + 0.35 * zischen + 0.30 * brummen
    return normalisiere(x, -17.0)


def mg():
    """2A42, 30 mm: Knall, Stoss, Rohrnachhall. Ein-Schuss, keine Schleife."""
    n = int(0.55 * FS)
    rng = np.random.default_rng(23)
    t = np.arange(n) / FS

    knall = rng.normal(0.0, 1.0, n) * np.exp(-t / 0.016)
    knall = np.fft.irfft(np.fft.rfft(knall) * bandpass_verlauf(n, FS, 300.0, 7000.0), n)

    stoss = (gedaempfte_sinus(72.0, n, FS, 0.055)
             + 0.5 * gedaempfte_sinus(148.0, n, FS, 0.030, 1.1))

    # Rohrnachhall: schmalbandiges, schnell abklingendes Rauschen.
    nachhall = periodisches_rauschen(n, bandpass_verlauf(n, FS, 1400.0, 9000.0), seed=31)
    nachhall *= np.exp(-t / 0.11) * np.clip((t - 0.02) / 0.02, 0.0, 1.0)

    x = knall + 0.9 * stoss + 0.5 * nachhall
    return normalisiere(x, -9.0)


def wind():
    """Fahrtwind: breites Rauschen, langsam atmend (2 Ereignisse je 2 s)."""
    n = int(2.0 * FS)
    n = n - (n % int(FS))                    # volle Sekunden fuer die Schleife
    grund = periodisches_rauschen(n, bandpass_verlauf(n, FS, 120.0, 2600.0), seed=41)
    t = np.arange(n) / FS
    # Atmung mit 1 Hz: ganzzahlige Ereignisse je Schleife, sonst hoert man
    # den Atemzug am Loop-Uebergang.
    atmung = 0.72 + 0.28 * np.sin(2.0 * np.pi * 1.0 * t)
    x = grund * atmung
    return normalisiere(x, -20.0)


def pruefe_naht(daten):
    """Groesse des Sprungs an der Schleifennaht, bezogen auf den Mittelwert.

    Ein Loop, der an der Naht springt, hoert man als Klick bei jeder
    Wiederholung - bei einem Sekundenloop also einmal je Sekunde, genau
    im Takt mit dem Blattschlag und damit besonders auffaellig. Die Zahl
    hier ist das Verhaeltnis Nahtsprung zu typischem Sample-Schritt; unter
    2 ist die Naht unhoerbar.
    """
    sprung = abs(float(daten[0]) - float(daten[-1]))
    schritt = float(np.median(np.abs(np.diff(daten)))) + 1e-12
    return sprung / schritt


def haupt_takt(daten, fs, tief, hoch):
    """Frequenz des staerksten Anteils der Huellkurve in einem Band.

    Beim Rotor muss hier der doppelte Blattschlag stehen (bei 300 rpm und
    je drei Blaettern oben wie unten: 30 Hz). Steht dort etwas anderes,
    ist die Rasterung im Takt fehlerhaft.
    """
    huelle = np.abs(np.fft.rfft(daten * np.hanning(len(daten))))
    f = np.fft.rfftfreq(len(daten), 1.0 / fs)
    band = (f >= tief) & (f <= hoch)
    return float(f[band][int(np.argmax(huelle[band]))])


def schreibe(pfad, daten, schleife):
    import wave
    peak = int(np.max(np.abs(daten)) * 32767.0)
    pcm = (np.clip(daten, -1.0, 1.0) * 32767.0).astype("<i2")
    with wave.open(pfad, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(FS)
        w.writeframes(pcm.tobytes())
    return {"datei": os.path.basename(pfad), "sekunden": len(pcm) / FS,
            "spitze_dbfs": round(20.0 * np.log10(peak / 32767.0 + 1e-12), 1),
            "schleife": schleife, "byte": os.path.getsize(pfad)}


def main(argv):
    ziel = argv[0] if argv else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", "Content", "Data",
        "Raw", "Ka52", "audio")
    ziel = os.path.abspath(ziel)
    os.makedirs(ziel, exist_ok=True)

    bericht = ["Ka-52-Audio  %d Hz, mono, 16 bit" % FS,
               "Bezug: Rotor %.0f rpm, Triebwerk %.0f rpm "
               "(dort ist der Ton pitch-neutral)" % (REF_ROTOR_RPM, REF_ENGINE_RPM)]
    erwaert = {"ka52_rotor.wav": (20.0, 40.0), "ka52_engine.wav": (8.0, 25.0),
               "ka52_wind.wav": (0.5, 4.0)}
    for name, daten, schleife in (
            ("ka52_rotor.wav", rotor(), True),
            ("ka52_engine.wav", triebwerk(), True),
            ("ka52_mg.wav", mg(), False),
            ("ka52_wind.wav", wind(), True)):
        info = schreibe(os.path.join(ziel, name), daten, schleife)
        bericht.append("%-16s %5.2f s  Spitze %+5.1f dBFS  Schleife=%s  %d Byte"
                       % (info["datei"], info["sekunden"], info["spitze_dbfs"],
                          "ja" if info["schleife"] else "nein", info["byte"]))
        if schleife:
            # Der Nahtsprung wird im Bericht festgehalten, damit niemand
            # spaeter auf "klingt doch gut" vertrauen muss: die Zahl ist
            # der Unterschied zwischen sauberer Schleife und Klick.
            bericht.append("%-16s Nahtsprung %.2f x typischer Sample-Schritt"
                           % ("", pruefe_naht(daten)))
        if name in erwaert:
            tief, hoch = erwaert[name]
            bericht.append("%-16s Haupt-Takt der Huellkurve: %.1f Hz "
                           "(erwartet %.0f..%.0f Hz)"
                           % ("", haupt_takt(daten, FS, tief, hoch), tief, hoch))

    text = "\n".join(bericht) + "\n"
    with open(os.path.join(ziel, "ka52_audio_bericht.txt"), "w", encoding="utf-8") as fh:
        fh.write(text)
    print(text)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
