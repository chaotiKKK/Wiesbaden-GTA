"""Erzeugt die Logo-Textur der SebboTower-Dachreklame (PNG, transparent).

Ein Bild nach Content/SebboTower/Textures/Source/ - Tools/import_sebbo_dach.py
holt es nach /Game/SebboTower/Textures und steckt es in eine maskierte,
zweiseitige Materialinstanz (Slot `SbLogo` aus dem Blender-Manifest
Tools/Blender/make_sebbo_dach.py):

  * T_WbSeboLogo.png - Wortmarke "SEBBO" in Weiss mit feiner Unterlinie.

Das Schild ist wie beim Nerobergbahn-Schriftzug KEIN Aufkleber: der Grund der
Datei bleibt vollstaendig transparent (Alpha 0), gemalt sind nur die
Buchstaben. Die Platte dahinter ist anthrazit und muss zwischen den Buchstaben
durchscheinen - darum maskiert das Unreal-Material ueber das Alpha und ist
zweiseitig (die Flaeche liegt 1 cm vor der Platte).

Flaeche im Mesh: 4,80 x 1,35 m (SebboDachLogo), Bildformat also 2048 x 576
(3,556:1). Die Versalien sind mit rund 50 % der Bildhoehe bewusst groesser
gezeichnet als beim Wagenschriftzug: das Schild haengt 70 m ueber der Strasse
und wird aus der Entfernung gesehen.

Aufruf (irgendein Python mit Pillow):
  python Tools/make_sebbo_dach_textures.py [<Zielordner>]
"""

import os
import sys

from PIL import Image, ImageDraw, ImageFont

OUT_DIR_DEFAULT = (r"C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal"
                   r"\Content\SebboTower\Textures\Source")
OUT_DIR = sys.argv[1] if len(sys.argv) > 1 else OUT_DIR_DEFAULT

# Weiss wie die Markierungen des Turms (M_WbLmWhite), als sRGB-Wert.
WEISS = (232, 232, 228, 255)

LOGO_W, LOGO_H = 2048, 576

FONTS = (r"C:\Windows\Fonts\segoeuib.ttf", r"C:\Windows\Fonts\arialbd.ttf",
         r"C:\Windows\Fonts\timesbd.ttf")


def font(paths, size):
    for p in paths:
        try:
            return ImageFont.truetype(p, size)
        except OSError:
            continue
    return ImageFont.load_default()


def zentriert(img, text, y_anteil, hoehe_anteil, buchstaben_abstand):
    """Wortmarke mit gesetzten Einzelbuchstaben zentriert einzeichnen.

    Einzelbuchstaben statt text(): die Schriftgroesse allein gibt keinen
    Sichtabstand zwischen den Versalien, und eine Wortmarke steht oder faellt
    mit ihrer Laufweite.
    """
    f = font(FONTS, int(hoehe_anteil * LOGO_H / 0.72))
    d = ImageDraw.Draw(img)

    # Einzelbuchstaben anordnen und Gesamtbreite messen.
    pos = []
    x = 0.0
    for ch in text:
        bb = d.textbbox((0, 0), ch, font=f)
        pos.append((x, bb))
        x += (bb[2] - bb[0]) + buchstaben_abstand
    gesamt = x - buchstaben_abstand

    y0 = y_anteil * LOGO_H
    x0 = (LOGO_W - gesamt) * 0.5
    for ch, (dx, bb) in zip(text, pos):
        d.text((x0 + dx - bb[0], y0 - bb[1]), ch, font=f, fill=WEISS)
    return x0, gesamt


def main():
    os.makedirs(OUT_DIR, exist_ok=True)

    img = Image.new("RGBA", (LOGO_W, LOGO_H), (0, 0, 0, 0))

    # Wortmarke: Versalienhoehe ~50 % der Bildhoehe, leichte Laufweite.
    x0, breite = zentriert(img, "SEBBO", 0.16, 0.50, 34)

    # Feine Unterlinie wie bei einer gezeichneten Wortmarke - haelt die
    # fuenf Buchstaben optisch zusammen.
    d = ImageDraw.Draw(img)
    linie_y = int(0.82 * LOGO_H)
    d.rectangle((x0, linie_y, x0 + breite, linie_y + 12), fill=WEISS)

    pfad = os.path.join(OUT_DIR, "T_WbSeboLogo.png")
    img.save(pfad)
    print("T_WbSeboLogo.png -> %s (%dx%d)" % (pfad, LOGO_W, LOGO_H))


main()
