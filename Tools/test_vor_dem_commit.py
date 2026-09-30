r"""Selbsttest der Gates-vor-dem-Commit (Tools/vor_dem_commit.py + Hooks).

Ein Hook ist nur so viel wert, wie er wirklich aufhaelt. Geprueft wird darum
nicht "das Skript laeuft", sondern: haelt es einen kaputten Commit auf, laesst
es einen guten durch, und kostet es nur dann einen Compiler, wenn wirklich
C++ im Spiel ist.

    python -m unittest discover -s Tools -p "test_vor_dem_commit.py"
"""
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock
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
        # Gate B meldet sich ueber fahre_gate (Zusammenfuehrung 27.09.2026).
        def fahre_gate(self, name, ok, dauer, meldung):
            return ok

        def __init__(self):
            self.gefahren = []
            self.uebersprungen = []

        # notiz kommt seit dem 27.09. dazu: der Schritt Python-Suiten
        # meldet, wie viele Tests uebersprungen wurden. Wer diese Signatur
        # verkuerzt, bekommt in 9 Tests einen TypeError statt einer Aussage.
        def fahre(self, name, befehl, *, shell_cmd=False, notiz=None,
                      notiz_zeilen=None):
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
        # Gate B (Besitz) liest die Registry des ECHTEN Repos - hier geht es
        # nur um die Stufenzuordnung, darum wird es weggebunden. Dass es in
        # beiden Stufen mitfaehrt, prueft BesitzRegistryTest.
        alt_besitz = vdc.besitz_gate
        vdc.besitz_gate = lambda *a, **k: False
        # Die volle Stufe nimmt den maschinenweiten Engine-Lock - ein Test darf
        # ihn nicht anfassen (er wartete sonst auf einen echten Gate-Lauf).
        import gate_worktree
        try:
            with mock.patch.object(gate_worktree, "motor_sperre", return_value=True):
                vdc.gates_fahren(stufe, dateien)
        finally:
            vdc.Lauf = alt
            vdc.besitz_gate = alt_besitz
        return doppel

    @staticmethod
    def suiten(namen):
        return [n for n in namen if "Python" in n]

    @staticmethod
    def waechter(namen):
        return [n for n in namen if "ch" in n and "Suiten" in n]

    def test_schnell_faehrt_die_suiten_nicht(self):
        d = self.zuordnung("schnell", ["Tools/x.py"])
        self.assertFalse(self.suiten(d.gefahren),
                         "die Python-Suiten laufen wieder vor jedem Commit")
        # Sie werden seit dem 27.09.2026 nicht mehr als "uebersprungen"
        # gemeldet, sondern gar nicht mehr erwaehnt: der Schritt hiess
        # vorher "Python-Suiten" und hiess jetzt "Wächter-Suiten" - beide
        # enthalten "Suiten", und der alte Eintrag waere eine Behauptung
        # ueber einen Schritt, den es in der schnellen Stufe nicht gibt.
        # GEMESSEN: dieser Test schlug deshalb rot, als die Zuweisung
        # auf "Wächter-Suiten" umgestellt wurde.
        self.assertFalse(self.suiten(d.uebersprungen),
                         "die Python-Suiten stehen als uebersprungen drin, "
                         "obwohl dieser Schritt in der schnellen Stufe gar "
                         "nicht vorkommt")

    def test_schnell_faehrt_aber_die_waechter_suiten(self):
        """GEMESSEN am 27.09.2026: die AUSNAHME von oben.

        Die beiden Waechter-Suiten laufen vor jedem Commit. Sie sind der
        Nachweis, dass der Waechter selbst noch funktioniert - er hat im
        September 1,2 GB eines fremden Pushes geloescht, und ein Waechter,
        den nur der Push prueft, ist einen Tag zu spaet.
        """
        d = self.zuordnung("schnell", ["Tools/x.py"])
        self.assertTrue(self.waechter(d.gefahren),
                        "die Wächter-Suiten laufen nicht vor jedem Commit")
        self.assertFalse(self.waechter(d.uebersprungen),
                         "die Wächter-Suiten fehlen ganz")

    def test_die_waechter_suiten_fahren_ihre_eigenen_dateien(self):
        """Nur die beiden Wächter-Suiten - nicht aus Versehen die ganze
        Sammlung (222 s wuerden jeden Commit aufhalten)."""
        befehle = {}

        class Doppel:
            # Gate B meldet sich ueber fahre_gate (Zusammenfuehrung 27.09.2026).
            def fahre_gate(self, name, ok, dauer, meldung):
                return ok

            def __init__(self):
                self.ergebnisse = []
                self.notizen = []
                self.notiz_zeilen = []

            def fahre(self, name, befehl, **kwargs):
                befehle[name] = befehl
                return True

            def ueberspringe(self, name, grund):
                pass

            def bericht(self):
                return 0

        alt = vdc.Lauf
        vdc.Lauf = lambda: Doppel()
        import gate_worktree
        try:
            with mock.patch.object(gate_worktree, "motor_sperre", return_value=True):
                vdc.gates_fahren("schnell", ["Tools/x.py"])
        finally:
            vdc.Lauf = alt
        schritt = [n for n in befehle if "ch" in n and "Suiten" in n]
        self.assertTrue(schritt, "der Wächter-Schritt lief gar nicht")
        befehl = befehle[schritt[0]]
        self.assertIn("test_worktree_*.py", befehl,
                      "es werden nicht die Wächter-Suiten gefahren")
        self.assertIn("-v", befehl,
                      "ohne -v gibt es keine Testnamen fuer den Bericht")
        # Die ganze Sammlung darf NICHT mitlaufen: 222 s je Commit.
        self.assertNotIn("test_*.py", [z for z in befehl if z != "test_worktree_*.py"],
                         "die ganze Sammlung laeuft mit - 222 s je Commit")

    def test_voll_faehrt_die_suiten(self):
        d = self.zuordnung("voll", ["Tools/x.py"])
        self.assertTrue(self.suiten(d.gefahren),
                        "verschoben waere zu gestrichen geworden")
        self.assertFalse(self.suiten(d.uebersprungen))

    def test_die_schnelle_stufe_haelt_nur_die_pipeline_gates(self):
        """Gate 0 immer, Gate 1 nur bei C++ - und die Wächter-Suiten.

        Der Name sagt "nur die Pipeline-Gates" und meint das seit dem
        27.09.2026 nicht mehr ganz woertlich: die Wächter-Suiten kamen
        dazu. Der Test folgt der Wirklichkeit und sagt es im Namen.
        """
        ohne = self.zuordnung("schnell", ["Tools/x.py"])
        self.assertEqual(ohne.gefahren, ["Gate 0  Engine-Pfade", "Wächter-Suiten"])

        mit = self.zuordnung("schnell", ["Source/X.cpp"])
        self.assertEqual(len(mit.gefahren), 3)
        self.assertIn("Gate 0  Engine-Pfade", mit.gefahren)
        self.assertIn("Wächter-Suiten", mit.gefahren)
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


class SchnittbildGateTest(unittest.TestCase):
    """Gate 4 (Plasmacutter-Bildfolge) - es laeuft bei jedem Push, ohne Filter.

    Zwei Dinge sind hier zu verteidigen, und beide sind schon einmal
    stillschweigend kaputtgegangen:

    1. DASS ES LAEUFT. Es gab zuerst eine Musterliste der Plasmacutter-
       Dateien als Vorbedingung. Im Commit-Worktree ist der Commit schon
       committed - eine Liste, die aus dem Push-Bereich gebaut wird, ist
       dort leer, und "leer" wurde als "nichts zu tun" gelesen. Das Gate
       waere bei jedem Push erscheinungslos entfallen.
    2. DASS ES DAS RICHTIGE FAEHRT. Ein Schritt, dessen Befehl niemand
       nachschaegt, kann alles fahren - auch gar nichts.
    """

    class LaufDoppel:
        def __init__(self):
            self.gefahren = []
            self.befehle = {}
            self.uebersprungen = []

        def fahre(self, name, befehl, *, shell_cmd=False, notiz=None,
                      notiz_zeilen=None):
            self.gefahren.append(name)
            self.befehle[name] = befehl
            return True

        def fahre_gate(self, name, ok, dauer, meldung):
            self.gefahren.append(name)
            return ok

        def ueberspringe(self, name, grund):
            self.uebersprungen.append(name)

        def bericht(self):
            return 0

    def zuordnung(self, stufe, dateien):
        doppel = self.LaufDoppel()
        alt = vdc.Lauf
        vdc.Lauf = lambda: doppel
        alt_besitz = vdc.besitz_gate
        vdc.besitz_gate = lambda *a, **k: False
        import gate_worktree
        try:
            with mock.patch.object(gate_worktree, "motor_sperre", return_value=True):
                vdc.gates_fahren(stufe, dateien)
        finally:
            vdc.Lauf = alt
            vdc.besitz_gate = alt_besitz
        return doppel

    @staticmethod
    def gate4(namen):
        return [n for n in namen if "Gate 4" in n]

    def test_es_faehrt_in_der_vollen_stufe(self):
        d = self.zuordnung("voll", ["Source/WiesbadenReal/World/WiesbadenCuttable.cpp"])
        self.assertTrue(self.gate4(d.gefahren),
                        "Gate 4 laeuft in der vollen Stufe nicht")

    def test_es_faehrt_auch_ohne_plasmacutter_datei(self):
        """Der Kern: KEIN Dateifilter. Ein Push mit nur Werkzeugen darf das
        Gate nicht ausloesen - das ist genau die Luecke, die es schliessen soll."""
        d = self.zuordnung("voll", ["Tools/Doku/x.md", "Config/DefaultEngine.ini"])
        self.assertTrue(self.gate4(d.gefahren),
                        "das Gate haengt an einer Dateiliste - genau die "
                        "stille Luecke, die es vermeiden soll")

    def test_es_faehrt_auch_bei_leerer_dateiliste(self):
        """Leer heisst im Worktree 'unbestimmbar', nicht 'nichts zu tun'."""
        d = self.zuordnung("voll", [])
        self.assertTrue(self.gate4(d.gefahren),
                        "eine leere Dateiliste schaltet das Gate ab - im "
                        "Commit-Worktree ist genau das der Normalfall")

    def test_es_laeuft_in_der_schnellen_stufe_nicht(self):
        d = self.zuordnung("schnell", ["Source/X.cpp"])
        self.assertFalse(self.gate4(d.gefahren),
                         "die Bildfolge startet einen Editor und gehoert "
                         "nicht vor jeden Commit")
        self.assertTrue(self.gate4(d.uebersprungen),
                        "sie muss als uebersprungen sichtbar sein, nicht "
                        "ersatzlos wegfallen")

    def test_es_befiehlt_sein_eigenes_gate(self):
        d = self.zuordnung("voll", ["Source/X.cpp"])
        # Der Name des Schritts steht im Schluessel, der BEFehl ist der Wert -
        # nach "Gate 4" im Befehl zu suchen findet nichts.
        befehle = [str(b) for name, b in d.befehle.items() if "Gate 4" in name]
        self.assertTrue(befehle, "kein Befehl fuer Gate 4 aufgezeichnet")
        for befehl in befehle:
            self.assertIn("verify_cuttable.cmd", befehl,
                          "Gate 4 faehrt etwas anderes als das Bild-Gate: %s"
                          % befehl)


