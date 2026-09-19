"""Vergleicht zwei Stau-Karten (Saved/Diagnose/staukarte*.txt) Strasse fuer Strasse.

Eine Karte allein sagt, WO es steht. Zwei Karten sagen, WAS eine Aenderung
bewirkt hat - und das ist die Frage, die bei einer Regel wie den
Kreuzungskonflikten zaehlt. Verglichen wird der Anteil des Tempolimits
(Tempo / Limit), nicht das absolute Tempo: 30 km/h sind in der Tempo-30-Zone
freie Fahrt und auf der Hauptachse Stau.

Aufruf:
    python Tools/vergleich_staukarten.py <vorher.txt> <nachher.txt> [MinMesswerte]

Gewichtet wird mit der Zahl der Messwerte: ein roter Punkt aus 30 Werten ist
Zufall, einer aus 40.000 ein Befund. Strassen, die in EINER der beiden Karten
zu wenige Messwerte haben, bleiben draussen - sonst vergleicht man Rauschen.
"""

import sys
from collections import defaultdict

DEFAULT_MIN_SAMPLES = 400


def oeffne(pfad):
    """Datei lesen, egal ob UTF-8 oder UTF-16 (siehe render_stau_karte.py)."""
    with open(pfad, "rb") as f:
        roh = f.read()
    for kodierung in ("utf-8-sig", "utf-16", "utf-8", "latin-1"):
        try:
            return roh.decode(kodierung)
        except (UnicodeDecodeError, UnicodeError):
            continue
    raise UnicodeError(f"{pfad}: keine passende Kodierung gefunden")


def lies(pfad):
    """STAU-Zeilen je Strasse buendeln: Tempo und Limit nach Messwerten gewichtet."""
    tempo = defaultdict(float)
    limit = defaultdict(float)
    steher = defaultdict(float)
    werte = defaultdict(float)

    for zeile in oeffne(pfad).splitlines():
        teile = zeile.split()
        if len(teile) < 7 or teile[0] != "STAU":
            continue
        try:
            mittel = float(teile[3])
            grenze = float(teile[4])
            steh = float(teile[5])
            anzahl = float(teile[6])
        except ValueError:
            continue
        name = " ".join(teile[7:]).strip()
        if not name or anzahl <= 0.0 or grenze <= 0.0:
            continue
        tempo[name] += mittel * anzahl
        limit[name] += grenze * anzahl
        steher[name] += steh * anzahl
        werte[name] += anzahl

    ergebnis = {}
    for name, anzahl in werte.items():
        ergebnis[name] = {
            "tempo": tempo[name] / anzahl,
            "limit": limit[name] / anzahl,
            "steher": steher[name] / anzahl,
            "werte": anzahl,
        }
    return ergebnis


def anteil(eintrag):
    return eintrag["tempo"] / eintrag["limit"] if eintrag["limit"] > 0.0 else 0.0


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1

    vorher = lies(sys.argv[1])
    nachher = lies(sys.argv[2])
    mindest = float(sys.argv[3]) if len(sys.argv) > 3 else DEFAULT_MIN_SAMPLES

    gemeinsam = [
        name for name in vorher
        if name in nachher
        and vorher[name]["werte"] >= mindest
        and nachher[name]["werte"] >= mindest
    ]

    print(f"Strassen in beiden Karten mit mindestens {mindest:.0f} Messwerten: "
          f"{len(gemeinsam)} (von {len(vorher)} / {len(nachher)})")

    if not gemeinsam:
        return 0

    # Gesamtbild zuerst: der Mittelwert ueber alle gemeinsamen Strassen,
    # gewichtet nach Messwerten. Ohne ihn erzaehlen die Extremfaelle unten
    # eine Geschichte, die fuer die Stadt nicht stimmt.
    def gesamt(karte):
        summe = sum(anteil(karte[n]) * karte[n]["werte"] for n in gemeinsam)
        gewicht = sum(karte[n]["werte"] for n in gemeinsam)
        return summe / gewicht if gewicht else 0.0

    print(f"Gesamt (gewichtet): {gesamt(vorher) * 100:.1f} % des Limits "
          f"-> {gesamt(nachher) * 100:.1f} %")

    veraendert = sorted(
        gemeinsam,
        key=lambda n: anteil(nachher[n]) - anteil(vorher[n]))

    def zeile(name):
        v, n = vorher[name], nachher[name]
        return (f"  {name[:34]:34s} {anteil(v) * 100:5.1f} % -> {anteil(n) * 100:5.1f} %"
                f"   ({v['tempo']:4.1f} -> {n['tempo']:4.1f} km/h bei Limit {n['limit']:4.1f},"
                f" Messwerte {min(v['werte'], n['werte']):.0f})")

    print("\nAm staerksten VERSCHLECHTERT:")
    for name in veraendert[:10]:
        print(zeile(name))

    print("\nAm staerksten VERBESSERT:")
    for name in reversed(veraendert[-10:]):
        print(zeile(name))

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
