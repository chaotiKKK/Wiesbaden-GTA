r"""Selbsttest des Plattenwaechters (Tools/platten_waechter.py).

Ein Aufrumwerkzeug wird seltener gelesen als benutzt - und sein Schaden
entsteht genau dort, wo niemand hinsieht. Geprueft wird darum nicht "das Skript
laeuft", sondern die drei Zusagen, auf die man sich verlassen muss:

1. Es meldet, ohne im gesunden Fall etwas zu kosten.
2. Es unterscheidet ALT von UNBENUTZT. Genau hier ist am 27.09.2026 ein
   627-MB-Ordner fast geflogen (`Saved/_aaa_source` = Eingangsdaten fuer
   `aaa_import_materials.py`). Das ist der Grund fuer die Klassen und der
   Grund fuer diesen Test.
3. Es loescht nur, was es als loeschbar ausgewiesen hat - und laesst sich
   auch dann nicht austricksen, wenn die Pfadangabe stimmt, die Klasse aber
   nicht.

    python -m unittest discover -s Tools -p "test_platten_waechter.py"
"""
import io
import json
import os
import sys
import tempfile
import time
import unittest
from contextlib import contextmanager, redirect_stdout
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import platten_waechter as pw  # noqa: E402

WURZEL = Path(__file__).resolve().parent.parent


@contextmanager
def tempfile_tmp():
    """Eigenes Tempverzeichnis - die Suite laeuft im Commit-Worktree neben
    dem echten Projekt und darf dort nichts anruehren."""
    with tempfile.TemporaryDirectory() as d:
        yield d


def tree(root, dateien):
    """Kleiner Baum mit festen Bytegroessen, ohne echte Dateien zu schreiben."""
    for name, groesse in dateien.items():
        p = os.path.join(root, name)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, "wb") as f:
            f.truncate(groesse)
    return root


class SchwelleTest(unittest.TestCase):
    def test_grenze_ist_strikte_untergrenze(self):
        self.assertTrue(pw.unter_schwelle(19.99, 20.0))
        self.assertFalse(pw.unter_schwelle(20.0, 20.0))
        self.assertFalse(pw.unter_schwelle(35.8, 20.0))

    def test_grenze_aus_der_konfiguration(self):
        self.assertEqual(pw.GRENZE_PROZENT, 20.0)

    def test_platz_liefert_prozent(self):
        with mock.patch.object(pw.shutil, "disk_usage") as du:
            du.return_value = mock.Mock(free=25, total=100)
            frei, gesamt, prozent = pw.platz("C:/x")
        self.assertEqual((frei, gesamt), (25, 100))
        self.assertAlmostEqual(prozent, 25.0)

    def test_gesunder_zustand_kostet_nichts(self):
        """Der Hook ruft `warnung`; im gesunden Fall darf NICHTS gemessen werden."""
        with mock.patch.object(pw, "platz", return_value=(300, 1000, 30.0)), \
             mock.patch.object(pw, "messen",
                               side_effect=AssertionError("darf nicht messen")) as m:
            self.assertIsNone(pw.warnung())
        m.assert_not_called()


