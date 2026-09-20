"""Selbsttest des Karten-Aufraeumens (Tools/karten_aufraeumen.py).

Geprueft werden vor allem die SICHERUNGEN - beim Loeschen ist der Schaden
endgueltig, und ein Werkzeug, das im Zweifel loescht, ist schlimmer als
Handarbeit.

Jeder Test baut einen Wegwerf-Baum mit echten Dateien und Ordnern; die
Modul-Pfade werden dorthin umgebogen. Gegen Attrappen waere nichts bewiesen -
die Fragen lauten "loescht es wirklich" und "laesst es wirklich stehen".

Aufruf (aus der Projektwurzel):
    python -m unittest discover -s Tools -p "test_karten_aufraeumen.py"
"""
import json
import os
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import karten_aufraeumen as ka  # noqa: E402

VOLL = int(1.9 * 1024 ** 3)
LEER = int(1.4 * 1024 ** 3)


class Wegwerfbaum(unittest.TestCase):
    """Ein Projektbaum mit Karten, externen Actors und einer Live-Karte."""

    def setUp(self):
        self.wurzel = Path(tempfile.mkdtemp(prefix="wb_karten_"))
        self.addCleanup(shutil.rmtree, self.wurzel, ignore_errors=True)
        (self.wurzel / "Content" / "Maps").mkdir(parents=True)
        (self.wurzel / "Content" / "__ExternalActors__" / "Maps").mkdir(parents=True)
        (self.wurzel / "Content" / "__ExternalObjects__" / "Maps").mkdir(parents=True)
        (self.wurzel / "Saved" / "Diagnose").mkdir(parents=True)

        # Modul auf den Wegwerfbaum umbiegen.
        self._alt = (ka.WURZEL, ka.KARTEN, ka.EXTERNE, ka.VORSCHLAG)
        ka.WURZEL = self.wurzel
        ka.KARTEN = self.wurzel / "Content" / "Maps"
        ka.EXTERNE = (self.wurzel / "Content" / "__ExternalActors__" / "Maps",
                      self.wurzel / "Content" / "__ExternalObjects__" / "Maps")
        ka.VORSCHLAG = self.wurzel / "Saved" / "Diagnose" / "bake_vorschlag.json"
        self.addCleanup(self._zuruecksetzen)

        # Live-Karte festlegen, ohne eine INI zu bauen.
        self._live = "WiesbadenCity_Alkis16"
        self._alte_standard = ka.standard_karte
        ka.standard_karte = lambda *a, **k: self._live
        self.addCleanup(lambda: setattr(ka, "standard_karte", self._alte_standard))

    def _zuruecksetzen(self):
        ka.WURZEL, ka.KARTEN, ka.EXTERNE, ka.VORSCHLAG = self._alt

    # -- Bausteine ---------------------------------------------------------

    def karte_anlegen(self, name, externe_bytes=0):
        (ka.KARTEN / (name + ".umap")).write_bytes(b"x" * 100)
        if externe_bytes:
            ordner = ka.EXTERNE[0] / name
            ordner.mkdir(parents=True, exist_ok=True)
            # Nicht wirklich 1,9 GB schreiben - die Groesse wird gemeldet.
            (ordner / "actors.bin").write_bytes(b"y" * 100)
            self._groessen = getattr(self, "_groessen", {})
            self._groessen[str(ordner)] = externe_bytes

    def groessen_vortaeuschen(self):
        """`groesse` liefert fuer angelegte Actor-Ordner den gewuenschten Wert."""
        echt = ka.groesse
        tabelle = getattr(self, "_groessen", {})

        def gemogelt(pfad):
            if str(pfad) in tabelle:
                return tabelle[str(pfad)]
            return echt(pfad)

        ka.groesse = gemogelt
        self.addCleanup(lambda: setattr(ka, "groesse", echt))

    def vorschlag_schreiben(self, neu, vorgaenger):
        ka.VORSCHLAG.write_text(json.dumps(
            {"neu": "/Game/Maps/" + neu, "vorgaenger": "/Game/Maps/" + vorgaenger}),
            encoding="utf-8")

    def namen_die_wegkommen(self, weg):
        return sorted(e[0] for e in weg)


