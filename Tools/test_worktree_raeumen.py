r"""Selbsttest: Worktree nur entfernen, wenn niemand darin arbeitet (Tools/worktree_raeumen.py).

Geprueft wird die Eigenschaft, um die es am 27.09.2026 ging: `git worktree
remove` loescht erst den Inhalt und meldet sich dann mit Exit 255 - der
Worktree, in dem der Push eines anderen Threads lief, war leer und der
Prozess lief weiter.

Jeder Test gegen einen echten Wegwerf-Worktree. `umbenennen` wird nur dort
zugelassen, wo es die Messung selbst ist; die Klassen mit `mock.patch`
ersetzen es, damit kein Test an einem echten Verzeichnis rumniert.
"""
import os
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import worktree_raeumen as wr  # noqa: E402


class RepoTest(unittest.TestCase):
    """Ein echtes Git-Repo mit einem echten Worktree darin, plus Remote.

    WICHTIG: `Saved/` steht hier NICHT in .gitignore. In den Gate-Tests hatte
    eine Fixture diesen Eintrag, und `git clean -fd` raeumte die Belege dann
    von selbst weg - der Test war aus dem falschen Grund gruen (AGENTS.md,
    "blinde Tests"). Die Wegwerf-Ordner sollen nicht heimlich durch git
    aufgeraeumt werden.
    """

    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.addCleanup(self._weg)
        self.haupt = Path(self.tmp) / "haupt"
        self.haupt.mkdir()
        self._git("init", "-b", "main")
        self._git("config", "user.email", "t@beispiel.de")
        self._git("config", "user.name", "Test")
        (self.haupt / "AGENTS.md").write_text("Hinweise\n")
        self._git("add", "-A")
        self._git("commit", "-m", "Grundstand")
        self.wt = Path(self.tmp) / "wt"
        self._git("worktree", "add", "-b", "zweig", str(self.wt), "HEAD")
        # Ein Remote, damit "nur hier liegende Commits" geprueft werden kann.
        origin = Path(self.tmp) / "origin.git"
        self._git("init", "--bare", "-b", "main", str(origin))
        self._git("remote", "add", "origin", str(origin))
        self._git("push", "-u", "origin", "main")

    def addCleanup(self, funktion):
        unittest.TestCase.addCleanup(self, funktion)

    def _weg(self):
        import shutil
        shutil.rmtree(self.tmp, ignore_errors=True)

    def _git(self, *args, cwd=None):
        return subprocess.run(["git", *args], cwd=str(cwd or self.haupt),
                              capture_output=True, text=True, encoding="utf-8",
                              errors="replace")


class ProbenTest(RepoTest):
    """Die Rename-Probe - sie ist die Messung, auf der der Wächter steht."""

    def test_freier_ordner_laesst_sich_umbenennen(self):
        self.assertTrue(wr.umbenennbar(self.wt))

    def test_der_ordner_heisst_danach_wieder_wie_vorher(self):
        wr.umbenennbar(self.wt)
        self.assertTrue((self.wt / "AGENTS.md").exists())
        self.assertFalse((self.wt.parent / (self.wt.name + ".worktree_raeumen_probe")).exists())

    def test_eine_offene_datei_im_ordner_blockiert_die_probe(self):
        # GEMESSEN am 27.09.2026: nur eine offene DATEI blockiert rename.
        # (Ein Prozess mit CWD darin blockiert nichts - der haeufigste Fall
        # bleibt also ungesehen, siehe Modultext.)
        datei = self.wt / "offen.txt"
        kind = subprocess.Popen(
            [sys.executable, "-c",
             "import sys,time; f=open(sys.argv[1],'a',buffering=1); time.sleep(60)", str(datei)])
        self.addCleanup(kind.kill)
        time.sleep(2.0)
        self.assertFalse(wr.umbenennbar(self.wt))
        kind.kill()
        kind.wait()
        time.sleep(0.5)
        self.assertTrue(wr.umbenennbar(self.wt))


