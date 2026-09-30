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
import datetime
import io
import json
import os
import shutil
import sys
import tempfile
import time
import unittest
from contextlib import contextmanager, redirect_stdout
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import platten_waechter as pw
import vor_dem_commit as vdc  # noqa: E402

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
    def messung(self, pfad, klasse, grund=""):
        return {"pfad": pfad, "klasse": klasse, "grund": grund, "bytes": 10,
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
            ], wurzel=t)
            # Innerhalb des with-Blocks pruefen: danach ist das Tempverzeichnis
            # weg, und die Assertionen wuerden aus dem richtigen Grund bestehen.
            self.assertEqual(geloescht, [ziel])
            self.assertEqual(abgewiesen, [])
            self.assertFalse(os.path.exists(ziel), "cache-Ordner muss weg sein")
            self.assertTrue(os.path.exists(behalten),
                            "eingabe/geschuetzt/ausgabe duerfen nicht angefasst werden")

    def test_fremder_pfad_wird_abgewiesen(self):
        """Richtig klassifiziert, aber ausserhalb - trotzdem nein."""
        with tempfile_tmp() as t, mock.patch.object(pw, "erlaubte_wurzeln",
                                                    return_value=(r"c:\projekt",)):
            geloescht, abgewiesen = pw.reinigen(
                [self.messung(r"C:\Users\HP\.bun\install", pw.LOESCHBAR)], wurzel=t)
        self.assertEqual(geloescht, [])
        self.assertEqual(len(abgewiesen), 1)

    def test_trockenlauf_loescht_nichts(self):
        with tempfile_tmp() as t, mock.patch.object(pw, "erlaubte_wurzeln",
                                                    return_value=(os.path.normpath(t).lower(),)):
            ziel = os.path.join(t, "weg")
            os.makedirs(ziel)
            geloescht, _ = pw.reinigen([self.messung(ziel, pw.LOESCHBAR)],
                                       trocken=True, wurzel=t)
            self.assertEqual(geloescht, [ziel])
            self.assertTrue(os.path.exists(ziel))

    def test_fehlende_ordner_werden_uebersprungen(self):
        """Fehlt der Ordner, ist das kein Fehler - aber auch kein Loeschfall."""
        with tempfile_tmp() as t, mock.patch.object(pw, "erlaubte_wurzeln",
                                                    return_value=(r"c:\projekt",)):
            geloescht, abgewiesen = pw.reinigen(
                [self.messung(r"c:\projekt\gibt\es\nicht", pw.LOESCHBAR)], wurzel=t)
        self.assertEqual(geloescht, [])
        self.assertEqual(abgewiesen, [])


