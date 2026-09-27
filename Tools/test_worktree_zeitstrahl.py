r"""Selbsttest: Worktree-Zeitleiste (Tools/worktree_zeitstrahl.py).

Der Kern der Zeitleiste ist eine RECHNUNG: Push-Start plus die kumulierten
Gate-Dauern ergibt die Uhrzeit jedes Gates. Eine Rechnung, die man nicht
nachrechnen kann, ist eine Behauptung mit Zeitangaben - also wird hier
nachgerechnet, und zwar gegen echte Logs aus `.planning`, nicht gegen
erfundene Beispielzeilen.

    python -m unittest discover -s Tools -p "test_worktree_zeitstrahl.py"
"""
import os
import re
import sys
import unittest
from datetime import datetime, timedelta
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import worktree_zeitstrahl as wz  # noqa: E402

WURZEL = Path(__file__).resolve().parent.parent
# DER Planordner - nicht WURZEL.parent. GEMESSEN am 27.09.2026: im
# Gate-Worktree war WURZEL.parent der Ordner `.gate-worktree`, in dem kein
# `.planning` liegt, also uebersprangen vier Tests dieser Datei strukturell.
# planordner() laesst git den HAUPTBAUM zeigen, aus dem der Worktree
# verlinkt ist. None heisst: kein Planordner auffindbar - dann wird
# zurueckgesprungen, aber nur auf einen Ordner, dessen .planning auch
# wirklich geprueft wurde.
STAMM = wz.planordner() or WURZEL.parent


def hat_log(relativ):
    p = STAMM / relativ
    return p.exists()


class RechnenTest(unittest.TestCase):
    """Die Gate-Zeiten aus einem echten Push-Log."""

    LOG = ".planning/meilensteine/push.log"

    @unittest.skipUnless(hat_log(LOG), "Push-Log nicht vorhanden")
    def test_die_rechnung_stimmt_mit_der_log_mtime_ueberein(self):
        # GEMESSEN am 27.09.2026: Start 14:16:17 + 853 s = 14:30:30, mtime
        # 14:30:37. Das sind 7 s - die Toleranz, die ich festsetze, ist
        # deshalb 60 s und nicht "genau".
        zeilen = wz.lies_text(STAMM / self.LOG)
        start, schritte, kum, wartete = wz.gate_zeiten(zeilen)
        self.assertIsNotNone(start)
        self.assertFalse(wartete, "Dieser Lauf hat nicht auf einen fremden Lock gewartet")
        self.assertEqual(start.strftime("%H:%M:%S"), "14:16:17")
        mtime = datetime.fromtimestamp((STAMM / self.LOG).stat().st_mtime)
        ende = start + timedelta(seconds=kum)
        self.assertLess(abs((mtime - ende).total_seconds()), 60,
                        "Rechnung und mtime passen nicht zusammen: %s vs %s" % (ende, mtime))

    @unittest.skipUnless(hat_log(LOG), "Push-Log nicht vorhanden")
    def test_die_gates_sind_aneinander_anschliessend(self):
        zeilen = wz.lies_text(STAMM / self.LOG)
        _, schritte, _, _ = wz.gate_zeiten(zeilen)
        self.assertGreaterEqual(len(schritte), 4)
        for a, b in zip(schritte, schritte[1:]):
            self.assertEqual(a["bis"], b["von"],
                             "Luecke oder Ueberlappung zwischen %s und %s" % (a["gate"], b["gate"]))

    @unittest.skipUnless(hat_log(LOG), "Push-Log nicht vorhanden")
    def test_die_dauern_stimmen_mit_dem_log_ueberein(self):
        zeilen = wz.lies_text(STAMM / self.LOG)
        _, schritte, _, _ = wz.gate_zeiten(zeilen)
        im_log = [int(m.group(1)) for z in zeilen if (m := re.search(r"gruen\s+(\d+) s", z))]
        ausgerechnet = [s["dauer_s"] for s in schritte]
        self.assertEqual(ausgerechnet, im_log[:len(ausgerechnet)])

    def test_ohne_lock_zeile_keine_zeit(self):
        self.assertEqual(wz.gate_zeiten(["irgendwas", "noch mehr"])[0], None)