class MessungTest(unittest.TestCase):
    def test_summe_und_dateizahl(self):
        with tempfile_tmp() as t:
            tree(t, {"a.bin": 100, "unter/b.bin": 250, "unter/tief/c.bin": 50})
            bytes_, dateien, vollstaendig = pw.groesse_messen(t)
        self.assertEqual(bytes_, 400)
        self.assertEqual(dateien, 3)
        self.assertTrue(vollstaendig)

    def test_leerer_ordner_ist_kein_fehler(self):
        with tempfile_tmp() as t:
            self.assertEqual(pw.groesse_messen(t), (0, 0, True))

    def test_fehlender_pfad_meldet_unvollstaendig(self):
        bytes_, dateien, vollstaendig = pw.groesse_messen(
            os.path.join("C:", "gibt", "es", "nicht", "wirklich"))
        self.assertEqual((bytes_, dateien), (0, 0))
        self.assertFalse(vollstaendig)

    def test_datei_statt_ordner(self):
        r"""`C:\pagefile.sys` ist eine Datei - scandir darauf wuerfe schiefgehen."""
        with tempfile_tmp() as t:
            p = os.path.join(t, "hiberfil.sys")
            with open(p, "wb") as f:
                f.truncate(1234)
            bytes_, dateien, vollstaendig = pw.groesse_messen(p)
        self.assertEqual((bytes_, dateien, vollstaendig), (1234, 1, True))

    def test_budget_bricht_ab_und_sagt_es(self):
        with tempfile_tmp() as t:
            tree(t, {"a.bin": 10, "b.bin": 10, "c.bin": 10})
            _, _, vollstaendig = pw.groesse_messen(t, deadline=time.monotonic() - 1)
        self.assertFalse(vollstaendig)

    def test_eine_deadline_fuer_alle_kandidaten(self):
        """Budget darf sich nicht je Kandidat aufaddieren."""
        kand = [("/a", pw.LOESCHBAR, ""), ("/b", pw.LOESCHBAR, "")]
        with mock.patch.object(pw, "groesse_messen", return_value=(0, 0, True)) as g:
            pw.messen(kand, budget=1.0)
        # Deadline ist das 2. Positionalargument - bei allen dieselbe Frist
        self.assertTrue(all(c.args[1] is not None for c in g.call_args_list))
        self.assertEqual(len({c.args[1] for c in g.call_args_list}), 1)


class KlassenTest(unittest.TestCase):
    """Die Klassen sind das Sicherheitsnetz - sie werden hier festgenagelt."""

    def test_alle_kandidaten_haben_klasse_und_grund(self):
        for pfad, klasse, grund in pw.KANDIDATEN:
            self.assertIn(klasse, (pw.LOESCHBAR, pw.REGENERIERBAR,
                                   pw.EINGABE, pw.GESCHUETZT), pfad)
            self.assertTrue(grund.strip(), "ohne Grund: %s" % pfad)

    def test_aaa_source_ist_eingabe_nie_cache(self):
        """DER Regressionstest: der Ordner, der am 27.09.2026 fast geflogen waere."""
        eintraege = [k for k in pw.KANDIDATEN if k[0].endswith("_aaa_source")]
        self.assertEqual(len(eintraege), 1, "Saved\\_aaa_source muss in der Liste stehen")
        _, klasse, grund = eintraege[0]
        self.assertEqual(klasse, pw.EINGABE)
        self.assertNotIn(klasse, pw.NUR_LOESCHEN)
        self.assertIn("aaa_import_materials.py", grund)

    def test_gebackene_chunks_und_rohdaten_sind_nie_loeschbar(self):
        verboten = ("__ExternalActors__", r"Data\Raw", r"Saved\Diagnose",
                    r"Saved\Package", "wt-gate5")
        for verboten_pfad in verboten:
            treffer = [k for k in pw.KANDIDATEN if verboten_pfad in k[0]]
            self.assertTrue(treffer, "%s fehlt in der Liste" % verboten_pfad)
            for _, klasse, _ in treffer:
                self.assertNotIn(klasse, pw.NUR_LOESCHEN, verboten_pfad)

    def test_temp_ist_nicht_automatisch_loeschbar(self):
        """%TEMP% enthaelt Arbeitsdaten laufender Werkzeuge - nie als Ganzes."""
        treffer = [k for k in pw.KANDIDATEN if k[0].endswith(r"AppData\Local\Temp")]
        self.assertEqual(treffer, [], "%TEMP% gehoert nicht in die Pfadliste")

    def test_loeschklasse_umfasst_nur_die_ausgewiesene(self):
        self.assertEqual(pw.NUR_LOESCHEN, (pw.LOESCHBAR,))

    def test_keine_doppelten_pfade(self):
        pfade = [os.path.normcase(pw._muster_aufloesen(p, "C:\\w", "C:\\h"))
                 for p, _, _ in pw.KANDIDATEN]
        self.assertEqual(len(pfade), len(set(pfade)))

    def test_platzhalter_werden_ersetzt(self):
        aufgeloest = pw._muster_aufloesen("{w}\\a\\{home}\\b", "C:\\w", "C:\\h")
        self.assertEqual(aufgeloest, "C:\\w\\a\\C:\\h\\b")


