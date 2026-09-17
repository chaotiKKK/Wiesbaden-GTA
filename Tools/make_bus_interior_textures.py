"""Erzeugt die Innenraum-Texturen des Fahrgast-Busses als PNG (Pillow, ohne Engine).

Anlass: Der Innenraum wurde allein ueber Vertexfarben gefaerbt - es gab kein
Textur-Asset, jede Flaeche war eine einfarbige Platte. Als Fahrgast sah man
deshalb "keine Texturen", sondern nur Farbflaechen.

Jetzt bekommt jede Flaechenart ihre eigene, KACHELNDE Textur:

  T_WbBusIntBoden     Linoleum/Laufsteg: feine Koernung, Ablaufspuren,
                       Fugenraster alle 128 px (= 50 cm)
  T_WbBusIntSitz      Sitzstoff: Koerperbindung + feiner Schussfaden
  T_WbBusIntWand      Wandbeplankung: Streichputz-Koernung, Fuge alle 256 px
  T_WbBusIntDecke     Dachhimmel: gelochte Platte (Lueftungsloecher), Fugen
  T_WbBusIntTechnik   Gebuerstetes Metall/Kunststoff: Haltestangen,
                       Fensterbaenke, Armaturenbrett, Fahrerplatz

Alle Texturen sind GRAU (R=G=B): die FARBE kommt weiter aus den Vertexfarben,
die Textur liefert nur die Oberflaeche. Sonst muesste man fuer jede Farbe eine
eigene Textur bauen, und das ESWE-Gelb der Haltestangen (Vertexfarbe) haette
keine Entsprechung mehr.

Kachelweite: eine Textur deckt 200 cm x 200 cm des Innenraums ab
(`WiesbadenBusInterior::TexCmPerTile`); die UVs werden daraus gerechnet, die
Texturen muessen also an den Raendern fortsetzbar sein - Saemtliche Muster
stehen darum auf Rasterweiten, die 512 ganzzahlig teilen (Fugen, Punkte,
Wellen mit ganzzahliger Frequenz).

Ausgabe: Content/Vehicles/Bus/Interior/Source/*.png. Die Materialien daraus
macht Tools/import_bus_interior.py (headless). Aufruf:

  python Tools/make_bus_interior_textures.py
"""
import math
import os
import random
from PIL import Image

# Ausgabepfad aus der Skriptlage ableiten (Tools/ liegt im Projektwurzel),
# damit der Aufruf nicht von einer fest verdrahteten Laufwerkskopie abhaengt.
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "Content", "Vehicles", "Bus", "Interior", "Source")

TILE = 512
# Wellenzahlen muessen ganzzahlig sein, sonst schliesst sich die Kachel nicht.
TWO_PI = 2.0 * math.pi


def waves(x, y, terms):
    """Kachelnde Niederfrequenz aus Sinuswellen (fx, fy, amplitude, phase)."""
    v = 0.0
    for fx, fy, amp, phase in terms:
        v += amp * math.sin(TWO_PI * (fx * x + fy * y) / TILE + phase)
    return v


def grey(value):
    return max(0, min(255, int(round(value * 255.0))))


def build(name, shade):
    """Schreibt eine 512x512-Textur; `shade(x, y, rnd)` liefert die Helligkeit."""
    rnd = random.Random(name)
    img = Image.new("L", (TILE, TILE))
    img.putdata([grey(shade(x, y, rnd)) for y in range(TILE) for x in range(TILE)])
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, name + ".png")
    img.convert("RGB").save(path)
    return path


def boden(x, y, rnd):
    # Linoleum: hell (die Vertexfarbe ist dunkel), Koernung, Ablaufspuren und
    # ein Fugenraster alle 50 cm - die Fuge selbst dunkler als der Belag.
    v = 0.86 + waves(x, y, [(1, 2, 0.05, 0.0), (2, -1, 0.04, 1.1), (3, 3, 0.025, 2.3)])
    v += rnd.uniform(-0.05, 0.05)
    if x % 128 < 2 or y % 128 < 2:
        v *= 0.80
    return v


def sitz(x, y, rnd):
    # Sitzstoff: 5-px-Karo (Koerperbindung) + diagonaler Schussfaden.
    v = 0.93 if ((x // 5) + (y // 5)) % 2 == 0 else 0.85
    if (x + y) % 10 < 2:
        v -= 0.05
    v += 0.02 * math.sin(TWO_PI * (x - y) / 32.0)
    v += rnd.uniform(-0.02, 0.02)
    return v


def wand(x, y, rnd):
    # Wandbeplankung: feine Streichkoernung, Plattenfuge alle 100 cm.
    v = 0.93 + waves(x, y, [(0, 1, 0.02, 0.4), (4, 0, 0.012, 1.7)]) + rnd.uniform(-0.025, 0.025)
    if x % 256 < 2 or y % 256 < 2:
        v *= 0.84
    return v


def decke(x, y, rnd):
    # Dachhimmel: gelochte Platte (Lueftung), Fugen alle 100 cm.
    v = 0.95 + rnd.uniform(-0.02, 0.02)
    # Loecher auf einem 16-px-Raster (16 teilt 512) - Lueftungs- und
    # Lautsprecherloecher des Himmels, wie im echten Stadtbus.
    dx, dy = x % 16 - 8, y % 16 - 8
    if dx * dx + dy * dy <= 2:
        v -= 0.09
    if x % 256 < 2 or y % 256 < 2:
        v *= 0.88
    return v


def technik(x, y, rnd):
    # Gebuerstetes Metall: Laengsschliff (pro Zeile konstant), Koernung,
    # ein paar tiefere Riefen.
    v = 0.86 + rnd.uniform(-0.05, 0.05)
    v += rnd.uniform(-0.03, 0.03)
    v += waves(x, y, [(0, 3, 0.03, 0.9), (0, 7, 0.02, 2.1)])
    return v


TEXTURES = [
    ("T_WbBusIntBoden", boden),
    ("T_WbBusIntSitz", sitz),
    ("T_WbBusIntWand", wand),
    ("T_WbBusIntDecke", decke),
    ("T_WbBusIntTechnik", technik),
]

for _name, _fn in TEXTURES:
    print("%s -> %s" % (_name, build(_name, _fn)))
print("FERTIG: %d Texturen in %s" % (len(TEXTURES), OUT))