class WartephaseTest(unittest.TestCase):
    """GEMESSEN: der Lock-Zeitpunkt ist NICHT immer der Gate-Beginn.

    `.planning/push-quicksicht.log` wartete rund 12 Minuten auf einen fremden
    Lock. Mit dieser Startzeit lag die gerechnete Endzeit 894 s neben der
    mtime - die Zeitleiste zeigte einen Lauf, den es so nicht gab.
    """

    WARTE_LOG = [
        "Lock: BELEGT durch einen anderen Lauf - PID 20400, Label push_gate, "
        "seit 2026-09-27 14:44:33, Rechner OMENBERT. - warte (hoechstens noch 60 min) ...",
        "Lock: BELEGT durch einen anderen Lauf - PID 20400, Label push_gate, "
        "seit 2026-09-27 14:44:33, Rechner OMENBERT. - warte (hoechstens noch 48 min) ...",
        "Engine-Lock: Lock: gehalten - PID 28260, Label push_gate, seit 2026-09-27 14:59:22, Rechner OMENBERT.",
    ]
    DIREKT_LOG = [
        "Engine-Lock: Lock: gehalten - PID 28560, Label push_gate, "
        "seit 2026-09-27 14:16:17, Rechner OMENBERT.",
    ]

    def test_warten_wird_erkannt(self):
        start, wartete = wz.gate_beginn(self.WARTE_LOG)
        self.assertTrue(wartete)
        self.assertEqual(start.strftime("%H:%M:%S"), "14:59:22",
                         "Nach dem Warten ist die 'gehalten'-Zeit der Laufbeginn.")

    def test_ohne_warten_ist_es_kein_wartefall(self):
        _, wartete = wz.gate_beginn(self.DIREKT_LOG)
        self.assertFalse(wartete)


class VollstaendigkeitTest(unittest.TestCase):
    """Ein Log ohne Abschluss ist eine Untergrenze - und das wird gesagt."""

    def test_log_ohne_abschluss_wird_vermerkt(self):
        zeilen = [
            "Engine-Lock: Lock: gehalten - PID 1, Label push_gate, seit 2026-09-27 14:00:00, Rechner X.",
            "  ... Gate 0  Engine-Pfade",
            "      gruen  10 s",
        ]
        self.assertFalse(wz.lauf_vollstaendig(zeilen))
        zeilen.append("Alle Gates gruen.")
        self.assertTrue(wz.lauf_vollstaendig(zeilen))

    @unittest.skipUnless(hat_log(".planning/push-quicksicht.log"), "Log nicht da")
    def test_der_quicksicht_lauf_ist_vollstaendig_gewesen(self):
        # Er endete mit EXIT 0 und einem neuen Branch auf origin - die
        # fehlende Abschlusszeile im Log darf nicht als Abbruch gelesen werden.
        zeilen = wz.lies_text(STAMM / ".planning/push-quicksicht.log")
        self.assertTrue(any("EXIT 0" in z for z in zeilen), zeilen[-3:])


