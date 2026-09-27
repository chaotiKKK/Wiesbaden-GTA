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


class StadtinhaltTest(RepoTest):
    """Die Verlinkung der gebackenen Stadt - das Erkennungszeichen vom 27.09.

    WICHTIG: Die Zaehlung selbst wird nicht nachgebaut, sondern `zaehlen`
    bekommt eine Funktion geschenkt. Sonst prueft der Test meine
    Arithmetik statt der Erkenntnis, und die Arithmetik kann nicht falsch
    sein. Die echte Messung an einem Gate-Worktree laeuft im
    `MessungTest` - die braucht das echte Projekt und steht deshalb separat.
    """

    def _befund(self, ist, soll):
        return wr.stadtinhalt_befund(self.wt, self.haupt,
                                     zaehlen=lambda p: ist, erwarten=soll)

    def test_alle_verlinkt_ist_unauffaellig(self):
        schlagwort, text, warnung = self._befund(28, 28)
        self.assertFalse(warnung)
        self.assertIn("28", text)

    def test_null_verlinkt_warnt(self):
        # Genau die Zahl im Push-Log des Worktrees, den ich leerte.
        schlagwort, text, warnung = self._befund(0, 28)
        self.assertTrue(warnung)
        self.assertIn("0 von 28", text)
        self.assertEqual(schlagwort, "Stadt")

    def test_null_und_halb_sagen_verschiedenes(self):
        # GEMESSEN: Sabotiert man `if ist == 0` zu `if ist < 0`, landet die
        # Null im allgemeinen "nur k von n"-Zweig. Die Warnung bleibt -
        # nur ihre BEGRUENDUNG geht verloren, und die ist das Eigentliche:
        # "haengt gar nicht an der Stadt" ist etwas anderes als "halb
        # vorbereitet". Deshalb unterscheidet der Test die beiden Texte,
        # nicht nur, dass beide warnen.
        _, null, _ = self._befund(0, 28)
        _, halb, _ = self._befund(14, 28)
        self.assertNotIn("halb", null)
        self.assertNotIn("0 von", halb)
        self.assertIn("haengt NICHT", null)

    def test_teilweise_verlinkt_warnt_ebenfalls(self):
        # Der gefaehrlichere Fall, weil er im Log nicht als Zahl auffaellt.
        schlagwort, text, warnung = self._befund(14, 28)
        self.assertTrue(warnung)
        self.assertIn("14 von 28", text)
        self.assertIn("halb", text)

    def test_mehr_als_erwartet_ist_kein_fehler(self):
        # Ein Worktree kann zusaetzliche Verbindungen haben (z.B. eine
        # Test-Fixture). Das ist kein Grund zu meckern.
        _, _, warnung = self._befund(35, 28)
        self.assertFalse(warnung)

    def test_warnung_blockiert_das_entfernen_nicht(self):
        ok, bericht = wr.pruefe(self.wt, self.haupt, umbenennen=False,
                                zaehlen=lambda p: 0, erwarten=28)
        self.assertTrue(ok, "Eine Warnung darf nicht wie ein Fehler behandelt werden: %s" % bericht)
        self.assertTrue(any("0 von 28" in t for _, t in bericht), bericht)

    def test_warnung_erscheint_im_bericht_auch_ohne_abbruch(self):
        # Sonst sieht man sie nur, wenn schon etwas anderes rot ist - und
        # dann ist es zu spaet.
        ok, bericht = wr.pruefe(self.wt, self.haupt, umbenennen=False,
                                zaehlen=lambda p: 7, erwarten=28)
        stadt = [t for k, t in bericht if k == "Stadt"]
        self.assertEqual(len(stadt), 1, bericht)
        self.assertIn("7 von 28", stadt[0])

    def test_nicht_messbar_wird_nicht_behauptet(self):
        # `os.path.islink` erkennt unter Windows keine Junction - mein erster
        # Zaehlversuch kam auf 0 von 28 und HATTE RECHT, aus dem falschen
        # Grund. "Nicht messbar" (None) und "0 Verlinkungen" muessen
        # unterscheidbar bleiben, sonst meldet der Waelter einen Befund, den
        # er nicht hat.
        befund = self._befund(None, 28)
        self.assertIsNone(befund)

    def test_ohne_erwartung_keine_aussage(self):
        # Ein Projekt ohne die gebackene Stadt hat keine Erwartung - dann
        # darf der Waelter nicht von 0 Verlinkungen faseln.
        befund = self._befund(0, None)
        self.assertIsNone(befund)

    def test_zaehlt_eine_junction_als_verbindung(self):
        # Die Messung, an der mein erster Versuch scheiterte.
        quelle = self.haupt / "Content" / "Generated"
        quelle.mkdir(parents=True)
        (quelle / "x.txt").write_text("x")
        ziel = self.wt / "Content" / "Generated"
        ziel.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(["cmd", "/c", "mklink", "/J", str(ziel), str(quelle)],
                       capture_output=True, text=True)
        if not os.path.isdir(str(ziel)):
            self.skipTest("mklink /J nicht moeglich")
        # os.path.islink meldet eine Junction als normale Datei - deshalb
        # wird ueber FILE_ATTRIBUTE_REPARSE_POINT gezaehlt.
        self.assertFalse(os.path.islink(str(ziel)), "Voraussetzung: islink ist blind")
        n = wr.verlinkte_stadtinhalte(self.wt)
        self.assertIsNotNone(n)
        self.assertGreaterEqual(n, 1, "Die Junction wurde nicht als Verbindung gezaehlt.")


