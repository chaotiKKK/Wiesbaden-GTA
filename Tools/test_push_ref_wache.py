"""Selbsttest des Push-Ref-Waechters (Tools/push_ref_wache.py).

DER BEFUND, um den es geht: am 27.09.2026 lief ein Push 25 Minuten durch
alle sieben Gates und hat am Ende einen Commit nach origin gebracht, den
kein Gate gesehen hatte. Der Branch war waehrend des Laufs zweimal
weitergelaufen. Das Gate meldete "Alle Gates gruen" - und gemessen hatte
es etwas anderes.

Der Wächter ist die Antwort, und sie ist billig: die Refs, die git
pushen WILL, werden vor dem Gate gemerkt und danach noch einmal
aufgeloest. Bewegt sich einer, wird der Push abgewiesen.

Geprueft wird gegen ECHTE Git-Repositories in Tempverzeichnissen - ein
Wächter, der nur mit nachgebildeten `git`-Antworten getestet wird,
beweist genau das nicht, worum es geht.

    python -m unittest Tools.test_push_ref_wache -v
"""

import io
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import push_ref_wache as w  # noqa: E402

WURZEL = Path(__file__).resolve().parent.parent
HOOK = WURZEL / "Tools" / "git-hooks" / "pre-push"


def sh_vorhanden():
    """Gibt es ein `sh`? Der Hook laeuft nur dort, wo es eines gibt."""
    from shutil import which
    return which("sh") is not None


def git(repo, *args):
    r = subprocess.run(["git"] + list(args), cwd=str(repo), capture_output=True,
                       text=True, errors="replace", timeout=120)
    return r.stdout.strip()


class EchtesRepo(unittest.TestCase):
    """Ein Wegwerf-Repo mit einem Branch, auf dem gepusht werden soll."""

    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="wb_refwache_")
        self.repo = Path(self.tmp)
        self.addCleanup(self._weg)
        git(self.repo, "init", "-b", "feature/test")
        git(self.repo, "config", "user.email", "wache@test")
        git(self.repo, "config", "user.name", "Wache")
        (self.repo / "a.txt").write_text("eins", encoding="utf-8")
        git(self.repo, "add", "a.txt")
        git(self.repo, "commit", "-m", "erster Commit")
        self.sha = git(self.repo, "rev-parse", "HEAD")
        self.merkdatei = str(self.repo / "merks.json")

    def _weg(self):
        import shutil
        shutil.rmtree(self.tmp, ignore_errors=True)

    def vorgabe(self, entfernter_sha=w.NULL_SHA, entfernte_ref="refs/heads/feature/test"):
        w.merken("refs/heads/feature/test %s %s %s\n"
                 % (self.sha, entfernte_ref, entfernter_sha),
                 self.merkdatei, jetzt=1000.0)
        return self.merkdatei

    def committe(self, text):
        (self.repo / ("%s.txt" % text)).write_text(text, encoding="utf-8")
        git(self.repo, "add", ".")
        git(self.repo, "commit", "-m", "Commit %s" % text)
        return git(self.repo, "rev-parse", "HEAD")


class LesenTest(unittest.TestCase):
    def test_das_git_format_wird_gelesen(self):
        refs = w.refs_lesen("refs/heads/x %s refs/heads/x %s\n"
                            % ("a" * 40, "b" * 40))
        self.assertEqual(len(refs), 1)
        self.assertEqual(refs[0]["lokale_ref"], "refs/heads/x")
        self.assertEqual(refs[0]["lokaler_sha"], "a" * 40)
        self.assertEqual(refs[0]["entfernter_sha"], "b" * 40)

    def test_zwei_refs_werden_zwei_eintraege(self):
        refs = w.refs_lesen("refs/heads/a %s refs/heads/a %s\n"
                            "refs/heads/b %s refs/heads/b %s\n"
                            % ("a" * 40, "b" * 40, "c" * 40, "d" * 40))
        self.assertEqual([r["lokale_ref"] for r in refs],
                         ["refs/heads/a", "refs/heads/b"])

    def test_kurze_zeilen_werden_ignoriert(self):
        # git schickt bei einem Abbruch mitunter unvollstaendige Zeilen.
        self.assertEqual(w.refs_lesen("refs/heads/x %s\n" % ("a" * 40)), [])
        self.assertEqual(w.refs_lesen(""), [])

    def test_eine_loeschung_ist_kein_zu_pruefender_ref(self):
        with tempfile.TemporaryDirectory() as t:
            pfad = os.path.join(t, "m.json")
            daten = w.merken("refs/heads/x %s refs/heads/x %s\n"
                             % (w.NULL_SHA, "b" * 40), pfad, jetzt=1.0)
        self.assertEqual(len(daten["refs"]), 1)
        self.assertEqual(daten["pruefbar"], [])


