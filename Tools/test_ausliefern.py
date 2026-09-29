"""Selbsttest des Auslieferns (Tools/ausliefern.py).

Jeder Test baut ein echtes, winziges Git-Repo im Temp-Verzeichnis - gegen
Attrappen waere nichts bewiesen, denn die Fallen stecken in git selbst.

Aufruf (aus der Projektwurzel):
    python -m unittest discover -s Tools -p "test_ausliefern.py"
"""
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ausliefern  # noqa: E402


def _ohne_git_umgebung():
    """Umgebung ohne GIT_*.

    Laeuft diese Suite aus einem pre-commit-Hook, zeigt GIT_INDEX_FILE auf
    den Index des LAUFENDEN Commits. Die Wegwerf-Repos hier rufen "git add" -
    das schrieb sonst dorthin statt in ihr eigenes Repo. Am 21.09.2026 stand
    so eine Wegwerfdatei im Index eines echten Commits.
    """
    return {k: v for k, v in os.environ.items() if not k.startswith("GIT_")}


def git(wurzel, *args):
    return subprocess.run(["git", *args], cwd=wurzel, capture_output=True,
                          text=True, encoding="utf-8", errors="replace",
                          check=True, env=_ohne_git_umgebung()).stdout


class BaumMitFremderArbeit(unittest.TestCase):
    """Ein Repo mit eigener und fremder Arbeit nebeneinander."""

    def setUp(self):
        self.wurzel = tempfile.mkdtemp(prefix="wb_ausliefern_")
        self.addCleanup(shutil.rmtree, self.wurzel, ignore_errors=True)
        git(self.wurzel, "init", "-q", "-b", "main")
        git(self.wurzel, "config", "user.email", "test@example.invalid")
        git(self.wurzel, "config", "user.name", "Test")
        # Ausgangsstand: vier Dateien.
        for name in ("meins_a.txt", "meins_b.txt", "fremd.txt", "wird_geloescht.txt"):
            self.schreiben(name, "start\n")
        git(self.wurzel, "add", "-A")
        git(self.wurzel, "commit", "-q", "-m", "Ausgangsstand")

    def schreiben(self, name, inhalt):
        pfad = os.path.join(self.wurzel, name)
        os.makedirs(os.path.dirname(pfad), exist_ok=True) if os.path.dirname(name) else None
        with open(pfad, "w", encoding="utf-8") as f:
            f.write(inhalt)

    def arbeit_anlegen(self):
        """Eigene Aenderungen UND fremde, die nicht mitgehen duerfen."""
        self.schreiben("meins_a.txt", "meine Aenderung\n")
        self.schreiben("meins_b.txt", "meine zweite\n")
        self.schreiben("fremd.txt", "HALBFERTIGE FREMDE ARBEIT\n")
        self.schreiben("fremd_neu.txt", "noch nicht mal verfolgt\n")
        os.remove(os.path.join(self.wurzel, "wird_geloescht.txt"))

    def dateien_im_kopf(self):
        kopf = git(self.wurzel, "rev-parse", "HEAD").strip()
        return ausliefern.commit_dateien(self.wurzel, kopf)


class FremdeArbeitBleibtLiegenTest(BaumMitFremderArbeit):
    def test_nur_die_gewollten_pfade_gehen_mit(self):
        self.arbeit_anlegen()
        ausliefern.befehl_commit(self.wurzel, ["meins_a.txt", "meins_b.txt"],
                                 nachricht="meins")
        self.assertEqual(self.dateien_im_kopf(), {"meins_a.txt", "meins_b.txt"})

    def test_fremde_arbeit_ist_hinterher_unveraendert_da(self):
        self.arbeit_anlegen()
        ausliefern.befehl_commit(self.wurzel, ["meins_a.txt"], nachricht="meins")
        with open(os.path.join(self.wurzel, "fremd.txt"), encoding="utf-8") as f:
            self.assertEqual(f.read(), "HALBFERTIGE FREMDE ARBEIT\n")
        offen = ausliefern.geaenderte_dateien(self.wurzel)
        self.assertIn("fremd.txt", offen)
        self.assertIn("fremd_neu.txt", offen)

    def test_fremdes_im_index_wird_nicht_mitgerissen(self):
        """Der gefaehrlichste Fall: jemand hat seine Arbeit schon gestaged."""
        self.arbeit_anlegen()
        git(self.wurzel, "add", "fremd.txt")      # fremder Agent hat gestaged
        ausliefern.befehl_commit(self.wurzel, ["meins_a.txt"], nachricht="meins")
        self.assertEqual(self.dateien_im_kopf(), {"meins_a.txt"})
        # ... und liegt danach immer noch gestaged bereit, unangetastet.
        gestaged = git(self.wurzel, "diff", "--cached", "--name-only").split()
        self.assertIn("fremd.txt", gestaged)


