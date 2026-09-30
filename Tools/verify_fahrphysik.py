"""Gate 7: die Fahrmessung gegen die kalibrierten Sollwerte des Kaefers.

Die Fahrphysik ist gegen echte Tests kalibriert (Road & Track 3/1971 und
9/1973, AMS 1302 LS - Quellen in AGENTS.md). Die Unit-Tests pruefen das
Modell im Labor; dieses Gate prueft den Wagen IM SPIEL: eine Messfahrt auf
der Wiese (Tools/fahrmessung.cmd) und ihre Kennzahlen gegen die Sollwerte.
Ein Umbau an Masse, Grip, Bremse, Antrieb oder an der Einbindung in die Welt
(Bodenkontakt, Federung, Zeitschritt), der das Fahrgefuehl verschiebt, faellt
so vor dem Push auf und nicht Wochen spaeter.

    python Tools/verify_fahrphysik.py [trocken.log] [--nass nass.log]

--nass prueft zusaetzlich die Regenfahrt (-WbWeather=Rain) gegen SOLLWERTE_NASS.

Rueckgabe: 0 = alle Kennzahlen im Sollband, 1 = mindestens eine daneben,
2 = NICHT GEMESSEN (Log fehlt, Lauf nicht zu Ende, zu wenige Proben,
Kennzahl fehlt). "Nicht gemessen" ist ausdruecklich nicht "in Ordnung".
Das Ergebnis steht zusaetzlich in Saved/Diagnose/fahrphysik_gate.txt.
"""

import os
import sys

HIER = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HIER)

import fahrmessung_auswerten as fa  # noqa: E402

WURZEL = os.path.dirname(HIER)
STANDARD_LOG = os.path.join(WURZEL, "Saved", "Logs", "wb_fahrmessung_gate_fahrphysik.log")
STANDARD_LOG_NASS = os.path.join(WURZEL, "Saved", "Logs", "wb_fahrmessung_gate_fahrphysik_nass.log")
ERGEBNIS = os.path.join(WURZEL, "Saved", "Diagnose", "fahrphysik_gate.txt")

GRUEN, ROT, NICHT_GEMESSEN = 0, 1, 2
MIN_PROBEN = 500   # 10 Hz; die WbDrive-Fahrt (40 s) liefert ~1100

# Sollwerte: (Kennzahl aus fahrmessung_auswerten, untere, obere Grenze, Bezug).
# Die Baender liegen um die Vorlage, weit genug fuer die Streuung zwischen
# zwei Laeufen (gemessen <= 2 %), eng genug fuer echte Rueckfaelle: jeder
# Stand VOR der Kalibrierung faellt durch (Radspin 2,1 s, Bremsweg 60,3 m,
# 0,64 g; mit 820 kg 0-60 mph in 15,4 s).
SOLLWERTE = [
    ("Radspin im Anfahren [s]", None, 0.2,
     "trocken kein Radspin (Radialreifen, mu 0,9)"),
    ("0-60 mph (96,6 km/h) [s]", 16.4, 20.0,
     "R&T 9/1973: 18,2 s, +-10 %"),
    ("Querbeschl. max rechts [g]", 0.65, 0.76,
     "R&T 9/1973: 0,704 g Kurvengrip"),
    ("Querbeschl. max links [g]", 0.65, 0.76,
     "R&T 9/1973: 0,704 g Kurvengrip"),
    ("Bremsweg auf 100 km/h normiert [m]", 48.0, 57.0,
     "R&T 9/1973: 158 ft aus 60 mph = ~52 m aus 100 km/h"),
    ("mittl. Verzoegerung [g]", 0.68, 0.84,
     "R&T 9/1973: 0,76 g"),
    ("Raeder gleiten beim Bremsen [%]", None, 5.0,
     "ABS: die Raeder gleiten nicht"),
]


