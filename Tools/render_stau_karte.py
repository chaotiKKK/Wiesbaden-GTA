"""Zeichnet aus Saved/Diagnose/staukarte.txt eine Luftkarte der Stauschwerpunkte.

Die Daten kommen aus einem Messlauf im Spiel
(FWiesbadenTrafficSimulation::WriteCongestionMap, Schalter -WbStauKarte): je
befahrener Spur das Mitteltempo, das Tempolimit, der Steher-Anteil und die Zahl
der Messwerte, dazu das ganze Strassennetz als Stadtplan.

Gefaerbt wird nach dem VERHAELTNIS Mitteltempo zu Limit, nicht nach dem Tempo
selbst. 30 km/h sind in einer Tempo-30-Zone freie Fahrt und auf einer
Hauptachse Stau - eine Karte nach absolutem Tempo faerbt darum jede
Wohnstrasse rot und jede Ausfallstrasse gruen, egal wie es dort laeuft.

Die PUNKTGROESSE zeigt, wie gut der Wert belegt ist (Zahl der Messwerte). Ein
roter Punkt aus 30 Messwerten ist ein Zufall, einer aus 40.000 ein Befund.

Aufruf:
  python Tools/render_stau_karte.py [Eingabe] [Ausgabe]
"""

import math
import os
import sys

from PIL import Image, ImageDraw, ImageFont

PROJECT = r"C:/freebuff/WiesbadenReal_Sicherung/WiesbadenReal"
DEFAULT_IN = PROJECT + "/Saved/Diagnose/staukarte.txt"
DEFAULT_OUT = PROJECT + "/Saved/Diagnose/staukarte.png"

WIDTH = 1800
MARGIN = 70
LEGEND_H = 150

BACKGROUND = (18, 20, 24)
NETWORK = (58, 62, 70)
TEXT = (232, 234, 238)
DIM = (150, 155, 162)

# Farbschluessel: Anteil des Limits -> Farbe.
STUFEN = [
    (0.75, (70, 200, 110), "fliesst (>= 75 % des Limits)"),
    (0.55, (190, 210, 70), "zaeh (55-75 %)"),
    (0.35, (240, 165, 55), "stockend (35-55 %)"),
    (0.20, (235, 95, 55), "Stau (20-35 %)"),
    (0.00, (215, 45, 60), "steht (< 20 %)"),
]


def farbe(ratio):
    for schwelle, rgb, _ in STUFEN:
        if ratio >= schwelle:
            return rgb
    return STUFEN[-1][1]


def oeffne(pfad):
    """Datei lesen, egal ob UTF-8 oder UTF-16.

    FFileHelper::SaveStringToFile schreibt UTF-16, sobald ein Zeichen
    ausserhalb von ASCII vorkommt - und deutsche Strassennamen tun das
    ("Strasse" mit scharfem S). Die Kodierung derselben Datei haengt damit vom
    Zufall ab, welche Strassen gemessen wurden. Die C++-Seite schreibt
    inzwischen fest UTF-8; dieser Leser kommt trotzdem mit beidem zurecht,
    damit aeltere Messungen weiter auswertbar bleiben.
    """
    with open(pfad, "rb") as f:
        roh = f.read()
    for kodierung in ("utf-8-sig", "utf-16", "utf-8", "latin-1"):
        try:
            return roh.decode(kodierung)
        except (UnicodeDecodeError, UnicodeError):
            continue
    raise UnicodeError(f"{pfad}: keine passende Kodierung gefunden")


def lade(pfad):
    netz, stau = [], []
    if True:
        f = oeffne(pfad).splitlines()
        for zeile in f:
            if zeile.startswith("NETZ "):
                t = zeile.split()
                netz.append((float(t[1]), float(t[2]), float(t[3]), float(t[4])))
            elif zeile.startswith("STAU "):
                t = zeile.split()
                # Der Name ist das LETZTE Feld und darf Leerzeichen tragen
                # ("Kaiser-Friedrich-Ring" geht, "Am Kurpark" auch) - deshalb
                # nur die ersten sechs Felder aufteilen, Rest ist Name.
                t = zeile.split(None, 7)
                stau.append({
                    "x": float(t[1]), "y": float(t[2]),
                    "kmh": float(t[3]), "limit": float(t[4]),
                    "steher": float(t[5]), "n": int(t[6]),
                    "name": t[7].strip() if len(t) > 7 else "(ohne Namen)",
                })
    return netz, stau


def schriften():
    """Windows-Arial, sonst die eingebaute Notschrift."""
    pfad = r"C:/Windows/Fonts/arial.ttf"
    pfad_fett = r"C:/Windows/Fonts/arialbd.ttf"
    try:
        return (ImageFont.truetype(pfad_fett, 30), ImageFont.truetype(pfad, 19),
                ImageFont.truetype(pfad, 15))
    except OSError:
        f = ImageFont.load_default()
        return f, f, f