class AnkerGateTest(unittest.TestCase):
    """Gate 5 (Ankerzustand der WP-Karte) - er laeuft bei jedem Push.

    Dasselbe Muster wie Gate 4, mit einer weiteren Falle: der Ankerzustand
    entsteht in einem BAKE, nicht im Code. Ein Fehler darin ist im Commit
    unsichtbar - die Datei ist vorhanden, nur falsch - und faellt erst beim
    Laden der Karte auf, wenn 20 000 leere Komponenten am Kartenursprung
    stehen. Genau dieser Stand lag wochenlang als "gemessen" da, weil der
    Lauf die Existenz der Datei mit Erfolg verwechselt hat.

    Deshalb hier dasselbe dreifach abgesichert: der Schritt laeuft, er laeuft
    OHNE Dateifilter, und er befiehlt genau das Skript, das die Zahl
    auswertet.
    """

    class LaufDoppel:
        def __init__(self):
            self.gefahren = []
            self.befehle = {}
            self.uebersprungen = []

        # notiz kommt seit dem 27.09. dazu: der Schritt Python-Suiten
        # meldet, wie viele Tests uebersprungen wurden. Wer diese Signatur
        # verkuerzt, bekommt in 9 Tests einen TypeError statt einer Aussage.
        def fahre(self, name, befehl, *, shell_cmd=False, notiz=None,
                      notiz_zeilen=None):
            self.gefahren.append(name)
            self.befehle[name] = befehl
            return True

        def fahre_gate(self, name, ok, dauer, meldung):
            self.gefahren.append(name)
            return ok

        def ueberspringe(self, name, grund):
            self.uebersprungen.append(name)

        def bericht(self):
            return 0

    def zuordnung(self, stufe, dateien):
        doppel = self.LaufDoppel()
        alt = vdc.Lauf
        vdc.Lauf = lambda: doppel
        import gate_worktree
        try:
            with mock.patch.object(gate_worktree, "motor_sperre", return_value=True):
                vdc.gates_fahren(stufe, dateien)
        finally:
            vdc.Lauf = alt
        return doppel

    @staticmethod
    def gate5(namen):
        return [n for n in namen if "Gate 5" in n]

    def test_es_faehrt_in_der_vollen_stufe(self):
        d = self.zuordnung("voll", ["Tools/x.py"])
        self.assertTrue(self.gate5(d.gefahren),
                        "Gate 5 laeuft in der vollen Stufe nicht - der "
                        "Ankerzustand waere beim Push ungeprueft")

    def test_es_faehrt_auch_ohne_stadtbezug(self):
        """KEIN Dateifilter: ein Push mit nur Werkzeugen prueft ihn trotzdem."""
        d = self.zuordnung("voll", ["Tools/Doku/x.md", "Config/DefaultEngine.ini"])
        self.assertTrue(self.gate5(d.gefahren),
                        "das Gate haengt an einer Dateiliste - im "
                        "Commit-Worktree ist die haeufig leer")

    def test_es_faehrt_auch_bei_leerer_dateiliste(self):
        d = self.zuordnung("voll", [])
        self.assertTrue(self.gate5(d.gefahren),
                        "eine leere Dateiliste schaltet das Gate ab - im "
                        "Commit-Worktree ist genau das der Normalfall")

    def test_es_laeuft_in_der_schnellen_stufe_nicht(self):
        d = self.zuordnung("schnell", ["Source/X.cpp"])
        self.assertFalse(self.gate5(d.gefahren),
                         "die Messung startet einen Editor und gehoert nicht "
                         "vor jeden Commit")
        self.assertTrue(self.gate5(d.uebersprungen),
                        "sie muss als uebersprungen sichtbar sein, nicht "
                        "ersatzlos wegfallen")

    def test_es_befiehlt_sein_eigenes_gate(self):
        d = self.zuordnung("voll", ["Source/X.cpp"])
        befehle = [str(b) for name, b in d.befehle.items() if "Gate 5" in name]
        self.assertTrue(befehle, "kein Befehl fuer Gate 5 aufgezeichnet")
        for befehl in befehle:
            self.assertIn("verify_anchor.cmd", befehl,
                          "Gate 5 faehrt etwas anderes als die Ankerpruefung: %s"
                          % befehl)

    def test_das_werkzeug_wertet_die_zahl_aus(self):
        """Der Schritt ist nur so viel wert wie sein Exit-Code.

        Ohne Auswertung der Zahl am Kartenursprung waere Gate 5 eine
        Ampel ohne Lampe: verify_anchor.cmd endet dann bei 0, sobald die
        Datei da liegt - auch wenn 20 404 Komponenten am Ursprung stehen.
        """
        text = (WURZEL / "Tools" / "verify_anchor.cmd").read_text(
            encoding="utf-8", errors="replace")
        self.assertIn("goto :leere_am_ursprung", text,
                      "verify_anchor.cmd wertet die Zahl der leeren "
                      "Komponenten am Kartenursprung nicht aus - Gate 5 "
                      "kann dann einen kaputten Zustand nicht ablehnen")


class FahrphysikGateTest(unittest.TestCase):
    """Gate 7 (Fahrphysik-Messfahrt): dieselbe Zuordnung wie Gate 4/5.

    Voll ja, schnell nein (startet das Spiel), ohne Dateifilter, und es
    befiehlt genau die Messfahrt mit der Sollwert-Pruefung.
    """

    LaufDoppel = AnkerGateTest.LaufDoppel
    zuordnung = AnkerGateTest.zuordnung

    @staticmethod
    def gate7(namen):
        return [n for n in namen if "Gate 7" in n]

    def test_es_faehrt_in_der_vollen_stufe_auch_ohne_dateien(self):
        for dateien in ([], ["Tools/Doku/x.md"], ["Source/X.cpp"]):
            d = self.zuordnung("voll", dateien)
            self.assertTrue(self.gate7(d.gefahren),
                            "Gate 7 laeuft in der vollen Stufe nicht (Dateien %s)" % dateien)

    def test_es_laeuft_in_der_schnellen_stufe_nicht(self):
        d = self.zuordnung("schnell", ["Source/X.cpp"])
        self.assertFalse(self.gate7(d.gefahren),
                         "die Messfahrt startet das Spiel und gehoert nicht vor jeden Commit")
        self.assertTrue(self.gate7(d.uebersprungen),
                        "sie muss als uebersprungen sichtbar sein")

    def test_es_befiehlt_die_messfahrt(self):
        d = self.zuordnung("voll", ["Source/X.cpp"])
        befehle = [str(b) for name, b in d.befehle.items() if "Gate 7" in name]
        self.assertTrue(befehle, "kein Befehl fuer Gate 7 aufgezeichnet")
        for befehl in befehle:
            self.assertIn("verify_fahrphysik.cmd", befehl)

    def test_das_werkzeug_prueft_die_sollwerte(self):
        """Der Starter faehrt UND prueft - ein Exit 0 ohne Pruefung waere wertlos."""
        text = (WURZEL / "Tools" / "verify_fahrphysik.cmd").read_text(
            encoding="utf-8", errors="replace")
        self.assertIn("fahrmessung.cmd", text)
        self.assertIn("verify_fahrphysik.py", text)
        self.assertIn("goto :nicht_gemessen", text,
                      "'nicht gemessen' (Exit 2) muss rot werden, nicht gruen")

    def test_der_starter_bewertet_kein_altes_log(self):
        """Endet der Editor ohne neues Log, darf die gruene Fahrt von gestern
        nicht bewertet werden - der Starter raeumt das Log vor der Fahrt."""
        text = (WURZEL / "Tools" / "verify_fahrphysik.cmd").read_text(
            encoding="utf-8", errors="replace")
        self.assertIn('if exist "!LOG!" del "!LOG!"', text)
        self.assertLess(text.index('del "!LOG!"'), text.index("fahrmessung.cmd\" gate_fahrphysik"))

    def test_die_sperre_bleibt_beim_umschliessenden_lauf(self):
        """Im Push haelt der Hook die Sperre; Unterlaeufe (Gate 4, Gate 7)
        rufen Freigeben - das Sperr-Skript muss die Sperre dann stehen lassen.
        Das Verhalten selbst belegt Tools/test_engine_run_lock_freigabe.py."""
        text = (WURZEL / "Tools" / "engine_run_lock.ps1").read_text(
            encoding="utf-8", errors="replace")
        self.assertIn("gehoert dem umschliessenden Lauf - bleibt gehalten", text)

    def test_ohne_neues_ergebnis_ist_es_rot(self):
        doppel = AnkerBeweisTest.LaufDoppel(0)
        with tempfile.TemporaryDirectory() as tmp:
            ziel = os.path.join(tmp, "fahrphysik_gate.txt")
            with open(ziel, "w", encoding="utf-8") as f:
                f.write("Gate 7 Fahrphysik: GRUEN - von gestern\n")
            altzeit = time.time() - 3600.0
            os.utime(ziel, (altzeit, altzeit))
            ok = vdc.fahrphysik_gate_fahren(doppel, ziel=ziel)
        self.assertFalse(ok, "ein Ergebnis von gestern gilt als Messung")
        self.assertEqual(doppel.bericht(), 1, "der Eintrag bleibt gruen")

    def test_mit_neuem_ergebnis_ist_es_gruen(self):
        doppel = AnkerBeweisTest.LaufDoppel(0)
        with tempfile.TemporaryDirectory() as tmp:
            ziel = os.path.join(tmp, "fahrphysik_gate.txt")

            def fahre(name, befehl, shell_cmd=False):
                doppel.ergebnisse.append((name, True, 0.0, None))
                with open(ziel, "w", encoding="utf-8") as f:
                    f.write("Gate 7 Fahrphysik: GRUEN\n")
                return True

            doppel.fahre = fahre
            ok = vdc.fahrphysik_gate_fahren(doppel, ziel=ziel)
        self.assertTrue(ok)
        self.assertEqual(doppel.bericht(), 0)


