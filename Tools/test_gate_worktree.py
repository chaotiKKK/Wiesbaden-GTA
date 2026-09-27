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
import time
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
class WegwerfRepo(unittest.TestCase):
    """Ein Wegwerf-Repo mit gebackener Stadt - die Grundlage fuer alles,
    was gate_worktree() wirklich auf der Platte macht."""

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
        # WICHTIG: Saved/ muss IGNORIERT sein, sonst raeumt "git clean -fd"
        # die Belege von selbst weg und der Test waere blind - er gruente dann
        # auch ohne das eigentliche Raeumen. Genau diese Zeile ist im echten
        # Projekt die Ursache (siehe BelegRaeumungTest).
        (self.projekt / ".gitignore").write_text("Content/Generated/\nSaved/\n", encoding="utf-8")
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


class EchterWorktreeTest(WegwerfRepo):
    """Ein Wegwerf-Repo: Worktree anlegen, Stadt verlinken, weiterruecken."""

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


class BelegRaeumungTest(WegwerfRepo):
    """Belege eines anderen Commits duerfen im Worktree keinen eigenen vortaeuschen.

    `git clean -fd` OHNE `-x` laesst ignorierte Dateien stehen (nachgemessen),
    `Saved/` steht in .gitignore, und der Worktree wird wiederverwendet. Also
    lagen beim Start der Python-Suiten - sie fahren VOR Gate 4 - die Belege des
    VORRIGEN Push-Laufs im Baum. Das hiess: `test_verify_cuttable_gate`
    uebersprang nicht, wie sein eigener Docstring vorsieht, sondern fuhr gegen
    die vollstaendigen Belege des vorigen Laufs.

    Geprueft wird beides: dass die Belege verschwinden, und dass dabei weder der
    Hauptordner noch eine Verknuepfung leerlaufen. Der zweite Teil ist kein
    Formalismus - `EchterWorktreeTest` dokumentiert, dass ein rekursives
    Loeschen durch eine Verbindung den Hauptordner leert, im Ernstfall die
    26 GB gebackene Stadt.
    """

    def test_der_putz_allein_laesst_die_belege_liegen(self):
        """Die URSACHE festnageln, sonst prueft der Rest ins Leere.

        GEMESSEN: "git clean -fd" OHNE -x laesst ignorierte Dateien stehen.
        Deshalb lag beim Start der Python-Suiten der Beleg des vorigen
        Push-Laufs im Worktree. Wird diese Zeile hier entfernt, gruennt
        der Folgetest aus dem falschen Grund - dann naemt "git clean" die
        Arbeit namlich selbst weg.
        """
        with mock.patch("sys.stdout", io.StringIO()):
            wt = gw.vorbereiten(self.projekt, self.eins)
        log, _, _ = self.lege_belege(wt)
        (self.projekt / "neu.txt").write_text("zwei", encoding="utf-8")
        self.git(self.projekt, "add", "neu.txt")
        self.git(self.projekt, "commit", "-qm", "zwei")
        zwei = self.git(self.projekt, "rev-parse", "HEAD")
        with mock.patch("sys.stdout", io.StringIO()):
            # vorbereiten() inklusive seines Putzes - nur OHNE belege_raeumen.
            with mock.patch.object(gw, "belege_raeumen", return_value=0):
                gw.vorbereiten(self.projekt, zwei)
        self.assertTrue(log.exists(),
                        "der Beleg verschwindet schon beim git clean - dann "
                        "traegt die Fixture die Ursache nicht (Saved/ ist nicht "
                        "ignoriert) und der Raeumungs-Test prueft ins Leere")

    def lege_belege(self, wurzel):
        (wurzel / "Saved" / "Logs").mkdir(parents=True, exist_ok=True)
        (wurzel / "Saved" / "Diagnose").mkdir(parents=True, exist_ok=True)
        log = wurzel / "Saved" / "Logs" / "wb_cut_schnitt.log"
        bild = wurzel / "Saved" / "Diagnose" / "schnitt_00_vorher.png"
        json_ = wurzel / "Saved" / "Logs" / "WbHealth.json"
        for p in (log, bild, json_):
            p.write_text("vom vorigen Push", encoding="utf-8")
        # Auch ein Unterordner: Autosave-artige Unterstrukturen sind Normalfall.
        (wurzel / "Saved" / "Logs" / "alt").mkdir(exist_ok=True)
        (wurzel / "Saved" / "Logs" / "alt" / "x.log").write_text("alt", encoding="utf-8")
        return log, bild, json_

    def test_die_zeitmarke_liegt_dort_wo_der_selbsttest_sie_sucht(self):
        """Die beiden Dateien teilen sich nur den Konstanten-Namen. Ob die Marke
        wirklich dort landet und von dort gelesen wird, kann niemand aus dem
        Namen ableiten - ohne diesen Test waere ein stilles Scheitern moeglich
        (die Suites wuerden dann eben immer ueberspringen)."""
        with mock.patch("sys.stdout", io.StringIO()):
            wt = gw.vorbereiten(self.projekt, self.eins)
        marke = wt / gw.BELEG_MARKE
        self.assertTrue(marke.exists(), "vorbereiten() setzt keine Zeitmarke: %s" % marke)
        try:
            beginn = float(marke.read_text(encoding="utf-8").strip())
        except ValueError:
            self.fail("die Zeitmarke ist keine lesbare Zahl: %r" % marke.read_text(encoding="utf-8"))
        self.assertLess(abs(time.time() - beginn), 300,
                        "die Zeitmarke ist nicht 'jetzt' - %r" % beginn)

    def test_die_zeitmarke_ueberlebt_das_raeumen_und_wird_erneut_gesetzt(self):
        """Sie liegt in Saved/ und NICHT in einem Belegordner - sonst loeschte
        der naechste Lauf sie weg und der Selbsttest faelle stillschweigend auf
        den Handbetriebs-Fall zurueck."""
        with mock.patch("sys.stdout", io.StringIO()):
            wt = gw.vorbereiten(self.projekt, self.eins)
        self.lege_belege(wt)
        self.lege_belege(self.projekt)          # Ablauf von 1 Stunde
        (wt / gw.BELEG_MARKE).write_text("%.3f" % (time.time() - 3600), encoding="utf-8")
        (self.projekt / "neu.txt").write_text("zwei", encoding="utf-8")
        self.git(self.projekt, "add", "neu.txt")
        self.git(self.projekt, "commit", "-qm", "zwei")
        zwei = self.git(self.projekt, "rev-parse", "HEAD")
        with mock.patch("sys.stdout", io.StringIO()):
            gw.vorbereiten(self.projekt, zwei)
        beginn = float((wt / gw.BELEG_MARKE).read_text(encoding="utf-8"))
        self.assertLess(abs(time.time() - beginn), 300,
                        "die Zeitmarke wurde nicht erneuert - die Belege des "
                        "neuen Laufs erschienen dann VOR ihrem Startzeitpunkt "
                        "und der Selbsttest ueberspringt")

    def test_die_belege_des_vorigen_laufs_sind_weg(self):
        with mock.patch("sys.stdout", io.StringIO()):
            wt = gw.vorbereiten(self.projekt, self.eins)
        log, bild, json_ = self.lege_belege(wt)
        (self.projekt / "neu.txt").write_text("zwei", encoding="utf-8")
        self.git(self.projekt, "add", "neu.txt")
        self.git(self.projekt, "commit", "-qm", "zwei")
        zwei = self.git(self.projekt, "rev-parse", "HEAD")
        with mock.patch("sys.stdout", io.StringIO()):
            gw.vorbereiten(self.projekt, zwei)
        for p in (log, bild, json_):
            self.assertFalse(p.exists(), "der Beleg vom vorigen Lauf liegt noch da: %s" % p)
        self.assertFalse((wt / "Saved" / "Logs" / "alt").exists(),
                         "ein Unterordner mit Belegen blieb stehen")

    def test_die_ordner_selber_bleiben_stehen(self):
        """Nur der INHALT wird geraeumt - die Werkzeuge legen ihre Ordner
        nicht ueberall selbst an."""
        with mock.patch("sys.stdout", io.StringIO()):
            wt = gw.vorbereiten(self.projekt, self.eins)
        self.lege_belege(wt)
        with mock.patch("sys.stdout", io.StringIO()):
            gw.belege_raeumen(wt, self.projekt)
        self.assertTrue((wt / "Saved" / "Logs").is_dir())
        self.assertTrue((wt / "Saved" / "Diagnose").is_dir())

    def test_der_hauptordner_bleibt_unberuehrt(self):
        """Das eigentliche Risiko: Saved/ des Arbeitsbaums ist der Beleg des
        Nutzers, und er liegt an DERSELBEN Stelle wie im Worktree."""
        with mock.patch("sys.stdout", io.StringIO()):
            wt = gw.vorbereiten(self.projekt, self.eins)
        haupt_log, haupt_bild, _ = self.lege_belege(self.projekt)
        self.lege_belege(wt)
        with mock.patch("sys.stdout", io.StringIO()):
            gw.belege_raeumen(wt, self.projekt)
        for p in (haupt_log, haupt_bild):
            self.assertTrue(p.exists(), "der Beleg des HAUPTORDNERS wurde mitgeraeumt: %s" % p)
        # Auch die gebackene Stadt im Hauptordner - der Grund fuer die Verlinkung.
        self.assertTrue((self.projekt / "Content" / "Generated" / "Chunk.uasset").exists())

    def test_ist_der_worktree_der_hauptordner_wird_nichts_geloescht(self):
        """WB_GATE_WORKTREE koennte auf den Hauptbaum zeigen. Dann waere das
        Raeumen genau das Loeschen der Belege des Nutzers."""
        haupt_log, haupt_bild, _ = self.lege_belege(self.projekt)
        with self.assertRaises(RuntimeError) as gefangen:
            gw.belege_raeumen(self.projekt, self.projekt)
        self.assertIn("Hauptordner", str(gefangen.exception))
        for p in (haupt_log, haupt_bild):
            self.assertTrue(p.exists(), "trotz Abbruch geloescht: %s" % p)

    def test_eine_verknuepfung_wird_nicht_verfolgt(self):
        """Saved/ wird nie verlinkt - aber diese Loeschung ist die erste, die
        ausserhalb von git etwas entfernt."""
        with mock.patch("sys.stdout", io.StringIO()):
            wt = gw.vorbereiten(self.projekt, self.eins)
        self.lege_belege(self.projekt)          # die echten Daten des Hauptordners
        verbindung = wt / "Saved" / "Logs"
        verbindung.parent.mkdir(parents=True, exist_ok=True)
        fertig = subprocess.run(["cmd", "/c", "mklink", "/J", str(verbindung),
                                 str(self.projekt / "Saved" / "Logs")],
                                capture_output=True, text=True,
                                encoding="utf-8", errors="replace")
        if fertig.returncode != 0:
            self.skipTest("Junction nicht anlegbar: %s" % fertig.stdout.strip())
        with self.assertRaises(RuntimeError) as gefangen:
            gw.belege_raeumen(wt, self.projekt)
        self.assertIn("Verknuepfung", str(gefangen.exception))
        self.assertTrue((self.projekt / "Saved" / "Logs" / "wb_cut_schnitt.log").exists(),
                        "die Loeschung ist DER Verknuepfung gefolgt und hat den "
                        "Hauptordner geleert")
        os.rmdir(verbindung)                    # nur den Verweis loesen, nicht das Ziel


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
                mock.patch.object(gw, "motor_sperre", return_value=True), \
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

    def test_build_release_bleibt_projektlokal_und_smoke_nutzt_den_helper(self):
        build = (WURZEL / "Tools" / "build_release.ps1").read_text(encoding="utf-8")
        smoke = (WURZEL / "Tools" / "smoke_test.ps1").read_text(encoding="utf-8")
        flight = (WURZEL / "Tools" / "flight_check.ps1").read_text(encoding="utf-8")
        self.assertNotIn("Get-Process UnrealEditor* -ErrorAction SilentlyContinue | Stop-Process", build)
        self.assertIn("Stop-ProjectEditors $Proj", build)
        self.assertIn('& "$PSScriptRoot\\cleanup_unreal_processes.cmd"', smoke)
        self.assertIn("Stop-ProjectEditors $Proj", smoke)
        # flight_check beendete am Ende JEDEN Editor auf dem Rechner - auch den
        # eines fremden Laufs, den es gar nicht gestartet hatte.
        self.assertNotIn("Get-Process UnrealEditor* -ErrorAction SilentlyContinue | Stop-Process", flight)
        self.assertIn("Stop-ProjectEditors $Proj", flight)

    def test_der_cleanup_helfer_raeumt_zenserver_und_wartet_drei_sekunden(self):
        ps1 = (WURZEL / "Tools" / "cleanup_unreal_processes.ps1").read_text(encoding="utf-8")
        batch = (WURZEL / "Tools" / "cleanup_unreal_processes.cmd").read_text(encoding="utf-8")
        self.assertIn('Get-Process -Name "UnrealEditor*"', ps1)
        self.assertIn('Get-Process -Name "zenserver"', ps1)
        self.assertIn("Start-Sleep -Seconds 3", ps1)
        self.assertIn("AddSeconds(10)", ps1)
        self.assertIn("zweiter Versuch", ps1)
        self.assertIn("Prozessbereinigung unvollstaendig", ps1)
        self.assertIn("cleanup_unreal_processes.ps1", batch)
        self.assertIn("%*", batch)

    def test_alle_bake_und_test_wrapper_sichern_den_engine_lock(self):
        cmd_wrapper = (
            "dump_alkis27_streets.cmd",
            "playtest_alkis27_runover.cmd",
            "rebake_alkis23.cmd",
            "rebake_alkis25.cmd",
            "rebake_alkis27.cmd",
            "run_ankunft_probe.cmd",
            "run_automation_test.cmd",
            "run_bus_audit.cmd",
            "run_bus_fahrbahn.cmd",
            "run_bus_ground.cmd",
            "run_bus_haltestelle.cmd",
            "run_bus_interior_proof.cmd",
            "run_bus_mitfahrt.cmd",
            "run_bus_mitfahrt_wagen.cmd",
            "run_bus_umlauf.cmd",
            "run_material_flags_proof.cmd",
            "verify_bus_materials.cmd",
            "verify_ka52.cmd",
            "verify_ka52_actor.cmd",
        )
        aufruf = 'call "%~dp0engine_run_lock.cmd" -Modus Start -Name {name}'
        for name in cmd_wrapper:
            text = (WURZEL / "Tools" / name).read_text(encoding="utf-8")
            # Das Label ist der Wrapper-Name OHNE Endung (im Log besser lesbar).
            self.assertIn(aufruf.replace("{name}", name[:-4]), text, name)
            # Der Lock-Aufruf nimmt die Bereinigung mit (Modus Start) - ein
            # zweiter, ungeschuetzter Aufruf waere wieder das gegenseitige
            # Beenden fremder Editoren.
            self.assertNotIn("cleanup_unreal_processes", text, name)
            starts = [i for i in (text.find("UnrealEditor.exe"),
                                  text.find("UnrealEditor-Cmd.exe")) if i >= 0]
            self.assertTrue(starts, name)
            self.assertLess(text.index(aufruf.replace("{name}", name[:-4])), min(starts), name)

        ps1_wrapper = ("flight_check.ps1", "health_check.ps1",
                       "health_multi.ps1", "smoke_test.ps1")
        for name in ps1_wrapper:
            text = (WURZEL / "Tools" / name).read_text(encoding="utf-8")
            self.assertIn('& "$PSScriptRoot\\engine_run_lock.ps1" -Modus Nehmen -Name', text, name)
            # Der Lock kommt VOR dem globalen Cleanup und vor dem Engine-Start.
            for spaeter in ('& "$PSScriptRoot\\cleanup_unreal_processes.cmd"', "Start-Process"):
                if spaeter in text:
                    self.assertLess(text.index("engine_run_lock.ps1"), text.index(spaeter), name)

    def test_der_cleanup_uebergeht_beendete_prozessleichen(self):
        # Ein beendeter Prozess, der nur noch in der Liste haengt (HasExited),
        # laesst sich nicht mehr beenden - auch nicht als Administrator. Er darf
        # die Bereinigung nicht scheitern lassen (26.09.2026: PID 43820 machte
        # jedes Push-Gate im Rauchtest rot).
        ps1 = (WURZEL / "Tools" / "cleanup_unreal_processes.ps1").read_text(encoding="utf-8")
        self.assertIn("function Test-Beendet", ps1)
        reste = ps1[ps1.index("function Get-EngineReste"):ps1.index("function Get-EngineLeichen")]
        self.assertIn("-not (Test-Beendet $_)", reste)
        self.assertIn("uebergangen", ps1)
        # Die Leichen werden erst NACH der Lock-Pruefung gemeldet.
        self.assertLess(ps1.index("engine_run_lock.ps1"), ps1.index("@(Get-EngineLeichen)"))

    def test_die_pipeline_und_der_cleanup_achten_auf_den_lock(self):
        build = (WURZEL / "Tools" / "build_release.ps1").read_text(encoding="utf-8")
        ps1 = (WURZEL / "Tools" / "cleanup_unreal_processes.ps1").read_text(encoding="utf-8")
        # VOR Gate 0, nicht erst vor Gate 2: Gate 1 beendet ueber
        # Stop-ProjectEditors die Editoren DIESES Projektordners, und im
        # Gate-Worktree ist genau dieser Ordner der geteilte Arbeitsplatz
        # zweier Sessions. Ein zweiter Gate-Lauf muss also schon dort abbrechen.
        self.assertIn('engine_run_lock.ps1") -Modus Nehmen -Name build_release', build)
        self.assertLess(build.index("engine_run_lock.ps1"), build.index('Section 0 "Engine-Pfade'))
        self.assertLess(build.index("engine_run_lock.ps1"), build.index("Stop-ProjectEditors $Proj"))
        # Der Cleanup fragt den Lock, BEVOR er etwas beendet, und bricht bei
        # belegtem Lock ab statt den fremden Editor zu killen.
        self.assertIn('-Modus Status -LockPfad $LockPfad', ps1)
        self.assertLess(ps1.index("engine_run_lock.ps1"), ps1.index("$prozesse = @(Get-EngineReste)"))
        self.assertIn("SperreIgnorieren", ps1)
        self.assertIn("belegt", ps1)