class ReinigenTest(unittest.TestCase):
    def messung(self, pfad, klasse):
        return {"pfad": pfad, "klasse": klasse, "grund": "", "bytes": 10,
                "dateien": 1, "vollstaendig": True}

    def test_nur_cache_wird_geloescht(self):
        with tempfile_tmp() as t, mock.patch.object(pw, "erlaubte_wurzeln",
                                                    return_value=(os.path.normpath(t).lower(),)):
            ziel = os.path.join(t, "weg")
            os.makedirs(ziel)
            with open(os.path.join(ziel, "x"), "wb") as f:
                f.truncate(5)
            behalten = os.path.join(t, "bleibt")
            os.makedirs(behalten)
            geloescht, abgewiesen = pw.reinigen([
                self.messung(ziel, pw.LOESCHBAR),
                self.messung(behalten, pw.EINGABE),
                self.messung(behalten, pw.GESCHUETZT),
                self.messung(behalten, pw.REGENERIERBAR),
            ])
            # Innerhalb des with-Blocks pruefen: danach ist das Tempverzeichnis
            # weg, und die Assertionen wuerden aus dem richtigen Grund bestehen.
            self.assertEqual(geloescht, [ziel])
            self.assertEqual(abgewiesen, [])
            self.assertFalse(os.path.exists(ziel), "cache-Ordner muss weg sein")
            self.assertTrue(os.path.exists(behalten),
                            "eingabe/geschuetzt/ausgabe duerfen nicht angefasst werden")

    def test_fremder_pfad_wird_abgewiesen(self):
        """Richtig klassifiziert, aber ausserhalb - trotzdem nein."""
        with mock.patch.object(pw, "erlaubte_wurzeln", return_value=(r"c:\projekt",)):
            geloescht, abgewiesen = pw.reinigen(
                [self.messung(r"C:\Users\HP\.bun\install", pw.LOESCHBAR)])
        self.assertEqual(geloescht, [])
        self.assertEqual(len(abgewiesen), 1)

    def test_trockenlauf_loescht_nichts(self):
        with tempfile_tmp() as t, mock.patch.object(pw, "erlaubte_wurzeln",
                                                    return_value=(os.path.normpath(t).lower(),)):
            ziel = os.path.join(t, "weg")
            os.makedirs(ziel)
            geloescht, _ = pw.reinigen([self.messung(ziel, pw.LOESCHBAR)], trocken=True)
            self.assertEqual(geloescht, [ziel])
            self.assertTrue(os.path.exists(ziel))

    def test_fehlende_ordner_werden_uebersprungen(self):
        """Fehlt der Ordner, ist das kein Fehler - aber auch kein Loeschfall."""
        with mock.patch.object(pw, "erlaubte_wurzeln", return_value=(r"c:\projekt",)):
            geloescht, abgewiesen = pw.reinigen(
                [self.messung(r"c:\projekt\gibt\es\nicht", pw.LOESCHBAR)])
        self.assertEqual(geloescht, [])
        self.assertEqual(abgewiesen, [])