class ProzessprobeTest(RepoTest):
    """Prozesse werden an ihrer Kommandozeile erkannt."""

    def test_der_ordner_in_einer_kommandozeile_zaehlt(self):
        kind = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(30)",
                                 str(self.wt / "AGENTS.md")])
        self.addCleanup(kind.kill)
        time.sleep(1.5)
        gefunden = wr.prozess_mit_pfad(self.wt)
        self.assertIn(kind.pid, [p for p, _, _ in gefunden],
                      "Der eigene Kindprozess wurde nicht gefunden: %s" % (gefunden,))

    def test_der_ordnerpfad_nicht_nur_als_unterordner(self):
        # /wt2 enthaelt /wt als Zeichenkette, ist aber ein anderer Ordner.
        self.assertEqual(wr.prozess_mit_pfad(self.haupt, eigene_pids=()), [])

    def test_eigene_pids_werden_ausgenommen(self):
        # Sonst wuerde sich der Wächter selbst sperren.
        gefunden = wr.prozess_mit_pfad(str(self.haupt), eigene_pids=[os.getpid()])
        self.assertNotIn(os.getpid(), [p for p, _, _ in gefunden])

    def test_ohne_powerShell_ist_die_liste_leer(self):
        with mock.patch.object(wr.subprocess, "run", side_effect=OSError("kein PowerShell")):
            self.assertEqual(wr.prozess_liste(), [])

    def test_kaputtes_json_ist_keine_prozessliste(self):
        with mock.patch.object(wr.subprocess, "run") as lauf:
            lauf.return_value = mock.Mock(returncode=0, stdout="{kein json", stderr="")
            self.assertEqual(wr.prozess_liste(), [])

    def test_ein_einzelner_prozess_kommt_als_objekt(self):
        # PowerShell liefert bei EINEM Treffer ein Objekt, nicht eine Liste -
        # ein `for z in json.loads(...)` liefe dann ueber die Schluessel.
        with mock.patch.object(wr.subprocess, "run") as lauf:
            lauf.return_value = mock.Mock(
                returncode=0,
                stdout='{"ProcessId":7,"Name":"a.exe","CommandLine":"x","ExecutablePath":""}',
                stderr="")
            self.assertEqual(len(wr.prozess_liste()), 1)


class AbbrechenTest(RepoTest):
    """Im Zweifel wird nicht geloescht. Das ist der eigentliche Auftrag."""

    def _ohne_umbenennen(self, pfad=None, haupt=None):
        return wr.pruefe(pfad or self.wt, haupt or self.haupt, umbenennen=False)

    def test_der_hauptordner_wird_abgewiesen(self):
        with self.assertRaises(wr.Belegt):
            self._ohne_umbenennen(self.haupt)

    def test_ein_fremder_ordner_wird_abgewiesen(self):
        fremd = Path(self.tmp) / "gibt_es_nicht"
        with self.assertRaises(wr.Unklar):
            self._ohne_umbenennen(fremd)

    def test_ein_ordner_der_nicht_von_git_ist_wird_abgewiesen(self):
        # Angelegt, aber nie mit `git worktree add` verknuepft. Ohne diese
        # Pruefung wuerde der Wächter einen herrenlosen Ordner entfernen.
        with self.assertRaises(wr.Unklar):
            self._ohne_umbenennen(self.haupt.parent)

    def test_der_waechter_sperrt_sich_nicht_selbst(self):
        # Ohne den Selbstbezug wuerde der eigene Befehl `python
        # Tools/worktree_raeumen.py <pfad>` seinen Pfad in der Kommandozeile
        # tragen und sich selbst als "Prozess mit diesem Pfad" melden - der
        # Wächter waere dann nie benutzbar.
        kind = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(30)",
                                 str(self.wt / "AGENTS.md")])
        self.addCleanup(kind.kill)
        time.sleep(1.5)
        gefunden = wr.prozess_mit_pfad(self.wt, eigene_pids=[kind.pid, os.getpid()])
        self.assertNotIn(kind.pid, [p for p, _, _ in gefunden])

    def test_ohne_wmi_wird_abgebrochen(self):
        with mock.patch.object(wr, "prozess_liste", return_value=[]):
            ok, bericht = self._ohne_umbenennen()
        self.assertFalse(ok, "Ohne Prozessliste muss abgebrochen werden: %s" % (bericht,))

    def test_commit_nur_hier_liegt_und_wird_gemeldet(self):
        (self.wt / "neu.txt").write_text("WIP\n")
        self._git("add", "-A", cwd=self.wt)
        self._git("commit", "-m", "nur hier", cwd=self.wt)
        ok, bericht = self._ohne_umbenennen()
        self.assertFalse(ok)
        self.assertTrue(any("nur hier" in t for _, t in bericht), bericht)

    def test_der_gefaehrliche_fall_ist_der_ohne_upstream(self):
        # GEMESSEN am 27.09.2026: `git log --not --branches` schluckt den
        # Commit, um den es geht - ein lokaler Worktree-Branch gilt ihm als
        # "liegt schon irgendwo". Genau dieser Fall ist der Branch
        # `wt-gatetest` mit vier Commits, die auf keinem origin existierten.
        # Deshalb fragt diese Pruefung nach REMOTEs, nicht nach Branches.
        (self.wt / "wip.txt").write_text("x\n")
        self._git("add", "-A", cwd=self.wt)
        self._git("commit", "-m", "nur hier", cwd=self.wt)

        vor = self._git("log", "--oneline", "zweig", "--not", "--remotes",
                        cwd=self.haupt).stdout
        mit = self._git("log", "--oneline", "zweig", "--not", "--remotes",
                        "--branches", cwd=self.haupt).stdout
        self.assertIn("nur hier", vor)
        self.assertNotIn("nur hier", mit,
                         "Messung falsch: --branches muesste den Commit hier schlucken")

        eigen = wr.eigene_commits(self.wt, self.haupt)
        self.assertTrue(any("nur hier" in z for z in eigen), eigen)
        ok, bericht = self._ohne_umbenennen()
        self.assertFalse(ok, "Ein Commit nur auf einem lokalen Branch muss blockieren")
        self.assertTrue(any("nur hier" in t for _, t in bericht), bericht)

    def test_nur_remote_commits_sind_frei(self):
        # Ein Commit, der auf origin liegt, darf den Worktree nicht blockieren.
        self._git("push", "origin", "zweig", cwd=self.haupt)
        eigen = wr.eigene_commits(self.wt, self.haupt)
        self.assertEqual(eigen, [], "Commits auf origin duerfen nicht blockieren: %s" % eigen)

    def test_freier_worktree_ist_frei(self):
        ok, bericht = self._ohne_umbenennen()
        self.assertTrue(ok, bericht)
        self.assertTrue(any(k == "Prozess" for k, _ in bericht), bericht)


