#!/usr/bin/env python3
"""2x2-Komparativraster: dieselben Kartenausschnitte bei Zoom 3 / 3.5 / 4 / 4.5
(LANCZOS 2.5x), passt in einen Viewport - direkter Schaerfevergleich."""
import os
from PIL import Image, ImageDraw

BASE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "Saved", "Diagnose")
CELLS = [(3, "Zoom 3  Texel 1.2x"), (3.5, "Zoom 3.5  Texel 1.4x"),
         (4, "Zoom 4  Texel 1.6x"), (4.5, "Zoom 4.5  Texel 1.8x")]

PX, PY, PW, PH = 990, 400, 240, 180
UP = 2.5
LABEL_H = 30
GAP = 12
CW = int(PW * UP)
CH = int(PH * UP) + LABEL_H

grid = Image.new("RGB", (CW * 2 + GAP, CH * 2 + GAP), (20, 22, 26))
draw = ImageDraw.Draw(grid)
for i, (z, label) in enumerate(CELLS):
    src = Image.open(os.path.join(BASE, f"map_zoom_{z}.png")).convert("RGB")
    patch = src.crop((PX - PW // 2, PY - PH // 2, PX + PW // 2, PY + PH // 2))
    big = patch.resize((CW, CH - LABEL_H), Image.LANCZOS)
    cx, cy = (i % 2) * (CW + GAP), (i // 2) * (CH + GAP)
    grid.paste(big, (cx, cy))
    draw.text((cx + 6, cy + (CH - LABEL_H) + 6), label, fill=(255, 255, 255))

out = os.path.join(BASE, "grid_345.png")
grid.save(out)
print("written", out, grid.size)