"""Erzeugt die Zielanzeige-Texturen (Zugzielanzeiger/Blind) fuer den Linie-6-Bus.

Zwei Bilder in ESWE-Orange (#EF7D00) mit schwarzer Liniennummer 6 und Ziel:
  * T_WbBusZiel_Mainz.png       -> "6  Mainz-Gonsenheim" (Fahrt Richtung Mainz)
  * T_WbBusZiel_Nordfriedhof.png -> "6  Nordfriedhof"     (Gegenrichtung)

Sie werden von Tools/import_bus_dest.py nach /Game/Vehicles/Bus/Ziel geholt und
dort in je ein unbeleuchtetes (Unlit) Material gesteckt; der Bus-Actor blendet
je Fahrtrichtung das passende Schild vor die Front (ersetzt die aufgebackene
"27 Freibad/Berufsschule"-Anzeige des Tripo-Modells).

Aufruf: python Tools/make_bus_dest_textures.py [<Zielordner>]
"""
import os
import sys
from PIL import Image, ImageDraw, ImageFont

OUT_DIR_DEFAULT = ("C:/freebuff/WiesbadenReal_Sicherung/WiesbadenReal/"
                   "Content/Vehicles/Bus/Ziel/Source")
OUT_DIR = sys.argv[1] if len(sys.argv) > 1 else OUT_DIR_DEFAULT

W, H = 1024, 256
ORANGE = (239, 125, 0, 255)     # ESWE #EF7D00
SCHWARZ = (16, 16, 16, 255)
FONTS = (r"C:\Windows\Fonts\arialbd.ttf", r"C:\Windows\Fonts\seguisb.ttf",
         r"C:\Windows\Fonts\ariblk.ttf")


def font(size):
    for p in FONTS:
        try:
            return ImageFont.truetype(p, size)
        except OSError:
            continue
    return ImageFont.load_default()


def fit_font(draw, text, max_w, max_h, start=200):
    """Groesste Schrift, die text in max_w x max_h haelt."""
    size = start
    while size > 8:
        f = font(size)
        bb = draw.textbbox((0, 0), text, font=f)
        if (bb[2] - bb[0]) <= max_w and (bb[3] - bb[1]) <= max_h:
            return f, bb
        size -= 4
    return font(8), draw.textbbox((0, 0), text, font=font(8))


def blind(nummer, ziel, dateiname):
    img = Image.new("RGBA", (W, H), ORANGE)
    d = ImageDraw.Draw(img)

    # Liniennummer gross links (eigenes Feld ~28 % Breite)
    num_feld = int(0.30 * W)
    fnum, bbn = fit_font(d, nummer, int(num_feld * 0.8), int(H * 0.82))
    nw, nh = bbn[2] - bbn[0], bbn[3] - bbn[1]
    d.text((num_feld // 2 - nw // 2 - bbn[0], H // 2 - nh // 2 - bbn[1]),
           nummer, font=fnum, fill=SCHWARZ)

    # duenner Trenner zwischen Nummer und Ziel
    d.line([num_feld, int(0.12 * H), num_feld, int(0.88 * H)],
           fill=SCHWARZ, width=max(2, W // 300))

    # Zielort im Restfeld, linksbuendig mit etwas Luft
    ziel_x0 = num_feld + int(0.03 * W)
    fz, bbz = fit_font(d, ziel, W - ziel_x0 - int(0.03 * W), int(H * 0.66))
    zh = bbz[3] - bbz[1]
    d.text((ziel_x0 - bbz[0], H // 2 - zh // 2 - bbz[1]),
           ziel, font=fz, fill=SCHWARZ)

    # WIP: die Ausrichtung auf dem Engine-Plane-Quad ist noch nicht final geloest
    # (Textur erscheint im Spiel gespiegelt) - hier bewusst die neutrale Vorlage
    # speichern, ohne Diagnose-Marker.
    img.save(os.path.join(OUT_DIR, dateiname))
    return img.size


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    a = blind("6", "Mainz-Gonsenheim", "T_WbBusZiel_Mainz.png")
    b = blind("6", "Nordfriedhof", "T_WbBusZiel_Nordfriedhof.png")
    print("###WBBUSZIEL### %s/T_WbBusZiel_Mainz.png %dx%d" % (OUT_DIR, a[0], a[1]))
    print("###WBBUSZIEL### %s/T_WbBusZiel_Nordfriedhof.png %dx%d" % (OUT_DIR, b[0], b[1]))


main()
