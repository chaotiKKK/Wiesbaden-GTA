"""Auswertung einer Messfahrt (Tools/fahrmessung.cmd) - Fahrphysik in Zahlen.

    python Tools/fahrmessung_auswerten.py Saved/Logs/wb_fahrmessung_<Name>.log [...]

Liest die WbFahrt-Zeilen (-WbFahrTelemetrie, 10 Hz) und teilt die Fahrt an den
GEMESSENEN Eingaben in die Phasen des WbDrive-Profils: Anfahren (Gas, keine
Lenkung), Lenken rechts, Lenken links, Bremsen. Mehrere Logs nebeneinander =
Vorher/Nachher-Vergleich in einer Tabelle.
"""
import math
import re
import sys

ZEILE = re.compile(r"WbFahrt ((?:\w+=-?[\d.]+ ?)+)")
G = 9.81


def lesen(pfad):
    proben = []
    with open(pfad, encoding="utf-8", errors="replace") as datei:
        for zeile in datei:
            treffer = ZEILE.search(zeile)
            if treffer:
                proben.append({k: float(v) for k, v in
                               (paar.split("=") for paar in treffer.group(1).split())})
    return proben


def erste(proben, bedingung, ab=0):
    for i in range(ab, len(proben)):
        if bedingung(proben[i]):
            return i
    return None


def zeit_bis(proben, start, bedingung):
    i = erste(proben, bedingung, start)
    return None if i is None else proben[i]["t"] - proben[start]["t"]


