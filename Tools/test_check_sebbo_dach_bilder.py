"""Tests fuer die beiden Bild-Werkzeuge der SebboTower-Dachaufbauten.

Geprueft wird das, was beim Bauen am laengsten gedauert hat: die Farbklassen
des Lagevergleichts und der Mass-Waechter der Quelldateien. Beides sind
stille Fehlerquellen - eine zu enge Klasse laesst den Pruefer ein Bild als
"leer" melden, ein vertauschtes Quellbild sieht vollstaendig plausibel aus.

  python -m unittest Tools.test_check_sebbo_dach_bilder -v
"""

import importlib.util
import os
import sys
import unittest

TOOLS = os.path.dirname(os.path.abspath(__file__))


def lade(name):
    """Laedt ein Werkzeug, ohne es auszufuehren (es ruft am Ende main())."""
    pfad = os.path.join(TOOLS, name)
    quelle = open(pfad, "r", encoding="utf-8").read()
    if name == "check_sebbo_dach_bilder.py":
        # sys.exit(main()) am Ende abschneiden.
        quelle = quelle.replace("sys.exit(main())", "pass")
    else:
        quelle = quelle.replace("\nmain()\n", "\n")
    ns = {"__name__": "geprueft", "__file__": pfad}
    exec(compile(quelle, pfad, "exec"), ns)
    return ns


BILDER = lade("check_sebbo_dach_bilder.py")
TEXTUREN = lade("make_sebbo_dach_textures.py")


class FarbklassenTest(unittest.TestCase):
    """Die Klassen trennen Bildteile - nicht Farbtabellen."""

    def test_rosa_trifft_herz_und_nicht_gelb(self):
        rosa = BILDER["_ist_rosa"]
        # gerendertes Herz (238,74,214) und Quellfarbe (254,60,209)
        self.assertTrue(rosa(238, 74, 214))
        self.assertTrue(rosa(254, 60, 209))
        # Gelb der Flamme, weisse Schrift, Marine und Himmel
        for rgb in ((239, 203, 105), (255, 255, 255), (45, 73, 119),
                    (52, 60, 72)):
            self.assertFalse(rosa(*rgb), "Rosa darf %s nicht fressen" % (rgb,))

    def test_gelb_trifft_flame_und_rosa_nicht(self):
        gelb = BILDER["_ist_gelb"]
        self.assertTrue(gelb(239, 203, 105))
        self.assertTrue(gelb(255, 208, 92))
        for rgb in ((238, 74, 214), (238, 247, 255), (45, 73, 119)):
            self.assertFalse(gelb(*rgb), "Gelb darf %s nicht fressen" % (rgb,))

    def test_hellgrau_trifft_schrift_und_nicht_platte(self):
        hellgrau = BILDER["_ist_hellgrau"]
        self.assertTrue(hellgrau(238, 247, 255))
        self.assertTrue(hellgrau(255, 255, 255))
        # Anthrazitplatte und Himmel sind unter der Schwelle, der helle
        # Vogel nicht (der ist farbig).
        for rgb in ((87, 91, 100), (52, 60, 72), (109, 130, 162)):
            self.assertFalse(hellgrau(*rgb),
                             "Weiss darf %s nicht fressen" % (rgb,))

    def test_gruen_nimmt_auch_gewaschene_farben(self):
        gruen = BILDER["_ist_gruen"]
        # Quelle dunkel, Render vom Licht gewaschen - beide muessen greifen,
        # sonst wandert der Schwerpunkt (am 27.09.: um 10 % nach oben).
        self.assertTrue(gruen(20, 60, 40))
        self.assertTrue(gruen(55, 86, 70))
        for rgb in ((45, 73, 119), (239, 203, 105), (238, 74, 214)):
            self.assertFalse(gruen(*rgb), "Gruen darf %s nicht fressen" % (rgb,))

    def test_himmel_erkennt_nur_den_hintergrund(self):
        himmel = BILDER["_ist_himmel"]
        # (182,194,210) ist der gemessene Himmel im Kontrollbild
        # vorschau_sebo_dach_logo_front.png (oben und an beiden Raendern).
        self.assertTrue(himmel(182, 194, 210))
        for rgb in ((45, 73, 119), (87, 91, 100), (239, 203, 105),
                    (238, 74, 214)):
            self.assertFalse(himmel(*rgb),
                             "Himmel darf %s nicht fressen" % (rgb,))


class QuellenTest(unittest.TestCase):
    """Die Masse der Bildvorlagen sind der Vertrag, nicht der Dateiname."""

    def test_erwartete_masse_sind_historisch_belegt(self):
        erwartet = TEXTUREN["QUELLEN_ERWARTET"]
        # Genau diese Verwechslung ist am 27.09.2026 passiert: die
        # 921x2048-Datei ist die Makroaufnahme der Bluete, das Cover ist
        # 650x800. Beide sahen nach "irgendein Foto" aus.
        self.assertEqual(erwartet["sebbo_magazin_cover.jpg"], (650, 800))
        self.assertEqual(erwartet["sebbo_pflanze_03.jpg"], (921, 2048))
        self.assertEqual(erwartet["sebbo_ag_logo.jpg"], (1875, 1875))

    def test_alle_fuenf_vorlagen_sind_vermerkt(self):
        self.assertEqual(len(TEXTUREN["QUELLEN_ERWARTET"]), 5)

    def test_alle_vorlagen_liegen_vor(self):
        from PIL import Image
        ordner = TEXTUREN["QUELLEN"]
        for name, (b, h) in TEXTUREN["QUELLEN_ERWARTET"].items():
            pfad = os.path.join(ordner, name)
            self.assertTrue(os.path.exists(pfad), "fehlt: %s" % pfad)
            ist = Image.open(pfad).size
            self.assertEqual(ist, (b, h),
                             "%s ist %dx%d, erwartet %dx%d"
                             % (name, ist[0], ist[1], b, h))

    def test_texturmasse_passen_zu_den_flaechen(self):
        """Seitenverhaeltnis der Textur gegen die Flaeche im Mesh."""
        felder = (("AG_W", "AG_H", 1.49 / 1.35, "SbLogoAG"),
                  ("LOGO_W", "LOGO_H", 3.28 / 1.35, "SbLogo"),
                  ("MAG_W", "MAG_H", 1.30 / 1.60, "MgCover"))
        for w, h, soll, slot in felder:
            ist = TEXTUREN[w] / float(TEXTUREN[h])
            self.assertLess(abs(ist - soll), 0.005,
                            "Textur fuer %s hat %.4f, Flaeche %.4f"
                            % (slot, ist, soll))


class SchwellenTest(unittest.TestCase):
    def test_toleranz_ist_fuenf_prozent(self):
        self.assertLessEqual(BILDER["TOLERANZ"], 0.05)

    def test_mindestanteil_verwirft_rautrauschen(self):
        # 244 Punkte im Cover waren Rauschen und lagen trotzdem 6 % daneben.
        self.assertGreaterEqual(BILDER["MINDEST_ANTEIL"], 0.0005)


if __name__ == "__main__":
    unittest.main()