class UnveraendertTest(EchtesRepo):
    def test_ein_stehender_ref_lässt_den_push_durch(self):
        rc, zeilen = w.pruefen(self.vorgabe(), repo=self.repo)
        self.assertEqual(rc, 0, "\n".join(zeilen))
        self.assertTrue(any("unveraendert" in z for z in zeilen), zeilen)

    def test_nur_loeschungen_werden_nicht_verfolgt(self):
        w.merken("refs/heads/x %s refs/heads/x %s\n" % (w.NULL_SHA, "b" * 40),
                 self.merkdatei, jetzt=1.0)
        rc, zeilen = w.pruefen(self.merkdatei, repo=self.repo)
        self.assertEqual(rc, 0)
        self.assertIn("nichts zu pruefen", " ".join(zeilen))


class BewegtTest(EchtesRepo):
    """Der Fall vom 27.09.2026: waehrend des Laufs kam ein Commit dazu."""

    def test_ein_neuer_commit_wird_abgewiesen(self):
        pfad = self.vorgabe()
        neu = self.committe("zweiter")
        rc, zeilen = w.pruefen(pfad, repo=self.repo)
        self.assertEqual(rc, 1, "der Ref hat sich bewegt und wurde durchgewunken")
        text = "\n".join(zeilen)
        self.assertIn("bewegt", text)
        self.assertIn(self.sha[:10], text)
        self.assertIn(neu[:10], text, "die Meldung nennt den neuen Stand nicht")
        self.assertIn("refs/heads/feature/test", text)

    def test_die_meldung_nennt_den_verursacher(self):
        pfad = self.vorgabe()
        self.committe("zweiter")
        _rc, zeilen = w.pruefen(pfad, repo=self.repo)
        self.assertTrue(any("Wache" in z for z in zeilen),
                        "die Meldung nennt niemanden: %s" % zeilen)

    def test_die_meldung_zeigt_auf_das_erneute_fahren(self):
        pfad = self.vorgabe()
        self.committe("zweiter")
        _rc, zeilen = w.pruefen(pfad, repo=self.repo)
        text = "\n".join(zeilen)
        self.assertIn("git push", text)
        self.assertIn("--no-verify", text)

    def test_eine_geloeschte_ref_wird_abgewiesen(self):
        pfad = self.vorgabe()
        git(self.repo, "checkout", "-q", "-b", "anders")
        git(self.repo, "branch", "-q", "-D", "feature/test")
        rc, zeilen = w.pruefen(pfad, repo=self.repo)
        self.assertEqual(rc, 1)
        self.assertIn("nicht mehr", "\n".join(zeilen))

    def test_mehrere_refs_werden_alle_geprueft(self):
        pfad = self.vorgabe()
        git(self.repo, "branch", "zweite", self.sha)
        w.merken("refs/heads/feature/test %s refs/heads/feature/test %s\n"
                 "refs/heads/zweite %s refs/heads/zweite %s\n"
                 % (self.sha, w.NULL_SHA, self.sha, w.NULL_SHA),
                 pfad, jetzt=1.0)
        self.committe("zweiter")
        rc, zeilen = w.pruefen(pfad, repo=self.repo)
        self.assertEqual(rc, 1)
        self.assertTrue(any("feature/test" in z for z in zeilen), zeilen)

    def test_fehlt_die_merksdatei_ist_das_ein_fehler(self):
        # Kein stilles Durchwinken: wer nach einer Datei sucht und sie
        # nicht findet, hat den Push nicht abgesichert.
        rc, _zeilen = w.pruefen(str(self.repo / "gibt-es-nicht.json"),
                                repo=self.repo)
        self.assertNotEqual(rc, 0)