def auswerten(proben):
    k = {}
    start = erste(proben, lambda p: p["gas"] > 0.5)
    if start is None:
        return {"Fehler": "keine Gasphase im Log"}
    lenk_re = erste(proben, lambda p: p["lenk"] > 0.02, start)
    lenk_li = erste(proben, lambda p: p["lenk"] < -0.02, lenk_re or start)
    bremse = erste(proben, lambda p: p["bremse"] > 0.5, start)

    # --- Anfahren ---------------------------------------------------------
    k["0-50 km/h [s]"] = zeit_bis(proben, start, lambda p: p["v"] >= 50)
    k["0-100 km/h [s]"] = zeit_bis(proben, start, lambda p: p["v"] >= 100)
    ende_a = lenk_re or bremse or len(proben)
    k["Radspin im Anfahren [s]"] = sum(0.1 for p in proben[start:ende_a] if p["spin"])
    k["Nicken Anfahren max [Grad]"] = max((abs(p["nick"]) for p in proben[start:ende_a]), default=0)
    # Federung beim Anfahren: eine Feder schiesst ueber die Ruhelage der
    # Beschleunigung hinaus, eine reine Glaettung kriecht darauf zu.
    t0 = proben[start]["t"]
    frueh = [p["nick"] for p in proben[start:ende_a] if p["t"] - t0 <= 2.0]
    ruhe = [p["nick"] for p in proben[start:ende_a] if 1.2 <= p["t"] - t0 <= 2.0]
    if frueh and ruhe and sum(ruhe) > 0:
        mittel = sum(ruhe) / len(ruhe)
        k["Nicken Anfahren Ueberschwingen [%]"] = 100.0 * (max(frueh) - mittel) / mittel

    # --- Lenken rechts: Sprung auf 0,6 bei Vollgas --------------------------
    if lenk_re is not None:
        ende_r = lenk_li or bremse or len(proben)
        rechts = proben[lenk_re:ende_r]
        k["Tempo beim Einlenken [km/h]"] = proben[lenk_re]["v"]
        lenk_end = max(p["lenk"] for p in rechts)
        k["Lenkwinkel 90 % erreicht [s]"] = zeit_bis(proben, lenk_re, lambda p: p["lenk"] >= 0.9 * lenk_end)
        gier_spitze = max(p["gier"] for p in rechts)
        # Stationaer = Mittel der Sekunde 2..3 nach dem Einlenken.
        stat = [p["gier"] for p in rechts if 2.0 <= p["t"] - rechts[0]["t"] <= 3.0]
        gier_stat = sum(stat) / len(stat) if stat else gier_spitze
        k["Gierrate 63 % [s]"] = zeit_bis(proben, lenk_re, lambda p: p["gier"] >= 0.63 * gier_stat)
        k["Gierrate 90 % [s]"] = zeit_bis(proben, lenk_re, lambda p: p["gier"] >= 0.9 * gier_stat)
        k["Gier-Ueberschwingen [%]"] = 100.0 * (gier_spitze - gier_stat) / gier_stat if gier_stat else None
        k["Querbeschl. max rechts [g]"] = max(abs(p["ay"]) for p in rechts) / G
        k["Wanken max rechts [Grad]"] = max(abs(p["wank"]) for p in rechts)
        k["Schwimmwinkel max rechts [Grad]"] = max(abs(p["schwimm"]) for p in rechts)

    # --- Wechsel rechts -> links -----------------------------------------
    if lenk_li is not None:
        ende_l = bremse or len(proben)
        links = proben[lenk_li:ende_l]
        k["Gier-Vorzeichenwechsel nach Umlenken [s]"] = zeit_bis(proben, lenk_li, lambda p: p["gier"] < 0)
        k["Querbeschl. max links [g]"] = max(abs(p["ay"]) for p in links) / G
        k["Wanken max links [Grad]"] = max(abs(p["wank"]) for p in links)
        k["Schwimmwinkel max links [Grad]"] = max(abs(p["schwimm"]) for p in links)

    # --- Bremsen --------------------------------------------------------------
    if bremse is not None:
        stopp = erste(proben, lambda p: p["v"] < 0.5, bremse)
        bereich = proben[bremse:(stopp + 1) if stopp is not None else len(proben)]
        v0 = proben[bremse]["v"]
        k["Bremsen ab [km/h]"] = v0
        if stopp is not None:
            dauer = proben[stopp]["t"] - proben[bremse]["t"]
            weg = sum((a["v"] + b["v"]) / 2 / 3.6 * (b["t"] - a["t"]) for a, b in zip(bereich, bereich[1:]))
            k["Bremsdauer [s]"] = dauer
            k["Bremsweg [m]"] = weg
            k["Bremsweg auf 100 km/h normiert [m]"] = weg * (100.0 / v0) ** 2 if v0 > 1 else None
            k["mittl. Verzoegerung [g]"] = (v0 / 3.6) / dauer / G if dauer > 0 else None
        k["Blockieranteil [%]"] = 100.0 * sum(1 for p in bereich if p["block"]) / max(len(bereich), 1)
        if "gleit" in proben[bremse]:
            # Raeder gleiten = blockiert ohne Seitenfuehrung (innerer Zustand).
            k["Raeder gleiten beim Bremsen [%]"] = 100.0 * sum(1 for p in bereich if p["gleit"]) / max(len(bereich), 1)
        k["Gierrate beim Bremsen max [Grad/s]"] = max(abs(p["gier"]) for p in bereich)
        # Lenkwirkung beim Bremsen: wie weit dreht der Wagen, waehrend er bremst?
        k["Kursaenderung beim Bremsen [Grad]"] = sum(
            (a["gier"] + b["gier"]) / 2 * (b["t"] - a["t"]) for a, b in zip(bereich, bereich[1:]))
        frueh = [p for p in bereich if p["t"] - proben[bremse]["t"] <= 2.0]
        k["Querbeschl. erste 2 s Bremsen [g]"] = sum(abs(p["ay"]) for p in frueh) / max(len(frueh), 1) / G
        k["Nicken Bremsen max [Grad]"] = max(abs(p["nick"]) for p in bereich)
        # Federung: was tut die Karosserie NACH dem Stillstand? Ein Feder-Masse-
        # System schwingt zurueck (Vorzeichenwechsel des Nickens), eine reine
        # Glaettung kriecht ohne Umkehr auf null.
        if stopp is not None:
            nach = proben[stopp:stopp + 30]
            nick_stopp = proben[stopp]["nick"]
            gegen = [p["nick"] for p in nach if p["nick"] * nick_stopp < 0]
            k["Nachschwingen nach Stopp [Grad]"] = max((abs(x) for x in gegen), default=0.0)
            ruhig = erste(nach, lambda p: abs(p["nick"]) < 0.1 * max(abs(nick_stopp), 1e-6))
            k["Nicken beruhigt nach Stopp [s]"] = None if ruhig is None else nach[ruhig]["t"] - nach[0]["t"]

    # --- Hub (Federweg der Wurzel gegen die Sollhoehe) ----------------------
    hub = [p["hub"] for p in proben[start:]]
    k["Hub RMS [cm]"] = math.sqrt(sum(h * h for h in hub) / len(hub))
    k["Hub max [cm]"] = max(abs(h) for h in hub)

    # --- Bodenkontakt je Rad und Kanten ---------------------------------------
    if "spalt" in proben[start]:
        fahrt = [p for p in proben[start:] if p["v"] > 5.0]
        if fahrt:
            k["Radspalt max [cm]"] = max(p["spalt"] for p in fahrt)
            k["Radspalt Mittel [cm]"] = sum(p["spalt"] for p in fahrt) / len(fahrt)
        # Kante = Knick im Bodenverlauf (zweite Differenz), nicht die Steigung:
        # bei 30 m/s liegen 3 m zwischen zwei Proben, ein Gefaelle allein
        # verschiebt die Bodenhoehe schon um Dezimeter.
        kanten = []
        for i in range(start + 1, len(proben) - 1):
            a, b, c = proben[i - 1], proben[i], proben[i + 1]
            if b["v"] > 5.0 and abs(c["boden"] - 2 * b["boden"] + a["boden"]) >= 6.0:
                if not kanten or i - kanten[-1] > 5:
                    kanten.append(i)
        k["Kanten ueberfahren"] = len(kanten)
        if kanten:
            spitzen, nach, spalte = [], [], []
            for i in kanten:
                fenster = proben[i:i + 12]
                spitze = max(fenster, key=lambda p: abs(p["fz"]))
                spitzen.append(abs(spitze["fz"]))
                danach = fenster[fenster.index(spitze):]
                gegen = [abs(p["fz"]) for p in danach if p["fz"] * spitze["fz"] < 0]
                nach.append(max(gegen, default=0.0))
                spalte.append(max(p["spalt"] for p in proben[max(i - 1, 0):i + 3]))
            k["Karosseriehub an Kanten, Mittel der Spitzen [cm]"] = sum(spitzen) / len(spitzen)
            k["Karosserie-Nachschwingen an Kanten [cm]"] = sum(nach) / len(nach)
            k["Radspalt an Kanten, Mittel [cm]"] = sum(spalte) / len(spalte)
    k["Proben"] = len(proben)
    return k


def zahl(x):
    if x is None:
        return "-"
    if isinstance(x, float):
        return "%.2f" % x
    return str(x)


def main(pfade):
    ergebnisse = [(pfad, auswerten(lesen(pfad))) for pfad in pfade]
    namen = []
    for _, erg in ergebnisse:
        for name in erg:
            if name not in namen:
                namen.append(name)
    breite = max(len(n) for n in namen)
    kopf = " | ".join(p.replace("\\", "/").rsplit("/", 1)[-1][:22].rjust(22) for p, _ in ergebnisse)
    print("%s | %s" % ("".ljust(breite), kopf))
    for name in namen:
        print("%s | %s" % (name.ljust(breite), " | ".join(zahl(erg.get(name)).rjust(22) for _, erg in ergebnisse)))


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    main(sys.argv[1:])
