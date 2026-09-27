"""Erzeugt die Texturen der SebboTower-Dachaufbauten (PNG).

Drei Dateien nach Content/SebboTower/Textures/Source/ - Tools/import_sebbo_dach.py
holt sie nach /Game/SebboTower/Textures und steckt jede in eine maskierte,
zweiseitige Materialinstanz (je ein `decal`-Slot aus dem Blender-Manifest
Tools/Blender/make_sebbo_dach.py):

  * T_WbSeboLogo.png     - Wortmarke "SEBBO" in Weiss, 2048 x 838, fuer das
                           rechte Feld des Dachschilds (3,30 x 1,35 m).
  * T_WbSeboLogoAG.png  - die seBBo-AG-Wortmarke (Flamme, Vogel, Herz) aus
                           Data/Raw/SebboTower/quellen/sebbo_ag_logo.jpg, 1200 x
                           1087, fuer das linke Schildfeld (1,49 x 1,35 m).
                           Der marineblaue Grund des Fotos wird per Flood-Fill
                           vom Bildrand her transparent geschaltet.
  * T_WbSeboMagazin.png - das Titelbild des Magazins "SeBBo", 650 x 800 in der
                           Aufloesung der Vorlage, fuer die Plakattafel des
                           Magazinstaenders.

GEMEINSAME REGEL (siehe AGENTS.md, Nerobergbahn-Wagen): KEIN Aufkleber, der
Grund bleibt transparent und gemalt sind nur die Buchstaben bzw. Formen. Das
Schild ist anthrazit, das Magazinmotiv sitzt auf einem marineblauen Feld - die
Platte dahinter muss jeweils durchscheinen, darum maskiert das
Unreal-Material ueber das Alpha und ist zweiseitig.

MASSE IM MESH (Tools/Blender/make_sebbo_dach.py, build_logo/build_magazin):
  Schildplatte 4,80 x 1,35 m, geteilt bei y = -0,91 m:
    links  1,49 m  ->  T_WbSeboLogoAG.png   (1,49/1,35 = 1,1037)
    rechts 3,28 m  ->  T_WbSeboLogo.png      (3,28/1,35 = 2,4296)
  Magazintafel 1,30 x 1,60 m (0,8125) -> T_WbSeboMagazin.png (650/800)

Die Versalien sind mit rund 50 % der Bildhoehe bewusst groesser gezeichnet als
beim Wagenschriftzug: das Schild haengt 70 m ueber der Strasse und wird aus der
Entfernung gesehen.

Aufruf (irgendein Python mit Pillow):
  python Tools/make_sebbo_dach_textures.py [<Zielordner>]
"""

import os
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
PROJEKT = os.path.dirname(HERE)
OUT_DIR_DEFAULT = os.path.join(PROJEKT, "Content", "SebboTower",
                               "Textures", "Source")
QUELLEN = os.path.join(PROJEKT, "Data", "Raw", "SebboTower", "quellen")
OUT_DIR = sys.argv[1] if len(sys.argv) > 1 else OUT_DIR_DEFAULT

# Weiss wie die Markierungen des Turms (M_WbLmWhite), als sRGB-Wert.
WEISS = (232, 232, 228, 255)

# Bildmasse je Textur. Die Seitenverhaeltnisse muessen mit den Flaechen in
# make_sebbo_dach.py uebereinstimmen (siehe Kopf), sonst ist das Schild
# verzerrt - 0,05 % Fehler sind nicht sichtbar, mehr schon.
LOGO_W, LOGO_H = 2048, 843
AG_W, AG_H = 1200, 1087
MAG_W, MAG_H = 650, 800

# Erwartete Masse der Quelldateien. Am 27.09.2026 waren zwei davon vertauscht
# (921x2048 ist die Makroaufnahme der Bluete, das Cover ist 650x800), und
# das Ergebnis war ein glaubwuerdiges, voellig falsches Cover auf der
# Plakattafel. Die Gegenprobe gehoert deshalb an den Anfang des Laufs.
QUELLEN_ERWARTET = {
    "sebbo_ag_logo.jpg": (1875, 1875),
    "sebbo_magazin_cover.jpg": (650, 800),
    "sebbo_pflanze_01.jpg": (1920, 1080),
    "sebbo_pflanze_02.jpg": (1920, 1080),
    "sebbo_pflanze_03.jpg": (921, 2048),
}

