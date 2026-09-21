r"""Selbsttest der Gates-vor-dem-Commit (Tools/vor_dem_commit.py + Hooks).

Ein Hook ist nur so viel wert, wie er wirklich aufhaelt. Geprueft wird darum
nicht "das Skript laeuft", sondern: haelt es einen kaputten Commit auf, laesst
es einen guten durch, und kostet es nur dann einen Compiler, wenn wirklich
C++ im Spiel ist.

    python -m unittest discover -s Tools -p "test_vor_dem_commit.py"
"""
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import vor_dem_commit as vdc  # noqa: E402

WURZEL = Path(__file__).resolve().parent.parent


class CompilerBedarfTest(unittest.TestCase):
    """Gate 1 kostet Minuten - es darf nur laufen, wenn es etwas zu pruefen gibt."""

    def test_cpp_verlangt_den_compiler(self):
        for datei in ("Source/X.cpp", "Source/X.h", "X.Build.cs", "Y.inl"):
            self.assertTrue(vdc.braucht_compiler([datei]), datei)

    def test_werkzeuge_und_doku_verlangen_ihn_nicht(self):
        for datei in ("Tools/x.py", "AGENTS.md", "Config/DefaultEngine.ini",
                      "Tools/x.cmd", "sweep.sh"):
            self.assertFalse(vdc.braucht_compiler([datei]), datei)

    def test_eine_cpp_datei_unter_vielen_genuegt(self):
        self.assertTrue(vdc.braucht_compiler(
            ["AGENTS.md", "Tools/x.py", "Source/WiesbadenReal/GIS/Y.cpp"]))

    def test_grossschreibung_ist_egal(self):
        self.assertTrue(vdc.braucht_compiler(["Source/X.CPP"]))

    def test_leere_liste_braucht_nichts(self):
        self.assertFalse(vdc.braucht_compiler([]))


class NotausgangTest(unittest.TestCase):
    """Ein Wachposten ohne Tuer wird eingerissen, nicht benutzt."""

    def test_wb_keine_gates_ueberspringt_alles(self):
        alt = os.environ.get("WB_KEINE_GATES")
        os.environ["WB_KEINE_GATES"] = "1"
        try:
            self.assertEqual(vdc.hauptprogramm(["--stufe", "voll"]), 0)
        finally:
            if alt is None:
                os.environ.pop("WB_KEINE_GATES", None)
            else:
                os.environ["WB_KEINE_GATES"] = alt


class HookTest(unittest.TestCase):
    """Die Hooks selbst: vorhanden, ausfuehrbar, und sie rufen den Laeufer."""

    def hook(self, name):
        return WURZEL / "Tools" / "git-hooks" / name

    def test_beide_hooks_liegen_im_repo(self):
        for name in ("pre-commit", "pre-push"):
            self.assertTrue(self.hook(name).is_file(), name)

    def test_pre_commit_faehrt_die_schnelle_stufe(self):
        text = self.hook("pre-commit").read_text(encoding="utf-8")
        self.assertIn("vor_dem_commit.py", text)
        self.assertIn("--stufe schnell", text)
        self.assertIn("--gestaged", text)

    def test_pre_push_faehrt_die_volle_stufe(self):
        text = self.hook("pre-push").read_text(encoding="utf-8")
        self.assertIn("--stufe voll", text)

    def test_beide_nennen_den_notausgang(self):
        for name in ("pre-commit", "pre-push"):
            text = self.hook(name).read_text(encoding="utf-8")
            self.assertIn("no-verify", text, name)

    def test_sie_finden_die_wurzel_selbst(self):
        """Ein fest eingetragener Pfad ueberlebt keinen zweiten Klon."""
        for name in ("pre-commit", "pre-push"):
            text = self.hook(name).read_text(encoding="utf-8")
            self.assertIn("git rev-parse --show-toplevel", text, name)
            self.assertNotIn("C:", text, "%s traegt einen festen Pfad" % name)


class SaubereUmgebungTest(unittest.TestCase):
    """Ein Hook darf nicht in den Commit hineinwirken, den er pruefen soll."""

    def test_git_variablen_fallen_heraus(self):
        alt = dict(os.environ)
        os.environ["GIT_INDEX_FILE"] = "irgendwo/index"
        os.environ["GIT_DIR"] = "irgendwo/.git"
        try:
            sauber = vdc.saubere_umgebung()
            self.assertNotIn("GIT_INDEX_FILE", sauber)
            self.assertNotIn("GIT_DIR", sauber)
        finally:
            os.environ.clear()
            os.environ.update(alt)

    def test_alles_andere_bleibt_stehen(self):
        alt = os.environ.get("WB_PROBE")
        os.environ["WB_PROBE"] = "bleibt"
        try:
            self.assertEqual(vdc.saubere_umgebung().get("WB_PROBE"), "bleibt")
        finally:
            if alt is None:
                os.environ.pop("WB_PROBE", None)
            else:
                os.environ["WB_PROBE"] = alt