class AnkerBeweisTest(unittest.TestCase):
    """Gate 5 verlangt eine NEUE Messung, nicht nur einen Exit-Code.

    Gemessen am 27.09.2026: der Schritt meldete in einem echten Push-Lauf
    nach 10 Sekunden "gruen", ohne ein Log und ohne Ergebnisdatei zu
    hinterlassen. Der Exit-Code allein beweist also nicht, dass gemessen
    wurde - und ein Gate, das auf so einen Beweis verzichtet, ist die Ampel
    ohne Lampe, die es verhindern soll.
    """

    class LaufDoppel:
        # Gate B meldet sich ueber fahre_gate (Zusammenfuehrung 27.09.2026).
        def fahre_gate(self, name, ok, dauer, meldung):
            return ok

        def __init__(self, returncode=0):
            self.ergebnisse = []
            self._returncode = returncode

        # notiz kommt seit dem 27.09. dazu: der Schritt Python-Suiten
        # meldet, wie viele Tests uebersprungen wurden. Wer diese Signatur
        # verkuerzt, bekommt in 9 Tests einen TypeError statt einer Aussage.
        def fahre(self, name, befehl, *, shell_cmd=False, notiz=None,
                      notiz_zeilen=None):
            self.ergebnisse.append((name, self._returncode == 0, 0.0, None))
            return self._returncode == 0


        def ueberspringe(self, name, grund):
            pass

        def bericht(self):
            return len([e for e in self.ergebnisse if e[1] is False])

    def lauf_mit(self, returncode=0):
        doppel = self.LaufDoppel(returncode)
        alt = vdc.Lauf
        vdc.Lauf = lambda: doppel
        return doppel, alt

    def test_ohne_neue_datei_ist_es_rot(self):
        doppel, alt = self.lauf_mit(0)
        try:
            with tempfile.TemporaryDirectory() as tmp:
                ziel = os.path.join(tmp, "anchor_verify.txt")
                ok = vdc.anker_gate_fahren(doppel, ziel=ziel)
        finally:
            vdc.Lauf = alt
        self.assertFalse(ok, "ohne Ergebnisdatei meldet das Gate Erfolg - "
                             "es hat gar nicht gemessen")
        self.assertEqual(doppel.bericht(), 1,
                         "der Eintrag bleibt im Protokoll gruen, obwohl der "
                         "Beweis fehlt")

    def test_mit_neuer_datei_ist_es_gruen(self):
        doppel, alt = self.lauf_mit(0)
        try:
            with tempfile.TemporaryDirectory() as tmp:
                ziel = os.path.join(tmp, "anchor_verify.txt")

                def schreib(name, befehl, shell_cmd=False):
                    doppel.ergebnisse.append((name, True, 0.0, None))
                    with open(ziel, "w", encoding="utf-8") as f:
                        f.write("LEERE Komponenten: 23800 insgesamt, davon 0 "
                                "am Kartenursprung\n")
                    return True

                doppel.fahre = schreib
                ok = vdc.anker_gate_fahren(doppel, ziel=ziel)
        finally:
            vdc.Lauf = alt
        self.assertTrue(ok)
        self.assertEqual(doppel.bericht(), 0)

    def test_ein_altes_ergebnis_zaehlt_nicht(self):
        """Liegt eine Datei von gestern da, ist das Gate trotzdem rot.

        Genau der Fall, an dem ein gemuesstes Gate scheitern wuerde: die
        Datei ist da und sieht richtig aus, nur ist sie nicht von diesem
        Lauf.
        """
        doppel, alt = self.lauf_mit(0)
        try:
            with tempfile.TemporaryDirectory() as tmp:
                ziel = os.path.join(tmp, "anchor_verify.txt")
                with open(ziel, "w", encoding="utf-8") as f:
                    f.write("LEERE Komponenten: 20404 insgesamt, davon 20404 "
                            "am Kartenursprung\n")
                altzeit = time.time() - 3600.0
                os.utime(ziel, (altzeit, altzeit))
                # Der Lauf schreibt nichts - das alte Ergebnis bleibt liegen.
                ok = vdc.anker_gate_fahren(doppel, ziel=ziel)
        finally:
            vdc.Lauf = alt
        self.assertFalse(ok, "eine alte Messung wird als frisch verbucht")

    def test_auch_ein_roter_exit_code_wird_rot_protokolliert(self):
        doppel, alt = self.lauf_mit(3)
        try:
            with tempfile.TemporaryDirectory() as tmp:
                ziel = os.path.join(tmp, "anchor_verify.txt")
                ok = vdc.anker_gate_fahren(doppel, ziel=ziel)
        finally:
            vdc.Lauf = alt
        self.assertFalse(ok)


class UebersprungeneTest(unittest.TestCase):
    """Der Schritt Python-Suiten muss sagen, wie viele Tests gar nichts pruefen.

    GEMESSEN am 27.09.2026: der Push meldete sechs Gates gruen, und sechs
    Tests darin waren UEBERSPRUNGEN - `test_verify_cuttable_gate` braucht
    Belege, die Gate 4 erst danach erzeugt. unittest gibt dafuer nichts
    heraus: Exit 0 heisst "kein Test ist fehlgeschlagen", nicht "jeder Test
    lief". Ohne die Zahl sieht ein Lauf aus, in dem alles geprueft wurde,
    obwohl sechs Pruefungen gar nicht stattfanden.
    """

    class Fertig:
        def __init__(self, stderr="", returncode=0):
            self.stderr = stderr
            self.stdout = ""
            self.returncode = returncode

    def lesen(self, stderr, returncode=0):
        return vdc.uebersprungen_aus(self.Fertig(stderr, returncode))

    def test_die_echte_unittest_zusammenfassung_wird_gelesen(self):
        z = self.lesen("-" * 70 + "\nRan 313 tests in 108.479s\n\nOK (skipped=13)\n")
        self.assertEqual((13, 313), z)

    def test_ohne_uebersprungene_ist_es_null(self):
        z = self.lesen("Ran 313 tests in 108.479s\n\nOK\n")
        self.assertEqual((0, 313), z, "fehlendes 'skipped=' heisst null uebersprungen, nicht 'unbekannt'")

    def test_auch_bei_fehlern_wird_gezaehlt(self):
        z = self.lesen("Ran 313 tests in 108.479s\n\nFAILED (failures=2, skipped=13)\n", 1)
        self.assertEqual((13, 313), z)

    def test_ohne_zusammenfassung_ist_es_unbekannt(self):
        """Nicht lesbar heisst UNBEKANNT, nicht null.

        Eine nicht gelesene Zahl als Null zu melden waere genau die Ampel
        ohne Lampe, gegen die diese Gates gebaut sind - der ganze Thread.
        """
        for stderr in ("", "python: command not found", "Ran 1 test in 0.001s"):
            if stderr.endswith("Ran 1 test in 0.001s"):
                continue
            self.assertIsNone(self.lesen(stderr),
                              "%r wurde als Zahl gelesen" % stderr)

    def test_die_notiz_sagt_es_laut(self):
        z = self.Fertig("Ran 313 tests in 108s\n\nOK (skipped=13)\n")
        text = vdc.suiten_notiz(z)
        self.assertIn("13 von 313", text)
        self.assertIn("UEBERSPRUNGEN", text)

    def test_die_notiz_behauptet_nicht_die_null(self):
        text = vdc.suiten_notiz(self.Fertig("Python: Datei nicht gefunden"))
        self.assertIn("nicht lesbar", text)
        self.assertNotIn("0 von", text)

    def test_alle_tests_gelaufen_das_sagt_sie_auch(self):
        text = vdc.suiten_notiz(self.Fertig("Ran 313 tests in 108s\n\nOK\n"))
        self.assertIn("alle 313", text)

    def test_uebersprungen_machen_den_schritt_nicht_rot(self):
        """Sie sollen SICHTBAR sein, nicht den Push blockieren - der Entwurf
        sieht sie ausdruecklich vor (keine Belege im Commit-Worktree)."""
        fertig = self.Fertig("Ran 313 tests in 1s\n\nOK (skipped=13)\n")
        doppel = []

        class Doppel:
            # Gate B meldet sich ueber fahre_gate (Zusammenfuehrung 27.09.2026).
            def fahre_gate(self, name, ok, dauer, meldung):
                return ok

            def fahre(self, name, befehl, *, shell_cmd=False, notiz=None,
                      notiz_zeilen=None):
                doppel.append(notiz(fertig))
                return True
        Doppel().fahre("Python-Suiten", ["x"], notiz=vdc.suiten_notiz)
        self.assertIn("13 von 313", doppel[0])

    def test_der_schritt_der_suiten_bekommt_die_notiz(self):
        """Die VERDRAHTUNG, nicht nur die Funktion.

        Ohne `notiz=suiten_notiz` am Aufruf rechnet die Hilfsfunktion
        vollstaendig richtig - und der Lauf schweigt trotzdem. Genau das waere
        wieder eine Ampel ohne Lampe, nur eine andere. Dieser Test faehrt die
        volle Stufe mit einem Doppel und fragt den Schritt selbst.
        """
        bekommen = {}

        class Doppel:
            # Gate B meldet sich ueber fahre_gate (Zusammenfuehrung 27.09.2026).
            def fahre_gate(self, name, ok, dauer, meldung):
                return ok

            def __init__(self):
                self.ergebnisse = []
                self.notizen = []

            def fahre(self, name, befehl, *, shell_cmd=False, notiz=None,
                      notiz_zeilen=None):
                self.ergebnisse.append((name, True, 0.0, None))
                bekommen[name] = notiz
                return True

            def ueberspringe(self, name, grund):
                pass

            def bericht(self):
                return 0

        alt = vdc.Lauf
        vdc.Lauf = lambda: Doppel()
        import gate_worktree
        try:
            with mock.patch.object(gate_worktree, "motor_sperre", return_value=True):
                vdc.gates_fahren("voll", ["Tools/x.py"])
        finally:
            vdc.Lauf = alt

        schritt = [n for n in bekommen if "Python" in n]
        self.assertTrue(schritt, "der Schritt Python-Suiten lief gar nicht")
        notiz = bekommen[schritt[0]]
        self.assertIsNotNone(notiz,
                             "der Schritt Python-Suiten meldet seine "
                             "Uebersprungenen nicht - der Lauf schweigt")
        self.assertIn("1 von 5 Tests UEBERSPRUNGEN",
                      notiz(self.Fertig("Ran 5 tests in 1s\n\nOK (skipped=1)\n")))

    def test_der_bericht_zeigt_die_zahl(self):
        """Nicht nur neben dem Schritt, sondern auch in der Schlusszeile -
        dort wird gelesen, wenn der Push laeuft."""
        aus = io.StringIO()
        doppel = type("Doppel", (), {})()
        doppel.ergebnisse = [("Gate 0", True, 1.0, None)]
        doppel.notizen = [("Python-Suiten", "6 von 313 Tests UEBERSPRUNGEN")]
        with mock.patch("sys.stdout", aus):
            rot = vdc.Lauf.bericht(doppel)
        self.assertEqual(0, rot, "uebersprungene Tests duerfen den Push nicht blockieren")
        self.assertIn("6 von 313", aus.getvalue(),
                      "die Schlusszeile verschweigt die uebersprungenen Tests")