class AusgabeKlasseTest(unittest.TestCase):
    r"""--auch-ausgabe: loeschbar, aber nur auf Zusage und nie bei laufendem Editor.

    Am 28.09.2026 von Hand geleert, weil kein Knopf dafuer existierte:
    4,14 GiB (Gate-Worktree-Intermediate, Saved\Cooked, Saved\Logs). Der
    Weg war richtig, die Arbeit falsch verteilt - sie gehoert ins Werkzeug,
    mit derselben Bremse wie die Caches.
    """

    def messung(self, pfad, klasse, grund="regenerierbar"):
        return {"pfad": pfad, "klasse": klasse, "grund": grund, "bytes": 10,
                "dateien": 1, "vollstaendig": True}

    def baue(self, t, *klassen):
        """Legt je Klasse einen Ordner mit Inhalt an und liefert die Pfade."""
        pfade = {}
        for klasse in klassen:
            ziel = os.path.join(t, klasse)
            os.makedirs(ziel)
            with open(os.path.join(ziel, "x"), "wb") as f:
                f.truncate(5)
            pfade[klasse] = ziel
        return pfade

    def test_mit_ausgabe_werden_die_objektdateien_geloescht(self):
        with tempfile_tmp() as t, mock.patch.object(pw, "erlaubte_wurzeln",
                                                    return_value=(os.path.normpath(t).lower(),)):
            pfade = self.baue(t, pw.LOESCHBAR, pw.REGENERIERBAR)
            geloescht, abgewiesen = pw.reinigen(
                [self.messung(pfade[pw.LOESCHBAR], pw.LOESCHBAR),
                 self.messung(pfade[pw.REGENERIERBAR], pw.REGENERIERBAR)],
                wurzel=t, klassen=pw.MIT_AUSGABE)
            self.assertEqual(sorted(geloescht), sorted(pfade.values()))
            self.assertEqual(abgewiesen, [])
            for pfad in pfade.values():
                self.assertFalse(os.path.exists(pfad), pfad)

    def test_ohne_die_option_bleibt_die_ausgabe_stehen(self):
        """Die Voreinstellung darf sich nicht verschoben haben."""
        with tempfile_tmp() as t, mock.patch.object(pw, "erlaubte_wurzeln",
                                                    return_value=(os.path.normpath(t).lower(),)):
            pfade = self.baue(t, pw.LOESCHBAR, pw.REGENERIERBAR, pw.EINGABE)
            geloescht, _ = pw.reinigen(
                [self.messung(pfade[pw.LOESCHBAR], pw.LOESCHBAR),
                 self.messung(pfade[pw.REGENERIERBAR], pw.REGENERIERBAR),
                 self.messung(pfade[pw.EINGABE], pw.EINGABE)], wurzel=t)
            self.assertEqual(geloescht, [pfade[pw.LOESCHBAR]])
            self.assertTrue(os.path.exists(pfade[pw.REGENERIERBAR]))
            self.assertTrue(os.path.exists(pfade[pw.EINGABE]))

    def test_bei_laufendem_editor_bleibt_die_ausgabe_stehen(self):
        """Die Sperre: Object-Dateien waehrend eines Laufs loeschen ergibt
        halbe Builds, die hinterher wie echte Fehler aussehen."""
        with tempfile_tmp() as t, mock.patch.object(pw, "erlaubte_wurzeln",
                                                    return_value=(os.path.normpath(t).lower(),)):
            pfade = self.baue(t, pw.LOESCHBAR, pw.REGENERIERBAR)
            os.makedirs(os.path.join(t, "Saved"))
            with open(os.path.join(t, "Saved", "EngineRun.lock"), "w") as f:
                f.write("PID 1")
            geloescht, abgewiesen = pw.reinigen(
                [self.messung(pfade[pw.LOESCHBAR], pw.LOESCHBAR),
                 self.messung(pfade[pw.REGENERIERBAR], pw.REGENERIERBAR)],
                wurzel=t, klassen=pw.MIT_AUSGABE)
            # Die Caches gehen trotzdem - die haengen an keinem Lauf.
            self.assertEqual(geloescht, [pfade[pw.LOESCHBAR]])
            self.assertTrue(os.path.exists(pfade[pw.REGENERIERBAR]),
                            "die Ausgabe-Klasse wurde trotz Lock geloescht")
            # Und sie wird als abgewiesen gemeldet, nicht stillschweigend
            # uebersprungen - ein stilles Ueberspringen sieht aus wie "nichts
            # zu loeschen".
            self.assertTrue(
                any(os.path.normpath(pfade[pw.REGENERIERBAR]) in p
                    for p in abgewiesen),
                "die gesperrte Ausgabe-Klasse steht nicht in 'abgewiesen': %r"
                % (abgewiesen,))

    def test_mit_ausgabe_kommt_keine_eingabe_hinein(self):
        self.assertEqual(pw.MIT_AUSGABE, (pw.LOESCHBAR, pw.REGENERIERBAR))
        for klasse in (pw.EINGABE, pw.GESCHUETZT):
            self.assertNotIn(klasse, pw.MIT_AUSGABE)
        self.assertEqual(pw.NUR_LOESCHEN, (pw.LOESCHBAR,),
                         "die Voreinstellung hat sich verschoben")

    def test_der_trockenlauf_zeigt_die_ausgabe_ohne_zu_loeschen(self):
        with tempfile_tmp() as t, mock.patch.object(pw, "erlaubte_wurzeln",
                                                    return_value=(os.path.normpath(t).lower(),)):
            pfade = self.baue(t, pw.REGENERIERBAR)
            geloescht, _ = pw.reinigen(
                [self.messung(pfade[pw.REGENERIERBAR], pw.REGENERIERBAR)],
                trocken=True, wurzel=t, klassen=pw.MIT_AUSGABE)
            self.assertEqual(geloescht, [pfade[pw.REGENERIERBAR]])
            self.assertTrue(os.path.exists(pfade[pw.REGENERIERBAR]))

    def test_der_gate_worktree_darf_geloescht_werden_sein_stamm_nicht(self):
        """ENG gefuehrt: sein Projektordner ja, der Stammordner nein.

        Der Stammordner `.gate-worktree` enthaelt die Worktrees ALLER
        Gate-Laeufe und fremder Threads. Freizugeben hiesse, einem Pfad
        zu erlauben, aus dem heraus beliebig viel zu loeschen - die genaue
        Sorte Regel, die das Werkzeug abschafft.
        """
        gate = os.path.normpath(pw._gate_worktree_ordner()).lower()
        stamm = os.path.dirname(gate)
        wurzeln = pw.erlaubte_wurzeln()
        self.assertIn(gate, wurzeln, "der Gate-Worktree ist nicht loeschbar")
        self.assertNotIn(stamm, wurzeln, "der Stammordner ist loeschbar")

    def test_der_gate_worktree_durchlaesst_die_wurzelpruefung(self):
        """Gegenprobe am Pfad-MUSTER - im Trockenlauf, es wird nichts geloescht.

        GEMESSEN 30.09.2026 (Push-Lauf wb_push18): mit dem REALen Pfad
        hing der Test an der Tagesform einer fremden Sitzung. Raeumt
        die den gemeinsamen Gate-Worktree genau zwischen Suite-Start
        und Test, verschluckt reinigen() den Eintrag still (isdir-
        Zweig) und der Test faellt OHNE Abgewiesen-Grund. Jetzt
        spiegelt ein Tempordner das Muster (erlaubte Wurzel, darunter
        der Projektordner mit Intermediate). Dass der echte Gate-
        Worktree in den erlaubten Wurzeln liegt und sein Stamm nicht,
        pinnt der Test davor - der braucht keine Existenz.
        """
        with tempfile_tmp() as t:
            os.makedirs(os.path.join(t, "Projekt"))
            pfad = os.path.normpath(os.path.join(t, "Projekt", "Intermediate"))
            os.makedirs(pfad)
            # Die Lock-Bremse ist an anderer Stelle getestet, mit Tempordnern.
            # Hier wird sie abgeschaltet: sonst haengt der Test daran, ob
            # gerade ein fremder Gate-Lauf laeuft.
            with mock.patch.object(pw, "erlaubte_wurzeln",
                                   return_value=(os.path.normpath(t).lower(),)), \
                 mock.patch.object(pw, "engine_lock_aktiv", return_value=False):
                geloescht, abgewiesen = pw.reinigen(
                    [{"pfad": pfad, "klasse": pw.REGENERIERBAR, "grund": "Test",
                      "bytes": 1, "dateien": 1, "vollstaendig": True}],
                    trocken=True, wurzel=t, klassen=pw.MIT_AUSGABE)
            self.assertEqual(geloescht, [pfad],
                             "der Pfad wurde abgewiesen: %r" % (abgewiesen,))

    def test_der_cache_kennt_die_gemessene_kandidatenliste(self):
        """Ein alter Messstand darf nicht fuer eine neue Liste sprechen.

        GEMESSEN am 28.09.2026: der Kandidat fuer den Gate-Worktree wurde
        auf einen existierenden Ordner umgestellt, der Cache enthielt aber
        die Messung ueber den alten. Der Bericht zeigte weiterhin
        "0,00 GiB" fuer 3,2 GiB, die real da lagen - mit derselben
        Sorgfalt dargestellt wie eine vollstaendige Zahl.
        """
        with tempfile_tmp() as t:
            os.makedirs(os.path.join(t, "Saved"))
            messung = [{"pfad": os.path.join(t, "x"), "klasse": pw.LOESCHBAR,
                        "grund": "g", "bytes": 1, "dateien": 1,
                        "vollstaendig": True}]
            pw.cache_sichern({"zeit": time.time(), "laufzeit": 0.1,
                              "messungen": messung,
                              "signatur": pw.kandidaten_signatur()}, wurzel=t)
            self.assertIsNotNone(pw.cache_laden(wurzel=t),
                                 "ein passend signierter Cache wird verworfen")
            # Jetzt eine ANDERE Liste - etwa weil ein Kandidat umgestellt
            # wurde. Der Messstand darf nicht mehr gelten.
            pw.cache_sichern({"zeit": time.time(), "laufzeit": 0.1,
                              "messungen": messung,
                              "signatur": "falsche-signatur"}, wurzel=t)
            self.assertIsNone(pw.cache_laden(wurzel=t),
                              "ein Cache einer anderen Kandidatenliste wurde "
                              "weiterverwendet")
            # Und ohne Signatur (alte Datei) erst recht nicht.
            pw.cache_sichern({"zeit": time.time(), "laufzeit": 0.1,
                              "messungen": messung}, wurzel=t)
            self.assertIsNone(pw.cache_laden(wurzel=t),
                              "ein Cache ohne Signatur wurde weiterverwendet")

    def test_die_signatur_aendert_sich_mit_der_liste(self):
        with mock.patch.object(pw, "KANDIDATEN", list(pw.KANDIDATEN)):
            vorher = pw.kandidaten_signatur()
            pw.KANDIDATEN.append((r"{gate}\X", pw.LOESCHBAR, "neu"))
            self.assertNotEqual(vorher, pw.kandidaten_signatur())
            pw.KANDIDATEN[-1] = (r"{gate}\X", pw.LOESCHBAR, "anders")
            self.assertNotEqual(vorher, pw.kandidaten_signatur(),
                                "auch eine neue Begruendung ist eine neue "
                                "Aussage - sie steht im Bericht")

    def test_der_gate_worktree_wird_vom_richtigen_ordner_gemessen(self):
        """Der Kandidat muss NEBEN dem Projekt liegen, nicht darin.

        Am 28.09.2026 stand er auf `{Projekt}\.gate-worktree\...` - ein
        Ordner, den es nicht gibt. Der Wächter meldete darum "0,00 GiB"
        fuer genau die Objketdateien, die beim Push-Lauf 3 GiB gross waren.
        """
        ordner = pw._gate_worktree_ordner()
        basis = os.path.normpath(str(WURZEL))
        self.assertFalse(os.path.normpath(ordner).lower().startswith(
            os.path.join(basis, ".gate-worktree").lower()),
            "der Gate-Worktree liegt nicht innerhalb des Projektordners: %s" % ordner)
        self.assertIn(".gate-worktree", os.path.normpath(ordner))
        # Und der Kandidat, der ihn messen soll, traegt den Pfad wirklich.
        muster = [m for m, k, _g in pw.KANDIDATEN if "{gate}" in m]
        self.assertEqual(len(muster), 1, muster)
        aufgeloest = pw._muster_aufloesen(muster[0])
        self.assertTrue(aufgeloest.endswith("Intermediate"))
        self.assertIn(os.path.normpath(ordner), os.path.normpath(aufgeloest))
        # Kein LOESCHBARER Kandidat darf auf einen Pfad INNERHALB des
        # Projekts zeigen, den es nicht gibt. Die geschuetzten Eintraege
        # (etwa der Worktree eines anderen Threads) bleiben unberuehrt -
        # sie richten sich nach fremder Benennung, nicht nach dem Layout.
        falsch = "{w}" + chr(92) + ".gate-worktree"
        for muster, klasse, _grund in pw.KANDIDATEN:
            if klasse in (pw.LOESCHBAR, pw.REGENERIERBAR):
                self.assertNotIn(falsch, muster, muster)

    def test_der_bericht_nennt_die_option_bei_ausgabe(self):
        messungen = [self.messung(r"c:\projekt\Intermediate\Build", pw.REGENERIERBAR)]
        messungen[0]["bytes"] = 5 * 2 ** 30
        mit = pw.bericht(100, 1000, 10.0, messungen, 20.0, 1.0, pw.MIT_AUSGABE)
        ohne = pw.bericht(100, 1000, 10.0, messungen, 20.0, 1.0)
        self.assertIn("--auch-ausgabe", mit)
        self.assertNotIn("--auch-ausgabe", ohne)
        # Ohne die Option wird die Ausgabe nicht als loeschbar angeboten.
        self.assertNotIn("Sicher loeschbar", ohne)


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
                              "messungen": self.MESSUNG,
                              "signatur": pw.kandidaten_signatur()}, t)
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
                              "messungen": self.MESSUNG,
                              "signatur": pw.kandidaten_signatur()}, t)
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
                           "messungen": self.MESSUNG,
                           "signatur": pw.kandidaten_signatur()}, f)
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
                           cwd=str(WURZEL), capture_output=True, text=True,
                           errors="replace")
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


