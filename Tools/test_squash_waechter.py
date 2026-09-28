r"""Selbsttest: die Squash-Falle beim Push (Tools/squash_waechter.py).

Das Wegwerf-Repo spielt PR #16 nach: ein Branch wird per "Squash and merge"
nach main gebracht, lebt weiter, und sein naechster PR bekommt
Scheinkonflikte. Der Waechter muss das melden - und nach dem ours-Merge
schweigen.

    python -m unittest discover -s Tools -p "test_squash_waechter.py"
"""
import io
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gate_worktree as gw  # noqa: E402
import squash_waechter as sw  # noqa: E402

NULL = sw.NULL_SHA


class SquashRepo(unittest.TestCase):
    """main: A -> C (Squash von f1+f2); feature: A -> f1 -> f2 -> f3."""

    def git(self, *args):
        return subprocess.run(["git", *args], cwd=str(self.repo), check=True,
                              capture_output=True, text=True).stdout.strip()

    def schreibe(self, name, inhalt, nachricht):
        (self.repo / name).write_text(inhalt, encoding="utf-8")
        self.git("add", "-A")
        self.git("commit", "-qm", nachricht)
        return self.git("rev-parse", "HEAD")

    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.repo = self.tmp / "repo"
        self.repo.mkdir()
        self.git("init", "-q", "-b", "main")
        self.git("config", "user.email", "t@t")
        self.git("config", "user.name", "t")
        self.a = self.schreibe("a.txt", "1\n", "A")
        self.git("checkout", "-qb", "feature")
        self.f1 = self.schreibe("a.txt", "2\n", "f1")
        self.f2 = self.schreibe("b.txt", "neu\n", "f2")
        # Der Squash: EIN Commit auf main mit dem Dateibaum von f2.
        self.c = self.git("commit-tree", self.f2 + "^{tree}", "-p", self.a, "-m", "Feature (#12)")
        self.git("update-ref", "refs/remotes/origin/main", self.c)
        self.git("symbolic-ref", "refs/remotes/origin/HEAD", "refs/remotes/origin/main")
        # Der Branch lebt weiter und aendert dieselbe Datei.
        self.f3 = self.schreibe("a.txt", "3\n", "f3")

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def push(self, sha, ziel="refs/heads/feature", alt=None):
        return "refs/heads/feature %s %s %s\n" % (sha, ziel, alt or NULL)


class Erkennung(SquashRepo):

    def test_squash_wird_gefunden(self):
        self.assertEqual(sw.befunde(self.repo, self.f3, "refs/remotes/origin/main"),
                         [(self.c, "Feature (#12)", self.f2)])

    def test_der_pr_haette_scheinkonflikte(self):
        self.assertIs(sw.hat_konflikte(self.repo, self.f3, "refs/remotes/origin/main"), True)

    def test_nach_dem_ours_merge_schweigt_er(self):
        self.git("merge", "-q", "-s", "ours", "--no-edit", self.c)
        kopf = self.git("rev-parse", "HEAD")
        self.assertEqual(self.git("rev-parse", "HEAD^{tree}"), self.git("rev-parse", self.f3 + "^{tree}"),
                         "der ours-Merge darf keine Datei aendern")
        self.assertEqual(sw.befunde(self.repo, kopf, "refs/remotes/origin/main"), [])
        self.assertIs(sw.hat_konflikte(self.repo, kopf, "refs/remotes/origin/main"), False)

    def test_main_lief_nach_dem_squash_weiter(self):
        # Ein weiterer Commit auf main (D) - der Squash C wird trotzdem
        # erkannt, und nach dem ours-Merge NUR von C ist die Falle weg,
        # D bleibt ein normaler Merge (der Rat nennt ihn).
        self.git("checkout", "-q", "--detach", self.c)
        d = self.schreibe("c.txt", "main\n", "D")
        self.git("update-ref", "refs/remotes/origin/main", d)
        self.git("checkout", "-q", "feature")
        funde = sw.befunde(self.repo, self.f3, "refs/remotes/origin/main")
        self.assertEqual([f[0] for f in funde], [self.c])
        text = "\n".join(sw.meldung("refs/heads/feature", funde, True, "refs/remotes/origin/main"))
        self.assertIn("git merge origin/main", text)
        self.git("merge", "-q", "-s", "ours", "--no-edit", self.c)
        self.assertEqual(sw.befunde(self.repo, self.git("rev-parse", "HEAD"), "refs/remotes/origin/main"), [])

    def test_frischer_branch_ab_main_ist_sauber(self):
        self.git("checkout", "-q", "-b", "neu", self.c)
        neu = self.schreibe("d.txt", "x\n", "neu")
        self.assertEqual(sw.befunde(self.repo, neu, "refs/remotes/origin/main"), [])

    def test_normal_gemergter_branch_ist_sauber(self):
        # Merge-Commit statt Squash: die Branch-Commits SIND auf main.
        self.git("checkout", "-q", "--detach", self.a)
        self.git("merge", "-q", "--no-ff", "--no-edit", self.f2)
        self.git("update-ref", "refs/remotes/origin/main", self.git("rev-parse", "HEAD"))
        self.git("checkout", "-q", "feature")
        self.assertEqual(sw.befunde(self.repo, self.f3, "refs/remotes/origin/main"), [])


