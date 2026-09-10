#!/usr/bin/env python3
"""3x-LANCZOS-Vergroesserung eines kleinen Kartenausschnitts (240x180 px,
Bildschirmmitte leicht versetzt) fuer die kritischen Zooms 2.5..5 als Streifen.
LANCZOS statt NEAREST: bildet ab, was das Spiel zeigt (weiches Bilinear-Upsampling
der Textur) - Blockigkeit kaeme sonst vom Upscaler selbst."""
import os
from PIL import Image, ImageDraw

BASE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "Saved", "Diagnose")
ZOOMS = [(2.5, "Zoom 2.5  Texel 1.0x"), (3, "Zoom 3  Texel 1.2x"),
         (3.5, "Zoom 3.5  Texel 1.4x"), (4, "Zoom 4  Texel 1.6x"),
         (4.5, "Zoom 4.5  Texel 1.8x"), (5, "Zoom 5  Texel 2.0x  Vektoren AN")]

PX, PY, PW, PH = 990, 400, 240, 180
UP = 3
LABEL_H = 34
PANEL_W = PW * UP
PANEL_H = PH * UP + LABEL_H

strip = Image.new("RGB", (PANEL_W * len(ZOOMS), PANEL_H), (20, 22, 26))
draw = ImageDraw.Draw(strip)
for i, (z, label) in enumerate(ZOOMS):
    src = Image.open(os.path.join(BASE, f"map_zoom_{z}.png")).convert("RGB")
    patch = src.crop((PX - PW // 2, PY - PH // 2, PX + PW // 2, PY + PH // 2))
    big = patch.resize((PW * UP, PH * UP), Image.LANCZOS)
    x0 = i * PANEL_W
    strip.paste(big, (x0, 0))
    draw.text((x0 + 8, PH * UP + 8), label, fill=(255, 255, 255))

out = os.path.join(BASE, "patch_strip.png")
strip.save(out)
print("written", out, strip.size)