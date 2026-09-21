"""Selbsttest der Galerie (Tools/galerie.py).

Geprueft wird vor allem die AUSWAHL: welches Bild kommt in die Galerie und
welches nicht. Eine Galerie, die stillschweigend Kontaktboegen und
Stau-Karten zwischen die Stadtansichten haengt, zeigt nicht, wie die Stadt
aussieht.

Dazu zwei Waechter ueber die erzeugte Seite - beide Fehler haben beim Bauen
Zeit gekostet und waren im Browser erst als schwarze Flaeche sichtbar.

Aufruf (aus der Projektwurzel):
    python -m unittest discover -s Tools -p "test_galerie.py"
"""
import datetime
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import galerie  # noqa: E402

from PIL import Image  # noqa: E402


class Wegwerfordner(unittest.TestCase):
    def setUp(self):
        self.wurzel = Path(tempfile.mkdtemp(prefix="wb_galerie_"))
        self.addCleanup(shutil.rmtree, self.wurzel, ignore_errors=True)
        self._alt = (galerie.QUELLE, galerie.ZIEL)
        galerie.QUELLE = self.wurzel
        galerie.ZIEL = self.wurzel / "galerie"
        self.addCleanup(self._zurueck)

    def _zurueck(self):
        galerie.QUELLE, galerie.ZIEL = self._alt

    def bild(self, name, breite, hoehe, farbe=(60, 90, 140)):
        pfad = self.wurzel / name
        pfad.parent.mkdir(parents=True, exist_ok=True)
        Image.new("RGB", (breite, hoehe), farbe).save(pfad)
        return pfad


class AuswahlTest(Wegwerfordner):
    def test_ein_spielbild_kommt_hinein(self):
        p = self.bild("Stadt00042.png", 1920, 1080)
        ok, grund, groesse = galerie.beurteilen(p)
        self.assertTrue(ok, grund)
        self.assertEqual(groesse, (1920, 1080))

    def test_fensteraufnahme_mit_rahmen_auch(self):
        """1616x939 ist ein Fenster mit Rahmen - 1.72:1, immer noch ein Spiel."""
        ok, _, _ = galerie.beurteilen(self.bild("Stadt00043.png", 1616, 939))
        self.assertTrue(ok)

    def test_kontaktbogen_faellt_am_format_heraus(self):
        ok, grund, _ = galerie.beurteilen(self.bild("streifen.png", 4320, 574))
        self.assertFalse(ok)
        self.assertIn("Spielformat", grund)

    def test_hochformat_faellt_heraus(self):
        ok, grund, _ = galerie.beurteilen(self.bild("turm.png", 960, 2256))
        self.assertFalse(ok)

    def test_miniatur_ist_zu_klein(self):
        ok, grund, _ = galerie.beurteilen(self.bild("klein.png", 800, 450))
        self.assertFalse(ok)
        self.assertIn("zu klein", grund)

    def test_staukarte_faellt_am_namen_heraus(self):
        """Sie hat Spielformat, zeigt aber eine Karte statt der Stadt."""
        p = self.bild("staukarte.png", 2100, 1050)
        ok, grund, _ = galerie.beurteilen(p)
        self.assertFalse(ok)
        self.assertIn("Stau-Karte", grund)

    def test_ausschnitt_faellt_am_namen_heraus(self):
        ok, grund, _ = galerie.beurteilen(self.bild("zoom_bank_nah.png", 1920, 1080))
        self.assertFalse(ok)
        self.assertIn("Ausschnitt", grund)

    def test_ordner_zoomcrop_wird_uebersprungen(self):
        self.bild("Stadt00001.png", 1920, 1080)
        self.bild("zoomcrop/Stadt00002.png", 1920, 1080)
        namen = [p.name for p in galerie.kandidaten()]
        self.assertIn("Stadt00001.png", namen)
        self.assertNotIn("Stadt00002.png", namen)

    def test_die_galerie_nimmt_sich_nicht_selbst_auf(self):
        """Sonst waechst sie bei jedem Lauf um ihre eigenen Bilder."""
        self.bild("Stadt00001.png", 1920, 1080)
        self.bild("galerie/000_Stadt00001.jpg", 1600, 900)
        namen = [p.name for p in galerie.kandidaten()]
        self.assertEqual(namen, ["Stadt00001.png"])