class BerichtTest(unittest.TestCase):
    MESSUNGEN = [
        {"pfad": r"C:\cache", "klasse": pw.LOESCHBAR, "grund": "Cache",
         "bytes": 10 * 1024 ** 3, "dateien": 5, "vollstaendig": True},
        {"pfad": r"C:\halb", "klasse": pw.LOESCHBAR, "grund": "Cache",
         "bytes": 3 * 1024 ** 3, "dateien": 5, "vollstaendig": False},
        {"pfad": r"C:\eingang", "klasse": pw.EINGABE, "grund": "Quelle",
         "bytes": 2 * 1024 ** 3, "dateien": 5, "vollstaendig": True},
        {"pfad": r"C:\schutz", "klasse": pw.GESCHUETZT, "grund": "Spiel",
         "bytes": 100 * 1024 ** 3, "dateien": 5, "vollstaendig": True},
        {"pfad": r"C:\weg", "klasse": pw.LOESCHBAR, "grund": "Cache",
         "bytes": 0, "dateien": 0, "vollstaendig": True},
    ]

    def bericht(self):
        return pw.bericht(10 * 1024 ** 3, 100 * 1024 ** 3, 10.0, self.MESSUNGEN)

    def test_lage_steht_vorne(self):
        t = self.bericht()
        self.assertIn("UNTER der Grenze", t.splitlines()[0])

    def test_abgeschnittene_messung_wird_markiert(self):
        self.assertIn("abgeschnitten", self.bericht())
        self.assertIn("Untergrenze", self.bericht())

    def test_leere_ordner_erscheinen_nicht(self):
        self.assertNotIn(r"C:\weg", self.bericht())

    def test_geschuetzte_und_eingaben_stehen_im_bericht(self):
        t = self.bericht()
        self.assertIn("Nicht anfassen - Eingangsdaten", t)
        self.assertIn("Nicht anfassen - geschuetzt", t)
        self.assertIn("C:\\eingang", t)
        self.assertIn("C:\\schutz", t)

    def test_loeschsumme_nur_aus_cache(self):
        """Nur die Klasse `cache` zaehlt - und eine abgeschnittene Messung
        zaehlt mit ihrem (zu kleinen) Wert, denn sie ist eine Untergrenze."""
        self.assertIn("Sicher loeschbar: 13.0 GB", self.bericht())
        self.assertNotIn("102.0 GB", self.bericht())

    def test_grund_steht_bei_jedem_eintrag(self):
        t = self.bericht()
        self.assertIn("Quelle", t)
        self.assertIn("Spiel", t)