class PipelineLockTest(unittest.TestCase):
    """Der Lock umschliesst die GANZE Push-Pipeline - ab dem Worktree-Checkout.

    Anlass (25.09.2026): ein paralleler `build_release -GatesOnly` im selben
    Gate-Worktree beendete in seinem Gate 1 den Gate-2-Editor des Push-Laufs,
    bevor dessen Lock griff - der Push-Lauf nahm ihn erst in build_release.
    """

    def test_push_nimmt_den_lock_vor_dem_ersten_checkout(self):
        folge = []
        with mock.patch.object(gw, "motor_sperre", side_effect=lambda name: folge.append("lock") or True), \
                mock.patch.object(gw, "pruefen", side_effect=lambda p, sha: folge.append(sha) or 0), \
                mock.patch.object(gw, "git", side_effect=lambda cwd, *a, **k: a[1] + "\n"), \
                mock.patch("sys.stdout", io.StringIO()):
            rc = gw.push_pruefen(WURZEL, "refs/heads/x %s refs/heads/x %s\nrefs/heads/y %s refs/heads/y %s\n"
                                 % (A, B, B, A))
        self.assertEqual(rc, 0)
        # Genau EINMAL, und vor jedem Commit - nicht je Commit neu.
        self.assertEqual(folge[0], "lock")
        self.assertEqual(folge.count("lock"), 1)
        self.assertEqual(len(folge), 3)

    def test_belegter_lock_faesst_den_worktree_nicht_an(self):
        with mock.patch.object(gw, "motor_sperre", return_value=False), \
                mock.patch.object(gw, "pruefen") as pruefen, \
                mock.patch.object(gw, "vorbereiten") as vorbereiten, \
                mock.patch.object(gw, "git", return_value="baum\n"), \
                mock.patch("sys.stdout", io.StringIO()) as aus:
            rc = gw.push_pruefen(WURZEL, "refs/heads/x %s refs/heads/x %s\n" % (A, B))
        self.assertEqual(rc, 1)
        pruefen.assert_not_called()
        vorbereiten.assert_not_called()
        self.assertIn("Engine-Lock belegt", aus.getvalue())

    def test_volle_stufe_nimmt_den_lock_vor_gate_0(self):
        folge = []
        with mock.patch.object(gw, "motor_sperre", side_effect=lambda name: folge.append("lock") or True), \
                mock.patch.object(vdc.Lauf, "fahre", side_effect=lambda name, *a, **k: folge.append(name) or True), \
                mock.patch("sys.stdout", io.StringIO()):
            self.assertEqual(vdc.gates_fahren("voll", ["Source/x.cpp"]), 0)
        self.assertEqual(folge[0], "lock")
        self.assertTrue(folge[1].startswith("Gate 0"))
        self.assertIn("Gate 2+3  Tests und Rauchtest", folge)

    def test_volle_stufe_ohne_lock_faehrt_kein_gate(self):
        with mock.patch.object(gw, "motor_sperre", return_value=False), \
                mock.patch.object(vdc.Lauf, "fahre") as fahre, \
                mock.patch("sys.stdout", io.StringIO()):
            self.assertEqual(vdc.gates_fahren("voll", ["Source/x.cpp"]), 1)
        fahre.assert_not_called()

    def test_schnelle_stufe_wartet_auf_niemanden(self):
        with mock.patch.object(gw, "motor_sperre") as sperre, \
                mock.patch.object(vdc.Lauf, "fahre", return_value=True), \
                mock.patch("sys.stdout", io.StringIO()):
            vdc.gates_fahren("schnell", ["Source/x.cpp"])
        sperre.assert_not_called()


