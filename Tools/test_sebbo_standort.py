r"""Selbsttest der abgeleiteten Turmkoordinate (Tools/sebbo_standort.py).

Die entscheidende Frage ist nicht "rechnet das Modul etwas aus", sondern
"rechnet es DASSELBE wie die Engine". Darum steht hier ein GEMESSENER Wert
aus dem Spiel-Log als Anker, nicht ein zweites Mal dieselbe Formel.

    python -m unittest discover -s Tools -p "test_sebbo_standort.py"
"""
import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sebbo_standort as st  # noqa: E402

# GEMESSEN am 25.09.2026 auf Karte Alkis23, aus
# Saved/Logs/tower_platter_alkis23.log:
#   LogWbSebboHq: Sebbo-Hauptsitz gebaut bei (-113514, -125729, 10953)
# Das ist die Stelle, an der die Engine den Turm WIRKLICH gebaut hat. Ein
# Anker aus derselben Formel waere wertlos - er wuerde nur bestaetigen, dass
# die Formel sich selbst gleicht.
#
# Der neue Standort rueckt die beiden Eingangsanker zur Platter Strasse;
# die alte Position (-110714, -127729) hatte Zugang zur Wolkenbruch.
ENGINE_X_CM = -113514.0
ENGINE_Y_CM = -125729.0


class TrifftDieEngineTest(unittest.TestCase):
    """Die Ableitung muss die gemessene Bauposition treffen."""

    def test_stimmt_mit_der_gebauten_position_ueberein(self):
        x, y = st.standort_cm()
        # 1 m Toleranz: die Logzeile rundet auf ganze Zentimeter, und der
        # -WbGoto-Zielpunkt muss nur die richtige Streaming-Zelle treffen.
        self.assertAlmostEqual(x, ENGINE_X_CM, delta=100.0)
        self.assertAlmostEqual(y, ENGINE_Y_CM, delta=100.0)

    def test_die_ausgabe_passt_in_WbGoto(self):
        """-WbGoto erwartet genau 'X,Y' - ohne Leerzeichen, ohne Klammern."""
        import io
        import contextlib
        puffer = io.StringIO()
        with contextlib.redirect_stdout(puffer):
            st.hauptprogramm([])
        text = puffer.getvalue().strip()
        self.assertRegex(text, r"^-?\d+,-?\d+$")
        x, y = (float(t) for t in text.split(","))
        self.assertAlmostEqual(x, ENGINE_X_CM, delta=100.0)
        self.assertAlmostEqual(y, ENGINE_Y_CM, delta=100.0)


class LiestWirklichDieHeaderTest(unittest.TestCase):
    """Abgeleitet heisst: der Header entscheidet, nicht eine Kopie im Modul.

    Ein Modul, das die richtige Zahl zufaellig eingetragen hat, besteht den
    Engine-Vergleich oben genauso. Geprueft wird darum, dass eine ANDERE
    Breite auch ein anderes Ergebnis liefert - sonst waere die Ableitung
    Kulisse.
    """

    def schreibe_site(self, lat, lon):
        pfad = Path(tempfile.mkdtemp(prefix="wb_site_")) / "SebboHqSite.h"
        pfad.write_text(
            "namespace SebboHqSite\n{\n"
            "\tinline constexpr double Latitude = %.9f;\n"
            "\tinline constexpr double Longitude = %.9f;\n"
            "\tinline constexpr double HeadingDegrees = 250.0;\n}\n" % (lat, lon),
            encoding="utf-8")
        self.addCleanup(lambda: pfad.unlink(missing_ok=True))
        return str(pfad)

    def test_ein_verschobener_standort_verschiebt_das_ergebnis(self):
        echt = st.standort_cm()
        # 0,01 Grad noerdlich sind rund 1,1 km - das muss sich zeigen.
        verschoben = st.standort_cm(
            site_h=self.schreibe_site(50.103882, 8.224528))
        self.assertAlmostEqual(verschoben[1] - echt[1], -111000.0, delta=3000.0,
                               msg="der Standort wird nicht aus dem Header gelesen")

    def test_dieselben_werte_ergeben_dieselbe_stelle(self):
        echt = st.standort_cm()
        kopie = st.standort_cm(site_h=self.schreibe_site(50.093702144, 8.224136765))
        self.assertAlmostEqual(kopie[0], echt[0], delta=1.0)
        self.assertAlmostEqual(kopie[1], echt[1], delta=1.0)

    def test_fehlende_konstante_bricht_ab_statt_zu_raten(self):
        pfad = Path(tempfile.mkdtemp(prefix="wb_leer_")) / "SebboHqSite.h"
        pfad.write_text("namespace SebboHqSite {}\n", encoding="utf-8")
        self.addCleanup(lambda: pfad.unlink(missing_ok=True))
        with self.assertRaises(SystemExit):
            st.standort_cm(site_h=str(pfad))


class KeineZweitwahrheitTest(unittest.TestCase):
    """Im Modul darf keine der abgeleiteten Zahlen fest stehen."""

    def test_modul_traegt_weder_standort_noch_ursprung(self):
        text = Path(st.__file__).read_text(encoding="utf-8")
        # Der Rechenteil - ohne Kopf- und Kommentarzeilen, die die Zahlen
        # erklaeren duerfen.
        code = "\n".join(z for z in text.splitlines()
                         if not z.lstrip().startswith("#"))
        for zahl in ("50.093882", "8.224528", "50.0824", "8.2400", "6378137"):
            self.assertNotIn(zahl, code,
                             "%s steht fest im Modul statt gelesen zu werden" % zahl)


class ProbenSkriptTest(unittest.TestCase):
    """run_ankunft_probe.cmd muss Karte und Koordinate beziehen, nicht kennen."""

    def setUp(self):
        self.cmd = (Path(st.__file__).resolve().parent / "run_ankunft_probe.cmd")
        self.text = self.cmd.read_text(encoding="utf-8", errors="replace")

    def test_die_koordinate_steht_nicht_mehr_fest(self):
        self.assertNotIn("-110983", self.text)
        self.assertNotIn("-128483", self.text)
        self.assertNotIn("-110985", self.text)

    def test_die_koordinate_kommt_aus_dem_ableiter(self):
        self.assertIn("sebbo_standort.py", self.text)

    def test_die_karte_ist_ein_parameter(self):
        """Die STARTZEILE muss die Karte einsetzen, nicht irgendeine Zeile.

        Erste Fassung prueft nur, ob "%KARTE%" irgendwo vorkommt - das tut es
        auch, wenn die Variable oben gesetzt und unten eine feste Karte
        gestartet wird. Genau so lief die Gegenprobe gruen durch.
        """
        start = [z for z in self.text.splitlines() if "UnrealEditor.exe" in z]
        self.assertEqual(len(start), 1, "genau eine Startzeile erwartet")
        self.assertIn("/Game/Maps/%KARTE%", start[0])
        for karte in ("Alkis16", "Alkis17"):
            self.assertNotIn(karte, start[0],
                             "die Startzeile traegt eine feste Karte")

    def test_der_zielpunkt_steht_nicht_in_der_startzeile(self):
        start = [z for z in self.text.splitlines() if "UnrealEditor.exe" in z][0]
        self.assertIn("-WbGoto=%ZIEL%", start)


if __name__ == "__main__":
    unittest.main()