class CacheTest(unittest.TestCase):
    MESSUNG = [{"pfad": "x", "klasse": pw.LOESCHBAR, "grund": "",
                "bytes": 1, "dateien": 1, "vollstaendig": True}]

    def test_rundlauf(self):
        with tempfile_tmp() as t:
            self.assertIsNone(pw.cache_laden(t))
            pw.cache_sichern({"zeit": 1000, "laufzeit": 1.0,
                              "messungen": self.MESSUNG}, t)
            self.assertIsNotNone(pw.cache_laden(t, jetzt=lambda: 1000 + 60))

    def test_leere_messliste_wird_nicht_gecacht(self):
        """"Nichts gemessen" ist kein Bestand von null Byte - sondern eine
        fehlende Messung. Sie darf nicht als Ergebnis in den Cache."""
        self.assertFalse(pw.messung_vollstaendig([]))
        with tempfile_tmp() as t:
            self.assertIsNone(pw.cache_sichern({"zeit": 1000, "messungen": []}, t))

    def test_zu_alt_wird_ignoriert(self):
        with tempfile_tmp() as t:
            pw.cache_sichern({"zeit": 1000, "laufzeit": 1.0,
                              "messungen": self.MESSUNG}, t)
            self.assertIsNone(pw.cache_laden(t, alter=30, jetzt=lambda: 1000 + 3600))

    def test_giftiger_cache_wird_verworfen(self):
        """Auch beim Lesen wird die Regel geprueft.

        GEMESSEN am 27.09.2026: die erste Fassung pruefte nur beim Schreiben.
        Eine von einer aelteren Fassung geschriebene Datei mit abgeschnittener
        Messung wurde weiterverwendet und meldete 14,35 GB statt 37,90 GB fuer
        Downloads - mit derselben Sorgfalt gesetzt wie eine vollstaendige Zahl.
        """
        giftig = [dict(self.MESSUNG[0], vollstaendig=False)]
        with tempfile_tmp() as t:
            pfad = pw.cache_pfad(t)
            os.makedirs(os.path.dirname(pfad), exist_ok=True)
            with open(pfad, "w", encoding="utf-8") as f:
                json.dump({"zeit": 1000, "laufzeit": 0.2, "messungen": giftig}, f)
            self.assertIsNone(pw.cache_laden(t, jetzt=lambda: 1000 + 60))
            # derselbe Cache mit vollstaendiger Messung wird sehr wohl gelesen
            with open(pfad, "w", encoding="utf-8") as f:
                json.dump({"zeit": 1000, "laufzeit": 1.0,
                           "messungen": self.MESSUNG}, f)
            self.assertIsNotNone(pw.cache_laden(t, jetzt=lambda: 1000 + 60))

    def test_kaputter_cache_ist_kein_fehler(self):
        with tempfile_tmp() as t:
            os.makedirs(os.path.dirname(pw.cache_pfad(t)), exist_ok=True)
            with open(pw.cache_pfad(t), "w", encoding="utf-8") as f:
                f.write("{kaputt")
            self.assertIsNone(pw.cache_laden(t))

    def test_unvollstaendige_messung_landet_nicht_im_cache(self):
        """Der Kernfehler, den der erste Wurf hatte: eine abgeschnittene
        Summe wurde gespeichert und als Bestand weiterverkauft."""
        unvollstaendig = [{"pfad": "x", "klasse": pw.LOESCHBAR, "grund": "",
                           "bytes": 1, "dateien": 1, "vollstaendig": False}]
        self.assertFalse(pw.messung_vollstaendig(unvollstaendig))
        with tempfile_tmp() as t:
            # Die Regel sitzt in cache_sichern, nicht im Aufrufer - sonst
            # genuegt ein Aufruf, der sie vergisst.
            self.assertIsNone(pw.cache_sichern(
                {"zeit": 1, "messungen": unvollstaendig}, t))
            self.assertFalse(os.path.exists(pw.cache_pfad(t)))
        vollstaendig = [dict(unvollstaendig[0], vollstaendig=True)]
        self.assertTrue(pw.messung_vollstaendig(vollstaendig))
        with tempfile_tmp() as t:
            self.assertIsNotNone(pw.cache_sichern(
                {"zeit": 1, "messungen": vollstaendig}, t))


class ProgrammTest(unittest.TestCase):
    def test_gesund_druckt_eine_zeile_und_returnt_0(self):
        with mock.patch.object(pw, "platz", return_value=(500, 1000, 50.0)), \
             mock.patch.object(pw, "messen", side_effect=AssertionError("nicht messen")), \
             redirect_stdout(io.StringIO()) as out:
            rc = pw.hauptprogramm([])
        self.assertEqual(rc, 0)
        self.assertIn("ueber der Grenze", out.getvalue())
        self.assertEqual(len(out.getvalue().splitlines()), 1)

    def test_knapp_returnt_3_und_meldet(self):
        messungen = BerichtTest.MESSUNGEN
        with mock.patch.object(pw, "platz", return_value=(100, 1000, 10.0)), \
             mock.patch.object(pw, "messen", return_value=messungen), \
             mock.patch.object(pw, "cache_sichern"), \
             mock.patch.object(pw, "cache_laden", return_value=None), \
             redirect_stdout(io.StringIO()) as out:
            rc = pw.hauptprogramm([])
        self.assertEqual(rc, 3)
        self.assertIn("UNTER der Grenze", out.getvalue())
        self.assertIn("Sicher loeschbar", out.getvalue())

    def test_knapp_mit_cache_ruft_die_messung_nicht(self):
        roh = {"zeit": time.time(), "laufzeit": 2.0, "messungen": BerichtTest.MESSUNGEN}
        with mock.patch.object(pw, "platz", return_value=(100, 1000, 10.0)), \
             mock.patch.object(pw, "cache_laden", return_value=roh), \
             mock.patch.object(pw, "messen", side_effect=AssertionError("nicht messen")), \
             redirect_stdout(io.StringIO()) as out:
            rc = pw.hauptprogramm([])
        self.assertEqual(rc, 3)
        self.assertIn("C:\\cache", out.getvalue())

    def test_reinigen_ohne_trocken_fuehrt_aus(self):
        with mock.patch.object(pw, "platz", return_value=(100, 1000, 10.0)), \
             mock.patch.object(pw, "messen", return_value=BerichtTest.MESSUNGEN), \
             mock.patch.object(pw, "cache_laden", return_value=None), \
             mock.patch.object(pw, "cache_sichern"), \
             mock.patch.object(pw, "reinigen", return_value=([], [])) as r, \
             redirect_stdout(io.StringIO()):
            pw.hauptprogramm(["--reinigen"])
        self.assertTrue(r.called)
        self.assertFalse(r.call_args.kwargs.get("trocken"))

    def test_reinigen_mit_trocken_loescht_nicht(self):
        with mock.patch.object(pw, "platz", return_value=(100, 1000, 10.0)), \
             mock.patch.object(pw, "messen", return_value=BerichtTest.MESSUNGEN), \
             mock.patch.object(pw, "cache_laden", return_value=None), \
             mock.patch.object(pw, "cache_sichern"), \
             mock.patch.object(pw, "reinigen", return_value=([], [])) as r, \
             redirect_stdout(io.StringIO()):
            pw.hauptprogramm(["--reinigen", "--trocken"])
        self.assertTrue(r.call_args.kwargs.get("trocken"))