# Nasse Fahrbahn (-WbWeather=Rain): keine zeitgenoessische Nassmessung des
# Kaefers gefunden - das Band ist das Verhaeltnis nass/trocken der Literatur
# (Wong, Theory of Ground Vehicles: Haftbeiwert 0,80-0,90 trocken, 0,50-0,70
# nass; Unfallaufnahme-Anhaltwerte Asphalt 7,5-8,0 / 6,0 m/s^2; moderne Pkw
# 9,5 / 6 m/s^2), angewandt auf die trockenen R&T-Werte (0,76 g, 0,704 g).
SOLLWERTE_NASS = [
    ("Belagsgrip Kurve + Bremsen max", None, 0.80,
     "nass gefahren - der Regen muss die Strasse erreichen"),
    ("Querbeschl. max rechts [g]", 0.44, 0.54,
     "nass: 0,704 g x 0,63..0,77"),
    ("Querbeschl. max links [g]", 0.44, 0.54,
     "nass: 0,704 g x 0,63..0,77"),
    ("Bremsweg auf 100 km/h normiert [m]", 66.0, 82.0,
     "nass: 0,48..0,59 g aus 100 km/h"),
    ("mittl. Verzoegerung [g]", 0.48, 0.59,
     "nass: 0,76 g x 0,63..0,77"),
    ("Raeder gleiten beim Bremsen [%]", None, 5.0,
     "ABS: die Raeder gleiten auch nass nicht"),
]


def pruefe_kennzahlen(k, sollwerte=None):
    """(Code, Zeilen) fuer die Kennzahlen einer Fahrt."""
    zeilen = []
    fehlt = []
    rot = []
    for name, unten, oben, bezug in (SOLLWERTE if sollwerte is None else sollwerte):
        wert = k.get(name)
        if not isinstance(wert, (int, float)):
            fehlt.append(name)
            zeilen.append("  [fehlt] %s - nicht gemessen (%s)" % (name, bezug))
            continue
        band = "%s..%s" % ("-" if unten is None else "%g" % unten, "%g" % oben)
        daneben = (unten is not None and wert < unten) or wert > oben
        marke = "ROT" if daneben else "ok"
        zeilen.append("  [%s] %s = %.2f (Soll %s; %s)" % (marke, name, wert, band, bezug))
        if daneben:
            rot.append(name)
    if fehlt:
        return NICHT_GEMESSEN, zeilen
    return (ROT if rot else GRUEN), zeilen


def pruefe_log(pfad, sollwerte=None):
    """(Code, Zeilen) fuer eine Messfahrt-Logdatei."""
    if not os.path.isfile(pfad):
        return NICHT_GEMESSEN, ["  [fehlt] Log %s - die Messfahrt lief nicht" % pfad]
    with open(pfad, encoding="utf-8", errors="replace") as datei:
        beendet = any("Messlauf beendet" in z for z in datei)
    if not beendet:
        return NICHT_GEMESSEN, ["  [fehlt] 'Messlauf beendet' - die Fahrt kam nicht bis zum Ende "
                                "(Absturz, Abbruch oder abgeschnittenes Log)"]
    proben = fa.lesen(pfad)
    if len(proben) < MIN_PROBEN:
        return NICHT_GEMESSEN, ["  [fehlt] nur %d Telemetrie-Proben (mindestens %d) - "
                                "lief -WbFahrTelemetrie?" % (len(proben), MIN_PROBEN)]
    kennzahlen = fa.auswerten(proben)
    if "Fehler" in kennzahlen:
        return NICHT_GEMESSEN, ["  [fehlt] %s" % kennzahlen["Fehler"]]
    code, zeilen = pruefe_kennzahlen(kennzahlen, sollwerte)
    return code, ["  %d Proben aus %s" % (len(proben), pfad)] + zeilen


def gesamt(codes):
    """Nicht gemessen schlaegt rot, rot schlaegt gruen."""
    if NICHT_GEMESSEN in codes:
        return NICHT_GEMESSEN
    return ROT if ROT in codes else GRUEN


def main(argv):
    args = list(argv[1:])
    nass = None
    if "--nass" in args:
        i = args.index("--nass")
        nass = args[i + 1] if i + 1 < len(args) else STANDARD_LOG_NASS
        del args[i:i + 2]
    pfad = args[0] if args else STANDARD_LOG
    code, zeilen = pruefe_log(pfad)
    if nass is not None:
        code_nass, zeilen_nass = pruefe_log(nass, SOLLWERTE_NASS)
        zeilen = ["  Trocken:"] + zeilen + ["  Nass (-WbWeather=Rain):"] + zeilen_nass
        code = gesamt([code, code_nass])
    kopf = {GRUEN: "GRUEN - Fahrphysik im Sollband",
            ROT: "ROT - Fahrphysik ausserhalb der Sollwerte",
            NICHT_GEMESSEN: "ROT - nicht gemessen"}[code]
    text = "\n".join(["Gate 7 Fahrphysik: " + kopf] + zeilen) + "\n"
    print(text, end="")
    os.makedirs(os.path.dirname(ERGEBNIS), exist_ok=True)
    with open(ERGEBNIS, "w", encoding="utf-8") as f:
        f.write(text)
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv))
