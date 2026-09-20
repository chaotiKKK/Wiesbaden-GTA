"""Selbsttest der Projektuebersicht (Tools/uebersicht.py).

Die Seite ersetzt eine von Hand gepflegte, die jahrelang falsch lag. Sie ist
nur dann besser, wenn sie MISST statt zu behaupten - geprueft wird darum
genau das: dass die Zahlen aus den Dateien kommen, dass jede ihre Quelle
nennt, und dass nichts unterwegs abgeschnitten wird.

Aufruf (aus der Projektwurzel):
    python -m unittest discover -s Tools -p "test_uebersicht.py"
"""
import datetime
import os
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import uebersicht  # noqa: E402


class LogAuswertungTest(unittest.TestCase):
    """Die Laufzeit-Zahlen kommen aus dem Spiel-Log."""

    def setUp(self):
        self.wurzel = Path(tempfile.mkdtemp(prefix="wb_uebersicht_"))
        self.addCleanup(shutil.rmtree, self.wurzel, ignore_errors=True)
        (self.wurzel / "Saved" / "Logs").mkdir(parents=True)
        self._alt = uebersicht.WURZEL
        uebersicht.WURZEL = self.wurzel
        self.addCleanup(lambda: setattr(uebersicht, "WURZEL", self._alt))

    def log_schreiben(self, *zeilen):
        (self.wurzel / "Saved" / "Logs" / "WiesbadenReal.log").write_text(
            "\n".join(zeilen) + "\n", encoding="utf-8")

    def test_punkte_im_wert_schneiden_die_zeile_nicht_ab(self):
        """Der Fehler, der die Seite eine falsche Zahl zeigen liess.

        "Spanne 20..180 s" enthaelt Punkte. Ein Muster, das bis zum ersten
        Punkt faengt, machte daraus "Spanne 20" - eine Zahl, die es so nicht
        gibt, und auf der Seite sah sie genauso verbindlich aus wie jede
        andere.
        """
        self.log_schreiben(
            "[...]LogWbTraffic: Signalprogramm: 936 von 1073 Kreuzungen mit eigener "
            "Abbiegephase, Umlauf 51 s im Mittel, Spanne 20..180 s, gruene Welle an.")
        _, treffer = uebersicht.aus_dem_log()
        werte = dict(treffer)
        self.assertIn("Signalprogramm", werte)
        self.assertIn("20..180 s", werte["Signalprogramm"])
        self.assertIn("gruene Welle an", werte["Signalprogramm"])

    def test_der_abschliessende_punkt_faellt_weg(self):
        self.log_schreiben("LogWbTraffic: Ampeln wirksam: 1073 im Netz, 5 Halte.")
        _, treffer = uebersicht.aus_dem_log()
        self.assertEqual(dict(treffer)["Ampeln"], "1073 im Netz")

    def test_die_letzte_meldung_gewinnt(self):
        """Ein Log enthaelt dieselbe Bilanz mehrfach - die juengste zaehlt."""
        self.log_schreiben(
            "LogWbCore: Ausstattungs-Spawner: 1 Schild.",
            "LogWbCore: Ausstattungs-Spawner: 52689 Schilder, 72434 Laternen.")
        _, treffer = uebersicht.aus_dem_log()
        self.assertIn("52689", dict(treffer)["Ausstattung"])

    def test_ohne_log_wird_nichts_behauptet(self):
        wann, treffer = uebersicht.aus_dem_log()
        self.assertIsNone(wann)
        self.assertEqual(treffer, [])

    def test_der_zeitpunkt_des_laufs_kommt_mit(self):
        """Ohne ihn waere die Seite genauso irrefuehrend wie die alte."""
        self.log_schreiben("LogWbTraffic: Ampeln wirksam: 7 im Netz.")
        wann, _ = uebersicht.aus_dem_log()
        self.assertIsInstance(wann, datetime.datetime)