class PlanTest(Wegwerfbaum):
    def test_neue_und_vorgaengerin_bleiben_der_rest_geht(self):
        for n in ("WiesbadenCity_Alkis15", "WiesbadenCity_Alkis16",
                  "WiesbadenCity_Alkis17"):
            self.karte_anlegen(n, VOLL)
        self.karte_anlegen("__StadtNeubau_3")
        self.vorschlag_schreiben("WiesbadenCity_Alkis17", "WiesbadenCity_Alkis16")

        behalten, weg, _ = ka.plan()
        self.assertEqual(behalten,
                         ["WiesbadenCity_Alkis16", "WiesbadenCity_Alkis17"])
        self.assertEqual(self.namen_die_wegkommen(weg),
                         ["WiesbadenCity_Alkis15", "__StadtNeubau_3"])

    def test_die_gespielte_karte_bleibt_auch_wenn_sie_keine_von_beiden_ist(self):
        """Sicherung 2: GameDefaultMap ist unantastbar.

        Wird eine Karte gebacken und die Vorgaengerin ausdruecklich anders
        gewaehlt, darf die gespielte Karte trotzdem nicht verschwinden -
        sonst startet das Spiel nach dem Aufraeumen gar nicht mehr.
        """
        for n in ("WiesbadenCity_Alkis15", "WiesbadenCity_Alkis16",
                  "WiesbadenCity_Alkis17", "WiesbadenCity_Alkis18"):
            self.karte_anlegen(n, VOLL)
        behalten, weg, _ = ka.plan(neu="WiesbadenCity_Alkis18",
                                   vorgaenger="WiesbadenCity_Alkis17")
        self.assertIn("WiesbadenCity_Alkis16", behalten, "die gespielte Karte")
        self.assertNotIn("WiesbadenCity_Alkis16", self.namen_die_wegkommen(weg))

    def test_externe_actors_gehen_mit(self):
        """Ohne sie waere das Aufraeumen sinnlos - sie sind 1,9 GB je Karte."""
        self.karte_anlegen("WiesbadenCity_Alkis16", VOLL)
        self.karte_anlegen("WiesbadenCity_Alkis17", VOLL)
        self.karte_anlegen("WiesbadenCity_Alkis15", VOLL)
        self.vorschlag_schreiben("WiesbadenCity_Alkis17", "WiesbadenCity_Alkis16")

        _, weg, _ = ka.plan()
        teile = [p for e in weg for p in e[3]]
        self.assertTrue(any(p.is_dir() for p in teile),
                        "der Actor-Ordner muss im Plan stehen")
        self.assertTrue(any(p.suffix == ".umap" for p in teile))

    def test_ohne_vorschlag_wird_die_juengste_andere_karte_geschont(self):
        self.karte_anlegen("WiesbadenCity_Alkis16", VOLL)
        self.karte_anlegen("WiesbadenCity_Alkis15", VOLL)
        self.karte_anlegen("WiesbadenCity_Alkis17", VOLL)
        # Alkis17 juenger machen als Alkis15.
        os.utime(ka.KARTEN / "WiesbadenCity_Alkis17.umap", (2_000_000_000, 2_000_000_000))
        os.utime(ka.KARTEN / "WiesbadenCity_Alkis15.umap", (1_000_000_000, 1_000_000_000))
        behalten, weg, hinweise = ka.plan()
        self.assertIn("WiesbadenCity_Alkis17", behalten)
        self.assertEqual(self.namen_die_wegkommen(weg), ["WiesbadenCity_Alkis15"])
        self.assertTrue(any("juengste" in h for h in hinweise))


class LeerbakeTest(Wegwerfbaum):
    """Sicherung 3: "FERTIG" ist kein Beleg (Alkis10/11 waren nur Gras)."""

    def test_zu_leichte_neue_karte_gilt_als_nicht_abgenommen(self):
        self.karte_anlegen("WiesbadenCity_Alkis18", LEER)
        self.groessen_vortaeuschen()
        ok, bytes_, grund = ka.neue_karte_ist_voll("WiesbadenCity_Alkis18")
        self.assertFalse(ok)
        self.assertIn("Leerbake", grund)
        self.assertGreater(bytes_, 0)

    def test_volle_karte_wird_angenommen(self):
        self.karte_anlegen("WiesbadenCity_Alkis18", VOLL)
        self.groessen_vortaeuschen()
        ok, _, _ = ka.neue_karte_ist_voll("WiesbadenCity_Alkis18")
        self.assertTrue(ok)

    def test_ganz_ohne_externe_actors_wird_nicht_geloescht(self):
        self.karte_anlegen("WiesbadenCity_Alkis18")      # nur die .umap
        ok, _, grund = ka.neue_karte_ist_voll("WiesbadenCity_Alkis18")
        self.assertFalse(ok)
        self.assertIn("keine externen Actors", grund)

    def test_leerbake_blockiert_den_ganzen_lauf(self):
        self.karte_anlegen("WiesbadenCity_Alkis16", VOLL)
        self.karte_anlegen("WiesbadenCity_Alkis17", VOLL)
        self.karte_anlegen("WiesbadenCity_Alkis18", LEER)
        self.karte_anlegen("WiesbadenCity_Alkis15", VOLL)
        self.vorschlag_schreiben("WiesbadenCity_Alkis18", "WiesbadenCity_Alkis17")
        self.groessen_vortaeuschen()

        code = ka.hauptprogramm(["--loeschen", "--ja"])
        self.assertEqual(code, 2, "muss abbrechen")
        self.assertTrue((ka.KARTEN / "WiesbadenCity_Alkis15.umap").exists(),
                        "und dabei NICHTS geloescht haben")


