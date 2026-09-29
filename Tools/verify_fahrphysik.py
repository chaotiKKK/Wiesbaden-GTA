"""Gate 7: die Fahrmessung gegen die kalibrierten Sollwerte des Kaefers.

Die Fahrphysik ist gegen echte Tests kalibriert (Road & Track 3/1971 und
9/1973, AMS 1302 LS - Quellen in AGENTS.md). Die Unit-Tests pruefen das
Modell im Labor; dieses Gate prueft den Wagen IM SPIEL: eine Messfahrt auf
der Wiese (Tools/fahrmessung.cmd) und ihre Kennzahlen gegen die Sollwerte.
Ein Umbau an Masse, Grip, Bremse, Antrieb oder an der Einbindung in die Welt
(Bodenkontakt, Federung, Zeitschritt), der das Fahrgefuehl verschiebt, faellt
so vor dem Push auf und nicht Wochen spaeter.

    python Tools/verify_fahrphysik.py [Saved/Logs/wb_fahrmessung_<Name>.log]

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


def pruefe_kennzahlen(k):
    """(Code, Zeilen) fuer die Kennzahlen einer Fahrt."""
    zeilen = []
    fehlt = []
    rot = []
    for name, unten, oben, bezug in SOLLWERTE:
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


def pruefe_log(pfad):
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
    code, zeilen = pruefe_kennzahlen(kennzahlen)
    return code, ["  %d Proben aus %s" % (len(proben), pfad)] + zeilen


def main(argv):
    pfad = argv[1] if len(argv) > 1 else STANDARD_LOG
    code, zeilen = pruefe_log(pfad)
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