class ProtokollTest(unittest.TestCase):
    """Jeder Loeschpfad hinterlaesst eine Begruendung und eine Groesse.

    Der Schutz des Waechters war doppelt (Klasse, Pfadnormalisierung) und ist
    um das LOESCHPROTOKOLL erweitert: die Absicht wird vor dem Eingriff
    geschrieben, das Ergebnis danach. Ohne schreibbares Protokoll wird
    garnichts geloescht.
    """

    def messung(self, pfad, klasse=pw.LOESCHBAR, grund="weil Cache", bytes_vorher=3 * 1024 ** 3):
        return {"pfad": pfad, "klasse": klasse, "grund": grund,
                "bytes": bytes_vorher, "dateien": 7, "vollstaendig": True}

    def cacheordner(self, wurzel, name="weg", bytes_vorher=5):
        ziel = os.path.join(wurzel, name)
        os.makedirs(ziel)
        with open(os.path.join(ziel, "x"), "wb") as f:
            f.truncate(bytes_vorher)
        return ziel

    def zeilen(self, wurzel):
        pfad = os.path.join(wurzel, "Saved", "Diagnose", "loeschprotokoll.jsonl")
        with open(pfad, "r", encoding="utf-8") as f:
            return [json.loads(z) for z in f if z.strip()]

    def test_protokoll_waechst_nicht_ueber_den_Lauf_hinweg(self):
        """Anhaengen, nicht ueberschreiben. Eine Datei, die jeder Lauf neu
        schreibt, verliert genau die Historie, die man braucht."""
        with tempfile_tmp() as t, mock.patch.object(pw, "erlaubte_wurzeln",
                                                    return_value=(os.path.normpath(t).lower(),)):
            a = self.cacheordner(t, "a")
            b = self.cacheordner(t, "b")
            pw.reinigen([self.messung(a, grund="erste")], wurzel=t)
            nach_eins = len(self.zeilen(t))
            self.assertGreater(nach_eins, 0)
            pw.reinigen([self.messung(b, grund="zweite")], wurzel=t)
            zeilen = self.zeilen(t)
            self.assertEqual(len(zeilen), nach_eins * 2)
            self.assertIn("erste", [z["begruendung"] for z in zeilen])
            self.assertIn("zweite", [z["begruendung"] for z in zeilen])

    def test_absicht_steht_vor_der_ergebniszeile(self):
        """Die Absicht muss VOR dem Eingriff im Protokoll stehen. Ein
        Protokoll, das erst nachher geschrieben wird, beweist nichts: genau
        da kann ein abgebrochener Lauf nicht mehr erklaert werden."""
        with tempfile_tmp() as t, mock.patch.object(pw, "erlaubte_wurzeln",
                                                    return_value=(os.path.normpath(t).lower(),)):
            ziel = self.cacheordner(t)
            pw.reinigen([self.messung(ziel, grund="DireX-Shadercache")], wurzel=t)
            phasen = [z["phase"] for z in self.zeilen(t)]
            self.assertEqual(phasen[0], "absicht")
            self.assertIn("ergebnis", phasen)
            self.assertLess(phasen.index("absicht"), phasen.index("ergebnis"))

    def test_jeder_eintrag_traegt_begruendung_und_groesse(self):
        with tempfile_tmp() as t, mock.patch.object(pw, "erlaubte_wurzeln",
                                                    return_value=(os.path.normpath(t).lower(),)):
            ziel = self.cacheordner(t, bytes_vorher=5 * 1024 ** 3)
            pw.reinigen([self.messung(ziel, grund="npm-Downloadcache")], wurzel=t)
            zeilen = self.zeilen(t)
            self.assertTrue(zeilen)
            for z in zeilen:
                self.assertTrue(z["begruendung"], "Eintrag ohne Begruendung: %r" % z)
                self.assertIn(z["phase"], ("absicht", "ergebnis"))
                self.assertIn("bytes", z)
                self.assertIn("gib", z)
                self.assertEqual(z["pfad"], os.path.normpath(ziel))
                self.assertTrue(z["zeit"])

    def test_ohne_schreibbares_protokoll_wird_nicht_geloescht(self):
        """DER ZUSATZLIECHE SCHUTZ. Ein Loeschen ohne Protokoll waere genau
        die Sorte Eingriff, die man spaeter nicht mehr erklaeren kann."""
        with tempfile_tmp() as t, mock.patch.object(pw, "erlaubte_wurzeln",
                                                    return_value=(os.path.normpath(t).lower(),)):
            ziel = self.cacheordner(t)
            geloescht, abgewiesen = pw.reinigen(
                [self.messung(ziel)], wurzel=t, protokoll=lambda zeilen: False)
            self.assertEqual(geloescht, [])
            self.assertTrue(os.path.exists(ziel),
                            "Ordner geloescht, obwohl das Protokoll nicht schreibbar war")
            self.assertIn(ziel, abgewiesen)

    def test_trockenlauf_protokolliert_aber_loescht_nicht(self):
        with tempfile_tmp() as t, mock.patch.object(pw, "erlaubte_wurzeln",
                                                    return_value=(os.path.normpath(t).lower(),)):
            ziel = self.cacheordner(t)
            geloescht, _ = pw.reinigen([self.messung(ziel, grund="wird gebraucht")],
                                       trocken=True, wurzel=t)
            self.assertEqual(geloescht, [ziel])
            self.assertTrue(os.path.exists(ziel), "Trockenlauf hat geloescht")
            zeilen = self.zeilen(t)
            self.assertEqual([z["phase"] for z in zeilen], ["trocken"])
            self.assertTrue(zeilen[0]["trocken"])
            self.assertEqual(zeilen[0]["begruendung"], "wird gebraucht")

    def test_nicht_verschwundener_ordner_steht_als_unvollstaendig_drin(self):
        """rmtree laeuft mit ignore_errors=True und SCHLUCKT Fehler. Ohne
        Nachpruefung stuende "ergebnis" im Protokoll, der Ordner laege noch
        da - das schlimmste denkbare Protokoll."""
        with tempfile_tmp() as t, mock.patch.object(pw, "erlaubte_wurzeln",
                                                    return_value=(os.path.normpath(t).lower(),)):
            ziel = self.cacheordner(t)
            with mock.patch.object(pw.shutil, "rmtree", return_value=None):
                geloescht, _ = pw.reinigen([self.messung(ziel)], wurzel=t)
            self.assertEqual(geloescht, [], "als geloescht gemeldet, obwohl da")
            phasen = [z["phase"] for z in self.zeilen(t)]
            self.assertIn("unvollstaendig", phasen)
            letzte = self.zeilen(t)[-1]
            self.assertFalse(letzte["weg"])

    def test_report_nennt_grund_und_summe(self):
        m = self.messung(r"C:\cache\a", grund="Shader-Ableitungen")
        text = pw.loeschreport([m], [r"C:\cache\a"])
        self.assertIn("3.00 GiB", text)
        self.assertIn("Shader-Ableitungen", text)
        self.assertIn("zusammen: 3.00 GiB", text)

    def test_report_warnt_bei_unvollstaendiger_messung(self):
        m = self.messung(r"C:\cache\a", grund="Shader")
        m["vollstaendig"] = False
        text = pw.loeschreport([m], [r"C:\cache\a"])
        self.assertIn("unvollstaendig", text)

    def test_protokoll_bleibt_im_wurzelverzeichnis_des_aufrufers(self):
        """GEMESSEN am 27.09.2026: ohne `wurzel` schrieben die Tests in das
        ECHTE loeschprotokoll.jsonl des Projekts - drei Testeintraege mit
        tmp-Pfaden standen nach einem Lauf dort. Ein Protokoll, in dem Tests
        stehen, beweist nichts ueber den Waechter."""
        with tempfile_tmp() as t:
            self.assertEqual(pw.protokoll_pfad(t),
                             os.path.join(t, "Saved", "Diagnose", "loeschprotokoll.jsonl"))
            ziel = self.cacheordner(t)
            with mock.patch.object(pw, "erlaubte_wurzeln",
                                   return_value=(os.path.normpath(t).lower(),)):
                pw.reinigen([self.messung(ziel)], wurzel=t)
            self.assertTrue(os.path.exists(pw.protokoll_pfad(t)))