class HookModusTest(unittest.TestCase):
    """Der Hook muss AUSFUEHRBAR im Index stehen, nicht nur auf der Platte.

    Git fuer Windows ignoriert das x-Bit, darum faellt das hier nicht auf.
    Auf einem POSIX-Klon UEBERSPRINGT git einen nicht ausfuehrbaren Hook -
    mit einem Hinweis, und COMMITTET TROTZDEM. Der Auftrag "vor jedem
    Commit" waere dort still nicht erfuellt, und zwar fail-open: genau die
    Richtung, die ein Wachposten nicht haben darf.
    """

    def test_beide_hooks_stehen_ausfuehrbar_im_index(self):
        fertig = subprocess.run(
            ["git", "ls-files", "-s", "Tools/git-hooks/"],
            cwd=WURZEL, capture_output=True, text=True,
            env=vdc.saubere_umgebung())
        self.assertEqual(fertig.returncode, 0, fertig.stderr)
        zeilen = [z for z in fertig.stdout.splitlines() if z.strip()]
        self.assertEqual(len(zeilen), 2, "erwartet werden zwei Hooks")
        for z in zeilen:
            modus, rest = z.split(None, 1)
            self.assertEqual(modus, "100755",
                             "%s steht mit Modus %s im Index" % (rest.split()[-1], modus))


class Gate0BefehlTest(unittest.TestCase):
    """Gate 0 muss die VORGEMERKTEN Dateien bekommen, nicht selbst suchen.

    Gate 0 startet mit saubere_umgebung(), also ohne GIT_*. Das muss so
    bleiben. Ohne GIT_INDEX_FILE sieht ein eigener `git diff --cached` aber
    den ECHTEN Index - und der ist bei `git commit --only` leer, dem Weg,
    den ausliefern.py benutzt. Neu vorgemerkte Dateien entgingen dem Gate.
    """

    def test_vorgemerkte_dateien_werden_uebergeben(self):
        befehl = vdc.gate0_befehl(["Tools/neu.cmd", "Source/X.cpp"])
        self.assertIn("--dateien", befehl,
                      "Gate 0 bekommt die vorgemerkten Dateien nicht")
        self.assertIn("Tools/neu.cmd", befehl)
        self.assertIn("Source/X.cpp", befehl)
        self.assertLess(befehl.index("--dateien"), befehl.index("Tools/neu.cmd"))

    def test_ohne_vormerkung_keine_leeren_argumente(self):
        self.assertNotIn("--dateien", vdc.gate0_befehl([]))

    def test_die_abdichtung_bleibt_wirksam(self):
        """Die Randbedingung: GIT_* darf NICHT durchgereicht werden."""
        alt = dict(os.environ)
        os.environ["GIT_INDEX_FILE"] = "irgendwo/next-index-4711.lock"
        try:
            self.assertNotIn("GIT_INDEX_FILE", vdc.saubere_umgebung())
        finally:
            os.environ.clear()
            os.environ.update(alt)


class StufenZuordnungTest(unittest.TestCase):
    """WAS laeuft auf WELCHER Stufe - die Frage, um die es hier geht.

    Geprueft wird die Zuordnung selbst, ohne ein einziges Gate zu starten:
    ein Lauf-Doppel schreibt nur mit, was gefahren und was uebersprungen
    wurde. Ein Test, der die Gates wirklich faehrt, wuerde Minuten kosten und
    trotzdem nur dasselbe sagen.

    Die Python-Suiten waren 31 s von 32 s der schnellen Stufe und sind kein
    Gate der Release-Pipeline. Sie liegen jetzt auf der vollen Stufe - aber
    sie muessen DORT auch wirklich liegen, sonst waere aus "verschoben"
    unbemerkt "gestrichen" geworden.
    """

    class LaufDoppel:
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

    def zuordnung(self, stufe, dateien):
        doppel = self.LaufDoppel()
        alt = vdc.Lauf
        vdc.Lauf = lambda: doppel
        try:
            vdc.gates_fahren(stufe, dateien)
        finally:
            vdc.Lauf = alt
        return doppel

    @staticmethod
    def suiten(namen):
        return [n for n in namen if "Python" in n]

    def test_schnell_faehrt_die_suiten_nicht(self):
        d = self.zuordnung("schnell", ["Tools/x.py"])
        self.assertFalse(self.suiten(d.gefahren),
                         "die Python-Suiten laufen wieder vor jedem Commit")
        self.assertTrue(self.suiten(d.uebersprungen),
                        "die Python-Suiten fehlen ganz, statt uebersprungen zu werden")

    def test_voll_faehrt_die_suiten(self):
        d = self.zuordnung("voll", ["Tools/x.py"])
        self.assertTrue(self.suiten(d.gefahren),
                        "verschoben waere zu gestrichen geworden")
        self.assertFalse(self.suiten(d.uebersprungen))

    def test_die_schnelle_stufe_haelt_nur_die_pipeline_gates(self):
        """Gate 0 immer, Gate 1 nur bei C++ - und sonst nichts."""
        ohne = self.zuordnung("schnell", ["Tools/x.py"])
        self.assertEqual(ohne.gefahren, ["Gate 0  Engine-Pfade"])

        mit = self.zuordnung("schnell", ["Source/X.cpp"])
        self.assertEqual(len(mit.gefahren), 2)
        self.assertIn("Gate 0  Engine-Pfade", mit.gefahren)
        self.assertTrue(any("Gate 1" in n for n in mit.gefahren))

    def test_die_volle_stufe_laesst_nichts_aus(self):
        d = self.zuordnung("voll", ["Source/X.cpp"])
        self.assertEqual(d.uebersprungen, [],
                         "auf der vollen Stufe darf nichts uebersprungen werden")
        self.assertTrue(any("Gate 2+3" in n for n in d.gefahren))

    def test_build_release_faehrt_die_suiten_nicht_mit(self):
        """Der Grund, warum die volle Stufe sie SELBST fahren muss.

        Gate 2+3 ist build_release.cmd -GatesOnly. Wuerde das die Suiten
        mitnehmen, waere der Eintrag auf der vollen Stufe doppelt. Es nimmt
        sie nicht mit - und faende jemand das eines Tages heraus und
        entfernte den Eintrag, faende dieser Test es auch heraus.
        """
        text = (WURZEL / "Tools" / "build_release.ps1").read_text(
            encoding="utf-8", errors="replace")
        self.assertNotIn("unittest", text)