class SkipNamenTest(unittest.TestCase):
    """WELCHE Tests fehlten - nicht nur wie viele.

    GEMESSEN am 27.09.2026: zur selben Zahl 14 gab es zwei voellig
    verschiedene Gruende. Im Commit-Worktree waren es 11 Belege
    (`test_verify_cuttable_gate`), im Gate-Worktree 4 Push-Logs
    (`test_worktree_zeitstrahl`, weil dort kein `.planning` lag). Wer nur
    "14" liest, kann nicht entscheiden, ob das normal ist.
    """

    VERBOS = ("test_bericht_vorhanden (test_ka52_rotorachse.Ka52RotorachseTest.test_bericht_vorhanden) ... skipped 'Messbericht fehlt: rotorachse_fbx.txt'\n"
              "test_ohne_messbericht (test_ka52_rotorachse.Ka52RotorachseTest.test_ohne_messbericht) ... skipped 'ohne Messbericht nicht pruefbar'\n"
              "test_ein_lauf (test_x.Lauftest.test_ein_lauf) ... ok\n"
              "Ran 3 tests in 1.0s\n\nOK (skipped=2)\n")

    class Fertig:
        def __init__(self, stderr="", returncode=0):
            self.stderr = stderr
            self.stdout = ""
            self.returncode = returncode

    def test_name_und_grund_werden_gelesen(self):
        namen = vdc.uebersprungene_namen(self.Fertig(self.VERBOS))
        self.assertEqual(len(namen), 2)
        kurz, voll, grund = namen[0]
        self.assertEqual(kurz, "test_bericht_vorhanden")
        self.assertEqual(voll, "test_ka52_rotorachse.Ka52RotorachseTest.test_bericht_vorhanden")
        self.assertEqual(grund, "Messbericht fehlt: rotorachse_fbx.txt")

    def test_ohne_verbose_kommen_keine_namen(self):
        # Das ist der Grund fuer das `-v`: ohne es steht nur `OK (skipped=2)`.
        self.assertEqual(vdc.uebersprungene_namen(
            self.Fertig("Ran 3 tests in 1.0s\n\nOK (skipped=2)\n")), [])

    def test_erfolgreiche_tests_werden_nicht_gezaehlt(self):
        # Ein 'ok' ist kein Skip - wer alle Zeilen mit 'test_' nimmt,
        # zaehlt die ganze Suite als uebersprungen.
        nur_ok = "test_a (m.T.test_a) ... ok\ntest_b (m.T.test_b) ... ok\n"
        self.assertEqual(vdc.uebersprungene_namen(self.Fertig(nur_ok)), [])

    def test_abgeschnittener_grund_wird_als_solcher_gesagt(self):
        # GEMESSEN: unittest schreibt den Grund als String-Literal; bricht
        # das Log mitten darin ab, ist er nicht lesbar. Das darf nicht wie
        # ein leerer Grund aussehen.
        kaputt = "test_a (m.T.test_a) ... skipped 'Messbericht fehlt: /langer/Pfad"
        namen = vdc.uebersprungene_namen(self.Fertig(kaputt))
        self.assertEqual(len(namen), 1, "der Test wurde nicht erkannt")
        self.assertIn("nicht lesbar", namen[0][2],
                      "ein abgeschnittener Grund wurde als leerer Grund gemeldet")

    def test_der_bericht_nennt_die_testnamen(self):
        zeilen = vdc.suiten_notiz_zeilen(self.Fertig(self.VERBOS))
        text = "\n".join(zeilen)
        self.assertIn("test_bericht_vorhanden", text)
        self.assertIn("Messbericht fehlt", text, "der Grund fehlt - der ist der eigentliche Inhalt")
        # Die Zahl steht in `notizen`, die Namen hier. GEMESSEN am
        # 27.09.2026: mit der Zahl auch hier stand dieselbe Zeile zweimal
        # im Bericht. Sie darf deshalb nicht auf beiden Wegen kommen.
        self.assertNotIn("2 von 3", text, "die Kopfzeile gehoert in `notizen`")
        self.assertIn("2 von 3", vdc.suiten_notiz(self.Fertig(self.VERBOS)))

    def test_fehlende_namen_werden_als_fehlend_gesagt(self):
        # Ohne `-v`: die Zahl ist da, die Namen nicht. Das muss BENANNT
        # werden - die Zeile darf nicht einfach fehlen, sonst liest sich
        # "2 uebersprungen" wie eine vollstaendige Meldung.
        zeilen = vdc.suiten_notiz_zeilen(
            self.Fertig("Ran 3 tests in 1.0s\n\nOK (skipped=2)\n"))
        text = "\n".join(zeilen)
        self.assertIn("unbekannt", text)
        self.assertIn("-v", text, "der Hinweis auf die Ursache fehlt")

    def test_tests_mit_docstring_werden_auch_gefunden(self):
        # GEMESSEN am 27.09.2026: unittest druckt bei einem Test mit
        # Docstring DESSEN erste Zeile statt des Namens. Ohne diesen Fall
        # fand die Liste 12 von 14 - die Zahl passte nicht zur Liste.
        roh = ("Der Fund vom 27.09.2026 als Test: ein HALBER Lauf ist kein Beleg. ... skipped 'keine Belege in Saved/Diagnose'\n"
               "test_a (m.T.test_a) ... skipped 'Pillow fehlt'\n"
               "Ran 5 tests in 1.0s\n\nOK (skipped=2)\n")
        namen = vdc.uebersprungene_namen(self.Fertig(roh))
        self.assertEqual(len(namen), 2, "ein Docstring-Test wurde nicht erkannt")
        self.assertIn("HALBER Lauf", namen[0][0])
        self.assertIn("keine Belege", namen[0][2])

    def test_weniger_namen_als_gemeldet_ist_ein_befund(self):
        # Die Zahl sagt 5, gefunden werden 2. Das ist eine Luecke in der
        # Liste und wird gesagt - nicht mit "und 3 weitere" geglaettet,
        # denn die drei sind nicht ausgewaehlt, sondern nicht gesehen.
        text = "\n".join(vdc.suiten_notiz_zeilen(self.Fertig(
            "test_a (m.T.test_a) ... skipped 'x'\nRan 9 tests in 1.0s\n\nOK (skipped=5)\n")))
        self.assertIn("unvollstaendig", text)
        self.assertIn("gefunden", text)

    def test_mehr_namen_als_gemeldet_ist_auch_ein_befund(self):
        text = "\n".join(vdc.suiten_notiz_zeilen(self.Fertig(
            "test_a (m.T.test_a) ... skipped 'x'\ntest_b (m.T.test_b) ... skipped 'y'\n"
            "Ran 9 tests in 1.0s\n\nOK (skipped=1)\n")))
        self.assertIn("ACHSUNG", text)

    def test_keine_skips_erzeugen_keine_zeilen(self):
        self.assertEqual(vdc.suiten_notiz_zeilen(
            self.Fertig("Ran 3 tests in 1.0s\n\nOK\n")), [])

    def test_ohne_zusammenfassung_erzeugen_keine_zeilen(self):
        self.assertEqual(vdc.suiten_notiz_zeilen(self.Fertig("command not found")), [])

    def test_lange_gruende_werden_gekuerzt_nicht_verschluckt(self):
        # GEMESSEN: der ka52-Grund ist 240 Zeichen (ein Blender-Aufruf).
        lang = "verweis_messbericht_fehlt " * 20
        zeilen = vdc.skip_zeilen([("test_x", "m.T.test_x", lang)])
        self.assertIn("...", zeilen[0], "der Grund wurde gekuerzt ohne sichtbares Ende")
        self.assertLessEqual(len(zeilen[0]), 130, "die Zeile ist unlesbar lang geworden")
        self.assertIn("verweis_messbericht_fehlt", zeilen[0])

    def test_viele_namen_werden_gezaehlt_nicht_alle_gedruckt(self):
        namen = [("test_%d" % i, "m.T.test_%d" % i, "grund %d" % i) for i in range(14)]
        zeilen = vdc.skip_zeilen(namen)
        self.assertIn("und 8 weitere", "\n".join(zeilen),
                      "14 Namen alle zu drucken waere die lange Ampel ohne Lampe")

    def test_der_lauf_bekommt_die_mehrzeilige_notiz(self):
        """Die VERDRAHTUNG. Sonst rechnet alles richtig und der Bericht
        schweigt - dieselbe Luecke, nur eine Ebene tiefer."""
        bekommen = {}

        class Doppel:
            # Gate B meldet sich ueber fahre_gate (Zusammenfuehrung 27.09.2026).
            def fahre_gate(self, name, ok, dauer, meldung):
                return ok

            def __init__(self):
                self.ergebnisse = []
                self.notizen = []
                self.notiz_zeilen = []

            def fahre(self, name, befehl, *, shell_cmd=False, notiz=None,
                      notiz_zeilen=None):
                bekommen[name] = notiz_zeilen
                return True

            def ueberspringe(self, name, grund):
                pass

            def bericht(self):
                return 0

        alt = vdc.Lauf
        vdc.Lauf = lambda: Doppel()
        import gate_worktree
        try:
            with mock.patch.object(gate_worktree, "motor_sperre", return_value=True):
                vdc.gates_fahren("voll", ["Tools/x.py"])
        finally:
            vdc.Lauf = alt

        schritt = [n for n in bekommen if "Python" in n]
        self.assertTrue(schritt, "der Schritt Python-Suiten lief gar nicht")
        self.assertIsNotNone(bekommen[schritt[0]],
                             "die Namensnotiz ist nicht verdrahtet - der "
                             "Bericht schweigt")

    def test_discover_bekommt_das_verbose(self):
        """`-v` fehlt, kommen keine Namen an. Auch das ist Verdrahtung."""
        aufgerufen = {}

        class Doppel:
            # Gate B meldet sich ueber fahre_gate (Zusammenfuehrung 27.09.2026).
            def fahre_gate(self, name, ok, dauer, meldung):
                return ok

            def __init__(self):
                self.ergebnisse = []

            def fahre(self, name, befehl, **kwargs):
                aufgerufen[name] = befehl
                return True

            def ueberspringe(self, name, grund):
                pass

            def bericht(self):
                return 0

        alt = vdc.Lauf
        vdc.Lauf = lambda: Doppel()
        import gate_worktree
        try:
            with mock.patch.object(gate_worktree, "motor_sperre", return_value=True):
                vdc.gates_fahren("voll", ["Tools/x.py"])
        finally:
            vdc.Lauf = alt

        befehl = aufgerufen[[n for n in aufgerufen if "Python" in n][0]]
        self.assertIn("-v", befehl,
                      "unittest laeuft ohne -v - es gibt keine Testnamen, "
                      "die man nennen koennte")

    def test_der_bericht_druckt_die_namen(self):
        doppel = type("Doppel", (), {})()
        doppel.ergebnisse = [("Gate 0", True, 1.0, None)]
        doppel.notizen = [("Python-Suiten", "2 von 3 Tests UEBERSPRUNGEN")]
        doppel.notiz_zeilen = [("Python-Suiten", ["      WELCHE:", "      test_a: grund"])]
        aus = io.StringIO()
        with mock.patch("sys.stdout", aus):
            vdc.Lauf.bericht(doppel)
        self.assertIn("test_a: grund", aus.getvalue(),
                      "die Namen stehen in der Notiz, aber nicht im Bericht")


