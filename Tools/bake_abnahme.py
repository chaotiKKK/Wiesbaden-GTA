"""Bake-Abnahme: prueft eine frisch gebackene Karte gegen ihre Vorgaengerin.

Ein Bake meldet "FERTIG", und trotzdem kann die Stadt leer sein - Alkis10 und
Alkis11 waren genau das: erfolgreich gemeldet, im Spiel nur Gras. Diese
Abnahme faehrt beide Karten unter gleichen Bedingungen und vergleicht, was im
Spiel ankommt.

    python Tools/bake_abnahme.py --neu WiesbadenCity_AlkisNN
    python Tools/bake_abnahme.py --neu <neu> --alt <alt> --dauer 150

Ohne `--alt` ist die Vorgaengerin die aktuelle Default-Karte
(`Config/DefaultEngine.ini`, gelesen ueber `Tools/karte.py`).

Rueckgabe: 0 = angenommen, 1 = ABGELEHNT, 2 = Lauf fehlgeschlagen.

ZWEI FALLEN, DIE HIER EINGEBAUT SIND
------------------------------------

1. **"FERTIG" ist kein Beleg.** Der Bake-Bericht sagt nur, dass der Lauf
   durchlief. Die Aussage im Spiel ist die Zeile
   `Stadt-Geometrie: N Chunk-Actors geladen (M ohne Render-Geometrie)` - M
   muss NULL sein. Zweites, unabhaengiges Signal: die Groesse der externen
   Actors (eine volle Stadt wiegt 1,8-1,9 GB, ein Leerbake 1,4 GB).

2. **Der ERSTE Lauf einer Karte misst den Cache, nicht die Karte.** Eine nie
   gespielte Karte baut beim Laden ihre abgeleiteten Daten auf. Gemessen am
   19.09.2026: Alkis17 brauchte 138 s, dann 84 s, dann 21 s - und warf beim
   kalten Laden GPU-Timeouts. Wer nach zwei Messpunkten urteilt, lehnt eine
   gesunde Karte ab. Darum faehrt die Abnahme **Aufwaermrunden, bis die
   Ladezeit steht**, und misst erst danach.

Die Pruefungen sind ABSICHTLICH einseitig: eine neue Karte darf besser sein,
aber nicht schlechter. Ein Netz, das um 30 % waechst, ist kein Fehler - eines,
das um 30 % schrumpft, hat Strassen verloren.
"""

import argparse
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from karte import standard_karte   # EINE Quelle fuer den Kartennamen

PROJEKT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
UPROJECT = os.path.join(PROJEKT, "WiesbadenReal.uproject")
EDITOR = r"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
LOG = os.path.join(PROJEKT, "Saved", "Logs", "WiesbadenReal.log")
EXTERNE_ACTORS = os.path.join(PROJEKT, "Content", "__ExternalActors__", "Maps")


# ---------------------------------------------------------------------------
#  Was aus einem Lauf gelesen wird
# ---------------------------------------------------------------------------

MUSTER = {
    "ladezeit_s": (r"Took ([\d.]+) seconds to LoadMap", float),
    "chunks": (r"Stadt-Geometrie: (\d+) Chunk-Actors geladen", int),
    "chunks_leer": (r"Chunk-Actors geladen \((\d+) ohne Render-Geometrie\)", int),
    "netz_km": (r"Verkehrs-Simulation initialisiert: Dichte [\d.]+, ([\d.]+) km Netz", float),
    "spuren": (r"km Netz, (\d+) Spuren", int),
    "ampeln": (r"Signalprogramm: \d+ von (\d+) Kreuzungen", int),
    "verbindungen": (r"Verkehr: (\d+) Verbindungen an \d+ Knoten", int),
    "knoten": (r"Verkehr: \d+ Verbindungen an (\d+) Knoten", int),
    "schilder": (r"Ausstattungs-Spawner: (\d+) Schilder", int),
    "laternen": (r"Markierungen, (\d+) Laternen", int),
    "bildzeit_ms": (r"Bildzeit \(\d+ Bilder\): Mittel ([\d.]+) ms", float),
    "bilder_s": (r"Mittel [\d.]+ ms \((\d+) Bilder/s\)", int),
    "schlechtestes_ms": (r"schlechtestes ([\d.]+) ms", float),
}