class GateVerweisTest(unittest.TestCase):
    """Der Platten-Hinweis nennt die Handlung, nicht nur eine Zahl.

    GEMESSEN am 27.09.2026: der Hinweis im Commit-Hook sagte
    "UNTER der Grenze (20 %)" und sonst nichts. Eine Zahl ohne Folge - der
    Leser weiss nicht, dass gleich der naechste Engine-Start scheitert, und
    schon gar nicht, wo die zweite Schwelle liegt.
    """

    def test_gate_grenze_wird_aus_der_ps1_gelesen(self):
        """NICHT hier wiederholen. Zwei Kopien einer Schwelle fallen
        auseinander, und dann sagt der Hinweis 10 % an, waehrend das Gate
        bei 12 % zuschlaegt."""
        wert = pw.gate_grenze()
        self.assertIsNotNone(wert, "ps1 nicht lesbar - der Hinweis nennt keine Grenze")
        self.assertEqual(wert, 10.0)
        # Und die ps1 muss auch wirklich diese Zahl verwenden.
        ps1 = os.path.join(pw.WURZEL, "Tools", "engine_run_lock.ps1")
        with open(ps1, "r", encoding="utf-8") as f:
            text = f.read()
        self.assertIn("[double]$PlattenGrenze = %s" % wert, text)

    def test_gate_grenze_liefert_none_statt_zu_raten(self):
        with tempfile.TemporaryDirectory() as t:
            self.assertIsNone(pw.gate_grenze(t),
                              "ohne ps1 wird geraten - das waere eine erfundene Zahl")

    def test_handlungsblock_schweigt_bei_genug_platz(self):
        self.assertIsNone(pw.handlungsblock(50.0), "gesunder Rechner darf nicht schreien")

    def test_handlungsblock_nennt_beide_schwellen_und_den_befehl(self):
        text = pw.handlungsblock(12.0)
        self.assertIn("WAS JETZT PASSIERT", text)
        self.assertIn("MELDET", text)          # ab 20 %
        self.assertIn("10 %", text)           # Gate ab 10 %
        self.assertIn("engine_run_lock.ps1", text)
        self.assertIn("PlattenTrotz", text)   # Notausgang

    def test_handlungsblock_sagt_wenn_es_schon_zu_spaet_ist(self):
        """Unter der Gate-Grenze ist \"bricht ab\" keine Zukunft mehr, sondern
        das Jetzt - der Block muss das unterscheiden."""
        knapp = pw.handlungsblock(6.0)
        self.assertIn("ABBRICHT JEDER Engine-Start", knapp)
        self.assertIn("erst aufraeumen, dann bauen", knapp)

    def test_bericht_traegt_den_handlungsblock(self):
        text = pw.bericht(50 * 1024 ** 3, 500 * 1024 ** 3, 10.0, [])
        self.assertIn("WAS JETZT PASSIERT", text)
        # ... und der Bericht wird auch sonst wo verwendet: der
        # Aufgabenplanungslauf soll die Handlung genauso sehen.
        self.assertIn("PlattenTrotz", text)

    def test_bericht_ohne_platznot_bleibt_ohne_block(self):
        text = pw.bericht(300 * 1024 ** 3, 500 * 1024 ** 3, 60.0, [])
        self.assertNotIn("WAS JETZT PASSIERT", text)