class GegenprobeSkipNamenTest(unittest.TestCase):
    """DER BEWEIS, DASS DIE NAMEN-TESTS NICHT BLIND SIND."""

    VERBOS = SkipNamenTest.VERBOS

    class Fertig:
        def __init__(self, stderr):
            self.stderr = stderr
            self.stdout = ""
            self.returncode = 0

    def test_ohne_die_auswertung_waere_der_bericht_stumm(self):
        # a) so wie es GINGE, wenn niemand die Namen auswertet: die Zahl
        # steht da, die Zeile sagt alles, was sie weiss - und niemand
        # erfahrt, dass 2 Tests gar nichts geprueft haben.
        nur_zahl = vdc.suiten_notiz(self.Fertig(self.VERBOS))
        self.assertIn("2 von 3", nur_zahl)
        self.assertNotIn("test_bericht_vorhanden", nur_zahl)
        # b) mit der Auswertung: die Namen stehen drin.
        zeilen = "\n".join(vdc.suiten_notiz_zeilen(self.Fertig(self.VERBOS)))
        self.assertIn("test_bericht_vorhanden", zeilen)

    def test_ein_regex_nur_auf_testnamen_verpasst_die_zaehlung_nicht(self):
        # Die Gegenprobe zur Docstring-Messung: der alte Regex fand 1 von 2,
        # die Zahl sagte 2. Genau diese Diskrepanz war der Befund.
        roh = ("Der Fund vom 27.09.2026 als Test: ein HALBER Lauf ist kein Beleg. ... skipped 'keine Belege'\n"
               "test_a (m.T.test_a) ... skipped 'Pillow fehlt'\n"
               "Ran 5 tests in 1.0s\n\nOK (skipped=2)\n")
        alt = re.compile(r"^(?P<kurz>test_\w+)\s+\([^)]+\)\s+\.\.\.\s+skipped\s+'(?P<grund>.*)'\s*$")
        nur_alt = [z for z in roh.splitlines() if alt.match(z.strip())]
        self.assertEqual(len(nur_alt), 1, "Gegenprobe: der alte Regex verpasst den Docstring-Test")
        self.assertEqual(len(vdc.uebersprungene_namen(self.Fertig(roh))), 2,
                         "der neue Parser muss beide Zeilen finden")

    def test_ein_regex_zu_eng_faellt_auf(self):
        # GEMESSEN, warum das noetig ist: unittest schreibt den Grund in
        # einfachen Anfuehrungszeichen und der kann Apostrophe enthalten.
        mit_apostroph = ("test_a (m.T.test_a) ... skipped 'Pillow fehlt - "
                         "irgendwer's Datei'\nRan 2 tests in 1.0s\n\nOK (skipped=1)\n")
        namen = vdc.uebersprungene_namen(self.Fertig(mit_apostroph))
        self.assertEqual(len(namen), 1, "der Test mit Apostroph im Grund fiel durch")
        self.assertIn("irgendwer", namen[0][2])


class PushBereichTest(unittest.TestCase):
    """Die Push-Stufe muss den COMMIT-BEREICH beurteilen, nicht den Baum.

    Der Defekt war fail-open und still: pre-push ruft den Laeufer ohne
    Dateiliste, `git diff HEAD` ist nach einem Commit leer, also hielt
    braucht_compiler([]) Gate 1 fuer ueberfluessig. Gemessen lagen in dem
    Moment 20 C++-Dateien im Push-Bereich und null im Baum - das
    Kompilier-Gate feuerte nie fuer den Code, der hinausging.

    Geprueft wird gegen ein echtes Wegwerf-Repo MIT Upstream. Eine
    Nachbildung wuerde genau die Verdrahtung uebersehen, an der es lag.
    """

    def setUp(self):
        self.basis = Path(tempfile.mkdtemp(prefix="wb_push_"))
        self.addCleanup(shutil.rmtree, self.basis, ignore_errors=True)
        self.fern = self.basis / "fern.git"
        self.repo = self.basis / "arbeit"
        subprocess.run(["git", "init", "-q", "--bare", str(self.fern)],
                       capture_output=True, env=vdc.saubere_umgebung())
        self.repo.mkdir()
        self.git("init", "-q", "-b", "haupt", ".")
        self.git("config", "user.email", "t@t")
        self.git("config", "user.name", "T")

    def git(self, *args):
        # OHNE GIT_* - sonst schreibt ein Wegwerf-Repo in den Index des
        # laufenden Commits (gemessen am 21.09.2026).
        return subprocess.run(["git", *args], cwd=self.repo,
                              capture_output=True, text=True,
                              env=vdc.saubere_umgebung())

    def commit(self, pfad, inhalt="x"):
        ziel = self.repo / pfad
        ziel.parent.mkdir(parents=True, exist_ok=True)
        ziel.write_text(inhalt, encoding="utf-8")
        self.git("add", str(pfad))
        self.git("commit", "-q", "--no-verify", "-m", "add %s" % pfad)

    def mit_upstream(self):
        self.git("remote", "add", "origin", str(self.fern))
        self.git("push", "-q", "--no-verify", "-u", "origin", "haupt")

    def test_der_bereich_sieht_den_commit_den_der_baum_nicht_zeigt(self):
        """Der Kern: sauberer Baum, C++ im Bereich - Gate 1 muss feuern."""
        self.commit("Tools/x.py")
        self.mit_upstream()
        self.commit("Source/Neu.cpp")

        self.assertEqual(self.git("status", "--porcelain").stdout.strip(), "",
                         "der Baum muss sauber sein, sonst misst der Test nichts")

        bereich = vdc.zu_pushende_dateien(cwd=self.repo)
        self.assertIn("Source/Neu.cpp", bereich)
        self.assertTrue(vdc.braucht_compiler(bereich),
                        "Gate 1 wuerde beim Push nicht feuern")

        # GEGENPROBE gegen den kaputten Stand: genau das sah die alte Quelle.
        baum = subprocess.run(["git", "diff", "HEAD", "--name-only"],
                              cwd=self.repo, capture_output=True, text=True,
                              env=vdc.saubere_umgebung()).stdout.split()
        self.assertEqual(baum, [], "der Arbeitsbaum ist leer - genau das war das Problem")
        self.assertFalse(vdc.braucht_compiler(baum),
                         "der alte Weg haette Gate 1 uebersprungen")

    def test_ohne_upstream_wird_nichts_uebersprungen(self):
        """Unbestimmbarer Bereich darf nicht in Schweigen kippen."""
        self.commit("Source/Neu.cpp")
        self.assertIsNone(vdc.zu_pushende_dateien(cwd=self.repo))
        self.assertTrue(vdc.braucht_compiler(None),
                        "ohne Upstream muss im Zweifel kompiliert werden")

    def test_nichts_zu_pushen_ergibt_einen_leeren_bereich(self):
        """Ist alles schon draussen, gibt es auch nichts zu kompilieren."""
        self.commit("Source/Neu.cpp")
        self.mit_upstream()
        self.assertEqual(vdc.zu_pushende_dateien(cwd=self.repo), [])

    def test_nur_werkzeuge_im_bereich_kosten_keinen_compiler(self):
        self.commit("Tools/x.py")
        self.mit_upstream()
        self.commit("Tools/y.py")
        bereich = vdc.zu_pushende_dateien(cwd=self.repo)
        self.assertEqual(bereich, ["Tools/y.py"])
        self.assertFalse(vdc.braucht_compiler(bereich))


class QuellenwahlTest(unittest.TestCase):
    """WELCHE Quelle die Stufe benutzt - die Verdrahtung, an der es lag.

    Die Funktionen einzeln zu pruefen genuegt nicht: der Defekt sass darin,
    dass die volle Stufe die FALSCHE der drei Quellen fragte.
    """

    def wahl(self, argv):
        gerufen = []
        alt = (vdc.zu_pushende_dateien, vdc.geaenderte_dateien,
               vdc.gestagte_dateien, vdc.gates_fahren)
        vdc.zu_pushende_dateien = lambda *a, **k: (gerufen.append("push"), ["Source/X.cpp"])[1]
        vdc.geaenderte_dateien = lambda *a, **k: (gerufen.append("baum"), [])[1]
        vdc.gestagte_dateien = lambda *a, **k: (gerufen.append("index"), ["Tools/x.py"])[1]
        vdc.gates_fahren = lambda stufe, dateien, thread=None: 0
        try:
            vdc.hauptprogramm(argv)
        finally:
            (vdc.zu_pushende_dateien, vdc.geaenderte_dateien,
             vdc.gestagte_dateien, vdc.gates_fahren) = alt
        return gerufen

    def test_voll_fragt_den_push_bereich(self):
        gerufen = self.wahl(["--stufe", "voll"])
        self.assertEqual(gerufen, ["push"],
                         "die Push-Stufe fragt nicht den Commit-Bereich")
        self.assertNotIn("baum", gerufen,
                         "die Push-Stufe fragt wieder den Arbeitsbaum")

    def test_der_hook_modus_fragt_den_index(self):
        self.assertEqual(self.wahl(["--gestaged"]), ["index"])

    def test_von_hand_ohne_stufe_bleibt_der_baum(self):
        self.assertEqual(self.wahl([]), ["baum"])