class EchterHookTest(unittest.TestCase):
    """Der Kern: haelt der Hook einen roten Commit WIRKLICH auf?

    Gegen ein echtes Wegwerf-Repo, nicht gegen eine Nachbildung. Ein Test,
    der nur die Hook-DATEI liest, wuerde jede Verdrahtungspanne uebersehen.
    """

    def setUp(self):
        self.repo = Path(tempfile.mkdtemp(prefix="wb_hook_"))
        self.addCleanup(shutil.rmtree, self.repo, ignore_errors=True)
        self.git("init", "-q", ".")
        self.git("config", "user.email", "t@t")
        self.git("config", "user.name", "T")
        (self.repo / "Tools" / "git-hooks").mkdir(parents=True)
        shutil.copy(WURZEL / "Tools" / "git-hooks" / "pre-commit",
                    self.repo / "Tools" / "git-hooks" / "pre-commit")
        self.git("config", "core.hooksPath", "Tools/git-hooks")

    def git(self, *args):
        # OHNE die GIT_*-Variablen eines etwaigen Hooks. Laeuft dieser Test
        # selbst aus einem pre-commit-Hook, zeigt GIT_INDEX_FILE auf den
        # Index des LAUFENDEN Commits - "git add -A" im Wegwerf-Repo schrieb
        # dann dorthin. Genau so stand am 21.09.2026 eine Wegwerfdatei im
        # Index des echten Commits.
        return subprocess.run(["git", *args], cwd=self.repo,
                              capture_output=True, text=True,
                              env=vdc.saubere_umgebung())

    def laeufer(self, exitcode):
        (self.repo / "Tools" / "vor_dem_commit.py").write_text(
            "import sys\nprint('nachgestellt')\nsys.exit(%d)\n" % exitcode,
            encoding="utf-8")

    def commits(self):
        fertig = self.git("rev-list", "--count", "HEAD")
        return int(fertig.stdout.strip()) if fertig.returncode == 0 else 0

    def test_rotes_gate_haelt_den_commit_auf(self):
        self.laeufer(1)
        (self.repo / "datei.txt").write_text("inhalt", encoding="utf-8")
        self.git("add", "-A")
        self.git("commit", "-m", "darf nicht durchkommen")
        self.assertEqual(self.commits(), 0, "der Commit kam trotz rotem Gate durch")

    def test_gruenes_gate_laesst_ihn_durch(self):
        self.laeufer(0)
        (self.repo / "datei.txt").write_text("inhalt", encoding="utf-8")
        self.git("add", "-A")
        self.git("commit", "-m", "soll durchkommen")
        self.assertEqual(self.commits(), 1, "ein gruener Lauf blockierte den Commit")

    def test_no_verify_ist_der_notausgang(self):
        self.laeufer(1)
        (self.repo / "datei.txt").write_text("inhalt", encoding="utf-8")
        self.git("add", "-A")
        self.git("commit", "--no-verify", "-m", "Notausgang")
        self.assertEqual(self.commits(), 1, "--no-verify kam nicht am Hook vorbei")


if __name__ == "__main__":
    unittest.main()