class GeloeschtePfadeTest(BaumMitFremderArbeit):
    """Falle 1: der Vorfall vom 20.09.2026."""

    def test_loeschung_und_aenderung_in_einem_commit(self):
        self.arbeit_anlegen()
        ausliefern.befehl_commit(
            self.wurzel, ["meins_a.txt", "wird_geloescht.txt"], nachricht="beides")
        self.assertEqual(self.dateien_im_kopf(), {"meins_a.txt", "wird_geloescht.txt"})

    def test_bereits_per_git_rm_entfernte_datei(self):
        self.arbeit_anlegen()
        git(self.wurzel, "rm", "-q", "--cached", "wird_geloescht.txt")
        ausliefern.befehl_commit(
            self.wurzel, ["meins_a.txt", "wird_geloescht.txt"], nachricht="beides")
        self.assertEqual(self.dateien_im_kopf(), {"meins_a.txt", "wird_geloescht.txt"})

    def test_so_ging_es_damals_schief(self):
        """Gegenprobe: der Vorfall, nachgestellt - und PRAEZISE.

        Die erste Fassung dieses Tests traf den falschen Fall und schlug fehl.
        Nachgemessen gibt es zwei, und nur einer ist gefaehrlich:

        * Datei nur im ARBEITSBAUM geloescht, im Index noch da:
          `git add` geht durch und nimmt die Loeschung mit. Harmlos.
        * Pfad in Index UND Arbeitsbaum weg (nach `git rm`):
          "fatal: pathspec did not match any files", Exitcode 128 - und
          NICHTS ist gestaged, auch die anderen Pfade nicht.

        Der zweite Fall ist der Vorfall vom 20.09.2026. Faellt dieser Test,
        hat git sein Verhalten geaendert und dieses Werkzeug darf einfacher
        werden.
        """
        self.arbeit_anlegen()
        # Harmlos: nur der Arbeitsbaum kennt die Loeschung.
        gutmuetig = subprocess.run(
            ["git", "add", "wird_geloescht.txt", "meins_a.txt"],
            cwd=self.wurzel, capture_output=True, text=True,
            errors="replace")
        self.assertEqual(gutmuetig.returncode, 0)
        self.assertEqual(
            sorted(git(self.wurzel, "diff", "--cached", "--name-only").split()),
            ["meins_a.txt", "wird_geloescht.txt"])

        # Gefaehrlich: der Pfad ist ueberall weg.
        git(self.wurzel, "reset", "-q")
        # KEIN reset hinter dem rm - im echten Vorfall gab es keins, und genau
        # dadurch war der Pfad in Index UND Arbeitsbaum weg.
        git(self.wurzel, "rm", "-q", "wird_geloescht.txt")
        ergebnis = subprocess.run(
            ["git", "add", "wird_geloescht.txt", "meins_a.txt"],
            cwd=self.wurzel, capture_output=True, text=True,
            errors="replace")
        self.assertNotEqual(ergebnis.returncode, 0, "git add muss abbrechen")
        self.assertIn("did not match any files", ergebnis.stderr)
        # UND DAS IST DER SCHADEN: im Index steht nur die Loeschung, die `git rm`
        # dort hinterlassen hat. Von den anderen Pfaden ist NICHTS gestaged -
        # ein `git commit` danach liefert eine einzelne Loeschung aus und sieht
        # dabei erfolgreich aus. So ist es am 20.09.2026 passiert.
        gestaged = sorted(git(self.wurzel, "diff", "--cached", "--name-only").split())
        self.assertEqual(gestaged, ["wird_geloescht.txt"])
        self.assertNotIn("meins_a.txt", gestaged,
                         "die eigentliche Arbeit blieb ungestaged - das war die Falle")

        # Und genau hier haelt das Werkzeug stand.
        ausliefern.befehl_commit(
            self.wurzel, ["meins_a.txt", "wird_geloescht.txt"], nachricht="beides")
        self.assertEqual(self.dateien_im_kopf(), {"meins_a.txt", "wird_geloescht.txt"})