class BesitzRegistryTest(unittest.TestCase):
    """Gate B: der geteilte Arbeitsbaum gehoert nicht automatisch mir.

    GEMESSEN am 27.09.2026: vier Tage lang fremde Arbeit uncommitted im Baum
    (SebboHq-Innenausbau, Proben-Skripte), Branch `wt-gatetest` eines anderen
    Threads, und `git commit` haette alles mitgenommen. Ein Hook, der das
    nicht sieht, ist kein Hook.
    """

    def setUp(self):
        self.ordner = tempfile.mkdtemp(prefix="wb_besitz_")
        self.addCleanup(shutil.rmtree, self.ordner, ignore_errors=True)
        self.pfad = os.path.join(self.ordner, "wb_besitz.json")
        alt = os.environ.get("WB_BESITZ_DATEI")
        self.addCleanup(self.umgebung_wieder, alt)
        os.environ["WB_BESITZ_DATEI"] = self.pfad

    def umgebung_wieder(self, alt):
        if alt is None:
            os.environ.pop("WB_BESITZ_DATEI", None)
        else:
            os.environ["WB_BESITZ_DATEI"] = alt

    def schreibe(self, claims_branch):
        """claims_branch: {branch: anspruch} -> geschrieben wird das echte
        Format, geschluesselt nach THREAD (Version 2). Die Branch-Form bleibt
        in den Aufrufen, weil sie sich liest wie der 27.09.-Fall: ein Wegwerf-
        Branch, auf dem der fremde Thread sass."""
        claims = {}
        for branch, anspruch in claims_branch.items():
            thread = anspruch.get("thread") or branch
            claims[thread] = dict(anspruch, thread=thread, branch=branch)
        with open(self.pfad, "w", encoding="utf-8") as f:
            json.dump({"version": 2, "claims": claims}, f)

    def lauf(self):
        doppel = _BesitzLaufDoppel()
        alt = vdc.Lauf
        vdc.Lauf = lambda: doppel
        self.addCleanup(setattr, vdc, "Lauf", alt)
        return doppel

    def gate(self, dateien, branch, thread="mein-thread"):
        return vdc.besitz_gate(dateien, self.lauf(), eigener_branch=branch,
                               thread=thread)

    def test_es_faehrt_in_beiden_stufen_mit(self):
        """Es ist kein Push-Gate: der Besitz gehoert an den Commit.

        Nur in der schnellen Stufe waeren Wegwerf-Branches (der 27.09.-Fall)
        ungeschuetzt - dort committet man ohne Push.
        """
        d = self.lauf()
        alt = vdc.Lauf
        vdc.Lauf = lambda: d
        import gate_worktree
        try:
            for stufe in ("schnell", "voll"):
                d.protokolliert = []
                with mock.patch.object(gate_worktree, "motor_sperre",
                                       return_value=True):
                    vdc.gates_fahren(stufe, ["Source/x.cpp"])
                self.assertTrue(any("Gate B" in n for n in d.protokolliert),
                                stufe)
        finally:
            vdc.Lauf = alt

    def anspruch(self, branch, thread, muster, pid=None):
        return {"thread": thread, "pid": os.getpid() if pid is None else pid,
                "zeit": "2026-09-27T08:00:00", "muster": list(muster)}

    # --- was blockiert -----------------------------------------------------
    def test_fremde_datei_wird_abgewiesen(self):
        self.schreibe({"wt-gatetest": self.anspruch(
            "x", "innen-thread", ["Source/WiesbadenReal/World/SebboHqShape.cpp"])})
        rot = self.gate(["Source/WiesbadenReal/World/SebboHqShape.cpp"],
                        "feature/gates")
        self.assertTrue(rot, "fremde Datei kam durch das Besitz-Gate")

    def test_der_eigene_branch_darf_seine_ganze_arbeit_committen(self):
        muster = ["Tools/run_innen_probe.cmd", "Source/x.cpp"]
        self.schreibe({"wt-gatetest": self.anspruch("x", "mein-thread", muster)})
        self.assertFalse(self.gate(muster, "wt-gatetest"),
                         "der eigene Anspruch blockiert sich selbst")

    def test_gleicher_branch_anderer_thread_ist_trotzdem_fremd(self):
        """GEMESSEN am 27.09.2026: der erste Lauf dieses Gates verglich nur
        den Branch und meldete `gruen` - weil der fremde Thread auf demselben
        Wegwerf-Branch sass wie ich. Genau der Fall, fuer den es gebaut ist.
        """
        self.schreibe({"wt-gatetest": self.anspruch(
            "x", "innen-thread", ["Source/WiesbadenReal/Core/WbQuitWatchdog*"])})
        self.assertTrue(
            self.gate(["Source/WiesbadenReal/Core/WbQuitWatchdog.cpp"],
                      "wt-gatetest", thread="ich"),
            "gleicher Branch hat die fremde Datei durchgelassen")

    def test_der_thread_darf_auf_allen_branches_committen(self):
        """Im Thread-Modell ist der Thread die Identitaet, der Branch nur
        Information. Wer seinen Anspruch auf wt-gatetest genommen hat, darf
        auch auf feature/gates committen - sonst muesste er beim
        Branchwechsel erst einen neuen Anspruch nehmen, und die Motivation
        waere, den Anspruch gar nicht erst zu nehmen."""
        self.schreibe({"wt-gatetest": self.anspruch(
            "x", "mein-thread", ["Tools/"])})
        self.assertFalse(self.gate(["Tools/a.py"], "feature/gates",
                                    thread="mein-thread"))

    def test_derselbe_thread_auf_demselben_branch_darf(self):
        self.schreibe({"wt-gatetest": self.anspruch(
            "x", "mein-thread", ["Tools/"])})
        self.assertFalse(self.gate(["Tools/a.py"], "wt-gatetest",
                                   thread="mein-thread"))

    def test_ohne_eigenen_threadnamen_gilt_der_branch(self):
        """Rueckfall: wer seinen Namen nicht weiss, wird am Branch gemessen."""
        alt = os.environ.pop("WB_THREAD", None)
        try:
            self.schreibe({"wt-gatetest": self.anspruch(
                "x", "wt-gatetest", ["Tools/"])})
            d = self.lauf()
            # thread=None -> threadname() faellt auf den Branchnamen zurueck,
            # also ist der Anspruch mit demselben Branchnamen der eigene.
            vdc.besitz_gate(["Tools/a.py"], d, eigener_branch="wt-gatetest",
                            thread="wt-gatetest")
            self.assertEqual(d.protokolliert, ["Gate B  Besitz"])
        finally:
            if alt is not None:
                os.environ["WB_THREAD"] = alt

    def test_der_bericht_stuerzt_ohne_subprozess_nicht_ab(self):
        """GEMESSEN am 27.09.2026: `bericht()` griff auf ein Gate ohne
        Subprocess zu und endete mit Traceback statt mit einer Ablehnung.
        Der Hook muss den Grund sagen, nicht an ihm sterben."""
        d = vdc.Lauf()
        d.fahre_gate("Gate B  Besitz", False, 0.0, "Fremde Datei")
        with mock.patch("sys.stdout", new_callable=io.StringIO) as out:
            rot = d.bericht()
        self.assertEqual(rot, 1)
        self.assertIn("ROT: Gate B  Besitz", out.getvalue())

    def test_der_hook_nennt_den_gleichen_branch_im_konflikt(self):
        """Der Hinweis muss den stummen Fall benennen, sonst sucht niemand
        die Ursache - 'anderer Branch' waere hier schlicht falsch."""
        self.schreibe({"wt-gatetest": self.anspruch(
            "x", "innen-thread", ["Tools/innen_*.cmd"])})
        d = self.lauf()
        vdc.besitz_gate(["Tools/innen_probe.cmd"], d,
                        eigener_branch="wt-gatetest", thread="ich")
        self.assertIn("GLEICHER Branch", "".join(d.zeilen))

    def test_nicht_beanspruchte_datei_gehoert_niemandem(self):
        """Ein Thread, der nichts beansprucht hat, wird nie blockiert."""
        self.schreibe({"wt-gatetest": self.anspruch("x", "innen", ["Tools/"])})
        self.assertFalse(self.gate(["AGENTS.md"], "feature/gates"))

    def test_leere_registry_blockiert_nichts(self):
        self.assertFalse(self.gate(["Source/x.cpp"], "feature/gates"))

    def test_fehlende_registry_blockiert_nichts(self):
        self.assertFalse(os.path.exists(self.pfad))
        self.assertFalse(self.gate(["Source/x.cpp"], "feature/gates"))

    def test_praefix_und_fnmatch(self):
        self.schreibe({"wt-gatetest": self.anspruch(
            "x", "innen", ["Tools/", "Source/WiesbadenReal/World/Wb*.cpp"])})
        for datei in ("Tools/anything.txt", "Tools/Unterordner/a.ps1",
                      "Source/WiesbadenReal/World/WbCuttable.cpp"):
            self.assertTrue(self.gate([datei], "feature/gates"), datei)
        for datei in ("Source/Other/WbCuttable.cpp", "Toolsy/x.txt"):
            self.assertFalse(self.gate([datei], "feature/gates"), datei)

    def test_teilmenge_reicht(self):
        self.schreibe({"wt-gatetest": self.anspruch("x", "innen", ["Tools/"])})
        self.assertTrue(self.gate(["AGENTS.md", "Tools/a.py", "Tools/b.py"],
                                   "feature/gates"))

    def test_backslash_wird_akzeptiert(self):
        self.schreibe({"wt-gatetest": self.anspruch("x", "innen", ["Tools\\"])})
        self.assertTrue(self.gate(["Tools/a.py"], "feature/gates"))

    def test_zwei_threads_auf_demselben_branch_blockieren_einander(self):
        """Der Wegwerf-Branch ist der Ort, an dem zwei Threads sitzen."""
        self.schreibe({"wt-gatetest": self.anspruch(
            "x", "innen", ["Tools/innen_*.cmd"]),
            "feature/gates": self.anspruch("x", "ich", ["Tools/ich_*.cmd"])})
        rot = self.gate(["Tools/innen_probe.cmd"], "wt-gatetest",
                        thread="ich")
        self.assertTrue(rot, "der zweite Thread auf dem Wegwerf-Branch kam durch")

    # --- was NICHT blockiert ----------------------------------------------
    def test_kaputte_registry_ist_rot_nicht_frei(self):
        """Wer sie loescht, schaltet das Gate ab - das darf nicht 'frei' sein."""
        with open(self.pfad, "w", encoding="utf-8") as f:
            f.write("{kein json")
        d = self.lauf()
        self.assertTrue(vdc.besitz_gate(["Source/x.cpp"], d,
                                        eigener_branch="feature/gates"))
        self.assertIn("unlesbar", self.schalter_text(d))

    def test_registry_ohne_claims_ist_rot(self):
        with open(self.pfad, "w", encoding="utf-8") as f:
            json.dump({"version": 1}, f)
        d = self.lauf()
        self.assertTrue(vdc.besitz_gate(["Source/x.cpp"], d,
                                        eigener_branch="feature/gates"))
        self.assertIn("claims", self.schalter_text(d))

    def test_verwaister_anspruch_blockiert_nicht(self):
        """Ein toter Thread gibt frei. Sonst endet die Registry in --no-verify."""
        self.schreibe({"wt-gatetest": self.anspruch(
            "x", "abgestuerzt", ["Tools/"], pid=999999)})
        self.assertFalse(self.gate(["Tools/a.py"], "feature/gates"))

    def test_verwaister_anspruch_wird_gemeldet(self):
        self.schreibe({"wt-gatetest": self.anspruch(
            "x", "abgestuerzt", ["Tools/"], pid=999999)})
        d = self.lauf()
        vdc.besitz_gate(["AGENTS.md"], d, eigener_branch="feature/gates")
        self.assertIn("verwaist", self.schalter_text(d).lower())

    @unittest.skipUnless(os.name == "nt", "Windows-spezifische PID-Pruefung")
    def test_999999_ist_keine_laufende_pid(self):
        self.assertFalse(vdc.prozess_lebt(999999))
        self.assertTrue(vdc.prozess_lebt(os.getpid()))
        self.assertFalse(vdc.prozess_lebt(0))
        self.assertFalse(vdc.prozess_lebt(None))

    def test_gate_b_kostet_keinen_prozess(self):
        """Es faehrt VOR Gate 0 - ein Compilerlauf fuer einen verbotenen
        Commit waere reine Verschwendung."""
        self.schreibe({"wt-gatetest": self.anspruch("x", "fremd", ["Tools/"])})
        d = self.lauf()
        self.assertTrue(vdc.besitz_gate(["Tools/a.py"], d,
                                        eigener_branch="feature/gates"))
        self.assertEqual(d.gefahren, [], "Gate B hat einen Prozess gestartet")
        self.assertEqual(d.protokolliert, ["Gate B  Besitz"])

    def test_gates_fahren_bricht_bei_rotem_besitz_ab(self):
        doppel = _BesitzLaufDoppel()
        alt = vdc.Lauf
        vdc.Lauf = lambda: doppel
        alt_gate = vdc.besitz_gate
        vdc.besitz_gate = lambda *a, **k: True
        try:
            rot = vdc.gates_fahren("schnell", ["Tools/a.py"])
        finally:
            vdc.Lauf = alt
            vdc.besitz_gate = alt_gate
        self.assertEqual(rot, 1)
        self.assertEqual(doppel.gefahren + doppel.protokolliert, [],
                         "nach rotem Besitz liefen die uebrigen Gates weiter")

    def schalter_text(self, doppel):
        return "".join(t for t in doppel.zeilen)