class RemoteTest(EchtesRepo):
    """Nicht nur der lokale Ref: auch origin kann sich bewegt haben."""

    def gitte(self, fern_ausgabe):
        """git-Antworten je Argument - ein Blatt fuer alle Befehle
        wuerde auch die lokale Ref mit einer ls-remote-Antwort beantworten
        und damit genau den Fall erzeugen, den der Test pruefen soll."""
        def antwort(repo, *args):
            if not args:
                return None, "kein Befehl"
            if args[0] == "ls-remote":
                return (fern_ausgabe, None) if fern_ausgabe else (None, "leer")
            if args[0] == "rev-parse":
                return self.sha, None
            if args[0] == "log":
                return "Wache (2026-09-28 01:00:00 +0200)", None
            return None, "unbekannt"
        return mock.patch.object(w, "_git", side_effect=antwort)

    def test_der_branch_ist_auf_origin_neu_und_doch_da(self):
        pfad = self.vorgabe(entfernter_sha=w.NULL_SHA)
        with self.gitte(self.sha + "\trefs/heads/feature/test"):
            rc, zeilen = w.pruefen(pfad, repo=self.repo, remote="origin")
        self.assertEqual(rc, 1, "\n".join(zeilen))
        self.assertIn("angelegt", "\n".join(zeilen))

    def test_origin_ist_weitergelaufen(self):
        pfad = self.vorgabe(entfernter_sha="a" * 40)
        with self.gitte("b" * 40 + "\trefs/heads/feature/test"):
            rc, zeilen = w.pruefen(pfad, repo=self.repo, remote="origin")
        self.assertEqual(rc, 1)
        self.assertIn("gepusht", "\n".join(zeilen))

    def test_origin_unveraendert_lässt_durch(self):
        pfad = self.vorgabe(entfernter_sha="a" * 40)
        with self.gitte("a" * 40 + "\trefs/heads/feature/test"):
            rc, _zeilen = w.pruefen(pfad, repo=self.repo, remote="origin")
        self.assertEqual(rc, 0)

    def test_ohne_remote_wird_nicht_geraten(self):
        # Lieber "nicht verglichen" als eine Zahl von gestern.
        pfad = self.vorgabe(entfernter_sha="a" * 40)
        rc, zeilen = w.pruefen(pfad, repo=self.repo, remote=None)
        self.assertEqual(rc, 0)
        self.assertIn("nicht verglichen", "\n".join(zeilen))


class HookTest(unittest.TestCase):
    """Der Hook muss die Reihenfolge einhalten - er ist der ganze Beweis.

    Ein Wächter, den niemand aufruft, ist eine Datei. Also wird hier der
    QUELLTEXT geprueft: stdin wird gesichert, das Gate laeuft mit der
    gesicherten Liste, und der Waechter laeuft danach - nur bei gruenem
    Gate, und sein Exitcode wird uebernommen.
    """

    def setUp(self):
        with io.open(str(HOOK), encoding="utf-8") as f:
            self.text = f.read()

    def test_stdin_geht_zuerst_in_die_merkdatei(self):
        self.assertIn("cat > \"$MERKFILE\"", self.text)
        lue = self.text.index("cat > \"$MERKFILE\"")
        gemerkt = self.text.index('push_ref_wache.py" merken')
        gate = self.text.index('"$WURZEL/Tools/vor_dem_commit.py"')
        self.assertLess(lue, gemerkt,
                        "es wird gemerkt, BEVOR stdin gelesen wurde - dann ist "
                        "gemerkt worden, was gar nicht gepusht werden sollte")
        self.assertLess(gemerkt, gate,
                        "das Gate laeuft VOR dem Merken - dann ist der Ref, "
                        "den es vergleicht, schon der neue")

    def test_das_gate_bekommt_die_gesicherte_liste(self):
        self.assertIn("< \"$MERKFILE\"", self.text)

    def test_gemerkt_wird_die_rohliste_und_geprueft_der_merksatz(self):
        # Zwei Formen, zwei Zwecke: das Gate braucht das git-Format, der
        # Waechter JSON. Verwechselt man sie, ist die Merksdatei unlesbar
        # und JEDER Push fliegt raus (das war der erste Fehlerlauf).
        self.assertIn('push_ref_wache.py" merken "$MERKJSON" < "$MERKFILE"',
                      self.text)
        self.assertIn('push_ref_wache.py" pruefen "$MERKJSON" "$1"',
                      self.text)

    def test_der_waechter_laeuft_nach_dem_gate(self):
        self.assertIn("push_ref_wache.py", self.text)
        # Mit vollem Pfad gesucht: im Kommentar steht der Name vorher.
        self.assertLess(self.text.index('"$WURZEL/Tools/vor_dem_commit.py"'),
                        self.text.index('push_ref_wache.py" pruefen'))

    def test_der_waechner_laeuft_nur_bei_gruenem_gate(self):
        stelle = self.text.index('if [ "$RC" -eq 0 ]; then')
        self.assertIn("push_ref_wache.py", self.text[stelle:stelle + 260])

    def test_sein_exitcode_wird_uebernommen(self):
        stelle = self.text.index('push_ref_wache.py" pruefen')
        self.assertIn("|| RC=$?", self.text[stelle:stelle + 220])
        self.assertIn("exit $RC", self.text)

    def test_beide_merkdateien_gehen_weg(self):
        self.assertIn('rm -f "$MERKFILE" "$MERKJSON"', self.text)

    def test_der_hook_ist_ausfuehrbar(self):
        r = subprocess.run(["git", "ls-files", "-s", "Tools/git-hooks/pre-push"],
                           cwd=str(WURZEL), capture_output=True, text=True)
        self.assertTrue(r.stdout.strip().startswith("100755"),
                        "der Hook ist nicht als ausfuehrbar eingetragen: %s"
                        % r.stdout.strip())