class NeueDateiTest(BaumMitFremderArbeit):
    """Falle 5: `git commit --only` kennt unverfolgte Dateien nicht.

    Gefunden, als das Werkzeug sich selbst ausliefern sollte: es brach mit
    "did not match any file(s) known to git" ab, obwohl die Datei danebenlag.
    """

    def test_brandneue_datei_laesst_sich_ausliefern(self):
        self.arbeit_anlegen()
        self.schreiben("ganz_neu.txt", "frisch\n")
        ausliefern.befehl_commit(self.wurzel, ["ganz_neu.txt"], nachricht="neu")
        self.assertEqual(self.dateien_im_kopf(), {"ganz_neu.txt"})
        with open(os.path.join(self.wurzel, "ganz_neu.txt"), encoding="utf-8") as f:
            self.assertEqual(f.read(), "frisch\n", "der Inhalt muss mitgehen, nicht nur der Name")

    def test_fremde_unverfolgte_datei_bleibt_unverfolgt(self):
        self.arbeit_anlegen()
        self.schreiben("ganz_neu.txt", "frisch\n")
        ausliefern.befehl_commit(self.wurzel, ["ganz_neu.txt"], nachricht="neu")
        zustand = ausliefern.status_dateien(self.wurzel)
        self.assertTrue(zustand.get("fremd_neu.txt", "").startswith("?"),
                        "fremd_neu.txt darf nicht einmal bekannt gemacht werden")

    def test_neue_und_geaenderte_datei_gemeinsam(self):
        self.arbeit_anlegen()
        self.schreiben("ganz_neu.txt", "frisch\n")
        ausliefern.befehl_commit(
            self.wurzel, ["ganz_neu.txt", "meins_a.txt", "wird_geloescht.txt"],
            nachricht="alles drei")
        self.assertEqual(self.dateien_im_kopf(),
                         {"ganz_neu.txt", "meins_a.txt", "wird_geloescht.txt"})


class TippfehlerTest(BaumMitFremderArbeit):
    """Falle 4: ein Pfad ohne Aenderung committet lautlos nichts."""

    def test_pfad_ohne_aenderung_ist_ein_fehler(self):
        self.arbeit_anlegen()
        with self.assertRaises(ausliefern.Fehler) as f:
            ausliefern.befehl_commit(self.wurzel, ["meins_a.txt", "gibtsnicht.txt"],
                                     nachricht="x")
        self.assertIn("gibtsnicht.txt", str(f.exception))

    def test_und_committet_dann_auch_nichts(self):
        self.arbeit_anlegen()
        vorher = git(self.wurzel, "rev-parse", "HEAD").strip()
        with self.assertRaises(ausliefern.Fehler):
            ausliefern.befehl_commit(self.wurzel, ["gibtsnicht.txt"], nachricht="x")
        self.assertEqual(git(self.wurzel, "rev-parse", "HEAD").strip(), vorher)


class VerzeichnisTest(BaumMitFremderArbeit):
    """Falle 3: ein Verzeichnis nimmt alles mit, was darin liegt."""

    def test_verzeichnis_wird_auf_dateien_aufgeloest(self):
        os.makedirs(os.path.join(self.wurzel, "unterordner"))
        self.schreiben("unterordner/meins.txt", "a\n")
        self.schreiben("unterordner/fremdes.txt", "b\n")
        zuordnung, leer = ausliefern.aufloesen(self.wurzel, ["unterordner"])
        self.assertEqual(leer, [])
        self.assertEqual(zuordnung["unterordner"],
                         ["unterordner/fremdes.txt", "unterordner/meins.txt"],
                         "beide Dateien muessen VORHER sichtbar sein")


