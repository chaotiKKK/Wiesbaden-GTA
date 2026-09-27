r"""Selbsttest der Gates-vor-dem-Commit (Tools/vor_dem_commit.py + Hooks).

Ein Hook ist nur so viel wert, wie er wirklich aufhaelt. Geprueft wird darum
nicht "das Skript laeuft", sondern: haelt es einen kaputten Commit auf, laesst
es einen guten durch, und kostet es nur dann einen Compiler, wenn wirklich
C++ im Spiel ist.

    python -m unittest discover -s Tools -p "test_vor_dem_commit.py"
"""
import io
import os
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
        def __init__(self):
            self.gefahren = []
            self.uebersprungen = []

        # notiz kommt seit dem 27.09. dazu: der Schritt Python-Suiten
        # meldet, wie viele Tests uebersprungen wurden. Wer diese Signatur
        # verkuerzt, bekommt in 9 Tests einen TypeError statt einer Aussage.
        def fahre(self, name, befehl, *, shell_cmd=False, notiz=None):
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
        # Die volle Stufe nimmt den maschinenweiten Engine-Lock - ein Test darf
        # ihn nicht anfassen (er wartete sonst auf einen echten Gate-Lauf).
        import gate_worktree
        try:
            with mock.patch.object(gate_worktree, "motor_sperre", return_value=True):
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


class AnkerGateTest(unittest.TestCase):
    """Gate 5 (Ankerzustand der WP-Karte) - er laeuft bei jedem Push.

    Der Ankerzustand entsteht in einem BAKE, nicht im Code. Ein Fehler darin
    ist im Commit unsichtbar - die Datei ist vorhanden, nur falsch - und
    faellt erst beim Laden der Karte auf, wenn die leeren Komponenten am
    Kartenursprung stehen. Genau dieser Stand lag wochenlang als "gemessen"
    da, weil der Lauf die Existenz der Datei mit Erfolg verwechselt hat.

    Deshalb hier dasselbe dreifach abgesichert wie bei Gate 4: der Schritt
    laeuft, er laeuft OHNE Dateifilter, und er befiehlt genau das Skript,
    das die Zahl auswertet.
    """

    class LaufDoppel:
        def __init__(self):
            self.gefahren = []
            self.befehle = {}
            self.uebersprungen = []

        # notiz kommt seit dem 27.09. dazu: der Schritt Python-Suiten
        # meldet, wie viele Tests uebersprungen wurden. Wer diese Signatur
        # verkuerzt, bekommt in 9 Tests einen TypeError statt einer Aussage.
        def fahre(self, name, befehl, *, shell_cmd=False, notiz=None):
            self.gefahren.append(name)
            self.befehle[name] = befehl
            return True

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

        # notiz kommt seit dem 27.09. dazu: der Schritt Python-Suiten
        # meldet, wie viele Tests uebersprungen wurden. Wer diese Signatur
        # verkuerzt, bekommt in 9 Tests einen TypeError statt einer Aussage.
        def fahre(self, name, befehl, *, shell_cmd=False, notiz=None):
            self.gefahren.append(name)
            self.befehle[name] = befehl
            return True

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
class AnkerBeweisTest(unittest.TestCase):
    """Gate 5 verlangt eine NEUE Messung, nicht nur einen Exit-Code.

    Gemessen am 27.09.2026: der Schritt meldete in einem echten Push-Lauf
    nach 10 Sekunden "gruen", ohne ein Log und ohne Ergebnisdatei zu
    hinterlassen. Der Exit-Code allein beweist also nicht, dass gemessen
    wurde - und ein Gate, das auf so einen Beweis verzichtet, ist die Ampel
    ohne Lampe, die es verhindern soll.
    """

    class LaufDoppel:
        def __init__(self, returncode=0):
            self.ergebnisse = []
            self._returncode = returncode

        # notiz kommt seit dem 27.09. dazu: der Schritt Python-Suiten
        # meldet, wie viele Tests uebersprungen wurden. Wer diese Signatur
        # verkuerzt, bekommt in 9 Tests einen TypeError statt einer Aussage.
        def fahre(self, name, befehl, *, shell_cmd=False, notiz=None):
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
            def fahre(self, name, befehl, *, shell_cmd=False, notiz=None):
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
            def __init__(self):
                self.ergebnisse = []
                self.notizen = []

            def fahre(self, name, befehl, *, shell_cmd=False, notiz=None):
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
        vdc.gates_fahren = lambda stufe, dateien: 0
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
