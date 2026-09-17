"""Erzeugt authentische ESWE-Punktmatrix-Blinds als PNG (Bernstein-LEDs auf Dunkel).

Ausgabe nach Data/Raw/Bus/blind/:
  blind_mainz.png   (breit)  "[6]  Mainz-Gonsenheim"   - Front/Seite Hinrichtung
  blind_nord.png    (breit)  "[6]  Nordfriedhof"        - Front/Seite Rueckrichtung
  blind_route6.png  (quadr.) "6"                        - Heck (nur Liniennummer)

Look: regelmaessiges LED-Punktraster, gezuendete Punkte bernstein, ungezuendete
sehr dunkel - wie ein echtes Rollband. Die Liniennummer steht in einem eigenen,
groesseren Feld links (durch eine Punkt-Trennlinie abgesetzt).
"""
import os
from PIL import Image, ImageDraw, ImageFont

OUT = r"C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Data\Raw\Bus\blind"
os.makedirs(OUT, exist_ok=True)

# Quelltexturen werden NORMAL/lesbar gespeichert (Nummer links, Text aufrecht).
# Die Engine-Plane rendert die Textur von aussen um 180 Grad gedreht; das gleicht
# der Platzer per negativer X- UND Y-Skala aus (= 180-Grad-Drehung), sodass im
# Spiel alles korrekt steht (per Ecken-Marker-Foto verifiziert).
def hflip_save(img, path):
    img.save(path)

ON = (255, 176, 0)     # gezuendete LED (Bernstein)
OFF = (26, 18, 5)      # ungezuendete LED (nur schwach sichtbar)
BG = (6, 6, 8)         # Gehaeuse-Hintergrund

def load_font(size):
    for p in (r"C:\Windows\Fonts\arialbd.ttf", r"C:\Windows\Fonts\ARIALBD.TTF",
              r"C:\Windows\Fonts\arial.ttf"):
        try:
            return ImageFont.truetype(p, size)
        except Exception:
            pass
    try:
        return ImageFont.truetype("DejaVuSans-Bold.ttf", size)
    except Exception:
        return ImageFont.load_default()

def fit_font(text, max_w, max_h):
    """Groesste Schrift, mit der text in max_w x max_h passt."""
    size = max_h
    while size > 6:
        f = load_font(size)
        l, t, r, b = f.getbbox(text)
        if (r - l) <= max_w and (b - t) <= max_h:
            return f
        size -= 2
    return load_font(8)

def draw_centered(mask_draw, box, text, font):
    x0, y0, x1, y1 = box
    l, t, r, b = font.getbbox(text)
    tw, th = r - l, b - t
    px = x0 + ((x1 - x0) - tw) // 2 - l
    py = y0 + ((y1 - y0) - th) // 2 - t
    mask_draw.text((px, py), text, fill=255, font=font)

def dotify(mask, pitch, radius):
    W, H = mask.size
    img = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(img)
    px = mask.load()
    y = pitch // 2
    while y < H:
        x = pitch // 2
        while x < W:
            lit = px[x, y] > 120
            c = ON if lit else OFF
            d.ellipse([x - radius, y - radius, x + radius, y + radius], fill=c)
            x += pitch
        y += pitch
    return img

def make_wide(dest, path):
    W, H = 1040, 208
    pitch, radius = 8, 3
    pad = 14
    route_w = 210                      # eigenes, groesseres Liniennummern-Feld links
    mask = Image.new("L", (W, H), 0)
    md = ImageDraw.Draw(mask)
    # Liniennummer gross links
    rf = fit_font("6", route_w - 2 * pad, H - 2 * pad)
    draw_centered(md, (pad, pad, route_w - pad, H - pad), "6", rf)
    # Ziel rechts
    df = fit_font(dest, (W - route_w) - 2 * pad, H - 3 * pad)
    draw_centered(md, (route_w + pad, pad, W - pad, H - pad), dest, df)
    img = dotify(mask, pitch, radius)
    # Punkt-Trennlinie zwischen Nummernfeld und Ziel
    d = ImageDraw.Draw(img)
    yy = pitch // 2
    while yy < H:
        d.ellipse([route_w - radius, yy - radius, route_w + radius, yy + radius], fill=(90, 64, 10))
        yy += pitch
    hflip_save(img, path)
    print("geschrieben:", path, img.size)

def make_route(path):
    W = H = 256
    pitch, radius = 9, 4
    pad = 22
    mask = Image.new("L", (W, H), 0)
    md = ImageDraw.Draw(mask)
    rf = fit_font("6", W - 2 * pad, H - 2 * pad)
    draw_centered(md, (pad, pad, W - pad, H - pad), "6", rf)
    hflip_save(dotify(mask, pitch, radius), path)
    print("geschrieben:", path, (W, H))

make_wide("Mainz-Gonsenheim", os.path.join(OUT, "blind_mainz.png"))
make_wide("Nordfriedhof", os.path.join(OUT, "blind_nord.png"))
make_route(os.path.join(OUT, "blind_route6.png"))
print("ENDE")