def main():
    ein = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_IN
    aus = sys.argv[2] if len(sys.argv) > 2 else DEFAULT_OUT

    if not os.path.exists(ein):
        print(f"FEHLT: {ein} - erst einen Messlauf mit -WbStauKarte fahren.")
        return 1

    netz, stau = lade(ein)
    if not stau:
        print("Keine Stau-Messwerte in der Datei.")
        return 1

    # Ausschnitt aus den MESSWERTEN, nicht aus dem Netz: gemessen wird nur dort,
    # wo Verkehr lief (rund um den Spieler). Das ganze 6-km-Netz als Rahmen
    # machte die Schwerpunkte zu einem Fleck in der Ecke.
    xs = [p["x"] for p in stau]
    ys = [p["y"] for p in stau]
    minx, maxx = min(xs), max(xs)
    miny, maxy = min(ys), max(ys)
    rand = 0.10 * max(maxx - minx, maxy - miny)
    minx, maxx = minx - rand, maxx + rand
    miny, maxy = miny - rand, maxy + rand

    spanne_x = max(maxx - minx, 1.0)
    spanne_y = max(maxy - miny, 1.0)
    zeichen_b = WIDTH - 2 * MARGIN
    massstab = zeichen_b / spanne_x
    zeichen_h = int(spanne_y * massstab)
    height = zeichen_h + 2 * MARGIN + LEGEND_H

    def abb(x, y):
        # Bild-Y laeuft nach unten, Welt-Y nach oben -> spiegeln.
        return (MARGIN + (x - minx) * massstab,
                MARGIN + zeichen_h - (y - miny) * massstab)

    bild = Image.new("RGB", (WIDTH, height), BACKGROUND)
    d = ImageDraw.Draw(bild, "RGBA")
    fett, normal, klein = schriften()

    # -- Stadtplan --------------------------------------------------------
    innerhalb = 0
    for x1, y1, x2, y2 in netz:
        if max(x1, x2) < minx or min(x1, x2) > maxx:
            continue
        if max(y1, y2) < miny or min(y1, y2) > maxy:
            continue
        d.line([abb(x1, y1), abb(x2, y2)], fill=NETWORK, width=1)
        innerhalb += 1

    # -- Messpunkte, schwach belegte zuerst (die starken liegen oben) -----
    stau.sort(key=lambda p: p["n"])
    groesste_n = max(p["n"] for p in stau)
    for p in stau:
        ratio = p["kmh"] / p["limit"] if p["limit"] > 0 else 1.0
        r = 3.0 + 9.0 * math.sqrt(p["n"] / groesste_n)
        cx, cy = abb(p["x"], p["y"])
        rgb = farbe(ratio)
        d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=rgb + (215,))

    # -- Titel ------------------------------------------------------------
    d.text((MARGIN, 22), "Wiesbaden - wo der Verkehr steht", font=fett, fill=TEXT)
    d.text((MARGIN, 58),
           f"{len(stau)} befahrene Strassen aus einem Messlauf, "
           f"{innerhalb} Strassenzuege im Ausschnitt",
           font=klein, fill=DIM)

    # -- Legende ----------------------------------------------------------
    ly = height - LEGEND_H + 18
    d.text((MARGIN, ly), "Farbe: Mitteltempo im Verhaeltnis zum Tempolimit",
           font=normal, fill=TEXT)
    ly += 30
    for i, (_, rgb, beschriftung) in enumerate(STUFEN):
        x = MARGIN + (i % 3) * 460
        y = ly + (i // 3) * 26
        d.ellipse([x, y + 3, x + 14, y + 17], fill=rgb + (235,))
        d.text((x + 24, y), beschriftung, font=klein, fill=DIM)

    d.text((MARGIN, height - 42),
           "Punktgroesse = Zahl der Messwerte. Ein roter Punkt aus wenigen "
           "Messwerten ist ein Zufall, einer aus vielen ein Befund.",
           font=klein, fill=DIM)

    # -- Massstabsbalken --------------------------------------------------
    for meter in (2000, 1000, 500, 200):
        px = meter * 100.0 * massstab
        if px < zeichen_b * 0.28:
            break
    bx, by = WIDTH - MARGIN - px, MARGIN + zeichen_h - 18
    d.line([(bx, by), (bx + px, by)], fill=TEXT, width=3)
    d.line([(bx, by - 6), (bx, by + 6)], fill=TEXT, width=3)
    d.line([(bx + px, by - 6), (bx + px, by + 6)], fill=TEXT, width=3)
    d.text((bx, by - 26), f"{meter} m", font=klein, fill=TEXT)

    # -- Strassenweise zusammenfassen -------------------------------------
    #
    # Eine verstopfte Achse erzeugt Dutzende roter Punkte. Ungruppiert fuellt
    # sie die ganze Rangliste und verdeckt jeden anderen Schwerpunkt.
    gruppen = {}
    for p in stau:
        g = gruppen.setdefault(p["name"], {
            "name": p["name"], "n": 0, "kmh_sum": 0.0, "limit_sum": 0.0,
            "steher_sum": 0.0, "x_sum": 0.0, "y_sum": 0.0, "abschnitte": 0})
        g["n"] += p["n"]
        # Nach Messwerten gewichtet: ein Abschnitt mit 30 Messwerten darf eine
        # Achse mit 40.000 nicht verschieben.
        g["kmh_sum"] += p["kmh"] * p["n"]
        g["limit_sum"] += p["limit"] * p["n"]
        g["steher_sum"] += p["steher"] * p["n"]
        g["x_sum"] += p["x"] * p["n"]
        g["y_sum"] += p["y"] * p["n"]
        g["abschnitte"] += 1

    strassen = []
    for g in gruppen.values():
        if g["n"] <= 0:
            continue
        g["kmh"] = g["kmh_sum"] / g["n"]
        g["limit"] = g["limit_sum"] / g["n"]
        g["steher"] = g["steher_sum"] / g["n"]
        g["x"] = g["x_sum"] / g["n"]
        g["y"] = g["y_sum"] / g["n"]
        g["ratio"] = g["kmh"] / g["limit"] if g["limit"] > 0 else 1.0
        # Schwer wiegt, was langsam UND viel befahren ist. Nur "langsam"
        # liefert Einzelfaelle, nur "viel befahren" die Hauptachsen.
        g["gewicht"] = (1.0 - min(g["ratio"], 1.0)) * math.log10(max(g["n"], 10))
        strassen.append(g)
    strassen.sort(key=lambda g: g["gewicht"], reverse=True)

    # -- Die schlimmsten beschriften --------------------------------------
    belegt = []
    beschriftet = 0
    for z in strassen:
        if beschriftet >= 8 or z["name"].startswith("("):
            continue
        cx, cy = abb(z["x"], z["y"])
        if not (MARGIN < cx < WIDTH - MARGIN and MARGIN < cy < MARGIN + zeichen_h):
            continue
        # Ueberlappende Beschriftungen sind schlimmer als eine fehlende.
        if any((cx - ox) ** 2 + (cy - oy) ** 2 < 150 ** 2 for ox, oy in belegt):
            continue
        belegt.append((cx, cy))
        beschriftet += 1

        text = f"{z['name']}  {z['ratio']*100:.0f} %"
        kasten = d.textbbox((0, 0), text, font=klein)
        bw, bh = kasten[2] - kasten[0], kasten[3] - kasten[1]
        tx, ty = cx + 14, cy - bh - 12
        d.line([(cx, cy), (tx + 4, ty + bh + 6)], fill=(255, 255, 255, 120), width=1)
        d.rectangle([tx - 5, ty - 4, tx + bw + 6, ty + bh + 6],
                    fill=(12, 14, 18, 225), outline=(120, 126, 136, 255))
        d.text((tx, ty), text, font=klein, fill=TEXT)

    bild.save(aus)

    # -- Und die Zahlen dazu, damit die Karte nicht allein steht ----------
    print(f"Karte: {aus} ({WIDTH}x{height})")
    print(f"Ausschnitt: X {minx/100:.0f}..{maxx/100:.0f} m, Y {miny/100:.0f}..{maxy/100:.0f} m")
    print()

    for schwelle, _, beschriftung in STUFEN:
        n = sum(1 for p in stau
                if (p["kmh"] / p["limit"] if p["limit"] > 0 else 1.0) >= schwelle)
        print(f"  ab {schwelle:.2f}: {n:5d} Strassen kumuliert  ({beschriftung})")
    print()

    print("Die schwersten Schwerpunkte, nach STRASSE zusammengefasst:")
    print("(zehn Punkte derselben Strasse sind EIN Befund, nicht zehn)")
    print()
    for z in strassen[:12]:
        print(f"  {z['name'][:34]:34s} {z['kmh']:5.1f} von {z['limit']:4.0f} km/h "
              f"= {z['ratio']*100:3.0f} %   Steher {z['steher']*100:3.0f} %   "
              f"{z['abschnitte']:3d} Abschnitte, {z['n']:7d} Messwerte")
    return 0


if __name__ == "__main__":
    sys.exit(main())