class EngineLockTest(unittest.TestCase):
    """Der Engine-Lock in der Tat: Tools/engine_run_lock.ps1 gegen echte Prozesse.

    Anlass ist der rote Push-Lauf vom 25.09.2026: der globale Cleanup beendete
    den Editor eines bereits als fehlgeschlagen gemeldeten Gate-Laufs und riss
    dadurch den zweiten, gruenen Push mit. Geprueft wird deshalb nicht nur der
    Code, sondern das Verhalten zwischen zwei wirklich laufenden Prozessen -
    mit einer temporaeren Lock-Datei, damit der maschinenweite Engine-Lock
    unberuehrt bleibt.
    """

    LOCK = WURZEL / "Tools" / "engine_run_lock.ps1"
    CLEANUP = WURZEL / "Tools" / "cleanup_unreal_processes.ps1"

    def ps(self, skript, *args):
        return subprocess.run(
            ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(skript)] + list(args),
            capture_output=True, text=True, timeout=120)

    def felder(self, pfad):
        text = Path(pfad).read_text(encoding="utf-8")
        return dict(zeile.split("=", 1) for zeile in text.splitlines() if "=" in zeile)

    def lebender_fremder(self):
        """Ein Prozess, der lebt und NICHT Vorfahre dieses Tests ist."""
        return subprocess.Popen(
            [sys.executable, "-c", "import time; time.sleep(120)"],
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))

    def test_freier_lock_ist_frei_nimmt_an_und_bleibt_reentrant(self):
        with tempfile.TemporaryDirectory() as tmp:
            pfad = str(Path(tmp) / "engine_run.lock")
            frei = self.ps(self.LOCK, "-Modus", "Status", "-LockPfad", pfad)
            self.assertEqual(frei.returncode, 0, frei.stdout + frei.stderr)
            self.assertIn("frei", frei.stdout)

            erst = self.ps(self.LOCK, "-Modus", "Nehmen", "-LockPfad", pfad, "-Name", "unittest")
            self.assertEqual(erst.returncode, 0, erst.stdout + erst.stderr)
            # Besitzer ist der AUFRUFPROZESS (dieser Test), nicht der
            # kurzlebige PowerShell-Kindprozess - sonst waere der Lock nach dem
            # Aufruf schon wieder frei.
            felder = self.felder(pfad)
            self.assertEqual(int(felder["OwnerPid"]), os.getpid())
            self.assertEqual(felder["Label"], "unittest")

            # Derselbe Lauf darf den Lock wiederholt nehmen (Gate -> Rauchtest
            # -> Cleanup): das ist der Reentrant-Fall, ohne den sich das Gate
            # selbst blockieren wuerde.
            nochmal = self.ps(self.LOCK, "-Modus", "Nehmen", "-LockPfad", pfad, "-Name", "unittest")
            self.assertEqual(nochmal.returncode, 0, nochmal.stdout + nochmal.stderr)
            self.assertIn("bereits", nochmal.stdout)

            frei_gibt = self.ps(self.LOCK, "-Modus", "Freigeben", "-LockPfad", pfad)
            self.assertEqual(frei_gibt.returncode, 0, frei_gibt.stdout + frei_gibt.stderr)
            self.assertFalse(os.path.exists(pfad))

    def test_fremder_lauf_sperrt_und_ein_verwaister_wird_uebernommen(self):
        with tempfile.TemporaryDirectory() as tmp:
            pfad = str(Path(tmp) / "engine_run.lock")
            fremder = self.lebender_fremder()
            try:
                Path(pfad).write_text(
                    "LockVersion=1\nOwnerPid=%d\nLabel=rebake_alkis25\nTakenAt=2026-09-25 11:00:00\n"
                    "Host=TEST\n" % fremder.pid, encoding="utf-8")
                status = self.ps(self.LOCK, "-Modus", "Status", "-LockPfad", pfad)
                self.assertEqual(status.returncode, 3, status.stdout + status.stderr)
                self.assertIn("BELEGT", status.stdout)
                self.assertIn("rebake_alkis25", status.stdout)

                nehmen = self.ps(self.LOCK, "-Modus", "Nehmen", "-LockPfad", pfad, "-Name", "unittest")
                self.assertEqual(nehmen.returncode, 3, nehmen.stdout + nehmen.stderr)
                self.assertIn("NICHT", nehmen.stdout.upper())  # "startet NICHT"
            finally:
                fremder.terminate()
                fremder.wait(timeout=30)

            # Besitzer weg -> verwaist -> der naechste Lauf uebernimmt, statt
            # sich an einem toten Lock zu versacken.
            uebernehmen = self.ps(self.LOCK, "-Modus", "Nehmen", "-LockPfad", pfad, "-Name", "unittest")
            self.assertEqual(uebernehmen.returncode, 0, uebernehmen.stdout + uebernehmen.stderr)
            self.assertIn("verwaist", uebernehmen.stdout)
            self.assertEqual(self.felder(pfad)["Label"], "unittest")

    def test_motor_sperre_gehoert_dem_python_lauf_und_wartet_auf_den_fremden(self):
        with tempfile.TemporaryDirectory() as tmp:
            pfad = str(Path(tmp) / "engine_run.lock")
            # Ein fremder Lauf, der nach 4 s endet.
            fremder = subprocess.Popen(
                [sys.executable, "-c", "import time; time.sleep(4)"],
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            try:
                Path(pfad).write_text(
                    "LockVersion=1\nOwnerPid=%d\nLabel=build_release\nTakenAt=2026-09-25 21:02:35\n"
                    "Host=TEST\n" % fremder.pid, encoding="utf-8")
                with mock.patch("sys.stdout", io.StringIO()) as aus:
                    self.assertFalse(gw.motor_sperre("unittest", warte_s=0, lock_pfad=pfad))
                self.assertIn("BELEGT", aus.getvalue())
                with mock.patch("sys.stdout", io.StringIO()) as aus:
                    gehalten = gw.motor_sperre("unittest", warte_s=60, lock_pfad=pfad,
                                               schlaf=lambda s: time.sleep(1))
                self.assertTrue(gehalten, aus.getvalue())
                self.assertIn("warte", aus.getvalue())
            finally:
                fremder.wait(timeout=30)
            self.assertEqual(int(self.felder(pfad)["OwnerPid"]), os.getpid())

    def test_tief_verschachtelte_laeufe_bleiben_eigen(self):
        # Die tiefste echte Abfrage liegt 6 Ebenen unter dem Hook (Cleanup ->
        # cmd -> smoke_test -> build_release -> cmd -> vor_dem_commit -> Hook).
        # Nachgebaut mit fuenf cmd-Ebenen plus PowerShell unter diesem Test.
        with tempfile.TemporaryDirectory() as tmp:
            pfad = str(Path(tmp) / "engine_run.lock")
            with mock.patch("sys.stdout", io.StringIO()):
                self.assertTrue(gw.motor_sperre("unittest", warte_s=0, lock_pfad=pfad))
            # Ohne Anfuehrungszeichen: die liessen sich durch fuenf cmd /c nicht
            # heil durchreichen; beide Pfade sind leerzeichenfrei.
            self.assertNotIn(" ", str(self.LOCK) + pfad)
            innen = "powershell -NoProfile -ExecutionPolicy Bypass -File %s -Modus Status -LockPfad %s" % (
                self.LOCK, pfad)
            befehl = ["cmd", "/c", "cmd /c cmd /c cmd /c cmd /c " + innen]
            lauf = subprocess.run(befehl, capture_output=True, text=True, encoding="cp850",
                                  errors="replace", timeout=120)
            self.assertEqual(lauf.returncode, 0, lauf.stdout + lauf.stderr)
            self.assertIn("von diesem Lauf gehalten", lauf.stdout)

    def test_start_modus_nimmt_den_lock_und_reinigt_trocken(self):
        with tempfile.TemporaryDirectory() as tmp:
            pfad = str(Path(tmp) / "engine_run.lock")
            lauf = self.ps(self.LOCK, "-Modus", "Start", "-LockPfad", pfad,
                           "-Name", "unittest", "-DryRun")
            self.assertEqual(lauf.returncode, 0, lauf.stdout + lauf.stderr)
            self.assertIn("Prozessbereinigung", lauf.stdout)
            self.assertEqual(self.felder(pfad)["OwnerPid"], str(os.getpid()))

    def test_der_cleanup_bricht_bei_fremdem_lock_ab(self):
        # -DryRun: der Abbruch muss auch im Probelauf greifen. Damit kann der
        # Test laufen, ohne dass im Fehlerfall ein Prozess getroffen wuerde.
        with tempfile.TemporaryDirectory() as tmp:
            pfad = str(Path(tmp) / "engine_run.lock")
            fremder = self.lebender_fremder()
            try:
                Path(pfad).write_text(
                    "LockVersion=1\nOwnerPid=%d\nLabel=rebake_alkis25\nTakenAt=2026-09-25 11:00:00\n"
                    "Host=TEST\n" % fremder.pid, encoding="utf-8")
                lauf = self.ps(self.CLEANUP, "-LockPfad", pfad, "-DryRun")
            finally:
                fremder.terminate()
                fremder.wait(timeout=30)
            ausgabe = lauf.stdout + lauf.stderr
            self.assertNotEqual(lauf.returncode, 0, ausgabe)
            self.assertIn("BELEGT", ausgabe)
            self.assertIn("belegt", ausgabe)
            # Und ohne fremden Lock laeuft derselbe Aufruf durch.
            frei = self.ps(self.CLEANUP, "-LockPfad", pfad, "-DryRun")
            self.assertEqual(frei.returncode, 0, frei.stdout + frei.stderr)
            self.assertIn("Prozessbereinigung", frei.stdout)

    def test_der_cleanup_roettet_bei_fremdem_lock_nicht(self):
        with tempfile.TemporaryDirectory() as tmp:
            pfad = str(Path(tmp) / "engine_run.lock")
            # Echter Prozess mit dem Namen, den der Cleanup abschiesst: eine
            # Kopie von ping.exe. Nur so laesst sich BEWEISEN, dass der Abbruch
            # vor dem Kill greift.
            attribut = subprocess.run(
                ["powershell", "-NoProfile", "-Command",
                 "(Get-Process -Name 'UnrealEditor*','zenserver' -ErrorAction SilentlyContinue).Count"],
                capture_output=True, text=True)
            if attribut.stdout.strip() not in ("0", ""):
                self.skipTest("Es laeuft ein echter Editor/zenserver - der Test wuerde ihn nicht "
                              "gefaehrden, laesst sich aber nicht sauber messbar.")
            fake = Path(tmp) / "UnrealEditor.exe"
            fake.write_bytes(Path(os.environ["SystemRoot"], "System32", "ping.exe").read_bytes())
            prozess = subprocess.Popen([str(fake), "-t", "127.0.0.1"],
                                       creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            try:
                time.sleep(2)
                laeuft = subprocess.run(
                    ["powershell", "-NoProfile", "-Command",
                     "(Get-Process -Name 'UnrealEditor*' -ErrorAction SilentlyContinue).Count"],
                    capture_output=True, text=True)
                if laeuft.stdout.strip() != "1":
                    self.skipTest("Der nachgemalte Editorprozess laesst sich nicht starten.")

                fremder = self.lebender_fremder()
                try:
                    Path(pfad).write_text(
                        "LockVersion=1\nOwnerPid=%d\nLabel=rebake_alkis25\nTakenAt=2026-09-25 11:00:00\n"
                        "Host=TEST\n" % fremder.pid, encoding="utf-8")
                    lauf = self.ps(self.CLEANUP, "-LockPfad", pfad)
                finally:
                    fremder.terminate()
                    fremder.wait(timeout=30)

                ausgabe = lauf.stdout + lauf.stderr
                self.assertNotEqual(lauf.returncode, 0, ausgabe)
                self.assertIn("belegt", ausgabe)
                # Das eigentliche Kriterium: der Editor des fremden Laufs lebt.
                nachher = subprocess.run(
                    ["powershell", "-NoProfile", "-Command",
                     "if (Get-Process -Id %d -ErrorAction SilentlyContinue) { 'LEBT' }" % prozess.pid],
                    capture_output=True, text=True).stdout
                self.assertIn("LEBT", nachher)
            finally:
                prozess.terminate()
                prozess.wait(timeout=30)


if __name__ == "__main__":
    unittest.main()