class EchteMaschineTest(unittest.TestCase):
    """Ein paar Behauptungen ueber den echten Rechner, die billig zu pruefen
    sind und die das Werkzeug sonst stillschweigend falsch machen wuerden."""

    def test_werkzeug_laeuft_und_beendet_sich(self):
        import subprocess
        r = subprocess.run([sys.executable, os.path.join("Tools", "platten_waechter.py")],
                           cwd=str(WURZEL), capture_output=True, text=True)
        self.assertIn(r.returncode, (0, 3), r.stderr[-400:])
        self.assertIn("Plattenwaechter:", r.stdout)

    def test_kandidatenliste_zeigt_auf_die_echte_platte(self):
        for pfad, klasse, _ in pw.kandidaten():
            if klasse == pw.LOESCHBAR:
                self.assertTrue(os.path.isabs(pfad), pfad)
                # loeschbare Pfade duerfen nie ausserhalb der zulaessigen Wurzeln
                # liegen - das waere eine Fehlklassifikation mit Schadenspotenzial
                self.assertTrue(
                    os.path.normpath(pfad).lower().startswith(pw.erlaubte_wurzeln()),
                    "%s liegt ausserhalb der erlaubten Wurzeln" % pfad)


class GateAnbindungTest(unittest.TestCase):
    """Der Waechter haengt im Commit-Gate. Drei Eigenschaften sind Pflicht:
    er wird wirklich aufgerufen, er erscheint VOR Gate 0, und er kann den
    Commit NIE verhindern - er ist ein Hinweis und wird auch als einer
    ausgewiesen."""

    def test_platten_hinweis_ist_im_gate_verdrahtet(self):
        import vor_dem_commit as vdc

        class DruckDoppel:
            """Wie der echte Lauf: drucken UND protokollieren. Ein reiner
            Mock wuerde nichts drucken, und die Reihenfolge laesst sich dann
            nur an der Aufrufliste ablesen - nicht an dem, was der Mensch
            im Hook tatsaechlich sieht."""
            def __init__(self):
                self.gefahren = []
                self.uebersprungen = []

            def fahre(self, name, befehl, *, shell_cmd=False):
                print("  ... %s" % name, flush=True)
                self.gefahren.append(name)
                return True

            def ueberspringe(self, name, grund):
                print("  ... %s" % name, flush=True)
                self.uebersprungen.append(name)

            def bericht(self):
                return 0

        doppel = DruckDoppel()
        alt = vdc.Lauf
        vdc.Lauf = lambda: doppel
        alt_besitz = vdc.besitz_gate
        vdc.besitz_gate = lambda *a, **k: False
        try:
            with mock.patch.object(pw, "warnung", return_value=None), \
                 redirect_stdout(io.StringIO()) as out:
                vdc.gates_fahren("schnell", ["Tools/x.py"])
        finally:
            vdc.Lauf = alt
            vdc.besitz_gate = alt_besitz
        text = out.getvalue()
        self.assertIn("Plattenplatz", text, "der Hinweis lief nicht mit")
        self.assertLess(text.index("Plattenplatz"), text.index("Gate 0"),
                        "der Hinweis muss vor Gate 0 kommen")

    def test_er_zaehlt_sich_nicht_als_gate(self):
        """Er darf weder die Gate-Zahl verfalschen noch die Stufenzuordnung
        verschieben - die Doppel in test_vor_dem_commit.py kennt nur echte
        Gates, und Gate 0 muss dort der EINZIGE gefahrene Eintrag bleiben."""
        import vor_dem_commit as vdc

        class Doppel:
            def __init__(self):
                self.gefahren = []
                self.uebersprungen = []

            def fahre(self, name, befehl, *, shell_cmd=False):
                self.gefahren.append(name)
                return True

            def ueberspringe(self, name, grund):
                self.uebersprungen.append(name)

            def bericht(self):
                return 0

        doppel = Doppel()
        alt = vdc.Lauf
        vdc.Lauf = lambda: doppel
        alt_besitz = vdc.besitz_gate
        vdc.besitz_gate = lambda *a, **k: False
        try:
            with mock.patch.object(pw, "warnung", return_value=None), \
                 redirect_stdout(io.StringIO()):
                vdc.gates_fahren("schnell", ["Tools/x.py"])
        finally:
            vdc.Lauf = alt
            vdc.besitz_gate = alt_besitz
        self.assertEqual(doppel.gefahren, ["Gate 0  Engine-Pfade"])

    def test_gesunde_platte_meldet_nur_den_hinweis(self):
        import vor_dem_commit as vdc
        with mock.patch.object(pw, "warnung", return_value=None), \
             redirect_stdout(io.StringIO()) as out:
            vdc.platten_hinweis()
        text = out.getvalue()
        self.assertIn("kein Gate", text)
        self.assertIn("Plattenplatz", text)

    def test_knappe_platte_zeigt_den_bericht(self):
        import vor_dem_commit as vdc
        with mock.patch.object(pw, "warnung", return_value="Plattenplatz: 5 % frei"), \
             redirect_stdout(io.StringIO()) as out:
            vdc.platten_hinweis()
        self.assertIn("5 % frei", out.getvalue())

    def test_ein_fehlendes_werkzeug_sperrt_nichts(self):
        """Im Push-Worktree fehlt ein UNCOMMITTER Werkzeug.

        `git worktree add` nimmt nur committete Dateien mit - dort ist ein
        neues Tools/platten_waechter.py nicht da. Der Import muss deshalb im
        try stehen, sonst stirbt der Commit-Hook an einem Werkzeug, das nur
        melden sollte. `sys.modules[x] = None` laesst `import x` wie in der
        Realitaet scheitern.
        """
        import vor_dem_commit as vdc
        with mock.patch.dict(sys.modules, {"platten_waechter": None}), \
             redirect_stdout(io.StringIO()) as out:
            vdc.platten_hinweis()  # darf keine Exception nach aussen geben
        text = out.getvalue()
        self.assertIn("nicht ausgefuehrt", text)
        self.assertIn("Plattenplatz", text)

    def test_ein_defekter_waechter_sperrt_nichts(self):
        import vor_dem_commit as vdc
        with mock.patch.object(pw, "warnung", side_effect=RuntimeError("kaputt")), \
             redirect_stdout(io.StringIO()) as out:
            vdc.platten_hinweis()  # darf keine Exception nach aussen geben
        self.assertIn("kaputt", out.getvalue())


if __name__ == "__main__":
    unittest.main()