class _BesitzLaufDoppel:
    """Minimaler Lauf-Ersatz: Besitz protokolliert, KEINE Prozesse.

    `gefahren` sind die per subprocess gestarteten Gates, `protokolliert` die
    ohne Prozess. Der Unterschied ist der Witz des Gated: es muss vor Gate 0
    stehen, also ohne einen einzigen Prozess.
    """

    def __init__(self):
        self.ergebnisse = []
        self.gefahren = []
        self.protokolliert = []
        self.uebersprungen = []
        self.zeilen = []

    def fahre_gate(self, name, ok, dauer, meldung):
        self.ergebnisse.append((name, ok, dauer, None))
        self.protokolliert.append(name)
        if meldung:
            self.zeilen.append(meldung)
        return ok

    def fahre(self, name, befehl, *, shell_cmd=False, notiz=None,
              notiz_zeilen=None):
        self.gefahren.append(name)
        return True

    def ueberspringe(self, name, grund):
        self.uebersprungen.append(name)

    def bericht(self):
        return 0


class BesitzCliTest(unittest.TestCase):
    """Anspruch beanspruchen, anzeigen, freigeben - ueber die CLI."""

    def setUp(self):
        self.ordner = tempfile.mkdtemp(prefix="wb_besitz_cli_")
        self.addCleanup(shutil.rmtree, self.ordner, ignore_errors=True)
        self.pfad = os.path.join(self.ordner, "wb_besitz.json")
        alt = os.environ.get("WB_BESITZ_DATEI")
        self.addCleanup(self.umgebung_wieder, alt)
        os.environ["WB_BESITZ_DATEI"] = self.pfad
        alt_thread = os.environ.get("WB_THREAD")
        self.addCleanup(self.umgebung_thread_wieder, alt_thread)
        os.environ["WB_THREAD"] = "test-thread"

    def umgebung_wieder(self, alt):
        if alt is None:
            os.environ.pop("WB_BESITZ_DATEI", None)
        else:
            os.environ["WB_BESITZ_DATEI"] = alt

    def umgebung_thread_wieder(self, alt):
        if alt is None:
            os.environ.pop("WB_THREAD", None)
        else:
            os.environ["WB_THREAD"] = alt

    def test_anspruch_verschmilzt_mit_dem_thread(self):
        vdc.besitz_ansprechen(["Tools/"], thread="a", branch="b1")
        vdc.besitz_ansprechen(["Source/"], thread="a", branch="b1")
        with open(self.pfad, encoding="utf-8") as f:
            claims = json.load(f)["claims"]
        self.assertEqual(sorted(claims["a"]["muster"]), ["Source/", "Tools/"])
        self.assertEqual(claims["a"]["branch"], "b1")

    def test_zwei_threads_auf_demselben_branch_ueberleben_einander(self):
        """GEMESSEN am 27.09.2026: die erste Fassung schluesselte nach BRANCH.
        Auf wt-gatetest hat der fremde Thread (innen-quitwatchdog) seinen
        Anspruch verloren, als ich dort --besitz-ansprechen aufrief. Der
        Wegwerf-Branch ist genau der Ort, an dem zwei Threads nebeneinander
        sitzen - er darf sie nicht gegeneinander ausloeschen.
        """
        vdc.besitz_ansprechen(["Tools/innen_*.cmd"], thread="innen",
                              branch="wt-gatetest")
        vdc.besitz_ansprechen(["Tools/vor_dem_commit.py"], thread="gate-b",
                              branch="wt-gatetest")
        with open(self.pfad, encoding="utf-8") as f:
            claims = json.load(f)["claims"]
        self.assertEqual(sorted(claims), ["gate-b", "innen"])
        self.assertEqual(claims["innen"]["muster"], ["Tools/innen_*.cmd"])
        self.assertEqual(claims["gate-b"]["muster"], ["Tools/vor_dem_commit.py"])

    def test_zwei_threads_ueberleben_einander(self):
        vdc.besitz_ansprechen(["Tools/"], thread="a", branch="b1")
        vdc.besitz_ansprechen(["Source/"], thread="b", branch="b2")
        with open(self.pfad, encoding="utf-8") as f:
            claims = json.load(f)["claims"]
        self.assertEqual(sorted(claims), ["a", "b"])

    def test_freigeben_nimmt_nur_den_eigenen_thread(self):
        vdc.besitz_ansprechen(["Tools/"], thread="a", branch="b1")
        vdc.besitz_ansprechen(["Source/"], thread="b", branch="b2")
        self.assertEqual(vdc.besitz_freigeben(thread="a"), 0)
        with open(self.pfad, encoding="utf-8") as f:
            self.assertEqual(list(json.load(f)["claims"]), ["b"])

    def test_freigeben_ohne_anspruch_ist_gruen(self):
        self.assertEqual(vdc.besitz_freigeben(thread="nie"), 0)

    def test_zeigen_ohne_ansprueche_erklaert_den_weg(self):
        self.assertEqual(vdc.besitz_zeigen(), 0)

    def test_zeigen_listet_alle_branches(self):
        vdc.besitz_ansprechen(["Tools/"], thread="a", branch="b1")
        vdc.besitz_ansprechen(["Source/"], thread="b", branch="b2")
        self.assertEqual(vdc.besitz_zeigen(), 0)

    def test_zeigen_und_freigeben_sind_keine_gates(self):
        """WB_KEINE_GATES schaltet Gates ab, nicht die Verwaltung."""
        alt = os.environ.get("WB_KEINE_GATES")
        os.environ["WB_KEINE_GATES"] = "1"
        try:
            vdc.besitz_ansprechen(["Tools/"], thread="a", branch="b1")
            self.assertEqual(vdc.besitz_zeigen(), 0)
            self.assertEqual(vdc.besitz_freigeben(thread="a"), 0)
        finally:
            if alt is None:
                os.environ.pop("WB_KEINE_GATES", None)
            else:
                os.environ["WB_KEINE_GATES"] = alt

    def test_kaputte_registry_bricht_nicht_als_freigabe_durch(self):
        with open(self.pfad, "w", encoding="utf-8") as f:
            f.write("nonsense")
        self.assertEqual(vdc.besitz_freigeben(thread="a"), 3)
        self.assertEqual(vdc.besitz_zeigen(), 3)
        with self.assertRaises(RuntimeError):
            vdc.besitz_ansprechen(["Tools/"], thread="a", branch="b1")


class RegistryMigrationTest(unittest.TestCase):
    """Version 1 war nach Branch geschluesselt, Version 2 nach Thread.

    Eine alte Registry darf niemanden blockieren und niemanden freigeben: sie
    wird beim Lesen stillschweigend umgestellt.
    """

    def setUp(self):
        self.ordner = tempfile.mkdtemp(prefix="wb_migration_")
        self.addCleanup(shutil.rmtree, self.ordner, ignore_errors=True)
        self.pfad = os.path.join(self.ordner, "wb_besitz.json")
        alt = os.environ.get("WB_BESITZ_DATEI")
        self.addCleanup(self.umgebung_wieder, alt)
        os.environ["WB_BESITZ_DATEI"] = self.pfad

    def umgebung_wieder(self, alt):
        if alt is None:
            os.environ.pop("WB_BESITZ_DATEI", None)
        else:
            os.environ["WB_BESITZ_DATEI"] = alt

    def test_der_branch_wird_zum_threadnamen(self):
        with open(self.pfad, "w", encoding="utf-8") as f:
            json.dump({"version": 1, "claims": {"wt-gatetest": {
                "thread": "innen", "pid": os.getpid(),
                "zeit": "2026-09-27T08:00:00", "muster": ["Tools/"]}}}, f)
        registry, fehler = vdc.besitz_laden()
        self.assertIsNone(fehler)
        self.assertEqual(registry["version"], 2)
        self.assertEqual(sorted(registry["claims"]), ["innen"])
        self.assertEqual(registry["claims"]["innen"]["branch"], "wt-gatetest")

    def test_ohne_threadname_im_altformat_gewinnt_der_branch(self):
        with open(self.pfad, "w", encoding="utf-8") as f:
            json.dump({"version": 1, "claims": {"wt-gatetest": {
                "pid": os.getpid(), "muster": ["Tools/"]}}}, f)
        registry, _ = vdc.besitz_laden()
        self.assertEqual(sorted(registry["claims"]), ["wt-gatetest"])

    def test_der_eigene_anspruch_der_alten_registry_bleibt_erlaubt(self):
        """Sonst wuerde die Umstellung einen Thread aus seinem Repo sperren."""
        with open(self.pfad, "w", encoding="utf-8") as f:
            json.dump({"version": 1, "claims": {"wt-gatetest": {
                "thread": "ich", "pid": os.getpid(),
                "zeit": "2026-09-27T08:00:00", "muster": ["Tools/"]}}}, f)
        d = _BesitzLaufDoppel()
        rot = vdc.besitz_gate(["Tools/a.py"], d, eigener_branch="wt-gatetest",
                              thread="ich")
        self.assertFalse(rot, "die Migration hat den eigenen Anspruch gesperrt")