def lies_log(pfad=LOG):
    """Kennzahlen aus dem Spiel-Protokoll. Mehrfach gemeldete Werte: der LETZTE
    zaehlt (die Diagnose laeuft alle 15 s, der spaeteste Wert ist der
    eingeschwungene)."""
    if not os.path.exists(pfad):
        return {}
    with open(pfad, "rb") as f:
        text = f.read().decode("utf-8", errors="replace")

    werte = {}
    for name, (muster, typ) in MUSTER.items():
        treffer = re.findall(muster, text)
        if treffer:
            werte[name] = typ(treffer[-1])

    # GPU-Timeouts: beim kalten Laden normal, im warmen Lauf ein Befund.
    werte["gpu_timeouts"] = len(re.findall(r"GPU timeout", text))
    return werte


def actors_gb(karte):
    """Groesse der externen Actor-Pakete - das zweite, unabhaengige Signal."""
    ordner = os.path.join(EXTERNE_ACTORS, karte)
    if not os.path.isdir(ordner):
        return 0.0
    summe = 0
    for wurzel, _, dateien in os.walk(ordner):
        for d in dateien:
            try:
                summe += os.path.getsize(os.path.join(wurzel, d))
            except OSError:
                pass
    return summe / (1024 ** 3)


# ---------------------------------------------------------------------------
#  Einen Lauf fahren
# ---------------------------------------------------------------------------

def fahre(karte, sekunden, still=False):
    """Startet das Spiel auf einer Karte und wartet, bis es sich beendet.

    -WbTime=13 erzwingt Tageslicht: Nacht und Tag kosten unterschiedlich viel
    Bildzeit, und zwei Karten zu unterschiedlichen Uhrzeiten zu vergleichen
    misst die Uhr, nicht den Bake.
    """
    if os.path.exists(LOG):
        try:
            os.remove(LOG)
        except OSError:
            pass

    befehl = [
        EDITOR, UPROJECT, f"/Game/Maps/{karte}",
        "-game", "-windowed", "-ResX=1280", "-ResY=720", "-nop4",
        "-WbTime=13", f"-WbQuitAfter={sekunden}",
    ]
    if not still:
        print(f"    laeuft ({sekunden} s Spielzeit) ...", flush=True)

    start = time.time()
    try:
        subprocess.run(befehl, check=False, timeout=sekunden + 600)
    except subprocess.TimeoutExpired:
        print("    ABBRUCH: der Lauf hing laenger als erlaubt.")
        return None

    werte = lies_log()
    werte["wanduhr_s"] = time.time() - start
    return werte


def messe(karte, dauer, max_aufwaermen, schwelle=0.20):
    """Aufwaermen, bis die Ladezeit steht - dann messen.

    Zwei aufeinanderfolgende Ladezeiten muessen sich um weniger als `schwelle`
    unterscheiden. Erst dann ist die Zahl eine Eigenschaft der KARTE und nicht
    des Caches.
    """
    print(f"  {karte}: aufwaermen")
    letzte = None
    for runde in range(1, max_aufwaermen + 1):
        werte = fahre(karte, 45, still=True)
        if werte is None:
            return None
        jetzt = werte.get("ladezeit_s")
        if jetzt is None:
            print(f"    Runde {runde}: keine Ladezeit im Protokoll - Lauf gescheitert?")
            return None
        print(f"    Runde {runde}: {jetzt:.1f} s")
        if letzte is not None and abs(jetzt - letzte) <= schwelle * max(letzte, 1.0):
            print(f"    steht (zwei Laeufe innerhalb von {schwelle:.0%})")
            break
        letzte = jetzt
    else:
        print(f"    WARNUNG: nach {max_aufwaermen} Runden noch nicht eingeschwungen.")

    print(f"  {karte}: messen")
    werte = fahre(karte, dauer)
    if werte is not None:
        werte["actors_gb"] = actors_gb(karte)
    return werte


# ---------------------------------------------------------------------------
#  Die Abnahme selbst
# ---------------------------------------------------------------------------

class Pruefung:
    """Eine Bedingung mit Begruendung - die Begruendung steht im Bericht."""

    def __init__(self, name, schluessel, art, grenze, warum):
        self.name = name
        self.schluessel = schluessel
        self.art = art          # "null", "nicht_kleiner", "nicht_groesser"
        self.grenze = grenze    # erlaubter relativer Verlust bzw. Zuwachs
        self.warum = warum