class AdminRestTest(RepoTest):
    """Verwaiste Admin-Eintraege - der Ordner ist weg, die Registrierung noch da.

    GEMESSEN am 27.09.2026 im echten Baum: `.git/worktrees/WiesbadenReal1`
    zeigte auf `Saved/_gate5_worktree/WiesbadenReal`, einen Ordner, den es
    nicht mehr gibt. `git worktree list` fuehrt ihn als prunable.
    """

    def _weg_machen(self, pfad):
        """Ordner loeschen, Admin-Eintrag stehen lassen - der Zustand, um den es geht."""
        import shutil
        shutil.rmtree(pfad, ignore_errors=True)
        self.assertFalse(pfad.exists())
        eintraege = list((self.haupt / ".git" / "worktrees").iterdir())
        self.assertTrue(eintraege, "Gegenprobe: es gibt Admin-Eintraege")

    def _detached_worktree_mit_commit(self, ordner):
        self._git("worktree", "add", "--detach", str(ordner), "HEAD")
        (ordner / "nur-hier.txt").write_text("Arbeit\n", encoding="utf-8")
        self._git("add", "-A", cwd=ordner)
        fertig = self._git("-c", "user.email=t@beispiel.de", "-c", "user.name=Test",
                           "commit", "-m", "nur im worktree", cwd=ordner)
        self.assertEqual(fertig.returncode, 0, fertig.stderr)
        return self._git("rev-parse", "HEAD", cwd=ordner).stdout.strip()

    def test_ein_worktree_der_noch_da_ist_ist_kein_rest(self):
        # DER Unterschied, um den es geht: der Ordner existiert, es wird
        # nichts gemeldet - auch dann nicht, wenn git "prunable" sagt.
        self.assertEqual(wr.admin_reste(self.haupt), [])
        (self.wt / ".git").unlink()
        roh = self._git("worktree", "list", "--porcelain").stdout
        self.assertIn("prunable", roh, "Gegenprobe: git nennt ihn prunable")
        self.assertTrue(self.wt.is_dir(), "Gegenprobe: der Ordner steht noch")
        self.assertEqual(wr.admin_reste(self.haupt), [],
                         "Ein Ordner mit Dateien darin ist kein verwaister Rest.")

    def test_nach_geloeschtem_ordner_wird_er_gefunden(self):
        self._weg_machen(self.wt)
        reste = wr.admin_reste(self.haupt)
        self.assertEqual(len(reste), 1)
        self.assertEqual(Path(reste[0]["pfad"]).name, self.wt.name)

    def test_der_detached_commit_verhindert_das_raeumen(self):
        # DER FALL. Ein Commit im DETACHED HEAD haengt am Reflog des
        # Admin-Eintrags. GEMESSEN: nach `prune` gab es keinen Ref und
        # keinen Reflog mehr darauf - nur das lose Objekt, weg beim
        # naechsten gc. Dieser Rest darf NICHT geraeumt werden.
        ordner = Path(self.tmp) / "detached"
        sha = self._detached_worktree_mit_commit(ordner)
        self._weg_machen(ordner)
        ok, bericht, spuren = wr.pruefe_admin_reste(self.haupt)
        self.assertFalse(ok, "Ein verwaister Commit darf nicht als frei durchgehen.")
        self.assertEqual(spuren, [])
        self.assertIn("verwaist", " ".join(t for _, t in bericht))

    def test_ohne_die_pruefung_waere_der_commit_weg(self):
        # Gegenprobe zum vorigen Test: `git worktree prune` macht es
        # tatsaechlich, und danach ist der Commit nicht mehr auffindbar.
        ordner = Path(self.tmp) / "detached2"
        sha = self._detached_worktree_mit_commit(ordner)
        self._weg_machen(ordner)
        fertig = self._git("worktree", "prune", "-v")
        self.assertEqual(fertig.returncode, 0, fertig.stderr)
        refs = self._git("for-each-ref", "--contains", sha).stdout.strip()
        self.assertEqual(refs, "", "Gegenprobe: nach prune zeigt kein Ref mehr darauf")
        self.assertIn("verwaist", wr.head_erreichbar(sha, self.haupt)[1],
                      "Gegenprobe: der Waechter haette es melden muessen")

    def test_ein_branch_commit_darf_geraeumt_werden(self):
        # Der Gegenfall: der Branch lebt im Haupt-Baum, der Commit bleibt
        # erreichbar. Hier blockiert der Waechter NICHT - sonst waere er
        # ein Werkzeug, das nichts tun kann.
        self._weg_machen(self.wt)
        ok, bericht, spuren = wr.pruefe_admin_reste(self.haupt)
        self.assertTrue(ok, " ".join(t for _, t in bericht))
        self.assertEqual(len(spuren), 1)

    def test_head_auf_einem_remote_ist_frei(self):
        ordner = Path(self.tmp) / "gepusht"
        self._git("worktree", "add", "-b", "ferner", str(ordner), "HEAD")
        (ordner / "f.txt").write_text("x\n", encoding="utf-8")
        self._git("add", "-A", cwd=ordner)
        self._git("-c", "user.email=t@beispiel.de", "-c", "user.name=Test",
                  "commit", "-m", "ins Remote", cwd=ordner)
        self._git("push", "-u", "origin", "ferner")
        self._weg_machen(ordner)
        self.assertTrue(wr.pruefe_admin_reste(self.haupt)[0],
                        "Alles liegt auf origin - das darf geraeumt werden.")

    def test_ohne_tun_bleibt_alles_stehen(self):
        self._weg_machen(self.wt)
        self.assertEqual(wr.raeume_admin_reste(self.haupt, tun=False), 0)
        self.assertEqual(len(wr.admin_reste(self.haupt)), 1, "Es wurde doch geraeumt.")

    def test_mit_tun_verschwindet_der_eintrag_und_der_branch_bleibt(self):
        self._weg_machen(self.wt)
        self.assertEqual(wr.raeume_admin_reste(self.haupt, tun=True), 0)
        self.assertEqual(wr.admin_reste(self.haupt), [])
        self.assertIn("zweig", self._git("branch", "--list", "zweig").stdout,
                      "Der Branch muss im Haupt-Baum weiterleben.")

    def test_ein_gelockter_rest_bleibt_liegen(self):
        # GEMESSEN: git fasst einen gelockten Eintrag beim prune nicht an.
        # Der Waechter prueft es selbst - und zwar BEVOR er die Erreichbarkeit
        # bewertet, denn ein Lock ist eine Absichtserklaerung von jemand.
        #
        # REIHENFOLGE (GEMESSEN, sonst scheitert schon `worktree lock`):
        # `git worktree lock` braucht den ORDNER. Ist er schon weg, endet der
        # Befehl mit Exit 128 und der Eintrag ist ungelockt - das Loeschen
        # kommt also zuletzt.
        self._git("worktree", "lock", "--reason", "Wartet auf Handarbeit", str(self.wt))
        roh = self._git("worktree", "list", "--porcelain").stdout
        self.assertIn("locked", roh, "Gegenprobe: der Eintrag ist gelockt")
        self._weg_machen(self.wt)
        ok, bericht, spuren = wr.pruefe_admin_reste(self.haupt)
        self.assertFalse(ok)
        self.assertEqual(spuren, [])
        self.assertIn("GELOCKT", " ".join(t for _, t in bericht))
        # Und er bleibt auch beim echten prune liegen - das Verhalten von
        # git ist hier die Erwartung, nicht die Behauptung.
        self.assertEqual(wr.raeume_admin_reste(self.haupt, tun=True), 0)
        self.assertEqual(len(wr.admin_reste(self.haupt)), 1,
                         "Ein gelockter Rest wurde geräumt - git fasst ihn nicht an.")

    def test_ein_prozess_auf_dem_pfad_blockiert(self):
        # Auch wenn der Ordner schon weg ist: ein Prozess, der seinen Pfad
        # noch in der Kommandozeile fuehrt, arbeitet darauf - er könnte ihn
        # gleich wieder anlegen.
        self._weg_machen(self.wt)
        kind = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(20)",
                                 str(self.wt / "AGENTS.md")])
        self.addCleanup(kind.kill)
        time.sleep(1.5)
        ok, bericht, spuren = wr.pruefe_admin_reste(self.haupt)
        if not wr.prozess_mit_pfad(self.wt):
            self.skipTest("Die Prozessliste nennt den Pfad nicht - nichts zu messen.")
        self.assertFalse(ok, "Ein laufender Prozess auf dem Pfad muss blockieren.")
        self.assertEqual(spuren, [])

    def test_ohne_haupt_kein_git_ist_unklar(self):
        # Kein Repo -> Unklar -> Exit 3, nicht "nichts zu tun".
        with mock.patch.object(wr, "admin_reste",
                               side_effect=wr.Unklar("kein Repo")):
            self.assertEqual(wr.raeume_admin_reste(self.haupt, tun=False), 3)

    def test_ohne_reste_ist_es_nur_ein_satz(self):
        self.assertEqual(wr.raeume_admin_reste(self.haupt, tun=False), 0)
        self.assertEqual(wr.raeume_admin_reste(self.haupt, tun=True), 0)

    def test_wenn_prune_neue_reste_erzeugt_wird_das_gemeldet(self):
        # `prune` nimmt ALLE faelligen Eintraege, auch die, die der
        # Waechter gar nicht gesehen hat. Erscheint danach etwas Neues, ist
        # das ein Befund - und kein "Erfolg".
        #
        # Nur der `prune`-Aufruf wird ersetzt; die Bestandsabfragen laufen
        # echt, sonst prueft der Test seinen eigenen Mock.
        echt = wr._git
        # Ein freier Rest muss da sein, sonst ruft der Wächter `prune`
        # gar nicht auf und der Test prueft nichts.
        self._weg_machen(self.wt)

        def mit_neuem_rest(*args, **kwargs):
            if args[:2] == ("worktree", "prune"):
                ordner = Path(self.tmp) / "wt2"
                self._git("worktree", "add", "-b", "zwei", str(ordner), "HEAD")
                import shutil
                shutil.rmtree(ordner, ignore_errors=True)
                return subprocess.CompletedProcess([], 0, "Removing worktrees/wt2", "")
            return echt(*args, **kwargs)

        with mock.patch.object(wr, "_git", side_effect=mit_neuem_rest):
            code = wr.raeume_admin_reste(self.haupt, tun=True)
        self.assertEqual(code, 1, "Ein fremder Rest wurde mitgenommen und kam nicht zur Sprache.")
        self.assertTrue(wr.admin_reste(self.haupt), "Gegenprobe: der zweite Rest ist da.")


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
