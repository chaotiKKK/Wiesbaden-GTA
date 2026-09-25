r"""Selbsttest: volles Gate beim Push im sauberen Worktree (Tools/gate_worktree.py).

Geprueft wird, was den Push frei macht: nur die Commits kommen in den
Worktree, die gebackene Stadt wird verlinkt, fremde Dateien bleiben draussen,
und der pre-push-Hook geht diesen Weg.

    python -m unittest discover -s Tools -p "test_gate_worktree.py"
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
import gate_worktree as gw  # noqa: E402
import vor_dem_commit as vdc  # noqa: E402

WURZEL = Path(__file__).resolve().parent.parent
A, B = "a" * 40, "b" * 40


class PushEingabeTest(unittest.TestCase):
    """Was der Hook auf stdin bekommt: <lokaler ref> <sha> <entfernter ref> <sha>."""

    def test_zwei_zweige_zwei_commits(self):
        text = "refs/heads/x %s refs/heads/x %s\nrefs/heads/y %s refs/heads/y %s\n" % (A, gw.NULL_SHA, B, A)
        self.assertEqual(gw.push_shas(text), [A, B])

    def test_loeschungen_pruefen_nichts(self):
        self.assertEqual(gw.push_shas("(delete) %s refs/heads/x %s\n" % (gw.NULL_SHA, A)), [])

    def test_derselbe_commit_nur_einmal_und_muell_zaehlt_nicht(self):
        text = "refs/heads/x %s refs/heads/x %s\nrefs/heads/y %s refs/heads/y %s\nkaputt\n" % (A, B, A, B)
        self.assertEqual(gw.push_shas(text), [A])

    def test_gleicher_baum_wird_einmal_geprueft(self):
        # Die PR-Kette auf main traegt dieselben Baeume wie der Zweig.
        baeume = {A: "t1", B: "t1", "c" * 40: "t2"}
        self.assertEqual(gw.je_baum_einer([A, B, "c" * 40], baeume.get), [A, "c" * 40])


class StadtinhaltTest(unittest.TestCase):
    """Verlinkt wird die gebackene Stadt - nie fremde Einzeldateien."""

    IGNORIERT = [
        "Content/Assets/People/Sylvia/SK_Sylvia.uasset",   # fremde Einzeldatei
        "Content/Data/", "Content/Data/Raw/",
        "Content/Generated/",
        "Content/Maps/__StadtNeubau_1.umap",               # Kratzkarte
        "Content/Materials/AAA/",
        "Content/__ExternalActors__/", "Content/__ExternalObjects__/",
        "Data/Raw/ALKIS/", "Data/Raw/Bus/bus.glb", "Data/Raw/Bus/blind/",
    ]
    UNVERSIONIERT = [
        "Content/Maps/WiesbadenCity_Alkis22.umap", "Content/Maps/WiesbadenCity_Alkis24.umap",
        "Content/Maps/__AaaRuntimeShot.umap", "Content/Assets/Fremd/Neu.uasset",
    ]

    def test_verzeichnisse_der_stadt(self):
        verzeichnisse, _ = gw.waehle_stadtinhalt(self.IGNORIERT, self.UNVERSIONIERT)
        self.assertEqual(verzeichnisse, [
            "Content/Data", "Content/Generated", "Content/Materials/AAA",
            "Content/__ExternalActors__", "Content/__ExternalObjects__",
            "Data/Raw/ALKIS", "Data/Raw/Bus/blind"])

    def test_nur_die_stadtkarten_als_dateien(self):
        _, dateien = gw.waehle_stadtinhalt(self.IGNORIERT, self.UNVERSIONIERT)
        self.assertEqual(dateien, ["Content/Maps/WiesbadenCity_Alkis22.umap",
                                   "Content/Maps/WiesbadenCity_Alkis24.umap"])

    def test_fremde_einzeldateien_bleiben_draussen(self):
        verzeichnisse, dateien = gw.waehle_stadtinhalt(self.IGNORIERT, self.UNVERSIONIERT)
        alles = verzeichnisse + dateien
        for fremd in ("SK_Sylvia", "__StadtNeubau", "__AaaRuntimeShot", "Fremd/Neu", "bus.glb"):
            self.assertFalse(any(fremd in e for e in alles), fremd)


class WorktreeOrtTest(unittest.TestCase):
    def test_neben_dem_projekt_mit_gleichem_namen(self):
        with mock.patch.dict(os.environ, {}, clear=False):
            os.environ.pop("WB_GATE_WORKTREE", None)
            ort = gw.gate_projekt(Path("C:/x/Sicherung/WiesbadenReal"))
        self.assertEqual(ort, Path("C:/x/Sicherung/.gate-worktree/WiesbadenReal"))

    def test_eigener_stamm_per_umgebung(self):
        with mock.patch.dict(os.environ, {"WB_GATE_WORKTREE": "D:/gate"}):
            self.assertEqual(gw.gate_projekt(Path("C:/x/WiesbadenReal")), Path("D:/gate/WiesbadenReal"))


@unittest.skipUnless(os.name == "nt", "Verzeichnis-Verbindungen gibt es nur unter Windows")
class EchterWorktreeTest(unittest.TestCase):
    """Ein Wegwerf-Repo: Worktree anlegen, Stadt verlinken, weiterruecken."""

    def git(self, cwd, *args):
        return subprocess.run(["git", *args], cwd=str(cwd), check=True, capture_output=True, text=True,
                              env=gw.saubere_umgebung()).stdout.strip()

    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.projekt = self.tmp / "Sicherung" / "WiesbadenReal"
        (self.projekt / "Content" / "Maps").mkdir(parents=True)
        self.git(self.projekt, "init", "-q")
        self.git(self.projekt, "config", "user.email", "t@t")
        self.git(self.projekt, "config", "user.name", "t")
        (self.projekt / ".gitignore").write_text("Content/Generated/\n", encoding="utf-8")
        (self.projekt / "Content" / "Maps" / "Alt.umap").write_text("v", encoding="utf-8")
        (self.projekt / "code.txt").write_text("eins", encoding="utf-8")
        self.git(self.projekt, "add", "-A")
        self.git(self.projekt, "commit", "-qm", "eins")
        self.eins = self.git(self.projekt, "rev-parse", "HEAD")
        # Gebackene Stadt (ignoriert) + Stadtkarte (unversioniert) + fremde Arbeit.
        (self.projekt / "Content" / "Generated").mkdir()
        (self.projekt / "Content" / "Generated" / "Chunk.uasset").write_text("gross", encoding="utf-8")
        (self.projekt / "Content" / "Maps" / "WiesbadenCity_Alkis22.umap").write_text("karte", encoding="utf-8")
        (self.projekt / "code.txt").write_text("fremd, halb fertig", encoding="utf-8")
        (self.projekt / "Fremd.txt").write_text("fremd", encoding="utf-8")
        self.env = mock.patch.dict(os.environ, {"WB_GATE_WORKTREE": str(self.tmp / "gate")})
        self.env.start()

    def tearDown(self):
        # Erst die Verbindungen loesen (nur der Verweis, nicht das Ziel), dann
        # loeschen: ein rekursives Loeschen DURCH eine Verbindung leerte sonst
        # den Hauptordner - im Ernstfall die gebackene Stadt.
        wt = gw.gate_projekt(self.projekt)
        self.env.stop()
        verbindung = wt / "Content" / "Generated"
        if os.path.isjunction(verbindung):
            os.rmdir(verbindung)
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_nur_der_commit_und_die_stadt(self):
        with mock.patch("sys.stdout", io.StringIO()):
            wt = gw.vorbereiten(self.projekt, self.eins)
        self.assertEqual((wt / "code.txt").read_text(encoding="utf-8"), "eins")   # nicht die fremde Fassung
        self.assertFalse((wt / "Fremd.txt").exists())
        self.assertEqual((wt / "Content" / "Generated" / "Chunk.uasset").read_text(encoding="utf-8"), "gross")
        self.assertEqual((wt / "Content" / "Maps" / "WiesbadenCity_Alkis22.umap").read_text(encoding="utf-8"), "karte")

    def test_weiterruecken_raeumt_reste_und_behaelt_die_stadt(self):
        with mock.patch("sys.stdout", io.StringIO()):
            wt = gw.vorbereiten(self.projekt, self.eins)
        (wt / "Rest.txt").write_text("vom letzten Lauf", encoding="utf-8")
        # Zweiter Commit im Hauptordner (nur eine eigene Datei).
        (self.projekt / "neu.txt").write_text("zwei", encoding="utf-8")
        self.git(self.projekt, "add", "neu.txt")
        self.git(self.projekt, "commit", "-qm", "zwei")
        zwei = self.git(self.projekt, "rev-parse", "HEAD")
        with mock.patch("sys.stdout", io.StringIO()):
            gw.vorbereiten(self.projekt, zwei)
        self.assertTrue((wt / "neu.txt").exists())
        self.assertFalse((wt / "Rest.txt").exists())
        self.assertTrue((wt / "Content" / "Generated" / "Chunk.uasset").exists())
        self.assertTrue((wt / "Content" / "Maps" / "WiesbadenCity_Alkis22.umap").exists())
        # Der Hauptordner bleibt, wie er war - auch die gebackene Stadt.
        self.assertEqual((self.projekt / "code.txt").read_text(encoding="utf-8"), "fremd, halb fertig")
        self.assertTrue((self.projekt / "Content" / "Generated" / "Chunk.uasset").exists())

    def test_die_stadt_ist_verlinkt_nicht_kopiert(self):
        with mock.patch("sys.stdout", io.StringIO()):
            wt = gw.vorbereiten(self.projekt, self.eins)
        self.assertTrue(os.path.isjunction(wt / "Content" / "Generated"))
        karte = wt / "Content" / "Maps" / "WiesbadenCity_Alkis22.umap"
        self.assertTrue(os.path.samefile(karte, self.projekt / "Content" / "Maps" / "WiesbadenCity_Alkis22.umap"))


class HookWegTest(unittest.TestCase):
    """Der pre-push-Hook muss diesen Weg gehen - sonst prueft er wieder den Baum."""

    def test_pre_push_reicht_die_commits_durch(self):
        text = (WURZEL / "Tools" / "git-hooks" / "pre-push").read_text(encoding="utf-8")
        self.assertIn("--stufe voll --push-refs", text)

    def test_push_refs_landet_im_worktree(self):
        with mock.patch.object(gw, "push_pruefen", return_value=0) as gerufen, \
                mock.patch("sys.stdin", io.StringIO("refs/heads/x %s refs/heads/x %s\n" % (A, B))), \
                mock.patch.dict(os.environ, {}, clear=False):
            os.environ.pop("WB_KEINE_GATES", None)
            self.assertEqual(vdc.hauptprogramm(["--stufe", "voll", "--push-refs"]), 0)
        gerufen.assert_called_once()
        self.assertIn(A, gerufen.call_args[0][1])

    def test_rot_im_worktree_weist_den_push_ab(self):
        with mock.patch.object(gw, "pruefen", return_value=1), \
                mock.patch.object(gw, "git", return_value="baum\n"), \
                mock.patch("sys.stdout", io.StringIO()):
            self.assertEqual(gw.push_pruefen(WURZEL, "refs/heads/x %s refs/heads/x %s\n" % (A, B)), 1)

    def test_gate_skripte_suchen_ihr_projekt_selbst(self):
        # Fest verdrahtete Pfade haetten im Worktree den Hauptordner gebaut/getestet.
        gate1 = (WURZEL / "Tools" / "build_gate1.cmd").read_text(encoding="utf-8")
        self.assertIn("%~dp0..", gate1)
        self.assertNotIn("C:\\freebuff\\WiesbadenReal_Sicherung\\WiesbadenReal\\WiesbadenReal.uproject", gate1)
        for name in ("build_release.ps1", "smoke_test.ps1"):
            text = (WURZEL / "Tools" / name).read_text(encoding="utf-8")
            self.assertIn("$PSScriptRoot", text, name)
            # Nicht in der Parameter-Vorgabe: mit [CmdletBinding()] ist $PSScriptRoot
            # dort unter PowerShell 5.1 leer (erster Worktree-Lauf, 25.09.2026).
            self.assertNotIn("$Root = (Split-Path", text, name)
            self.assertIn("if (-not $Root)", text, name)

    def test_nur_eigene_editoren_werden_beendet(self):
        for name in ("build_release.ps1", "smoke_test.ps1"):
            text = (WURZEL / "Tools" / name).read_text(encoding="utf-8")
            self.assertNotIn("Get-Process UnrealEditor* -ErrorAction SilentlyContinue | Stop-Process", text, name)
            self.assertIn("Stop-ProjectEditors $Proj", text, name)


if __name__ == "__main__":
    unittest.main()