class SeiteTest(unittest.TestCase):
    """Die erzeugte Seite - Form und Ehrlichkeit."""

    def messwerte(self, **abweichend):
        m = {
            "quelltext": [("GIS", 60, 32164), ("World", 64, 24807)],
            "tests": 247,
            "testgebiete": [("GIS", 84), ("Vehicles", 48)],
            "py_suiten": 10, "py_tests": 112,
            "live": "WiesbadenCity_Alkis16",
            "stadtkarten": ["WiesbadenCity_Alkis16"],
            "actorgroessen": {"WiesbadenCity_Alkis16": 2_000_000_000},
            "schild_png": 147, "schild_katalog": 110,
            "logzeit": datetime.datetime(2026, 9, 20, 21, 26),
            "logzeilen": [("Ampeln", "1073 im Netz")],
            "commits": "346", "zweig": "main",
            "letzter": "20.09.2026|abc1234|Etwas getan",
            "specs": ["a.md", "b.md"], "werkzeuge": 42,
        }
        m.update(abweichend)
        return m

    def test_jede_zahl_steht_mit_ihrer_quelle_da(self):
        """Eine erzeugte Seite ohne Quellenangabe ist nur eine Behauptung
        mit besserem Ruf - man muesste ihr glauben statt nachsehen."""
        seite = uebersicht.seite_bauen(self.messwerte())
        for quelle in ("Config/DefaultEngine.ini", "git ls-files",
                       "TrafficSignCatalog.json", "Saved/Logs/WiesbadenReal.log",
                       "IMPLEMENT_SIMPLE_AUTOMATION_TEST"):
            self.assertIn(quelle, seite, "Quelle fehlt: %s" % quelle)

    def test_der_zeitpunkt_des_spiellaufs_steht_auf_der_seite(self):
        seite = uebersicht.seite_bauen(self.messwerte())
        self.assertIn("20.09.2026 um 21:26", seite)
        self.assertIn("nicht der Gegenwart", seite)

    def test_ohne_log_sagt_sie_das_statt_zu_schweigen(self):
        seite = uebersicht.seite_bauen(self.messwerte(logzeilen=[], logzeit=None))
        self.assertIn("kein Spiel-Log", seite)

    def test_keine_platzhalter_bleiben_stehen(self):
        seite = uebersicht.seite_bauen(self.messwerte())
        self.assertNotIn("@@", seite)

    def test_die_seite_ist_eigenstaendig(self):
        seite = uebersicht.seite_bauen(self.messwerte())
        for fremd in ("http://", "https://", "<script"):
            self.assertNotIn(fremd, seite, "externe Quelle: %s" % fremd)

    def test_sie_behauptet_nicht_was_sie_nicht_messen_kann(self):
        """Der Kern des Umbaus: "Modul X ist fertig" ist keine Messung.

        Die alte Seite meldete "Phase 1 fertig" und "Phasen 2-12 offen",
        waehrend Verkehr, Wetter und Fahrzeuge laengst liefen.
        """
        seite = uebersicht.seite_bauen(self.messwerte())
        # NUR der Inhalt, nicht der Fusstext: der erklaert ausdruecklich,
        # warum hier nichts "fertig" heisst, und muss das Wort dafuer
        # benutzen duerfen. Eine erste Fassung dieses Tests verbot die
        # Zeichenkette ueberall und fiel an der eigenen Begruendung.
        inhalt = seite[seite.index("<header"):seite.index("<footer")]
        for behauptung in ("fertig", "Phase 1", "Phasen 2", "Noch offen",
                           "ssonn", "freebuff_city_wi"):
            self.assertNotIn(behauptung, inhalt,
                             "unbelegte Behauptung im Inhalt: %s" % behauptung)

    def test_die_gemessenen_werte_stehen_wirklich_drin(self):
        seite = uebersicht.seite_bauen(self.messwerte())
        self.assertIn("247", seite)
        self.assertIn("WiesbadenCity_Alkis16", seite)
        self.assertIn("32.164", seite)      # Zeilen mit Tausenderpunkt
        self.assertIn("1073 im Netz", seite)


class MessungTest(unittest.TestCase):
    """Die Messungen gegen das echte Repo - sie muessen plausibel sein."""

    def test_es_gibt_quelltext_und_tests(self):
        m = uebersicht.messen()
        self.assertGreater(m["tests"], 100, "das Projekt hat viele Tests")
        self.assertGreater(sum(n for _, n, _ in m["quelltext"]), 50)
        self.assertTrue(m["live"], "eine gespielte Karte muss ermittelbar sein")

    def test_die_testzahl_stimmt_mit_der_makrozahl_ueberein(self):
        """Gegenprobe ueber einen zweiten Weg: waeren die Zahlen geschaetzt,
        liefen sie auseinander."""
        gesamt, gebiete = uebersicht.automationstests()
        self.assertEqual(gesamt, sum(n for _, n in gebiete),
                         "jedes Makro traegt genau einen Testnamen")


if __name__ == "__main__":
    unittest.main()