PRUEFUNGEN = [
    Pruefung("Leere Chunks", "chunks_leer", "null", 0,
             "Alkis10/11 meldeten FERTIG und waren im Spiel nur Gras. "
             "Ein einziger Chunk ohne Render-Geometrie ist einer zu viel."),
    Pruefung("Externe Actors (GB)", "actors_gb", "nicht_kleiner", 0.10,
             "Zweites, unabhaengiges Signal: volle Stadt 1,8-1,9 GB, Leerbake 1,4 GB."),
    Pruefung("Strassennetz (km)", "netz_km", "nicht_kleiner", 0.02,
             "Fehlende Strassen sind der zweite grosse Bake-Fehler."),
    Pruefung("Fahrspuren", "spuren", "nicht_kleiner", 0.02, "wie oben, feiner aufgeloest"),
    Pruefung("Verbindungen", "verbindungen", "nicht_kleiner", 0.02,
             "Kreuzungen ohne Verbindungen heisst: der Verkehr kommt nicht durch."),
    Pruefung("Ampeln", "ampeln", "nicht_kleiner", 0.02, "Signalisierte Knoten"),
    Pruefung("Schilder", "schilder", "nicht_kleiner", 0.05, "Strassenausstattung"),
    Pruefung("Laternen", "laternen", "nicht_kleiner", 0.05, "Strassenausstattung"),
    Pruefung("Bilder/s", "bilder_s", "nicht_kleiner", 0.10,
             "Eine Karte, die 20 % Bildrate kostet, ist kein Fortschritt."),
    Pruefung("Ladezeit (s)", "ladezeit_s", "nicht_groesser", 0.25,
             "ERST NACH DEM AUFWAERMEN gemessen - der erste Lauf misst den Cache."),
    # GPU-Timeouts sind KEIN hartes Kriterium: sie kommen auch auf einer
    # gesunden Karte unter Last vereinzelt vor (gemessen 2 in einem warmen
    # Lauf auf der Live-Karte). Rot wird es erst, wenn die neue Karte
    # deutlich mehr davon hat als die alte - dann steckt etwas in den
    # Render-Daten (so fiel Alkis8 auf).
    Pruefung("GPU-Timeouts", "gpu_timeouts", "hoechstens_plus", 3,
             "Vereinzelt normal; deutlich mehr als die Vorgaengerin deutet auf "
             "defekte Render-Daten (so fiel Alkis8 auf)."),

    # Nur zur Ansicht: haengt davon ab, wo der Spieler steht, und taugt
    # deshalb nicht als Schranke.
    Pruefung("Geladene Chunks", "chunks", "hinweis", 0,
             "ortsabhaengig - nur zur Ansicht"),
    Pruefung("schlechtestes Bild (ms)", "schlechtestes_ms", "hinweis", 0,
             "einzelner Ausreisser, zu unstet fuer eine Schranke"),
]


def bewerte(alt, neu):
    """Liefert (zeilen, rot) - rot ist True, wenn eine Pruefung fehlschlaegt."""
    zeilen = []
    rot = False

    for p in PRUEFUNGEN:
        a = alt.get(p.schluessel)
        n = neu.get(p.schluessel)

        if n is None:
            zeilen.append((p.name, "?", "?", "FEHLT", p.warum))
            rot = True
            continue

        if p.art == "hinweis":
            zeilen.append((p.name, "-" if a is None else f"{a:g}", f"{n:g}",
                           "Hinweis", p.warum))
            continue

        if p.art == "hoechstens_plus":
            ok = (a is None) or (n <= a + p.grenze)
            zeilen.append((p.name, "-" if a is None else f"{a:g}", f"{n:g}",
                           "ok" if ok else f"ROT (mehr als +{p.grenze:g})", p.warum))
            rot = rot or not ok
            continue

        if p.art == "null":
            ok = (n == 0)
            urteil = "ok" if ok else "ROT"
            zeilen.append((p.name, "-" if a is None else f"{a:g}", f"{n:g}", urteil, p.warum))
            rot = rot or not ok
            continue

        if a is None or a == 0:
            zeilen.append((p.name, "-", f"{n:g}", "kein Vergleich", p.warum))
            continue

        aenderung = (n - a) / a
        if p.art == "nicht_kleiner":
            ok = aenderung >= -p.grenze
        else:
            ok = aenderung <= p.grenze

        zeilen.append((p.name, f"{a:g}", f"{n:g}",
                       ("ok" if ok else "ROT") + f"  ({aenderung:+.1%})", p.warum))
        rot = rot or not ok

    return zeilen, rot


# ---------------------------------------------------------------------------
#  Selbsttest: die Regeln gegen bekannte Staende pruefen
# ---------------------------------------------------------------------------
#
# Eine Abnahme, die noch nie etwas abgelehnt hat, ist kein Beweis - sie koennte
# genauso gut immer gruen sagen. Darum zwei aufgezeichnete Staende: ein
# gesunder (Alkis16 gegen Alkis17, gemessen) und die ECHTE Signatur des
# Leerbakes Alkis10 (444 von 444 Chunks ohne Render-Geometrie, 1,4 GB statt
# 1,8 GB Actors - so stand es im Protokoll vom 16.09.2026).