class RauschenTest(RepoTest):
    """raeumen() fasst nichts an, wenn die Pruefung nicht freigibt."""

    def test_ohne_tun_wird_nichts_geloescht(self):
        with mock.patch.object(wr, "pruefe", return_value=(True, [("Prozess", "frei")])):
            code = wr.raeumen(self.wt, self.haupt, tun=False)
        self.assertEqual(code, 0)
        self.assertTrue(self.wt.exists())

    def test_belegt_heisst_exit_1_und_der_ordner_bleibt(self):
        with mock.patch.object(wr, "pruefe", return_value=(False, [("Prozess", "belegt")])):
            code = wr.raeumen(self.wt, self.haupt, tun=True)
        self.assertEqual(code, 1)
        self.assertTrue(self.wt.exists(), "Der Ordner wurde trotz Belegung geloescht.")

    def test_unklar_heisst_exit_3(self):
        with mock.patch.object(wr, "pruefe", side_effect=wr.Unklar("keine Ahnung")):
            self.assertEqual(wr.raeumen(self.wt, self.haupt, tun=True), 3)
        with mock.patch.object(wr, "pruefe", side_effect=wr.Belegt("Hauptordner")):
            self.assertEqual(wr.raeumen(self.wt, self.haupt, tun=True), 2)


class GegenprobeTest(RepoTest):
    """DER BEWEIS, DASS DIE TESTS NICHT BLIND SIND.

    Ersetzt man `pruefe` durch ein Nicken, muss der Aufrufer das an einem
    Worktree merken, in dem ein Prozess laeuft - sonst waere der ganze
    Wächter eine Behauptung. Genau so hat es die `beleg_vollstaendig`-Lücke
    im September gegeben: die Tests waren gruen, weil sie auf einem
    vorigen Lauf aufbauten.
    """

    def test_ohne_die_pruefung_waere_der_worktree_weg(self):
        kind = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(30)",
                                 str(self.wt / "AGENTS.md")])
        self.addCleanup(kind.kill)
        time.sleep(1.5)
        # a) so wie es GINGE, wenn niemand pruefte:
        blind = subprocess.run(["git", "worktree", "remove", "--force", str(self.wt)],
                               cwd=str(self.haupt), capture_output=True, text=True)
        self.assertEqual(blind.returncode, 0, "Gegenprobe: git entfernt den Worktree")
        self.assertFalse(self.wt.exists(), "Gegenprobe: Inhalt ist restlos weg")
        # b) mit dem Waechter: er haette es vorher gemeldet.
        gefunden = wr.prozess_mit_pfad(self.wt)
        self.assertTrue(gefunden, "Der Wächter sieht den Prozess nicht, den git ignoriert.")


if __name__ == "__main__":
    unittest.main()
