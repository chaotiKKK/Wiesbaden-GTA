"""Erzeugt eine KACHELBARE Rauschtextur als PNG.

Die Stadt-Materialien haben ihr Aussehen bisher mit
MaterialExpressionNoise berechnet - dreioktaviges Simplex-Rauschen, JE
BILDPUNKT. Gemessen kostete das rund die Haelfte der Bildzeit:

    normale Materialien:  Bildzeit 146 ms, Renderer 142-146 ms
    einfarbige:           Bildzeit  70-99 ms, Renderer   8,8 ms

Bei 1280 x 720 sind das ueber 900.000 Auswertungen je Bild, und das Gelaende
allein fuellt fast den ganzen Schirm.

Dieselbe Optik entsteht aus einer Textur, die einmal berechnet und dann nur
noch abgetastet wird. Eine Texturabtastung ist um Groessenordnungen billiger
als dreioktaviges Rauschen.

Die Textur MUSS kachelbar sein, sonst zeigt jede Wiederholung eine Naht. Das
wird hier dadurch erreicht, dass das Wertrauschen auf einem periodischen Gitter
aufgebaut wird: Der Zugriff auf die Gitterpunkte laeuft modulo der
Gitterbreite, wodurch rechter und linker Rand dieselben Werte benutzen.

Aufruf:  py Tools/make_noise_texture.py <ausgabe.png> [groesse]
"""

import math
import struct
import sys
import zlib

SIZE = int(sys.argv[2]) if len(sys.argv) > 2 else 512
OUT = sys.argv[1] if len(sys.argv) > 1 else 'T_WbNoise.png'

# Oktaven: Gitterbreite und Gewicht. Die Gitterbreiten muessen SIZE teilen,
# sonst ist das Ergebnis nicht kachelbar.
OCTAVES = [(4, 0.5), (8, 0.25), (16, 0.15), (32, 0.10)]

SEED = 20260826


def hash_value(ix, iy, period):
    """Wiederholbarer Zufallswert 0..1 auf einem periodischen Gitter."""
    x = ix % period
    y = iy % period
    h = (x * 374761393 + y * 668265263 + SEED * 1442695040888963407) & 0xFFFFFFFF
    h = (h ^ (h >> 13)) * 1274126177 & 0xFFFFFFFF
    h = h ^ (h >> 16)
    return (h & 0xFFFFFF) / float(0xFFFFFF)


def smooth(t):
    """Weiche Ueberblendung (3t^2 - 2t^3) - ohne sie sieht man das Gitter."""
    return t * t * (3.0 - 2.0 * t)


def octave(px, py, period):
    """Bilinear ueberblendetes Wertrauschen auf einem Gitter mit `period`."""
    step = SIZE / float(period)
    gx = px / step
    gy = py / step

    x0 = int(math.floor(gx))
    y0 = int(math.floor(gy))
    fx = smooth(gx - x0)
    fy = smooth(gy - y0)

    v00 = hash_value(x0, y0, period)
    v10 = hash_value(x0 + 1, y0, period)
    v01 = hash_value(x0, y0 + 1, period)
    v11 = hash_value(x0 + 1, y0 + 1, period)

    top = v00 + (v10 - v00) * fx
    bot = v01 + (v11 - v01) * fx
    return top + (bot - top) * fy


rows = []
total_weight = sum(w for _, w in OCTAVES)

for y in range(SIZE):
    row = bytearray()
    for x in range(SIZE):
        value = 0.0
        for period, weight in OCTAVES:
            value += octave(x, y, period) * weight
        value /= total_weight
        row.append(max(0, min(255, int(value * 255.0 + 0.5))))
    rows.append(bytes(row))

# --- PNG schreiben (Graustufen, 8 Bit) -----------------------------------
raw = b''.join(b'\x00' + r for r in rows)   # Filterbyte 0 je Zeile


def chunk(tag, data):
    return (struct.pack('>I', len(data)) + tag + data
            + struct.pack('>I', zlib.crc32(tag + data) & 0xFFFFFFFF))


png = b'\x89PNG\r\n\x1a\n'
png += chunk(b'IHDR', struct.pack('>IIBBBBB', SIZE, SIZE, 8, 0, 0, 0, 0))
png += chunk(b'IDAT', zlib.compress(raw, 9))
png += chunk(b'IEND', b'')

with open(OUT, 'wb') as f:
    f.write(png)

# Nahtprobe: Rand gegen Rand. Kachelbar heisst, dass die Fortsetzung ueber die
# Kante stetig ist - der Unterschied zwischen letzter und erster Spalte darf
# nicht groesser sein als zwischen zwei beliebigen Nachbarspalten.
inner = sum(abs(rows[y][x] - rows[y][x + 1]) for y in range(SIZE) for x in range(SIZE - 1))
inner /= float(SIZE * (SIZE - 1))
seam = sum(abs(rows[y][SIZE - 1] - rows[y][0]) for y in range(SIZE)) / float(SIZE)

print('WBNOISE %dx%d geschrieben: %s' % (SIZE, SIZE, OUT))
print('WBNOISE Nahtprobe: Kante %.2f gegen Nachbarschaft %.2f (kachelbar, wenn aehnlich)'
      % (seam, inner))