class HinweisVerweisTest(unittest.TestCase):
    """Der Verweis im Commit-Hook: nur bei Platznot, und ohne Buchhaltung."""

    def ausgabe(self, warnung_text):
        puffer = io.StringIO()
        with redirect_stdout(puffer):
            vdc.platten_hinweis()
        return puffer.getvalue()

    def test_bei_platznot_nennt_er_die_gate_schwelle(self):
        with mock.patch.object(pw, "warnung", return_value="Plattenwaechter: knapp"):
            text = self.ausgabe(None)
        self.assertIn("NAECHSTES", text)
        self.assertIn("engine_run_lock.ps1", text)
        self.assertIn("10", text)
        self.assertIn("PlattenTrotz", text)

    def test_ohne_platznot_kommt_kein_verweis(self):
        """Der gesunde Fall ist der Normalfall - er darf nicht lauter werden."""
        with mock.patch.object(pw, "warnung", return_value=None):
            text = self.ausgabe(None)
        self.assertNotIn("NAECHSTES", text)

    def test_er_zaehlt_sich_nicht_als_gate(self):
        """Die fremde LaufDoppel in test_vor_dem_commit.py kennt nur fahre/
        ueberspringe/bericht. Ein `Lauf` hier wuerde AttributeError werfen -
        der Hinweis darf nur drucken."""
        class Doppel:
            """Kennt absichtlich NUR die drei echten Methoden - wie im
            fremden Test. Jede zusaetzliche Nutzung faellt hier auf."""
            def __init__(self):
                self.aufrufe = []

            def fahre(self, *a, **k):
                self.aufrufe.append("fahre")

            def ueberspringe(self, *a, **k):
                self.aufrufe.append("ueberspringe")

            def bericht(self):
                return 0

        doppel = Doppel()
        with mock.patch.object(pw, "warnung", return_value="Plattenwaechter: knapp"), \
             redirect_stdout(io.StringIO()):
            vdc.platten_hinweis()
        self.assertEqual(doppel.aufrufe, [], "der Hinweis ging durch den Lauf")

    def test_fehlender_waechter_erzeugt_keinen_absturz(self):
        """Der Push-Worktree hat nur committete Dateien. Fehlt
        platten_waechter.py, darf der Hook nicht sterben."""
        with mock.patch.dict(sys.modules, {"platten_waechter": None}):
            with redirect_stdout(io.StringIO()):
                vdc.platten_hinweis()  # darf nicht werfen