# Der marineblaue Grund des Logos und die Marke fuer "weg damit". Wird als
# Fuellfarbe benutzt, deshalb kann sie nicht vorkommen.
MARINE = (6, 59, 109)
FUELL = (1, 254, 1)

FONTS = (r"C:\Windows\Fonts\segoeuib.ttf", r"C:\Windows\Fonts\arialbd.ttf",
         r"C:\Windows\Fonts\timesbd.ttf")

BEWEIS = []


def log(msg):
    print("###WBSTX### %s" % msg)


def font(paths, size):
    for p in paths:
        try:
            return ImageFont.truetype(p, size)
        except OSError:
            continue
    return ImageFont.load_default()


def zentriert(img, text, breiten_anteil, hoehen_anteil, abstand_anteil):
    """Wortmarke mit gesetzten Einzelbuchstaben zentriert einzeichnen.

    GESETZT wird nach der BREITE, nicht nach der Schriftgroesse: das Schild-
    Feld ist 2,43:1, eine feste Versalhoehe fuellt es fast vollstaendig und
    die Buchstaben werden zu breit (gemessen 99 % der Feldbreite). Die
    Schriftgroesse ist deshalb das Ergebnis: die groesste, mit der die Wort-
    marke noch in `breiten_anteil` der Bildbreite passt.

    Einzelbuchstaben statt text(): die Schriftgroesse allein gibt keinen
    Sichtabstand zwischen den Versalien, und eine Wortmarke steht oder faellt
    mit ihrer Laufweite.
    """
    w, h = img.size
    ziel = breiten_anteil * w
    abstand = max(1, int(abstand_anteil * h))
    d = ImageDraw.Draw(img)

    def masse(size):
        f = font(FONTS, size)
        kanten = [d.textbbox((0, 0), ch, font=f) for ch in text]
        breite = sum((bb[2] - bb[0]) for bb in kanten) + abstand * (len(text) - 1)
        return f, kanten, breite

    size = int(h)
    while size > 8:
        f, kanten, breite = masse(size)
        if breite <= ziel:
            break
        size -= 4
    f, kanten, breite = masse(size)

    # Block aus Versalien-Oberkante bis Grundlinie mittig setzen, nicht die
    # Grundlinie selbst - sonst haengt die Wortmarke bei Versalien optisch
    # zu tief im Feld.
    oben = min(bb[1] for bb in kanten)
    unten = max(bb[3] for bb in kanten)
    y0 = (h - (unten - oben)) * hoehen_anteil - oben
    x0 = (w - breite) * 0.5
    for ch, bb, dx in zip(text, kanten,
                          _laufweiten(kanten, abstand)):
        d.text((x0 + dx - bb[0], y0 - oben), ch, font=f, fill=WEISS)
    return x0, breite, y0 + unten


def _laufweiten(kanten, abstand):
    """Start-x je Buchstabe bei fester Laufweite."""
    out = []
    x = 0.0
    for bb in kanten:
        out.append(x)
        x += (bb[2] - bb[0]) + abstand
    return out


def farb_zaehler(img, ziel, tol):
    """Zaehlt Bildpunkte in Farbnachbarschaft von `ziel`."""
    px = img.convert("RGB").load()
    w, h = img.size
    n = 0
    for y in range(0, h, 2):
        for x in range(0, w, 2):
            p = px[x, y]
            if (abs(p[0] - ziel[0]) <= tol and abs(p[1] - ziel[1]) <= tol
                    and abs(p[2] - ziel[2]) <= tol):
                n += 1
    return n


# ---------------------------------------------------------------------------
# 1. Wortmarke SEBBO (rechtes Schildfeld)
# ---------------------------------------------------------------------------
def baue_wortmarke():
    img = Image.new("RGBA", (LOGO_W, LOGO_H), (0, 0, 0, 0))

    # 80 % der Feldbreite, senkrecht mittig (0,42 - etwas ueber Mitte, weil
    # die Unterlinie unten schwerer wirkt als der Schriftblock oben).
    x0, breite, grundlinie = zentriert(img, "SEBBO", 0.80, 0.42, 0.030)

    # Feine Unterlinie wie bei einer gezeichneten Wortmarke - haelt die
    # fuenf Buchstaben optisch zusammen.
    d = ImageDraw.Draw(img)
    linie_y = int(grundlinie + 0.075 * LOGO_H)
    d.rectangle((x0, linie_y, x0 + breite, linie_y + 12), fill=WEISS)

    pfad = os.path.join(OUT_DIR, "T_WbSeboLogo.png")
    img.save(pfad)
    log("T_WbSeboLogo.png  -> %s (%dx%d), Wortbreite %.0f px (%.1f %% der "
        "Feldbreite, %.0f cm im Feld von 3,28 m)"
        % (pfad, LOGO_W, LOGO_H, breite, 100.0 * breite / LOGO_W,
           328.0 * breite / LOGO_W))
    return pfad


