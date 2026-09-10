#!/usr/bin/env python3
"""Regeneriert map_zoom_gallery.html mit base64-eingebetteten Crop-PNGs
(der Preview-Server serviert nur die HTML-Datei, keine statischen PNGs)."""
import base64
import os

BASE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "Saved", "Diagnose", "zoomcrop")
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "map_zoom_gallery.html")

ZOOMS = [
    (1, "0.4x", ""), (2, "0.8x", ""), (2.5, "1.0x", " (Textur-Limit)"),
    (3, "1.2x", ""), (3.5, "1.4x", ""), (4, "1.6x", ""), (4.5, "1.8x", ""),
    (5, "2.0x", " Vektoren AN"), (8, "3.2x", " Vektoren AN"),
]


def data_uri(name):
    with open(os.path.join(BASE, name), "rb") as f:
        return "data:image/png;base64," + base64.b64encode(f.read()).decode()


def row(suffix, scale_label):
    figs = []
    for z, mag, note in ZOOMS:
        name = f"crop_{z}{suffix}.png"
        figs.append(
            f'  <figure><img src="{data_uri(name)}"><figcaption>'
            f'Zoom {z} &middot; {scale_label}{mag}x{note}</figcaption></figure>')
    return "\n".join(figs)


html = f"""<!doctype html>
<html>
<head>
<meta charset="utf-8">
<title>Zoom-Serie Weltkarte</title>
<style>
  body{{background:#111;color:#ddd;font-family:Consolas,monospace;margin:0;padding:12px}}
  h2{{color:#9cf;font-size:14px}}
  .row{{display:flex;gap:10px;margin:8px 0 16px;width:max-content}}
  figure{{margin:0;text-align:center}}
  figcaption{{font-size:12px;margin-top:4px;white-space:nowrap}}
  img{{display:block;image-rendering:pixelated}}
  .on{{color:#fea}}
</style>
</head>
<body>
<h2>Center-Crops 1:1 (360x280) — rohe Textur; ab Zoom 5 (Texel 2.0x) Vektor-Overlay AN</h2>
<div class="row">
{row("", "")}
</div>
<h2>2x NEAREST-Upscale derselben Crops (vergroessert die Pixelstruktur)</h2>
<div class="row">
{row("_2x", "")}
</div>
</body>
</html>
"""

with open(OUT, "w", encoding="utf-8") as f:
    f.write(html)
print("gallery written:", OUT, os.path.getsize(OUT) // 1024, "KB")