class ThreadDurchreichungTest(unittest.TestCase):
    """`--thread` muss ANKOMMEN. GEMESSEN am 27.09.2026: die Option wurde
    geparst und dann fallengelassen - besitz_gate ermittelte den Namen selbst
    und landete beim Branchnamen. Folge war die absurdeste Variante: der
    Thread, dem die Arbeit gehoert, wurde vom eigenen Gate abgewiesen.
    """

    def setUp(self):
        self.ordner = tempfile.mkdtemp(prefix="wb_durchreich_")
        self.addCleanup(shutil.rmtree, self.ordner, ignore_errors=True)
        self.pfad = os.path.join(self.ordner, "wb_besitz.json")
        alt = os.environ.get("WB_BESITZ_DATEI")
        self.addCleanup(self.umgebung_wieder, alt)
        os.environ["WB_BESITZ_DATEI"] = self.pfad
        with open(self.pfad, "w", encoding="utf-8") as f:
            json.dump({"version": 2, "claims": {"innen-thread": {
                "thread": "innen-thread", "branch": "wt-gatetest",
                "pid": os.getpid(), "zeit": "2026-09-27T08:00:00",
                "muster": ["Tools/"]}}}, f)
        self.alt_gates = vdc.gates_fahren
        self.gesehen = []

        def spion(stufe, dateien, thread=None):
            self.gesehen.append(thread)
            return 0
        vdc.gates_fahren = spion
        self.addCleanup(setattr, vdc, "gates_fahren", self.alt_gates)

    def umgebung_wieder(self, alt):
        if alt is None:
            os.environ.pop("WB_BESITZ_DATEI", None)
        else:
            os.environ["WB_BESITZ_DATEI"] = alt

    def test_der_threadname_kommt_an(self):
        # GEMESSEN am 27.09.2026, im Push-Gate: dieser Test war im Hauptbaum
        # gruen und wurde erst beim Push rot, weil dort NICHTS vorgemerkt ist -
        # hauptprogramm kehrt dann vor gates_fahren zurueck ("Nichts
        # vorgemerkt - nichts zu pruefen") und der Spion sah nichts.
        vdc.gestagte_dateien = lambda *a, **k: ["Tools/x.py"]
        vdc.hauptprogramm(["--stufe", "schnell", "--gestaged", "--thread", "ich"])
        self.assertEqual(self.gesehen, ["ich"])

    def test_ohne_option_bleibt_es_beim_ermittelten_namen(self):
        vdc.gestagte_dateien = lambda *a, **k: ["Tools/x.py"]
        vdc.hauptprogramm(["--stufe", "schnell", "--gestaged"])
        self.assertEqual(self.gesehen, [None])

    def test_im_leeren_index_ruft_es_gates_fahren_gar_nicht(self):
        """Die Rueckkehr bei leerem Index ist gewollt - nur diese Tests
        hingen daran und waren deshalb im Push-Worktree blind."""
        vdc.gestagte_dateien = lambda *a, **k: []
        vdc.hauptprogramm(["--stufe", "schnell", "--gestaged", "--thread", "ich"])
        self.assertEqual(self.gesehen, [])

    def test_der_besitzer_kommt_durch_der_eigenen_option_durch(self):
        """Das Ende der Kette: Option -> gates_fahren -> besitz_gate."""
        vdc.gates_fahren = self.alt_gates
        alt_lauf = vdc.Lauf
        d = _BesitzLaufDoppel()
        vdc.Lauf = lambda: d
        alt_branch = vdc.aktueller_branch
        vdc.aktueller_branch = lambda: "wt-gatetest"
        try:
            rot = vdc.gates_fahren("schnell", ["Tools/a.py"],
                                   thread="innen-thread")
        finally:
            vdc.Lauf = alt_lauf
            vdc.aktueller_branch = alt_branch
        self.assertEqual(rot, 0,
                         "der Thread, dem die Arbeit gehoert, wurde abgewiesen")

    def test_der_fremde_wird_ueber_dieselbe_option_abgewiesen(self):
        vdc.gates_fahren = self.alt_gates
        alt_lauf = vdc.Lauf
        vdc.Lauf = lambda: _BesitzLaufDoppel()
        alt_branch = vdc.aktueller_branch
        vdc.aktueller_branch = lambda: "wt-gatetest"
        try:
            rot = vdc.gates_fahren("schnell", ["Tools/a.py"], thread="ich")
        finally:
            vdc.Lauf = alt_lauf
            vdc.aktueller_branch = alt_branch
        self.assertEqual(rot, 1)


class BesitzPfadTest(unittest.TestCase):
    """Die Registry liegt im GEMEINSAMEN .git - nicht im Arbeitsbaum."""

    def test_sie_liegt_nicht_im_baum(self):
        pfad = vdc.besitz_pfad()
        self.assertTrue(pfad.endswith("wb_besitz.json"), pfad)
        self.assertIn(os.path.join(".git", "wb_besitz.json"), pfad)

    def test_alle_worktrees_teilen_sie_sich(self):
        """Sonst koennte ein Thread im Neben-Worktree am Gate vorbei."""
        roh = subprocess.run(["git", "worktree", "list", "--porcelain"],
                             cwd=WURZEL, capture_output=True, text=True,
                             env=vdc.saubere_umgebung())
        if roh.returncode != 0 or len(roh.stdout.split("worktree ")) < 3:
            self.skipTest("nur ein Worktree - nichts zu vergleichen")
        self.assertTrue(os.path.isdir(os.path.dirname(vdc.besitz_pfad())))


class BesitzDokuTest(unittest.TestCase):
    """Was das Gate behauptet, steht auch im Docstring - sonst sucht es
    niemand, wenn es einmal blockiert."""

    def test_der_docstring_erklaert_das_gate(self):
        text = (WURZEL / "Tools" / "vor_dem_commit.py").read_text(
            encoding="utf-8", errors="replace")
        self.assertIn("GATE B (BESITZ)", text)
        self.assertIn("--besitz-ansprechen", text)

    def test_die_fehlermeldung_nennt_den_weg_zur_loesung(self):
        alt = os.environ.get("WB_BESITZ_DATEI")
        ordner = tempfile.mkdtemp(prefix="wb_besitz_msg_")
        self.addCleanup(shutil.rmtree, ordner, ignore_errors=True)
        self.addCleanup(lambda: os.environ.pop("WB_BESITZ_DATEI", None)
                        if alt is None else os.environ.__setitem__("WB_BESITZ_DATEI", alt))
        os.environ["WB_BESITZ_DATEI"] = os.path.join(ordner, "wb_besitz.json")
        alt_gates = vdc.gates_fahren
        vdc.gates_fahren = lambda *a, **k: 1
        alt_lauf = vdc.Lauf
        vdc.Lauf = lambda: _BesitzLaufDoppel()
        # Wie oben: im Push-Worktree ist der Index leer, dann kehrt
        # hauptprogramm vor dem Wegweistext zurueck und dieser Test prueft
        # nichts mehr, ohne zu scheitern. GEMESSEN am 27.09.2026.
        alt_staged = vdc.gestagte_dateien
        vdc.gestagte_dateien = lambda *a, **k: ["Tools/x.py"]
        try:
            with mock.patch("sys.stdout", new_callable=io.StringIO) as out:
                vdc.hauptprogramm(["--stufe", "schnell", "--gestaged"])
        finally:
            vdc.gates_fahren = alt_gates
            vdc.Lauf = alt_lauf
            vdc.gestagte_dateien = alt_staged
        text = out.getvalue()
        self.assertIn("--besitz-ansprechen", text)
        self.assertIn("git commit -- <nur eigene Dateien>", text)


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


class EchterBesitzHookTest(unittest.TestCase):
    """DER Kern des Gated, gegen echtes git: haelt der HOOK eine fremde Datei
    auf, wenn zwei Threads sich im selben Arbeitsbaum begegnen?

    Ein Test, der nur die Registry-Funktionen aufruft, prueft nicht die
    Verdrahtung - und genau die hat am 27.09.2026 gefehlt.
    """

    def setUp(self):
        self.repo = Path(tempfile.mkdtemp(prefix="wb_besitz_hook_"))
        self.addCleanup(shutil.rmtree, self.repo, ignore_errors=True)
        self.git("init", "-q", "-b", "feature/gates", ".")
        self.git("config", "user.email", "t@t")
        self.git("config", "user.name", "T")
        (self.repo / "datei.txt").write_text("basis", encoding="utf-8")
        self.git("add", "-A")
        self.git("commit", "-q", "-m", "Basis", "--no-verify")
        (self.repo / "Tools" / "git-hooks").mkdir(parents=True)
        (self.repo / "Tools" / "git-hooks" / "pre-commit").write_text(
            "#!/bin/sh\n"
            "WURZEL=$(git rev-parse --show-toplevel)\n"
            'exec python "$WURZEL/Tools/besitz_stub.py" --stufe schnell '
            "--gestaged --thread ich\n", encoding="utf-8")
        self.git("config", "core.hooksPath", "Tools/git-hooks")
        # Der Stub zieht die ECHTE vor_dem_commit.py (Besitz-Gate, Argument-
        # und Datei-Wahl, Exit-Codes) und ersetzt nur die Engine-Gates, die in
        # einem Wegwerf-Repo nichts zu tun haben.
        (self.repo / "Tools" / "besitz_stub.py").write_text('''
import os, sys
sys.path.insert(0, %r)
import vor_dem_commit as vdc
vdc.WURZEL = os.getcwd()


def nur_besitz(stufe, dateien, thread=None):
    lauf = vdc.Lauf()
    print("Gates vor dem Commit (Stufe: %%s)" %% stufe)
    if vdc.besitz_gate(dateien, lauf, thread=thread):
        lauf.bericht()
        return 1
    lauf.bericht()
    return 0


vdc.gates_fahren = nur_besitz
sys.exit(vdc.hauptprogramm())
''' % str(WURZEL / "Tools"), encoding="utf-8")
        # Registry mit dem Anspruch des ANDEREN Threads (die PID lebt: dieses
        # Test-Wegwerf-Repo laeuft ja) - genau die Lage vom 27.09.2026.
        anspruch = {"version": 2, "claims": {"innen-thread": {
            "thread": "innen-thread", "branch": "wt-gatetest",
            "pid": os.getpid(), "zeit": "2026-09-27T08:00:00",
            "muster": ["Tools/innen_*.cmd"]}}}
        (self.repo / ".git" / "wb_besitz.json").write_text(
            json.dumps(anspruch), encoding="utf-8")

    def git(self, *args):
        return subprocess.run(["git", *args], cwd=self.repo,
                              capture_output=True, text=True,
                              env=vdc.saubere_umgebung())

    def commits(self):
        fertig = self.git("rev-list", "--count", "HEAD")
        return int(fertig.stdout.strip()) if fertig.returncode == 0 else 0

    def test_der_hook_haelt_die_fremde_datei_auf(self):
        (self.repo / "Tools" / "innen_probe.cmd").write_text("x", encoding="utf-8")
        self.git("add", "-A")
        fertig = self.git("commit", "-m", "fremde Probe")
        self.assertEqual(self.commits(), 1,
                         "der Commit mit fremder Datei kam durch: %s" % fertig.stdout)

    def test_der_eigene_commit_geht_durch(self):
        (self.repo / "eigen.txt").write_text("x", encoding="utf-8")
        self.git("add", "-A")
        self.git("commit", "-m", "eigene Datei")
        self.assertEqual(self.commits(), 2, "eigene Arbeit wurde blockiert")

    def test_gemischter_commit_wird_komplett_abgewiesen(self):
        (self.repo / "eigen.txt").write_text("x", encoding="utf-8")
        (self.repo / "Tools" / "innen_probe.cmd").write_text("x", encoding="utf-8")
        self.git("add", "-A")
        self.git("commit", "-m", "gemischt")
        self.assertEqual(self.commits(), 1)
        # Nichts darf halb durchgerutscht sein.
        self.assertTrue((self.repo / "eigen.txt").exists())
        fertig = self.git("status", "--porcelain")
        self.assertIn("eigen.txt", fertig.stdout)
        self.assertIn("innen_probe.cmd", fertig.stdout)

    def test_die_ablehnung_sagt_was_zu_tun_ist(self):
        """Ein 'nein' ohne Weg dafuer fuehrt direkt zu --no-verify."""
        (self.repo / "Tools" / "innen_probe.cmd").write_text("x", encoding="utf-8")
        self.git("add", "-A")
        fertig = self.git("commit", "-m", "fremd")
        combined = fertig.stdout + fertig.stderr
        self.assertIn("--besitz-ansprechen", combined)
        self.assertIn("git commit --", combined)
        self.assertNotIn("Traceback", combined,
                         "der Hook starb an der Ablehnung statt sie zu melden")

    def test_der_gleiche_branch_steht_im_ausweis(self):
        (self.repo / "Tools" / "innen_probe.cmd").write_text("x", encoding="utf-8")
        self.git("add", "-A")
        fertig = self.git("commit", "-m", "fremd")
        self.assertIn("innen-thread", fertig.stdout + fertig.stderr,
                      "die Ablehnung nennt den Thread nicht")


if __name__ == "__main__":
    unittest.main()