# ---------------------------------------------------------------------------
# 2. seBBo-AG-Wortmarke (linkes Schildfeld) - aus dem Foto freigestellt
# ---------------------------------------------------------------------------
def ag_inhaltsbox(quelle, tol=55):
    """Umschliessendes Rechteck alles dessen, was NICHT der Grundton ist."""
    rgb = Image.open(quelle).convert("RGB")
    px = rgb.load()
    w, h = rgb.size
    minx, miny, maxx, maxy = w, h, -1, -1
    for y in range(h):
        for x in range(w):
            p = px[x, y]
            if (abs(p[0] - MARINE[0]) > tol or abs(p[1] - MARINE[1]) > tol
                    or abs(p[2] - MARINE[2]) > tol):
                if x < minx:
                    minx = x
                if x > maxx:
                    maxx = x
                if y < miny:
                    miny = y
                if y > maxy:
                    maxy = y
    return rgb, (minx, miny, maxx, maxy)


def baue_ag_logo():
    quelle = os.path.join(QUELLEN, "sebbo_ag_logo.jpg")
    if not os.path.exists(quelle):
        log("WARNUNG: %s fehlt - T_WbSeboLogoAG.png wird nicht gebaut."
            % quelle)
        return None

    rgb, box = ag_inhaltsbox(quelle)
    BEWEIS.append("AG-Quellenbild %dx%d, Inhalt x %d..%d y %d..%d"
                  % ((rgb.size[0], rgb.size[1]) + box))

    # Sicherheitsrand um den Inhalt, damit die Formen nicht an der Flaeche
    # anschlagen. Beide Seiten bekommen denselben Rand, das Seitenverhaeltnis
    # der Textur bleibt damit das des Inhalts.
    rand = 40
    box = (max(0, box[0] - rand), max(0, box[1] - rand),
           min(rgb.size[0], box[2] + 1 + rand), min(rgb.size[1], box[3] + 1 + rand))
    geschnitten = rgb.crop(box)
    breite, hoehe = geschnitten.size
    log("AG-Bildausschnitt %dx%d, Verhaeltnis %.4f (Soll %.4f)"
        % (breite, hoehe, breite / float(hoehe), 1.49 / 1.35))

    # Der Grund muss WEG, das Logo bleiben. Ein Flood-Fill vom Bildrand her
    # nimmt genau den von aussen zusammenhaengenden Grund - der helle Vogel
    # liegt zwar direkt auf dem Grund, hat aber eine voellig andere Farbe
    # (101,161,215 gegen 6,59,109) und blockiert die Fuellung. Ein blosser
    # Farbtest wuerde ihn mitfressen; deshalb der Beweis unten.
    #
    # `thresh` ist bei Pillow die SUMME der Band-Differenzen, 170 also rund
    # 57 je Kanal - das schluckt die JPEG-Ringe im flachen Grund, aber nicht
    # den hellen Vogel.
    arbeitsbild = geschnitten.copy()
    vogel_vorher = farb_zaehler(geschnitten, (101, 161, 215), 40)
    ImageDraw.floodfill(arbeitsbild, (0, 0), FUELL, thresh=170)

    # Alpha: alles, was nicht die Fuellfarbe bekommen hat, ist deckend. Ueber
    # die Differenz zum Fullbild geht das in C und nicht in einer Schleife ueber
    # 3 Millionen Punkte - und es ist exakt: Differenz 0 in ALLEN Baendern
    # heisst Punkt gleich Fuell.
    r, g, b = arbeitsbild.split()
    differenz = ImageChops.difference(arbeitsbild,
                                      Image.new("RGB", (breite, hoehe), FUELL))
    dr, dg, db = differenz.split()
    alpha = ImageChops.lighter(ImageChops.lighter(dr, dg), db)
    alpha = alpha.point(lambda v: 0 if v == 0 else 255)
    rgb_platte = Image.merge("RGBA", (r, g, b, alpha))

    vogel_nachher = farb_zaehler(rgb_platte, (101, 161, 215), 40)
    transparent = alpha.histogram()[0]
    BEWEIS.append("AG-Grund transparent: %d von %d Bildpunkten (%.1f %%)"
                  % (transparent, breite * hoehe,
                     100.0 * transparent / (breite * hoehe)))
    BEWEIS.append("AG-Vogel unversehrt: %d helle Blau-Punkte vorher, %d nachher"
                  % (vogel_vorher, vogel_nachher))

    # Nur behalten, wenn der Vogel wirklich uebrig ist. Sonst faellt der
    # Vogel beim Skalieren weg und das Schild zeigt eine Flamme im Nichts.
    if vogel_nachher < 0.6 * vogel_vorher:
        log("WARNUNG: Flood-Fill hat den Vogel mitgefressen "
            "(%d -> %d) - thresh zu gross." % (vogel_vorher, vogel_nachher))
        return None

    endgroesse = (AG_W, AG_H)
    rgb_platte = rgb_platte.resize(endgroesse, Image.LANCZOS)
    # Nach dem Skalieren die weichen Alpha-Raender auf 0/255 ziehen, sonst
    # bekommt das Unreal-Material einen grauen Saum um das Logo.
    r, g, b, a = rgb_platte.split()
    a = a.point(lambda v: 0 if v < 128 else 255)
    rgb_platte = Image.merge("RGBA", (r, g, b, a))

    pfad = os.path.join(OUT_DIR, "T_WbSeboLogoAG.png")
    rgb_platte.save(pfad)
    log("T_WbSeboLogoAG.png -> %s (%dx%d, Flaeche 1,49 x 1,35 m)"
        % (pfad, AG_W, AG_H))
    return pfad


