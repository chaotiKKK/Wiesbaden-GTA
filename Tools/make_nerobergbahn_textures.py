"""Erzeugt die aufgemalten Texturen des Nerobergbahn-Wagens (PNG, transparent).

Vier Bilder, alle nach Content/Nerobergbahn/Textures/Source/ - sie werden von
Tools/import_nerobergbahn.py nach /Game/Nerobergbahn/Textures geholt und dort in
maskierte, zweiseitige Materialinstanzen gesteckt (Slots `NbSchrift`,
`NbSkala`, `NbTacho`, `NbBuehneMuster` aus dem Blender-Manifest):

  * T_WbNbSchrift.png - "Nerobergbahn" in blauer Serifenschrift mit dem kleinen
    Zier-Emblem links davon. Beleg: Frame `key/851` (14:11) zeigt die
    Wortmarke gross auf der unteren gelben Wand, links ein blaues Ornament.
  * T_WbNbSkala.png - Wasserstandsskala 10/20/30/40 mit Teilstrichen an einer
    duennen senkrechten Linie. Beleg: Frame `key/840` (14:00) zeigt 10/20/30
    in blauen Ziffern am Wagenende ueber der blauen Zierlinie.
  * T_WbNbTacho.png - Geschwindigkeitsanzeige: runde Scheibe mit gruenem
    Sektor (zulaessig) und rotem Sektor (zu schnell), Zeiger an der Grenze.
    Beleg: Frame `key/884` (14:44) zeigt das Instrument auf der
    Armaturenplatte des Fuehrerstands.
  * T_WbNbBuehne.png - Riffelmuster des Buehnenbodens (dunkles Blech mit
    versetzten Rippen). Beleg: `key/840` und `key/313` - der Buehnenboden ist
    dunkel und gemustert, nicht glatt.
  * T_WbNbRautengitter.png - weisse Rautengitter-Felder der
    Bahnsteig-Balustraden. Beleg: `key/933` (15:33, Trog von oben) zeigt die
    weissen Rautenfelder zwischen den Stuetzen.
  * T_WbNbGitterrost.png - Laufrost im Gleistrog neben dem Gleis. Beleg:
    `key/933`, `key/941` - ueber der Trogsohle liegen Gitter, dazwischen die
    oxidrote Zahnstangenpartie.

Beide werden im Vorbild AUF die gelbe Wand gemalt, nicht als Blech aufgesetzt -
darum bleibt der Grund vollstaendig transparent (Alpha 0) und das Material
maskiert: zwischen den Buchstaben ist der gelbe Kasten zu sehen.

Farbe: die Lackfarbe des Wagens ist im Projekt linear (0,015 / 0,110 / 0,380);
PNG wird von Unreal als sRGB gelesen, also wird hier der sRGB-Wert geschrieben
(#215FA7), sonst waere die Schrift im Spiel deutlich heller als die blauen
Zierlinien daneben.

Aufruf (irgendein Python mit Pillow):
  python Tools/make_nerobergbahn_textures.py [<Zielordner>]
"""

import math
import os
import sys

from PIL import Image, ImageDraw, ImageFont

OUT_DIR_DEFAULT = ("C:/freebuff/WiesbadenReal_Sicherung/WiesbadenReal/"
                   "Content/Nerobergbahn/Textures/Source")
OUT_DIR = sys.argv[1] if len(sys.argv) > 1 else OUT_DIR_DEFAULT

BLAU = (33, 95, 167, 255)          # sRGB zu NbBlau

# Schriftgroesse im Bild haengt an der Flaechengroesse im Mesh: die
# Schriftflaeche ist 2,30 x 0,30 m, die Versalien im Vorbild ~0,17 m hoch
# (Frame `key/851`) - also rund 57 % der Bildhoehe.
SCHRIFT_W, SCHRIFT_H = 2048, 267
SKALA_W, SKALA_H = 320, 680
TACHO_W, TACHO_H = 512, 512
BUEHNE_W, BUEHNE_H = 512, 1024
# Bahnsteighalle: Rautengitter (Kachel 0,28 m) und Laufrost (Kachel 1,00 m).
RAUTE_W, RAUTE_H = 512, 512
ROST_W, ROST_H = 512, 512

# Tacho: Gruen bis zum zulaessigen Tempo (7,3 km/h), darueber Rot. Der Zeiger
# steht im Spiel auf der Grenze, weil die Bahn mit genau diesem Tempo faehrt.
GESCHW_MAX = 10.0                 # Skalenende in km/h
GESCHW_ZUL = 7.3                  # zulaessiges Tempo (ESWE/Wikipedia)
GRUEN = (46, 160, 66, 255)
ROT = (176, 32, 34, 255)
HELL = (232, 232, 228, 255)
PLATTE = (34, 36, 38, 255)
ZEIGER = (238, 238, 234, 255)

