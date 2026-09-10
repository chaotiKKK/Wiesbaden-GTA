#!/usr/bin/env python3
"""Spike-Auswertung: Schaerfe-Metrik + Center-Crops ueber die Zoom-Serie.

- lapVar: Varianz des Laplacian im zentralen Kartenbereich -> je kleiner, desto
  weicher/pixelig die Textur (reines Schaerfemass fuer die rohe Textur).
- edgeDens: Anteil Pixel mit |Gradient| > 25 (Kantendichte).
- meanGrad: mittlere Gradientenstaerke.
Texel-Vergroesserung = Zoom / 2.5 (Basistextur mit kSuperSample=2.5 gebacken).
Ab Zoom 5 (Mag 2.0) zeichnet der Live-Vektor-Overlay -> Messung dort mischt.
"""
import os
from PIL import Image
import numpy as np

BASE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "Saved", "Diagnose")
OUT = os.path.join(BASE, "zoomcrop")
os.makedirs(OUT, exist_ok=True)

ZOOMS = [1, 2, 2.5, 3, 3.5, 4, 4.5, 5, 8]


def metrics(path):
    a = np.asarray(Image.open(path).convert("L"), dtype=np.float64)
    H, W = a.shape
    x0, x1, y0, y1 = int(W * 0.20), int(W * 0.80), int(H * 0.15), int(H * 0.85)
    c = a[y0:y1, x0:x1]
    gx = np.abs(np.diff(c, axis=1))
    gy = np.abs(np.diff(c, axis=0))
    grad = np.sqrt(gx[:-1, :] ** 2 + gy[:, :-1] ** 2)
    lap = np.abs(4 * c[1:-1, 1:-1] - c[2:, 1:-1] - c[:-2, 1:-1]
                 - c[1:-1, 2:] - c[1:-1, :-2])
    return float(lap.var()), float((grad > 25).mean()), float(grad.mean())


print(f"{'zoom':>5} {'texelMag':>8} {'lapVar':>12} {'edgeDens':>9} {'meanGrad':>9}")
for z in ZOOMS:
    p = os.path.join(BASE, f"map_zoom_{z}.png")
    lv, ed, mg = metrics(p)
    print(f"{z:>5} {z / 2.5:>8.2f} {lv:>12.1f} {ed:>9.4f} {mg:>9.2f}")

# Center-Crops (dichtes Stadtnetz an der Netzmitte), 1:1 und 2x NEAREST.
CX, CY, CW, CH = 880, 450, 360, 280
for z in ZOOMS:
    im = Image.open(os.path.join(BASE, f"map_zoom_{z}.png"))
    crop = im.crop((CX - CW // 2, CY - CH // 2, CX + CW // 2, CY + CH // 2))
    crop.save(os.path.join(OUT, f"crop_{z}.png"))
    crop.resize((CW * 2, CH * 2), Image.NEAREST).save(os.path.join(OUT, f"crop_{z}_2x.png"))
print("crops written to", OUT)