class DevBuildsAltlastenTest(unittest.TestCase):
    """Die Keep-N-Klasse fuer dev-builds - der Kompromiss mit dem fremden
    Thread (docs/plattenstrategie.md): die letzten N Build-Baeume und alles
    Frische bleiben SEINE Beweise; aelteres wird nur sichtbar vorgemerkt
    und geloescht erst mit --auch-devbuilds, protokolliert wie jede
    Klasse. Alle Tests bauen ihre Ordner im eigenen Tempverzeichnis -
    die echten dev-builds werden nie beruehrt.
    """

    def basis(self, now):
        """Temp-Basis mit drei Build-Baeumen: heute, vor 2 Tagen, vor 5 Tagen."""
        d = tempfile.mkdtemp(prefix="wb_devbuilds_")
        self.addCleanup(shutil.rmtree, d, ignore_errors=True)
        import shutil as _shutil
        for name, alter_tage, inhalt in (
                ("2026-09-29-bugtank-frisch", 0.1, b"frisch"),
                ("2026-09-27-bugtank-mittel", 2.0, b"mittel"),
                ("2026-09-24-bugtank-alt", 5.0, b"alt" * 100)):
            voll = os.path.join(d, name)
            os.makedirs(voll)
            with open(os.path.join(voll, "probe.bin"), "wb") as fh:
                fh.write(inhalt * 10)
            stempel = (now - datetime.timedelta(days=alter_tage)).timestamp()
            os.utime(voll, (stempel, stempel))
        return d

    def test_keep_n_und_mindestalter_zusammen(self):
        import datetime as dt
        now = dt.datetime.now()
        d = self.basis(now)
        altlasten, behaltene = pw.dev_builds_altlasten(
            basis=d, jetzt=now, behalten=2, mindestalter_tage=0.5)
        namen = {a["pfad"] for a in altlasten}
        # Keep-N-Platz 1 (frisch) und Platz 2 (2 Tage alt) bleiben - der
        # zweite ist AUCH aelter als 0.5 Tage, aber unter den letzten 2.
        self.assertEqual(len(altlasten), 1, altlasten)
        self.assertIn(os.path.join(d, "2026-09-24-bugtank-alt"), namen)
        self.assertEqual(len(behaltene), 2)

    def test_mindestalter_schuetzt_den_keep_n_ueberlauf(self):
        """Ein dritter Build aus einem frischen Schub ist kein Altlast,
        solange er juenger als das Mindestalter ist. Bei 3 Tagen ist nur
        der 5-Tage-Baum alt - der 2-Tage-Baum ist Keep-N-Platz 2 UND
        Burst-geschaetzt."""
        import datetime as dt
        now = dt.datetime.now()
        d = self.basis(now)
        altlasten, _ = pw.dev_builds_altlasten(
            basis=d, jetzt=now, behalten=1, mindestalter_tage=3.0)
        self.assertEqual([a["pfad"] for a in altlasten],
                         [os.path.join(d, "2026-09-24-bugtank-alt")],
                         "nur der 5-Tage-Baum ist Altlast; der 2-Tage-Baum "
                         "bleibt Burst-geschaetzt")

    def test_leere_und_fehlende_basis_sind_still(self):
        import datetime as dt
        d = tempfile.mkdtemp(prefix="wb_devbuilds_leer_")
        self.addCleanup(shutil.rmtree, d, ignore_errors=True)
        self.assertEqual(pw.dev_builds_altlasten(basis=d), ([], []))
        self.assertEqual(
            pw.dev_builds_altlasten(
                basis=os.path.join(d, "fehlt")), ([], []))

    def test_altlast_misst_vollstaendig_und_begruendet(self):
        import datetime as dt
        now = dt.datetime.now()
        d = self.basis(now)
        altlasten, _ = pw.dev_builds_altlasten(
            basis=d, jetzt=now, behalten=2, mindestalter_tage=0.5)
        a = altlasten[0]
        self.assertTrue(a["vollstaendig"])
        self.assertEqual(a["klasse"], "dev-builds")
        self.assertGreater(a["bytes"], 0)
        self.assertIn("docs/plattenstrategie.md", a["grund"])
        erwartet = (now - datetime.timedelta(days=5.0)).strftime("%d.%m.%Y")
        self.assertIn(erwartet, a["grund"], "die Begruendung nennt das "
                                           "Build-Datum (deutsches Format)")

    def test_single_file_im_basisordner_zaehlt_nicht(self):
        import datetime as dt
        now = dt.datetime.now()
        d = self.basis(now)
        stempel = (now - dt.timedelta(days=9)).timestamp()
        lose = os.path.join(d, "notizen.txt")
        with open(lose, "w") as fh:
            fh.write("x")
        os.utime(lose, (stempel, stempel))
        altlasten, _ = pw.dev_builds_altlasten(
            basis=d, jetzt=now, behalten=2, mindestalter_tage=0.5)
        self.assertNotIn(lose, {a["pfad"] for a in altlasten},
                         "eine lose Datei ist kein Build-Baum")