class Meldung(SquashRepo):

    def test_meldung_nennt_befund_und_handlung(self):
        zeilen = sw.pruefen(self.repo, self.push(self.f3))
        text = "\n".join(zeilen)
        self.assertIn("feature lebt nach einem Squash-Merge weiter", text)
        self.assertIn(self.c[:9], text)
        self.assertIn(self.f2[:9], text)
        self.assertIn("SCHEINKONFLIKTE", text)
        self.assertIn("git merge -s ours %s" % self.c[:9], text)

    def test_hinweis_druckt_als_hinweis_nicht_als_gate(self):
        with mock.patch("sys.stdout", io.StringIO()) as aus:
            sw.hinweis(self.repo, self.push(self.f3))
        self.assertIn("Squash-Falle (Hinweis, kein Gate)", aus.getvalue())

    def test_ohne_falle_bleibt_der_hook_still(self):
        self.git("merge", "-q", "-s", "ours", "--no-edit", self.c)
        with mock.patch("sys.stdout", io.StringIO()) as aus:
            self.assertEqual(sw.hinweis(self.repo, self.push(self.git("rev-parse", "HEAD"))), [])
        self.assertEqual(aus.getvalue(), "")

    def test_push_auf_main_selbst_wird_nicht_geprueft(self):
        self.assertEqual(sw.pruefen(self.repo, self.push(self.f3, ziel="refs/heads/main")), [])

    def test_loeschungen_und_tags_pruefen_nichts(self):
        text = ("(delete) %s refs/heads/feature %s\nrefs/tags/v1 %s refs/tags/v1 %s\n"
                % (NULL, self.f3, self.f3, NULL))
        self.assertEqual(sw.pruefen(self.repo, text), [])

    def test_ohne_origin_kein_vergleich(self):
        self.git("update-ref", "-d", "refs/remotes/origin/HEAD")
        self.git("update-ref", "-d", "refs/remotes/origin/main")
        self.assertEqual(sw.pruefen(self.repo, self.push(self.f3)), [])

    def test_unbekannte_shas_werden_still(self):
        # So ruft der Selbsttest von gate_worktree den Push auf: Fantasie-Shas.
        self.assertEqual(sw.pruefen(self.repo, self.push("a" * 40)), [])


class NieBlockieren(unittest.TestCase):
    """Ein Waechter, der den Push reisst, wird abgeschaltet - also darf er nie."""

    def test_fehler_im_waechter_wird_zur_zeile(self):
        with mock.patch.object(sw, "pruefen", side_effect=RuntimeError("kaputt")), \
                mock.patch("sys.stdout", io.StringIO()) as aus:
            zeilen = sw.hinweis(".", "egal")
        self.assertEqual(len(zeilen), 1)
        self.assertIn("kaputt", aus.getvalue())

    def test_fehlendes_modul_reisst_den_push_nicht(self):
        # Im Push-Worktree fehlt ein noch nicht committetes Werkzeug - wie
        # beim Plattenwaechter am 27.09.2026.
        with mock.patch.dict(sys.modules, {"squash_waechter": None}), \
                mock.patch("sys.stdout", io.StringIO()) as aus:
            self.assertEqual(gw.squash_hinweis(".", "egal"), [])
        self.assertIn("nicht ausgefuehrt", aus.getvalue())

    def test_push_meldet_vor_dem_warten_auf_den_lock(self):
        folge = []
        with mock.patch.object(gw, "squash_hinweis", side_effect=lambda *a: folge.append("squash")), \
                mock.patch.object(gw, "motor_sperre", side_effect=lambda name: folge.append("lock") or False), \
                mock.patch.object(gw, "git", return_value="baum\n"), \
                mock.patch("sys.stdout", io.StringIO()):
            gw.push_pruefen(Path(__file__).resolve().parent.parent,
                            "refs/heads/x %s refs/heads/x %s\n" % ("a" * 40, "b" * 40))
        self.assertEqual(folge, ["squash", "lock"])

    def test_ein_befund_blockiert_den_push_nicht(self):
        with mock.patch.object(gw, "squash_hinweis", return_value=["Falle!"]), \
                mock.patch.object(gw, "motor_sperre", return_value=True), \
                mock.patch.object(gw, "pruefen", return_value=0), \
                mock.patch.object(gw, "git", return_value="baum\n"), \
                mock.patch("sys.stdout", io.StringIO()):
            self.assertEqual(gw.push_pruefen(Path(__file__).resolve().parent.parent,
                                             "refs/heads/x %s refs/heads/x %s\n" % ("a" * 40, "b" * 40)), 0)


if __name__ == "__main__":
    unittest.main()