GESUND_ALT = {
    "chunks_leer": 0, "actors_gb": 1.84, "netz_km": 2824.8, "spuren": 117351,
    "verbindungen": 199867, "ampeln": 1073, "schilder": 52689, "laternen": 72434,
    "bilder_s": 125, "ladezeit_s": 21.2, "gpu_timeouts": 0, "chunks": 31,
    "schlechtestes_ms": 29.0,
}

GESUND_NEU = dict(GESUND_ALT, bilder_s=127, ladezeit_s=21.5)

LEERBAKE = dict(GESUND_ALT, chunks_leer=444, actors_gb=1.39, chunks=444)

OHNE_STRASSEN = dict(GESUND_ALT, netz_km=1900.0, spuren=79000, verbindungen=130000)

LANGSAM = dict(GESUND_ALT, bilder_s=95, ladezeit_s=40.0)


def selbsttest():
    faelle = [
        ("gesunde Karte", GESUND_ALT, GESUND_NEU, False),
        ("Leerbake (Alkis10-Signatur)", GESUND_ALT, LEERBAKE, True),
        ("fehlende Strassen", GESUND_ALT, OHNE_STRASSEN, True),
        ("langsamer geworden", GESUND_ALT, LANGSAM, True),
        ("Kennzahl fehlt im Protokoll", GESUND_ALT, {"chunks_leer": 0}, True),
    ]
    fehler = 0
    for name, alt, neu, erwartet_rot in faelle:
        _, rot = bewerte(alt, neu)
        ok = (rot == erwartet_rot)
        print(f"  {name:32s} -> {'ROT' if rot else 'gruen':5s} "
              f"{'ok' if ok else 'FEHLER (erwartet ' + ('ROT' if erwartet_rot else 'gruen') + ')'}")
        fehler += 0 if ok else 1

    if fehler:
        print(f"\nSelbsttest FEHLGESCHLAGEN ({fehler} Fall/Faelle).")
        return 1
    print("\nSelbsttest bestanden: die Abnahme sagt nicht immer gruen.")
    return 0


def main():
    zerleger = argparse.ArgumentParser(description=__doc__)
    zerleger.add_argument("--neu", help="die frisch gebackene Karte")
    zerleger.add_argument("--selbsttest", action="store_true",
                          help="nur die Regeln gegen aufgezeichnete Staende pruefen")
    zerleger.add_argument("--alt", default=None,
                          help="Vorgaengerin (Vorgabe: die Default-Karte)")
    zerleger.add_argument("--dauer", type=int, default=120,
                          help="Spielzeit des Messlaufs in Sekunden")
    zerleger.add_argument("--aufwaermen", type=int, default=3,
                          help="hoechstens so viele Aufwaermrunden je Karte")
    args = zerleger.parse_args()

    if args.selbsttest:
        return selbsttest()

    if not args.neu:
        zerleger.error("--neu fehlt (oder --selbsttest verwenden)")

    alt_name = args.alt or standard_karte()
    if alt_name == args.neu:
        print(f"FEHLER: {args.neu} ist bereits die Vergleichskarte. "
              f"Mit --alt eine andere Vorgaengerin angeben.")
        return 2

    for karte in (alt_name, args.neu):
        umap = os.path.join(PROJEKT, "Content", "Maps", karte + ".umap")
        if not os.path.exists(umap):
            print(f"FEHLER: {umap} fehlt.")
            return 2

    print(f"Bake-Abnahme: {args.neu}  gegen  {alt_name}")
    print(f"  Messlauf je {args.dauer} s, bis zu {args.aufwaermen} Aufwaermrunden.\n")

    ergebnisse = {}
    for karte in (alt_name, args.neu):
        werte = messe(karte, args.dauer, args.aufwaermen)
        if werte is None:
            print(f"\nABBRUCH: Lauf auf {karte} gescheitert.")
            return 2
        ergebnisse[karte] = werte
        print()

    zeilen, rot = bewerte(ergebnisse[alt_name], ergebnisse[args.neu])

    print("=" * 78)
    print(f"{'Kennzahl':<22}{alt_name[-8:]:>12}{args.neu[-8:]:>12}   Urteil")
    print("-" * 78)
    for name, a, n, urteil, _ in zeilen:
        print(f"{name:<22}{a:>12}{n:>12}   {urteil}")
    print("=" * 78)

    if rot:
        print("\nABGELEHNT. Begruendung der fehlgeschlagenen Pruefungen:\n")
        for name, a, n, urteil, warum in zeilen:
            if urteil.startswith("ROT") or urteil == "FEHLT":
                print(f"  {name}: {a} -> {n}")
                print(f"      {warum}\n")
        return 1

    print("\nANGENOMMEN: die neue Karte ist in allen Punkten mindestens gleichwertig.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