class ReflogTest(unittest.TestCase):
    """Reflog-Zeilen sind echt; erfundene muessen abgewiesen werden."""

    ZEILE = ("0a3fc00a73431d70b5e470556abb881ab20835ad 452fc54587c46be35133b1777489d828ca2e706b "
             "tu.blitzbert <tu.blitzbert@gmail.com> 1790512411 +0200\tcommit: Test")

    def setUp(self):
        import tempfile
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(lambda: __import__("shutil").rmtree(self.tmp, ignore_errors=True))
        (self.tmp / "logs").mkdir()
        (self.tmp / "logs" / "HEAD").write_text(self.ZEILE + "\n", encoding="utf-8")

    def test_echte_zeile_wird_gelesen(self):
        e = wz.reflog_ereignisse(self.tmp, "HEAD", datetime.min)
        self.assertEqual(len(e), 1)
        self.assertEqual(e[0]["wer"], "tu.blitzbert")
        self.assertIn("commit: Test", e[0]["text"])
        self.assertEqual(e[0]["quelle"], "reflog")

    def test_kaputte_zeile_wird_ignoriert(self):
        with open(self.tmp / "logs" / "HEAD", "a", encoding="utf-8") as f:
            f.write("Unsinn\n")
        self.assertEqual(len(wz.reflog_ereignisse(self.tmp, "HEAD", datetime.min)), 1)

    def test_die_grenze_filtert(self):
        # Eine Grenze in der Zukunft schluckt alles - sonst waeren die
        # "letzten 4 Stunden" stillschweigend ein Time Travel.
        spaeter = datetime.now() + timedelta(hours=1)
        self.assertEqual(wz.reflog_ereignisse(self.tmp, "HEAD", spaeter), [])

    def test_zeitstempel_ist_echt_und_nicht_aus_der_zukunft(self):
        # GEMESSEN: 1790512411 ist 2026-09-27 14:33:31. Vergleicht man nur
        # gegen "jetzt", ist jeder Zeitstempel gueltig - auch datetime.now(),
        # womit die Sabotage "echte Zeit durch jetzt ersetzen" durchging.
        e = wz.reflog_ereignisse(self.tmp, "HEAD", datetime.min)
        zeit = datetime.fromisoformat(e[0]["zeit"])
        erwartet = datetime.fromtimestamp(1790512411)
        self.assertEqual(zeit.replace(microsecond=0), erwartet,
                         "Der Epoch-Wert des Reflogs wurde nicht ausgewertet.")
        self.assertLess(zeit, datetime.now() + timedelta(minutes=1))

    def test_der_zeitstempel_ist_der_einzige_nicht_freie(self):
        # Die Rohzeile enthaelt zwei SHAs und einen Unix-Zeitwert; nur der
        # letzte gehoert in die Zeit. Ein Regex, das zu frueh greift,
        # liefert eine falsche Uhrzeit und faellt hier auf.
        m = wz.RE_REFLOG.match(self.ZEILE)
        self.assertIsNotNone(m)
        self.assertEqual(m.group(4), "1790512411")
        self.assertEqual(m.group(6), "commit: Test")