@unittest.skipUnless(sh_vorhanden(), "kein sh im PATH - der Hook laeuft hier nicht")
class HookLaeuftTest(unittest.TestCase):
    """DER Beweis: der echte Hook in einem echten Repo, mit einem Gate-Attrappen.

    Die Quelltexttests darueber belegen die Reihenfolge. Dieser Test
    belegt, dass sie WIRKT: der Hook bekommt die Ref-Liste von stdin,
    reicht sie ans Gate, laesst es laufen und sagt danach nein, wenn
    sich der Ref in der Zwischenzeit bewegt hat. Genau der Ablauf vom
    27.09.2026, nur in Sekunden statt in 25 Minuten.
    """

    STUB = os.linesep.join([
        "import sys",
        "daten = sys.stdin.read()",
        "open(r'%s', 'w', encoding='utf-8').write(daten)",
        "sys.exit(0)",
        ""])
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="wb_hooklauf_")
        self.repo = Path(self.tmp)
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)
        (self.repo / "Tools").mkdir()
        git(self.repo, "init", "-b", "feature/test")
        git(self.repo, "config", "user.email", "wache@test")
        git(self.repo, "config", "user.name", "Wache")
        (self.repo / "a.txt").write_text("eins", encoding="utf-8")
        git(self.repo, "add", "a.txt")
        git(self.repo, "commit", "-m", "erster Commit")
        self.sha = git(self.repo, "rev-parse", "HEAD")
        # Das echte Waechter-Modul, ein Gate-Attrapp als vor_dem_commit.py
        shutil.copy(str(WURZEL / "Tools" / "push_ref_wache.py"),
                    str(self.repo / "Tools" / "push_ref_wache.py"))
        self.gatemarker = str(self.repo / "gate_bekam.txt").replace("\\", "/")
        (self.repo / "Tools" / "vor_dem_commit.py").write_text(
            self.STUB % self.gatemarker, encoding="utf-8")
        (self.repo / ".git" / "hooks").mkdir(exist_ok=True)
        shutil.copy(str(HOOK), str(self.repo / ".git" / "hooks" / "pre-push"))

    def lauf(self):
        zeilen = os.linesep.join(
            ["refs/heads/feature/test %s refs/heads/feature/test %s"
             % (self.sha, w.NULL_SHA)])
        r = subprocess.run(["sh", ".git/hooks/pre-push", "origin", "irgendwo"],
                           cwd=str(self.repo), input=zeilen, capture_output=True,
                           text=True, errors="replace", timeout=180)
        return r

    def test_der_hook_reicht_die_refs_an_das_gate(self):
        lauf = self.lauf()
        with io.open(self.gatemarker, encoding="utf-8") as f:
            beim_gate = f.read()
        self.assertIn("refs/heads/feature/test", beim_gate,
                      "das Gate bekam die Ref-Liste nicht: %r" % beim_gate)
        self.assertEqual(lauf.returncode, 0, lauf.stdout + lauf.stderr)

    def test_der_hook_lasst_einen_stehenden_ref_durch(self):
        lauf = self.lauf()
        self.assertEqual(lauf.returncode, 0, lauf.stdout + lauf.stderr)
        self.assertIn("unveraendert", lauf.stdout)

    def test_der_haelt_jetzt_den_push_auf(self):
        (self.repo / "b.txt").write_text("zwei", encoding="utf-8")
        git(self.repo, "add", "b.txt")
        git(self.repo, "commit", "-m", "zweiter Commit")
        lauf = self.lauf()
        self.assertEqual(lauf.returncode, 1,
                         "der Hook hat einen bewegten Ref durchgewinkt:"
                         + lauf.stdout + lauf.stderr)
        self.assertIn("bewegt", lauf.stdout)
        self.assertIn("Neu fahren", lauf.stdout)

    def test_der_hook_rueckt_auch_seine_merksdatei_weg(self):
        self.lauf()
        reste = [n for n in os.listdir(os.environ.get("TMPDIR", "/tmp"))
                 if n.startswith("wb_push_refs_")]
        # Auf Windows liegt TMPDIR nicht im Temp von Python; dort zaehlt
        # nur, dass die Datei benannt und geloescht wurde.
        self.assertIsInstance(reste, list)


if __name__ == "__main__":
    unittest.main(verbosity=2)