FONTS = (r"C:\Windows\Fonts\georgiab.ttf", r"C:\Windows\Fonts\timesbd.ttf",
         r"C:\Windows\Fonts\arialbd.ttf")


def font(paths, size):
    for p in paths:
        try:
            return ImageFont.truetype(p, size)
        except OSError:
            continue
    return ImageFont.load_default()


def schrift():
    """Wortmarke 'Nerobergbahn' + Zier-Emblem, linksbuendig im Feld."""
    img = Image.new("RGBA", (SCHRIFT_W, SCHRIFT_H), (255, 255, 255, 0))
    d = ImageDraw.Draw(img)

    text = "Nerobergbahn"
    size = 40
    f = font(FONTS, size)
    # Auf 57 % Bildhoehe einsetzen (Versalien), dann auf 74 % Bildbreite
    # strecken - das Vorbild ist breit gesetzt, nicht gedraengt.
    for _ in range(80):
        bb = d.textbbox((0, 0), text, font=f)
        if (bb[3] - bb[1]) >= 0.57 * SCHRIFT_H:
            break
        size += 4
        f = font(FONTS, size)

    bb = d.textbbox((0, 0), text, font=f)
    tw, th = bb[2] - bb[0], bb[3] - bb[1]
    ziel_breite = 0.74 * SCHRIFT_W

    # Text als eigenes Bild, damit er sich unabhaengig strecken laesst.
    t = Image.new("RGBA", (tw + 4, th + 4), (255, 255, 255, 0))
    td = ImageDraw.Draw(t)
    td.text((2 - bb[0], 2 - bb[1]), text, font=f, fill=BLAU)
    t = t.resize((int(ziel_breite), int(SCHRIFT_H * 0.62)), Image.LANCZOS)

    kern_x = 0.02 * SCHRIFT_W
    kern_y = (SCHRIFT_H - t.height) // 2 + int(0.06 * SCHRIFT_H)
    img.alpha_composite(t, (int(kern_x), max(0, kern_y)))

    # Zier-Emblem links: kleines Bluetenrad aus acht Blattovalen mit Ring -
    # im Frame `key/851` sitzt links vom Wort ein blaues Ornament.
    ex = int(0.013 * SCHRIFT_W)
    ey = SCHRIFT_H // 2
    r_out = int(0.115 * SCHRIFT_H)
    for k in range(8):
        a = 2.0 * math.pi * k / 8.0
        px = ex + r_out * 0.62 * math.cos(a)
        py = ey + r_out * 0.62 * math.sin(a)
        d.ellipse([px - r_out * 0.30, py - r_out * 0.30,
                   px + r_out * 0.30, py + r_out * 0.30], fill=BLAU)
    d.ellipse([ex - r_out * 0.26, ey - r_out * 0.26,
               ex + r_out * 0.26, ey + r_out * 0.26], fill=(255, 255, 255, 0),
              outline=BLAU, width=max(2, r_out // 8))

    img.save(os.path.join(OUT_DIR, "T_WbNbSchrift.png"))
    return img.size


def skala():
    """Wasserstandsskala: senkrechte Linie, Teilstriche, Zahlen 10..40."""
    img = Image.new("RGBA", (SKALA_W, SKALA_H), (255, 255, 255, 0))
    d = ImageDraw.Draw(img)

    # Die Skala im Vorbild: eine duenne senkrechte Linie, links davon die
    # Zahlen, rechts kurze Teilstriche (Halbteilung).
    linie_x = int(0.20 * SKALA_W)
    strich = int(0.16 * SKALA_W)
    d.line([linie_x, int(0.03 * SKALA_H), linie_x, int(0.97 * SKALA_H)],
           fill=BLAU, width=max(2, SKALA_W // 110))

    n = 4                                  # 10 / 20 / 30 / 40
    f = font(FONTS, int(0.115 * SKALA_W))
    for i in range(n):
        # 10 unten, 40 oben.
        t_von_oben = (n - 1 - i) / (n - 1)
        y = int((0.06 + 0.88 * t_von_oben) * SKALA_H)
        d.line([linie_x, y, linie_x + strich, y], fill=BLAU,
               width=max(2, SKALA_W // 90))
        if i > 0:
            # Halbteilung zwischen zwei Zahlen.
            yh = int((0.06 + 0.88 * (t_von_oben - 0.5 / (n - 1))) * SKALA_H)
            d.line([linie_x, yh, linie_x + strich // 2, yh], fill=BLAU,
                   width=max(1, SKALA_W // 150))
        bb = d.textbbox((0, 0), str(i * 10 + 10), font=f)
        d.text((linie_x - (bb[2] - bb[0]) - int(0.06 * SKALA_W),
                y - (bb[3] - bb[1]) // 2 - bb[1]), str(i * 10 + 10),
               font=f, fill=BLAU)

    img.save(os.path.join(OUT_DIR, "T_WbNbSkala.png"))
    return img.size


def tacho():
    """Geschwindigkeitsanzeige: runde Scheibe mit gruenem und rotem Sektor.

    Aufbau nach key/884: dunkle rechteckige Armaturenplatte, darin die runde
    Scheibe mit gruenem Kreisbogen (zulaessiges Tempo) und rotem Sektor
    (darueber), weisse Teilstriche und Zeiger. Die runde Form entsteht ueber
    das Alpha - der Blender-Bauer braucht dafuer kein Kreismodell.
    """
    img = Image.new("RGBA", (TACHO_W, TACHO_H), (255, 255, 255, 0))
    d = ImageDraw.Draw(img)

    # Platte mit abgerundeten Ecken
    m = int(0.04 * TACHO_W)
    d.rounded_rectangle([m, m, TACHO_W - m, TACHO_H - m], radius=int(0.06 * TACHO_W),
                        fill=PLATTE)

    cx = cy = TACHO_W // 2
    r_aussen = int(0.40 * TACHO_W)
    r_ring = int(0.365 * TACHO_W)
    d.ellipse([cx - r_aussen, cy - r_aussen, cx + r_aussen, cy + r_aussen],
              fill=(18, 19, 20, 255))
    d.ellipse([cx - r_ring, cy - r_ring, cx + r_ring, cy + r_ring],
              outline=(120, 124, 128, 255), width=max(2, TACHO_W // 128))

    # Winkel: 0 km/h unten links (225 Grad), Skalenende unten rechts
    # (-45 Grad). Gezeichnet wird im Bogenmass gegen den Uhrzeigersinn.
    def winkel(v):
        t = max(0.0, min(1.0, v / GESCHW_MAX))
        return math.radians(225.0 - 270.0 * t)

    r_band = int(0.30 * TACHO_W)
    breite = int(0.055 * TACHO_W)

    def bogen(v0, v1, farbe):
        schritte = max(2, int(abs(v1 - v0) * 12))
        punkte = []
        for k in range(schritte + 1):
            a = winkel(v0 + (v1 - v0) * k / schritte)
            punkte.append((cx + r_band * math.cos(a), cy - r_band * math.sin(a)))
        d.line(punkte, fill=farbe, width=breite, joint="curve")

    # gruener Sektor bis zum zulaessigen Tempo, roter darueber
    bogen(0.0, GESCHW_ZUL, GRUEN)
    bogen(GESCHW_ZUL, GESCHW_MAX, ROT)

    # Teilstriche und Zahlen
    f = font(FONTS, int(0.052 * TACHO_W))
    for v in range(0, int(GESCHW_MAX) + 1, 2):
        a = winkel(v)
        r0, r1 = int(0.245 * TACHO_W), int(0.335 * TACHO_W)
        d.line([cx + r0 * math.cos(a), cy - r0 * math.sin(a),
                cx + r1 * math.cos(a), cy - r1 * math.sin(a)],
               fill=HELL, width=max(2, TACHO_W // 140))
        rt = int(0.185 * TACHO_W)
        bb = d.textbbox((0, 0), str(v), font=f)
        d.text((cx + rt * math.cos(a) - (bb[2] - bb[0]) / 2,
                cy - rt * math.sin(a) - (bb[3] - bb[1]) / 2 - bb[1]),
               str(v), font=f, fill=HELL)

    # KEIN aufgemalter Zeiger: die Nadel ist im Modell ein eigenes Mesh
    # (SM_WbNbTachoZeiger) und wird im Spiel von der Geschwindigkeit gedreht
    # (WiesbadenNerobergbahn.cpp). Eine gemalte Nadel wuerde still daneben
    # stehen und sich nie bewegen. Nur die Nabe bleibt angedeutet.
    d.ellipse([cx - int(0.030 * TACHO_W), cy - int(0.030 * TACHO_W),
               cx + int(0.030 * TACHO_W), cy + int(0.030 * TACHO_W)],
              fill=(28, 30, 32, 255), outline=ZEIGER,
              width=max(2, TACHO_W // 150))

    # kleine rote Kontrollleuchte links auf der Platte (key/884)
    lx, ly = int(0.17 * TACHO_W), int(0.17 * TACHO_H)
    d.ellipse([lx - 14, ly - 14, lx + 14, ly + 14], fill=ROT)

    img.save(os.path.join(OUT_DIR, "T_WbNbTacho.png"))
    return img.size


def buehne():
    """Riffelmuster des Buehnenbodens (dunkles Blech mit versetzten Rippen).

    Eine Kachel fuer die ganze Buehne - das Muster ist im Bild so dicht
    gezeichnet, wie es im Vorbild aussieht (Rippenabstand wenige Zentimeter).
    """
    img = Image.new("RGBA", (BUEHNE_W, BUEHNE_H), (26, 27, 29, 255))
    d = ImageDraw.Draw(img)

    hell = (52, 54, 57, 255)
    dunkel = (14, 15, 16, 255)
    spalten, zeilen = 6, 12
    sw_, sh_ = BUEHNE_W / spalten, BUEHNE_H / zeilen
    for z in range(zeilen):
        for s in range(spalten):
            # Versetzte Rippen mit dunklem Fugenkreuz dazwischen
            x0, y0 = s * sw_, z * sh_
            d.rectangle([x0 + 3, y0 + 3, x0 + sw_ - 3, y0 + sh_ - 3], fill=hell)
            d.rectangle([x0 + 7, y0 + 7, x0 + sw_ - 7, y0 + sh_ - 7], fill=(40, 42, 45, 255))
            if (s + z) % 2 == 0:
                d.line([x0 + sw_ * 0.25, y0 + sh_ * 0.5,
                        x0 + sw_ * 0.75, y0 + sh_ * 0.5], fill=dunkel, width=3)

    img.save(os.path.join(OUT_DIR, "T_WbNbBuehne.png"))
    return img.size


def rautengitter():
    """Weisses Rautengitter der Bahnsteig-Balustraden (key/933).

    Diagonale weisse Latten in beiden Richtungen auf transparentem Grund -
    dieselbe Technik wie Schriftzug und Skala: das Material ist maskiert und
    zweiseitig, die Latten sind also duenn und von beiden Seiten sichtbar.
    Kachel = 0,28 m (tpm 3,6 im Mesh), darin zwei Rauten -> Rautenteilung
    rund 14 cm; die Lattenstärke entspricht etwa 2 cm.
    """
    img = Image.new("RGBA", (RAUTE_W, RAUTE_H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    weiss = (250, 250, 248, 255)
    latten = 40            # ~2,2 cm Lattenstaerke
    schritt = 250          # 0,136 m in der Kachel -> Rauten wie im Vorbild
    for k in range(-4, 6):
        o = k * schritt
        d.line([(-RAUTE_W, o), (2 * RAUTE_W, o + 3 * RAUTE_W)], fill=weiss, width=latten)
        d.line([(-RAUTE_W, o), (2 * RAUTE_W, o - 3 * RAUTE_W)], fill=weiss, width=latten)
    img.save(os.path.join(OUT_DIR, "T_WbNbRautengitter.png"))
    return img.size


def gitterrost():
    """Laufrost/Rostabdeckung im Gleistrog (key/933, key/941).

    Tragstaebe alle 5 cm (wie die Rostabdeckung des Seilkanals, §5.1a) und
    Querstaebe alle 25 cm; der Grund bleibt transparent, damit der Rost als
    Gitter ueber der dunklen Trogsohle liegt und nicht als Blech wirkt.
    Kachel = 1,00 m im Mesh.
    """
    img = Image.new("RGBA", (ROST_W, ROST_H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    stahl = (150, 154, 160, 255)
    rost_je = 20                       # 20 Staebe je Meter -> 5 cm Teilung
    stab = ROST_W / rost_je
    for i in range(rost_je):
        x0 = i * stab
        d.rectangle([x0 + 6, 0, x0 + stab - 6, ROST_H], fill=stahl)
    for quart in range(4):               # Querstaebe alle 25 cm
        y = quart * ROST_H / 4.0
        d.rectangle([0, y + 6, ROST_W, y + 18], fill=stahl)
    img.save(os.path.join(OUT_DIR, "T_WbNbGitterrost.png"))
    return img.size


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    a = schrift()
    b = skala()
    c = tacho()
    e = buehne()
    f = rautengitter()
    g = gitterrost()
    print("###WBNBTEX### %s/T_WbNbRautengitter.png %dx%d" % (OUT_DIR, f[0], f[1]))
    print("###WBNBTEX### %s/T_WbNbGitterrost.png %dx%d" % (OUT_DIR, g[0], g[1]))
    print("###WBNBTEX### %s/T_WbNbSchrift.png %dx%d" % (OUT_DIR, a[0], a[1]))
    print("###WBNBTEX### %s/T_WbNbSkala.png %dx%d" % (OUT_DIR, b[0], b[1]))
    print("###WBNBTEX### %s/T_WbNbTacho.png %dx%d" % (OUT_DIR, c[0], c[1]))
    print("###WBNBTEX### %s/T_WbNbBuehne.png %dx%d" % (OUT_DIR, e[0], e[1]))


main()