# ---------------------------------------------------------------------------
# 3. Magazin-Cover (Plakattafel des Magazinstaenders)
# ---------------------------------------------------------------------------
def baue_magazin():
    quelle = os.path.join(QUELLEN, "sebbo_magazin_cover.jpg")
    if not os.path.exists(quelle):
        log("WARNUNG: %s fehlt - T_WbSeboMagazin.png wird nicht gebaut."
            % quelle)
        return None
    img = Image.open(quelle).convert("RGB")
    w, h = img.size
    BEWEIS.append("Magazin-Cover %dx%d, Verhaeltnis %.4f (Soll %.4f)"
                  % (w, h, w / float(h), 1.30 / 1.60))
    if abs((w / float(h)) - 0.8125) > 0.005:
        log("WARNUNG: Cover-Seitenverhaeltnis %.4f weicht von der Tafel "
            "(0,8125) ab - das Motiv wird verzerrt." % (w / float(h)))
    img = img.resize((MAG_W, MAG_H), Image.LANCZOS).convert("RGBA")
    # Deckend: das Cover ist eine Farbflaeche, kein Aufkleber. Das Alpha ist
    # trotzdem gesetzt, damit derselbe maskierte Decal-Slot wie beim Schild
    # greift.
    alpha = Image.new("L", (MAG_W, MAG_H), 255)
    img.putalpha(alpha)
    pfad = os.path.join(OUT_DIR, "T_WbSeboMagazin.png")
    img.save(pfad)
    log("T_WbSeboMagazin.png -> %s (%dx%d, Flaeche 1,30 x 1,60 m, "
        "nativ 500 px/m)" % (pfad, MAG_W, MAG_H))
    return pfad


def pruefe_quellen():
    """Masse der Quelldateien gegen die Erwartung - vertauschte Dateien
    auffallen, bevor daraus ein glaubwuerdiges falsches Asset wird."""
    for name, (b, h) in sorted(QUELLEN_ERWARTET.items()):
        pfad = os.path.join(QUELLEN, name)
        if not os.path.exists(pfad):
            log("WARNUNG: Quelle fehlt: %s" % pfad)
            continue
        ist = Image.open(pfad).size
        if ist != (b, h):
            log("WARNUNG: %s ist %dx%d, erwartet %dx%d - vertauschte Datei?"
                % (name, ist[0], ist[1], b, h))
        else:
            BEWEIS.append("Quelle %-24s %4dx%-4d wie erwartet"
                          % (name, ist[0], ist[1]))


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    pruefe_quellen()
    baue_wortmarke()
    baue_ag_logo()
    baue_magazin()
    log("FERTIG: Texturen in %s" % OUT_DIR)
    for zeile in BEWEIS:
        log("  %s" % zeile)


main()