class DevBuildsReinigenTest(unittest.TestCase):
    """reinigen(): dev-builds nur ALT, nur mit der Klasse, protokolliert.

    Der zentrale Schutz: ein normaler --reinigen-Lauf (NUR_LOESCHEN)
    beruehrt dev-builds NICHT - auch dann nicht, wenn jemand eine
    Misch-Messung mit der falschen Klasse einschmuggelt.
    """

    def _basis_mit_altlast(self, now, alt_tage=5.0, frisch_tage=0.1):
        import datetime as dt
        d = tempfile.mkdtemp(prefix="wb_devbuilds_rein_")
        self.addCleanup(shutil.rmtree, d, ignore_errors=True)
        basis = os.path.join(d, "Saved", "Package", "dev-builds")
        os.makedirs(basis)
        pfade = {}
        for name, alter_tage in (("build-alt", alt_tage),
                                 ("build-frisch", frisch_tage)):
            voll = os.path.join(basis, name)
            os.makedirs(voll)
            with open(os.path.join(voll, "probe.bin"), "wb") as fh:
                fh.write(b"x" * 50)
            stempel = (now - dt.timedelta(days=alter_tage)).timestamp()
            os.utime(voll, (stempel, stempel))
            pfade[name] = voll
        return d, basis, pfade

    def test_normaler_reinigen_lauf_fasst_devbuilds_nicht_an(self):
        import datetime as dt
        now = dt.datetime.now()
        d, basis, pfade = self._basis_mit_altlast(now)
        alt, beh = pw.dev_builds_altlasten(basis=basis, jetzt=now,
                                           behalten=1, mindestalter_tage=0.5)
        self.assertEqual(len(alt), 1)
        geloescht, abgewiesen = pw.reinigen(
            alt, trocken=False, protokoll=lambda zeilen: True,
            wurzel=d, klassen=pw.NUR_LOESCHEN)
        self.assertEqual(geloescht, [], "NUR_LOESCHEN enthaelt keine "
                                        "dev-builds-Klasse")
        self.assertTrue(os.path.isdir(pfade["build-alt"]),
                        "der normale Lauf hat die Altlast NICHT angefasst")

    def test_reinigen_mit_devbuilds_klasse_nur_altlast(self):
        import datetime as dt
        now = dt.datetime.now()
        d, basis, pfade = self._basis_mit_altlast(now)
        alt, beh = pw.dev_builds_altlasten(basis=basis, jetzt=now,
                                           behalten=1, mindestalter_tage=0.5)
        messungen = alt + [{"pfad": pfade["build-frisch"],
                            "klasse": "dev-builds", "grund": "frisch",
                            "bytes": 50, "dateien": 1,
                            "vollstaendig": True}]
        geloescht, abgewiesen = pw.reinigen(
            messungen, trocken=False, protokoll=lambda zeilen: True,
            wurzel=d, klassen=pw.NUR_LOESCHEN + (pw.DEV_BUILDS,),
            dev_builds_behalten=1, dev_builds_basis=basis)
        self.assertIn(os.path.normpath(pfade["build-alt"]),
                      [os.path.normpath(p) for p in geloescht])
        self.assertFalse(os.path.exists(pfade["build-alt"]),
                         "die Altlast ist weg")
        self.assertTrue(os.path.isdir(pfade["build-frisch"]),
                        "der frische Build bleibt - Beweis des fremden Threads")
        self.assertTrue(any("frisch oder in den letzten" in z for z in abgewiesen),
                        "die frische Misch-Messung wird mit Grund abgewiesen")

    def test_trocken_vermerkt_altlast_ohne_zu_loeschen(self):
        import datetime as dt
        now = dt.datetime.now()
        d, basis, pfade = self._basis_mit_altlast(now)
        alt, _ = pw.dev_builds_altlasten(basis=basis, jetzt=now,
                                         behalten=1, mindestalter_tage=0.5)
        protokoll = []
        geloescht, _ = pw.reinigen(
            alt, trocken=True,
            protokoll=lambda zeilen: protokoll.extend(zeilen) or True,
            wurzel=d, klassen=pw.NUR_LOESCHEN + (pw.DEV_BUILDS,),
            dev_builds_behalten=1, dev_builds_basis=basis)
        self.assertEqual(len(geloescht), 1)
        self.assertTrue(os.path.isdir(pfade["build-alt"]), "trocken loescht nicht")
        self.assertTrue(all(z.get("phase") == "trocken" and z.get("trocken")
                            for z in protokoll), "die Trocken-Phase steht im Protokoll")

    def test_fehlendes_protokoll_stoppt_auch_devbuilds(self):
        import datetime as dt
        now = dt.datetime.now()
        d, basis, pfade = self._basis_mit_altlast(now)
        alt, _ = pw.dev_builds_altlasten(basis=basis, jetzt=now,
                                         behalten=1, mindestalter_tage=0.5)
        geloescht, abgewiesen = pw.reinigen(
            alt, trocken=False, protokoll=lambda zeilen: False,
            wurzel=d, klassen=pw.NUR_LOESCHEN + (pw.DEV_BUILDS,),
            dev_builds_basis=basis)
        self.assertEqual(geloescht, [], "ohne schreibbares Protokoll wird "
                                        "nichts geloescht - auch keine Altlast")
        self.assertTrue(os.path.isdir(pfade["build-alt"]))

    def test_override_behalten_wirkt_im_filter(self):
        import datetime as dt
        now = dt.datetime.now()
        d, basis, pfade = self._basis_mit_altlast(now, alt_tage=5.0,
                                                   frisch_tage=0.1)
        # Nur der FRISCHE Build existiert real; die Altlast-Messung wird
        # von Hand gebaut, um den Pfad-Filter gegen den Override zu pruefen.
        messungen = [{"pfad": pfade["build-alt"], "klasse": "dev-builds",
                      "grund": "handgebaut", "bytes": 50, "dateien": 1,
                      "vollstaendig": True}]
        geloescht, abgewiesen = pw.reinigen(
            messungen, trocken=False, protokoll=lambda zeilen: True,
            wurzel=d, klassen=pw.NUR_LOESCHEN + (pw.DEV_BUILDS,),
            dev_builds_behalten=5, dev_builds_basis=basis)
        # Mit Keep-N=5 waere der Build BEHALTEN - aber er steht NICHT in
        # den frisch berechneten Altlasten... Doch: der Pfad-Filter prueft
        # gegen dev_builds_altlasten(behalten=5), und dort zaehlt der
        # Ordner als Platz 1 von 2 -> behalten -> abgewiesen.
        self.assertEqual(geloescht, [],
                         "der Override im Filter schuetzt den Build")
        self.assertTrue(os.path.isdir(pfade["build-alt"]))
        self.assertTrue(any("frisch oder in den letzten" in z
                            for z in abgewiesen))


if __name__ == "__main__":
    unittest.main()