class PlanordnerTest(unittest.TestCase):
    """Die Aufloesung des Planordners - der Grund, warum diese Tests im
    Gate-Worktree ueberhaupt laufen."""

    def setUp(self):
        import tempfile
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(lambda: __import__("shutil").rmtree(self.tmp, ignore_errors=True))

    def test_der_echte_planordner_wird_gefunden(self):
        gefunden = wz.planordner()
        if gefunden is None:
            self.skipTest("Kein .planning in Reichweite - hier gibt es nichts zu pruefen.")
        # Er MUSS der Ordner sein, in dem die Push-Logs liegen, sonst
        # pruefen alle Log-Tests eine fremde Ablage.
        self.assertTrue((gefunden / ".planning").is_dir())
        self.assertEqual(gefunden, STAMM,
                         "planordner() und der vom Test benutzte Ordner sind "
                         "verschieden - die Log-Tests laufen woanders als gedacht.")

    def test_ohne_planning_daneben_geht_es_zurueck_auf_den_uebernachsten(self):
        # Nachgebauter Gate-Worktree: <Aufsicht>/.gate-worktree/WiesbadenReal
        # ohne .planning. Der naechste Kandidat (hier der Hauptbaum) muss
        # greifen.
        aufsicht = self.tmp / "Aufsicht"
        haupt = aufsicht / "WiesbadenReal"
        gate = aufsicht / ".gate-worktree" / "WiesbadenReal"
        for d in (haupt, gate):
            d.mkdir(parents=True)
        (aufsicht / ".planning").mkdir()
        # Ein Worktree ohne eigenes .git: git schlaegt fehl, also greift der
        # rueckwaerts laufende Kandidat - genau der Fall, den der Hauptcode
        # uebersehen hat.
        gefunden = wz.planordner(gate)
        self.assertEqual(gefunden, aufsicht)

    def test_lieblose_falsche_kandidaten_werden_uebersprungen(self):
        # Der ERSTE Kandidat muss .planning wirklich enthalten, sonst
        # gewinnt ein Ordner, in dem die Dateien fehlen.
        aufsicht = self.tmp / "A2"
        gate = aufsicht / ".gate-worktree" / "WiesbadenReal"
        (aufsicht / "WiesbadenReal").mkdir(parents=True)
        gate.mkdir(parents=True)
        (aufsicht / ".planning").mkdir()
        gefunden = wz.planordner(gate)
        self.assertNotEqual(gefunden, gate,
                            "Der Gate-Ordner wurde gewaehlt, obwohl dort kein "
                            ".planning liegt.")
        self.assertEqual(gefunden, aufsicht)

    def test_ganz_ohne_planning_ist_es_none_statt_ein_falscher_ordner(self):
        # Nichts zu finden heisst None. Ein zurueckfallender WURZEL.parent
        # waere hier stillschweigend falsch: die Log-Tests prueften dann einen
        # Ordner, den es nicht gibt, und meldeten es als gemessen.
        allein = self.tmp / "GanzAllein" / "WiesbadenReal"
        allein.mkdir(parents=True)
        self.assertIsNone(wz.planordner(allein))

    def test_git_ordner_liefert_einen_weg_der__existiert(self):
        gd = wz.git_ordner(WURZEL)
        if gd is None:
            self.skipTest("git antwortet nicht - die Aufloesung kann nicht geprueft werden.")
        self.assertTrue(gd.is_dir(), "git nannte %s, das ist aber kein Ordner" % gd)

    def test_ein_falscher_git_ordner_wird_erkannt(self):
        # Sabotage-Gegenprobe: planordner() darf nicht blind jedem/git
        # glauben. Ein gemeinsamer .git ohne .planning-Kandidaten darf die
        # Aufloesung nicht zum Erfolg verhelfen.
        tor = self.tmp / "Tor"
        projekt = tor / ".gate-worktree" / "WiesbadenReal"
        projekt.mkdir(parents=True)
        (tor / "falsch.git").mkdir()
        # Auch ohne echten .git-Inhalt: der Pfad wird nur als Kandidat
        # benutzt, entscheidend ist der Inhalt von .planning.
        gefunden = wz.planordner(projekt)
        self.assertIsNone(gefunden,
                          "planordner() erfand einen Planordner, wo keiner ist.")


class BerichtTest(unittest.TestCase):
    """Der Bericht behauptet nicht mehr, als er gemessen hat."""

    def setUp(self):
        import tempfile
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(lambda: __import__("shutil").rmtree(self.tmp, ignore_errors=True))
        (self.tmp / ".git").mkdir()

    def test_leerer_baum_liefert_leeren_bericht_ohne_absturz(self):
        bericht = wz.baue_zeitleiste(self.tmp / ".git", self.tmp, None)
        self.assertEqual(bericht["ereignisse"], [])
        self.assertIn("worktrees", bericht)

    def test_ohne_lock_steht_das_dort(self):
        bericht = wz.baue_zeitleiste(self.tmp / ".git", self.tmp, None)
        # Die Lock-Datei fehlt in der Testumgebung fast sicher - dann ist das
        # eine Aussage, keine Luecke.
        self.assertIn("engine_lock", bericht)
        self.assertIsInstance(bericht["engine_lock"], dict)

    def test_alle_quellen_sind_vertreten(self):
        bericht = wz.baue_zeitleiste(self.tmp / ".git", self.tmp, None)
        for schluessel in ("erzeugt", "ereignisse", "engine_lock", "worktrees"):
            self.assertIn(schluessel, bericht)


if __name__ == "__main__":
    unittest.main()