class MerkbuchUndPushTest(BaumMitFremderArbeit):
    """Die Meldung vor dem Push."""

    def setUp(self):
        super().setUp()
        # Eine "Ferne" als reines Repo daneben, damit push echt ist.
        self.ferne = tempfile.mkdtemp(prefix="wb_ferne_")
        self.addCleanup(shutil.rmtree, self.ferne, ignore_errors=True)
        git(self.ferne, "init", "-q", "--bare", "-b", "main")
        git(self.wurzel, "remote", "add", "origin", self.ferne)
        git(self.wurzel, "push", "-q", "-u", "origin", "main")

    def test_sauberer_commit_wird_durchgelassen(self):
        self.arbeit_anlegen()
        ausliefern.befehl_commit(self.wurzel, ["meins_a.txt"], nachricht="meins")
        beanstandet, zeilen = ausliefern.befehl_pruefen(self.wurzel, still=True)
        self.assertEqual(beanstandet, 0, "\n".join(zeilen))
        self.assertEqual(ausliefern.befehl_push(self.wurzel), 0)

    def test_fremde_datei_im_commit_wird_vor_dem_push_gemeldet(self):
        self.arbeit_anlegen()
        ausliefern.befehl_commit(self.wurzel, ["meins_a.txt"], nachricht="meins")
        # Jemand schiebt nachtraeglich Fremdes in denselben Commit.
        git(self.wurzel, "add", "fremd.txt")
        git(self.wurzel, "commit", "-q", "--amend", "--no-edit")
        # Nach --amend ist die Nummer neu -> unbekannt, und genau das muss
        # gemeldet werden statt durchzugehen (Falle 5).
        beanstandet, zeilen = ausliefern.befehl_pruefen(self.wurzel, still=True)
        self.assertEqual(beanstandet, 1)
        self.assertIn("nicht ueber dieses Werkzeug", "\n".join(zeilen))
        self.assertEqual(ausliefern.befehl_push(self.wurzel), 2, "Push muss verweigern")

    def test_fremder_commit_daneben_wird_gemeldet(self):
        self.arbeit_anlegen()
        ausliefern.befehl_commit(self.wurzel, ["meins_a.txt"], nachricht="meins")
        git(self.wurzel, "add", "fremd.txt")
        git(self.wurzel, "commit", "-q", "-m", "fremder Commit")
        beanstandet, zeilen = ausliefern.befehl_pruefen(self.wurzel, still=True)
        self.assertEqual(beanstandet, 1)
        self.assertIn("fremder Commit", "\n".join(zeilen))
        self.assertEqual(ausliefern.befehl_push(self.wurzel), 2)

    def test_trotzdem_pusht_nach_ausdruecklicher_ansage(self):
        self.arbeit_anlegen()
        git(self.wurzel, "add", "fremd.txt")
        git(self.wurzel, "commit", "-q", "-m", "fremder Commit")
        self.assertEqual(ausliefern.befehl_push(self.wurzel, trotzdem=True), 0)

    def test_merkbuch_liegt_ausserhalb_der_versionierung(self):
        self.arbeit_anlegen()
        ausliefern.befehl_commit(self.wurzel, ["meins_a.txt"], nachricht="meins")
        pfad = ausliefern.journal_pfad(self.wurzel)
        self.assertTrue(os.path.exists(pfad))
        self.assertIn(".git", pfad.replace("\\", "/").split("/"))


class MittenDrinTest(BaumMitFremderArbeit):
    """Falle: waehrend eines Merges nichts anfassen."""

    def test_merge_blockiert_das_ausliefern(self):
        git(self.wurzel, "checkout", "-q", "-b", "seite")
        self.schreiben("meins_a.txt", "von der Seite\n")
        git(self.wurzel, "commit", "-q", "-am", "Seite")
        git(self.wurzel, "checkout", "-q", "main")
        self.schreiben("meins_a.txt", "von main\n")
        git(self.wurzel, "commit", "-q", "-am", "Main")
        subprocess.run(["git", "merge", "seite"], cwd=self.wurzel, capture_output=True)
        self.assertIsNotNone(ausliefern.mitten_drin(self.wurzel))
        with self.assertRaises(ausliefern.Fehler) as f:
            ausliefern.befehl_commit(self.wurzel, ["meins_a.txt"], nachricht="x")
        self.assertIn("Merge", str(f.exception))


if __name__ == "__main__":
    unittest.main()
