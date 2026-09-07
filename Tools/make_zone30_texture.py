"""Erzeugt die Fahrbahn-Markierungstextur '30' (weisse Ziffern, transparent).

Deutsche Tempo-30-Zonen tragen eine grosse weisse '30' auf dem Asphalt (keine
rote Ringscheibe - das waere das Schild). Die Ziffern sind laenglich gestreckt,
damit sie aus flachem Blickwinkel des Fahrers richtig proportioniert wirken.

Ausgabe: PNG mit weissen Ziffern auf voll transparentem Grund -> ein maskiertes
Material malt nur die Ziffern auf die Fahrbahn, der Asphalt bleibt drumherum
sichtbar. Bildoben (V=0) zeigt in Fahrtrichtung (der Fahrer liest die '30'
beim Heranfahren aufrecht).

Aufruf (irgendein Python mit Pillow):
  python Tools/make_zone30_texture.py <out.png>
"""

import sys

from PIL import Image, ImageDraw, ImageFont

OUT = sys.argv[1] if len(sys.argv) > 1 else "T_WbZone30.png"

# Hochformat: Strassenziffern sind deutlich hoeher als breit.
W, H = 512, 900
img = Image.new("RGBA", (W, H), (255, 255, 255, 0))
draw = ImageDraw.Draw(img)

text = "30"
FONT_PATH = None
for path in (r"C:\Windows\Fonts\arialbd.ttf", r"C:\Windows\Fonts\Arial.ttf"):
    try:
        ImageFont.truetype(path, 100)
        FONT_PATH = path
        break
    except OSError:
        continue

# Schriftgroesse so waehlen, dass '30' ~72 % der Bildbreite fuellt (Rand bleibt,
# nichts laeuft aus dem Bild).
font = ImageFont.load_default()
if FONT_PATH is not None:
    size = 100
    for _ in range(40):
        f = ImageFont.truetype(FONT_PATH, size)
        bb = draw.textbbox((0, 0), text, font=f)
        if (bb[2] - bb[0]) >= 0.72 * W:
            break
        size += 12
    font = ImageFont.truetype(FONT_PATH, size)

# Zentrieren ueber die Bounding-Box.
bbox = draw.textbbox((0, 0), text, font=font)
tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
tx = (W - tw) // 2 - bbox[0]
ty = (H - th) // 2 - bbox[1]
draw.text((tx, ty), text, fill=(255, 255, 255, 255), font=font)

# Vertikal auf Strassenmarkierungs-Proportion strecken (schlanker/hoeher).
img = img.resize((W, int(H * 1.15)), Image.LANCZOS)

img.save(OUT)
print("###ZONE30TEX### geschrieben: %s (%dx%d)" % (OUT, img.width, img.height))