class RueckfrageTest(Wegwerfbaum):
    """Sicherung 1: ohne Terminal wird nicht geloescht."""

    def setUp(self):
        super().setUp()
        self.karte_anlegen("WiesbadenCity_Alkis16", VOLL)
        self.karte_anlegen("WiesbadenCity_Alkis17", VOLL)
        self.karte_anlegen("WiesbadenCity_Alkis15", VOLL)
        self.karte_anlegen("__StadtNeubau_3")
        self.vorschlag_schreiben("WiesbadenCity_Alkis17", "WiesbadenCity_Alkis16")
        self.groessen_vortaeuschen()

    def test_ohne_terminal_wird_nichts_geloescht(self):
        """Der Fall eines abgesetzten Bakes mit Protokoll-Umleitung.

        Vorausgesetzt werden darf das nicht: in dieser Testumgebung IST stdin
        ein Terminal (erste Fassung dieses Tests ist daran gescheitert). Der
        Fall wird darum hergestellt.
        """
        self._kein_terminal()
        # Und die Rueckfrage darf gar nicht erst kommen.
        self._antwort_verbieten()
        code = ka.hauptprogramm(["--loeschen"])
        self.assertEqual(code, 2)
        self.assertTrue((ka.KARTEN / "WiesbadenCity_Alkis15.umap").exists())

    def test_ohne_terminal_loescht_ja_trotzdem(self):
        """--ja ist der Weg fuer Skripte - sonst waere das Werkzeug dort nutzlos."""
        self._kein_terminal()
        self._antwort_verbieten()
        code = ka.hauptprogramm(["--loeschen", "--ja"])
        self.assertEqual(code, 0)
        self.assertFalse((ka.KARTEN / "WiesbadenCity_Alkis15.umap").exists())

    def test_nein_an_der_rueckfrage_loescht_nichts(self):
        self._antwort("nein")
        code = ka.hauptprogramm(["--loeschen"])
        self.assertEqual(code, 1)
        self.assertTrue((ka.KARTEN / "WiesbadenCity_Alkis15.umap").exists())

    def test_alles_ausser_ja_loescht_nichts(self):
        """Ein Druck auf die Eingabetaste darf 1,9 GB nicht loeschen."""
        for antwort in ("", "j", "y", "yes", "JAWOHL", "  "):
            with self.subTest(antwort=antwort):
                self._antwort(antwort)
                code = ka.hauptprogramm(["--loeschen"])
                self.assertEqual(code, 1)
                self.assertTrue((ka.KARTEN / "WiesbadenCity_Alkis15.umap").exists())

    def test_ja_loescht_wirklich(self):
        self._antwort("ja")
        code = ka.hauptprogramm(["--loeschen"])
        self.assertEqual(code, 0)
        self.assertFalse((ka.KARTEN / "WiesbadenCity_Alkis15.umap").exists())
        self.assertFalse((ka.EXTERNE[0] / "WiesbadenCity_Alkis15").exists(),
                         "auch die externen Actors")
        self.assertFalse((ka.KARTEN / "__StadtNeubau_3.umap").exists())
        # ... und die beiden behaltenen stehen unangetastet da.
        self.assertTrue((ka.KARTEN / "WiesbadenCity_Alkis16.umap").exists())
        self.assertTrue((ka.KARTEN / "WiesbadenCity_Alkis17.umap").exists())
        self.assertTrue((ka.EXTERNE[0] / "WiesbadenCity_Alkis17").exists())

    def test_ohne_loeschen_passiert_nichts(self):
        self._terminal(True)
        code = ka.hauptprogramm([])
        self.assertEqual(code, 0)
        self.assertTrue((ka.KARTEN / "WiesbadenCity_Alkis15.umap").exists())

    def _antwort(self, text):
        """Antwort vorgeben - und dafuer sorgen, dass ueberhaupt gefragt wird."""
        self._terminal(True)
        import builtins
        echt = builtins.input
        builtins.input = lambda *a: text
        self.addCleanup(lambda: setattr(builtins, "input", echt))

    def _terminal(self, vorhanden):
        """stdin als Terminal vortaeuschen - oder als keines.

        WARUM HERSTELLEN STATT VORFINDEN: ob stdin ein Terminal ist, haengt
        davon ab, WIE der Testlauf gestartet wurde. Stand in derselben
        Aufrufkette vorher ein Here-Dokument, ist stdin verbraucht und
        `isatty()` liefert False - sonst True. Eine erste Fassung dieser
        Tests hing daran und war mal gruen, mal rot, ohne dass sich eine
        Zeile geaendert haette.
        """
        class Stdin:
            @staticmethod
            def isatty():
                return vorhanden
        echt = sys.stdin
        sys.stdin = Stdin()
        self.addCleanup(lambda: setattr(sys, "stdin", echt))

    def _kein_terminal(self):
        self._terminal(False)

    def _antwort_verbieten(self):
        """Jede Rueckfrage ist hier ein Fehler - ohne Terminal darf keine kommen."""
        import builtins
        echt = builtins.input
        def platzt(*a):
            raise AssertionError("Es darf ohne Terminal nicht gefragt werden")
        builtins.input = platzt
        self.addCleanup(lambda: setattr(builtins, "input", echt))


if __name__ == "__main__":
    unittest.main()