class SeiteTest(Wegwerfordner):
    """Waechter ueber die erzeugte Seite - beide Fehler kosteten Zeit."""

    def eintrag(self, index, tag, name="Stadt"):
        return {"index": index, "web": "%03d.jpg" % index, "mini": "%03d_mini.jpg" % index,
                "titel": name, "tag": tag, "zeit": "12:00", "gross": "1920 x 1080"}

    def test_kein_doppeltes_prozentzeichen_im_javascript(self):
        """`%%` stammte aus der alten %-Formatierung und blieb beim Umstellen
        stehen. Im Browser war es ein SyntaxError - die Seite baute ihren
        Aufbau auf, zeigte aber nie ein Bild, die Buehne blieb schwarz."""
        seite = galerie.seite_bauen([self.eintrag(0, "20.09.2026")])
        js = seite[seite.index("<script>"):]
        self.assertNotIn("%%", js)
        self.assertIn("% BILDER.length", js)

    def test_keine_platzhalter_bleiben_stehen(self):
        seite = galerie.seite_bauen([self.eintrag(0, "20.09.2026")])
        self.assertNotIn("@@", seite)

    def test_nach_echtem_datum_sortiert_nicht_als_zeichenkette(self):
        """"01.10.2026" ist juenger als "20.09.2026", steht als Zeichenkette
        aber davor. Solange alle Bilder aus einem Monat stammen, faellt das
        nie auf."""
        seite = galerie.seite_bauen([
            self.eintrag(0, "01.10.2026", "neu"),
            self.eintrag(1, "20.09.2026", "alt"),
        ])
        # NUR im Miniaturenstreifen suchen: die Kopfzeile nennt die Spanne als
        # "aelter bis neuer" und enthaelt das alte Datum zu Recht zuerst.
        # Eine erste Fassung dieses Tests durchsuchte die ganze Seite und fiel
        # darum, obwohl die Sortierung stimmte.
        streifen = seite[seite.index('class="tag"'):seite.index("<footer")]
        self.assertLess(streifen.index("01.10.2026"), streifen.index("20.09.2026"),
                        "der neuere Tag gehoert nach oben")

    def test_die_seite_ist_eigenstaendig(self):
        """Kein CDN, kein externes Skript - sonst braucht sie Netz."""
        seite = galerie.seite_bauen([self.eintrag(0, "20.09.2026")])
        for fremd in ("http://", "https://", "//cdn", "<script src"):
            self.assertNotIn(fremd, seite, "externe Quelle: %s" % fremd)

    def test_ohne_bilder_bricht_sie_nicht(self):
        seite = galerie.seite_bauen([])
        self.assertIn("<html", seite)
        self.assertNotIn("@@", seite)


class BauenTest(Wegwerfordner):
    def test_ein_voller_lauf_schreibt_seite_und_bilder(self):
        for i in range(3):
            self.bild("Stadt0000%d.png" % i, 1920, 1080)
        self.bild("staukarte.png", 2100, 1050)      # muss draussen bleiben
        code = galerie.bauen()
        self.assertEqual(code, 0)

        seite = galerie.ZIEL / "index.html"
        self.assertTrue(seite.exists())
        text = seite.read_text(encoding="utf-8")
        self.assertIn("3 Spielbilder", text)
        self.assertNotIn("staukarte", text)

        # Je Bild eine Web- und eine Mini-Fassung, und beide sind kleiner.
        jpgs = sorted(p.name for p in galerie.ZIEL.glob("*.jpg"))
        self.assertEqual(len(jpgs), 6)
        with Image.open(galerie.ZIEL / jpgs[0]) as im:
            self.assertLessEqual(im.width, galerie.WEB_BREITE)

    def test_die_originale_bleiben_unberuehrt(self):
        p = self.bild("Stadt00000.png", 1920, 1080)
        vorher = p.read_bytes()
        galerie.bauen()
        self.assertEqual(p.read_bytes(), vorher)


if __name__ == "__main__":
    unittest.main()
