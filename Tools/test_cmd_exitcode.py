# Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
"""Verschluckte Exit-Codes in .cmd-Werkzeugen.

GEMESSEN am 28.09.2026: das Push-Gate meldete Gate 4 (Plasmacutter-
Bildfolge) nach 0 s gruen, obwohl der Lauf gar nicht gestartet war - die
Platte stand an der 10-Prozent-Grenze, der Engine-Lock verweigerte den
Start, und verify_cuttable.cmd druckte "ROT". Es gab kein neues Bild.

Die Ursache ist cmd.exe selbst: ein "exit /b 1" in einem INNEREN Block, hinter
dem im umschliessenden Block noch ein Befehl folgt, kommt beim Aufrufer von
"cmd /c" als 0 an. Genau so ruft vor_dem_commit.py die Gate-Skripte auf.
Sicher ist nur ein "exit /b" auf oberster Ebene, erreicht per goto.

Drei Ebenen:
  * RegelTest       - kein Tools/*.cmd hat das Muster (auch kuenftige nicht)
  * CmdVerhalten    - das cmd.exe-Verhalten selbst, als Beleg fuer die Regel
  * Gate4ExitTest   - das echte verify_cuttable.cmd mit vorgeschobenem Lauf:
                      jeder Fehlerweg endet rot, der Gutfall gruen
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parent


def verschluckte_exits(text):
    """(Zeile des exit /b, Zeile des Blockendes, folgender Befehl) je Fund.

    Ein Fund ist ein "exit /b" in einem Block, der selbst in einem Block
    steht, und hinter dessen schliessender Klammer im umschliessenden Block
    noch ein Befehl kommt. Kommentare und Leerzeilen zaehlen nicht als Befehl.
    """
    zeilen = text.splitlines()

    def ist_befehl(s):
        k = s.lower()
        return bool(s) and not (k == "rem" or k.startswith("rem ") or k.startswith("::"))

    funde, offen = [], []          # je offene Klammer: Zeilen der exit /b darin
    for nr, roh in enumerate(zeilen, 1):
        s = roh.strip()
        if not ist_befehl(s):
            continue
        if s.startswith(")") and offen:
            exits = offen.pop()
            if exits and offen:    # der geschlossene Block lag in einem anderen
                for folge in zeilen[nr:]:
                    f = folge.strip()
                    if not ist_befehl(f):
                        continue
                    if not f.startswith(")"):
                        funde.append((exits, nr, f))
                    break
        if offen and re.search(r"\bexit\s+/b\b", s, re.I):
            offen[-1].append(nr)
        if s.endswith("("):        # auch ") else (" oeffnet einen Block
            offen.append([])
    return funde


# Die Muster, an denen das cmd.exe-Verhalten am 28.09.2026 gemessen wurde.
MUSTER_VERSCHLUCKT = (
    "@echo off\r\nsetlocal EnableDelayedExpansion\r\nif 1==1 (\r\n  if 1==1 (\r\n"
    "    echo x\r\n    exit /b 1\r\n  )\r\n  echo danach\r\n)\r\nexit /b 0\r\n")
MUSTER_GOTO = (
    "@echo off\r\nsetlocal EnableDelayedExpansion\r\nif 1==1 (\r\n  if 1==1 goto :rot\r\n"
    "  echo danach\r\n)\r\nexit /b 0\r\n:rot\r\necho x\r\nexit /b 1\r\n")
MUSTER_LETZTER = (
    "@echo off\r\nsetlocal EnableDelayedExpansion\r\nif 1==1 (\r\n  if 1==1 (\r\n"
    "    echo x\r\n    exit /b 1\r\n  )\r\n)\r\nexit /b 0\r\n")


class RegelTest(unittest.TestCase):
    def test_kein_werkzeug_verschluckt_seinen_exit_code(self):
        befund = {}
        for pfad in sorted(TOOLS.glob("*.cmd")):
            funde = verschluckte_exits(pfad.read_text(encoding="utf-8", errors="replace"))
            if funde:
                befund[pfad.name] = funde
        self.assertEqual(befund, {},
                         "exit /b in einem inneren Block mit Befehlen danach - unter "
                         "'cmd /c' kommt dann 0 an. Per goto auf oberste Ebene legen: %s" % befund)

    def test_der_detektor_findet_das_muster(self):
        funde = verschluckte_exits(MUSTER_VERSCHLUCKT)
        self.assertEqual(len(funde), 1, funde)
        self.assertEqual(funde[0][2], "echo danach")

    def test_goto_auf_oberster_ebene_ist_sauber(self):
        self.assertEqual(verschluckte_exits(MUSTER_GOTO), [])

    def test_exit_als_letzter_befehl_im_block_ist_sauber(self):
        self.assertEqual(verschluckte_exits(MUSTER_LETZTER), [])

    def test_kommentare_nach_dem_block_zaehlen_nicht(self):
        text = MUSTER_LETZTER.replace(")\r\n)\r\n", ")\r\n  REM nur ein Kommentar\r\n)\r\n", 1)
        self.assertEqual(verschluckte_exits(text), [])


def cmd_lauf(ordner, skript, *argumente):
    return subprocess.run(["cmd", "/c", skript, *argumente], cwd=str(ordner),
                          capture_output=True, text=True, encoding="cp850", errors="replace")


@unittest.skipUnless(os.name == "nt", "cmd.exe gibt es nur unter Windows")
class CmdVerhaltenTest(unittest.TestCase):
    """Das Verhalten, auf dem die Regel beruht. Wird der erste Test hier einmal
    rot, hat cmd.exe sich geaendert - die Regel bleibt trotzdem richtig."""

    def setUp(self):
        self.ordner = Path(tempfile.mkdtemp(prefix="wb_cmdexit_"))
        self.addCleanup(shutil.rmtree, self.ordner, ignore_errors=True)
        (self.ordner / "T").mkdir()

    def lauf(self, text):
        (self.ordner / "T" / "probe.cmd").write_bytes(text.encode("ascii"))
        return cmd_lauf(self.ordner, r"T\probe.cmd").returncode

    def test_innerer_block_mit_befehl_danach_verliert_den_code(self):
        self.assertEqual(self.lauf(MUSTER_VERSCHLUCKT), 0)

    def test_goto_behaelt_den_code(self):
        self.assertEqual(self.lauf(MUSTER_GOTO), 1)


@unittest.skipUnless(os.name == "nt", "cmd.exe gibt es nur unter Windows")
class Gate4ExitTest(unittest.TestCase):
    """verify_cuttable.cmd so aufgerufen wie vom Push-Gate (cmd /c), mit
    vorgeschobenem run_cut_shots.cmd und verify_cuttable.py - ohne Engine."""

    def setUp(self):
        self.projekt = Path(tempfile.mkdtemp(prefix="wb_gate4_"))
        self.addCleanup(shutil.rmtree, self.projekt, ignore_errors=True)
        (self.projekt / "Tools").mkdir()
        (self.projekt / "Saved" / "Logs").mkdir(parents=True)
        shutil.copy(TOOLS / "verify_cuttable.cmd", self.projekt / "Tools" / "verify_cuttable.cmd")

    def gate(self, lauf_code, pruef_code, *argumente):
        (self.projekt / "Tools" / "run_cut_shots.cmd").write_bytes(
            ("@echo off\r\necho LAUF GESTARTET\r\nexit /b %d\r\n" % lauf_code).encode("ascii"))
        (self.projekt / "Tools" / "verify_cuttable.py").write_text(
            "import sys\nprint('PRUEFUNG GELAUFEN')\nsys.exit(%d)\n" % pruef_code, encoding="utf-8")
        return cmd_lauf(self.projekt, r"Tools\verify_cuttable.cmd", *argumente)

    def test_lauf_nicht_gestartet_ist_rot(self):
        # DER Fall vom 28.09.2026: Engine-Lock verweigert (Platte), Rueckgabe 2.
        r = self.gate(2, 0)
        self.assertEqual(r.returncode, 1, r.stdout)
        self.assertIn("nicht gestartet", r.stdout)
        self.assertNotIn("PRUEFUNG GELAUFEN", r.stdout,
                         "ohne Lauf darf nicht gegen alte Bilder geprueft werden")

    def test_lauf_abgebrochen_ist_rot(self):
        r = self.gate(1, 0)
        self.assertEqual(r.returncode, 1, r.stdout)
        self.assertIn("abgebrochen", r.stdout)
        self.assertNotIn("PRUEFUNG GELAUFEN", r.stdout)

    def test_pruefung_durchgefallen_ist_rot(self):
        r = self.gate(0, 1)
        self.assertEqual(r.returncode, 1, r.stdout)
        self.assertIn("durchgefallen", r.stdout)

    def test_lauf_und_pruefung_gut_ist_gruen(self):
        r = self.gate(0, 0)
        self.assertEqual(r.returncode, 0, r.stdout)
        self.assertIn("LAUF GESTARTET", r.stdout)
        self.assertIn("PRUEFUNG GELAUFEN", r.stdout)
        self.assertIn("GRUEN", r.stdout)

    def test_nur_pruefen_startet_keinen_lauf(self):
        # Der Lauf gaebe 2 zurueck - er darf gar nicht erst gerufen werden.
        r = self.gate(2, 0, "-NurPruefen")
        self.assertEqual(r.returncode, 0, r.stdout)
        self.assertNotIn("LAUF GESTARTET", r.stdout)
        r = self.gate(2, 1, "-NurPruefen")
        self.assertEqual(r.returncode, 1, r.stdout)


if __name__ == "__main__":
    unittest.main()
