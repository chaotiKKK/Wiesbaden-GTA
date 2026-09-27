r"""Selbsttest: die Belege des Rauchtests muessen aus DIESEM Lauf stammen.

GEMESSEN am 27.09.2026, dieselbe Fehlerklasse wie bei Gate 4 (36bc800):
``smoke_test.ps1`` loeschte seine drei Belege mit

    Remove-Item $LogFile -ErrorAction SilentlyContinue

und VERTRAUTE darauf, dass sie damit weg sind. ``-ErrorAction
SilentlyContinue`` schluckt jeden Fehler. Beide naheliegenden Faelle wurden
nachgemessen und hinterlassen die Datei:

  * Read-only-Flag  -> bleibt liegen (``-Force`` entfernt sie)
  * offenes Handle eines haengenden Editors -> bleibt liegen

Danach las die Auswertung genau diese alte Datei: die Warte-Schleife sah die
gewohnten Muster sofort (``$n >= $MinCount``), der frische Editor wurde nach
vier Sekunden gekillt, und die gemeldeten Zahlen waren die des VORRIGEN
Laufs. Bei ``WbHealth.json`` gingen sogar zwei Pruefungen darauf (``Health-Gate``
und ``Materialien``).

Warum das im Push-Gate nicht selten ist: ``Saved/`` steht in ``.gitignore``,
und ``gate_worktree.py`` putzt mit ``git clean -fd`` OHNE ``-x`` - ignorierte
Dateien ueberleben den Putz, der Worktree sammelt also ueber beliebig viele
Push-Laeufe hinweg Belege an.

``build_release.ps1`` Gate 2 hatte dieselbe blinde Loeschung. Dort faellt der
Schaden nur kleiner aus, weil zusaetzlich der Abschluss-Marker geprueft wird -
der steht aber genauso in einem alten, vollstaendig abgeschlossenen Log.

    python -m unittest discover -s Tools -p "test_rauchtest_beleg.py" -v

Die beiden PowerShell-Funktionen werden hieraus aus der .ps1 HERAUSGEZOGEN und
in PowerShell ausgefuehrt - der Test prueft also den ausgelieferten Code und
nicht eine nachgebaute Kopie. Ein Nachbau wuerde genau die Aenderung
vermissen, um die es geht.
"""

import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

WURZEL = Path(__file__).resolve().parent.parent
SMOKE = WURZEL / "Tools" / "smoke_test.ps1"
RELEASE = WURZEL / "Tools" / "build_release.ps1"

# Nachlauf des Harness je Modus. "TRUE"/"FALSE" ist das Ergebnis von
# Test-Frisch, "WEG"/"DA" das von Remove-Beleg.
#
# WICHTIG: der param-Block steht ZUERST. PowerShell akzeptiert ihn nur als
# erste Anweisung eines Skripts - steht er hinter den Funktionen, wird das
# Skript komplett als Argumentliste gelesen, $Modus bleibt leer, und der
# Switch schweigt. Genau so sieht ein "gruener" Test aus, der nichts
# geprueft hat.
PARAM = "param([string]$Modus, [string]$Ziel)"

HARNESS = r"""
switch ($Modus) {
  "frisch"  { if (Test-Frisch $Ziel (Get-Date).AddSeconds(-30)) { "TRUE" } else { "FALSE" } }
  "alt"     { if (Test-Frisch $Ziel (Get-Date).AddSeconds(-30)) { "TRUE" } else { "FALSE" } }
  "fehlt"   { if (Test-Frisch $Ziel (Get-Date).AddSeconds(-30)) { "TRUE" } else { "FALSE" } }
  "ohneStart" { if (Test-Frisch $Ziel $null) { "TRUE" } else { "FALSE" } }
  "loeschen" { Remove-Beleg $Ziel; if (Test-Path $Ziel) { "DA" } else { "WEG" } }
  "geloescht" { Remove-Beleg $Ziel; if (Test-Path $Ziel) { "DA" } else { "WEG" } }
  "gesperrt" {
    # Ein haengender Prozess haelt die Datei offen - der gemessene Zustand.
    $fs = [System.IO.File]::Open($Ziel, 'Open', 'Read', 'None')
    Remove-Beleg $Ziel
    $fs.Close()
    "KEIN ABBRUCH"
  }
}
"""


def lade_funktionen(text, name):
    """Holt eine PowerShell-Funktion samt Rumpf aus dem Skript.

    Sie endet in diesem Projekt auf einer `}` in Spalte 1 - so wie alle
    anderen Funktionen in smoke_test.ps1. Das ist schaerfer als eine
    Klammerzaehlung und bricht, sobald jemand umformatiert.
    """
    zeilen = text.splitlines()
    start = None
    for i, z in enumerate(zeilen):
        if z.startswith("function %s(" % name):
            start = i
            break
    if start is None:
        raise AssertionError("Funktion %s() nicht in %s gefunden" % (name, SMOKE))
    for j in range(start + 1, len(zeilen)):
        if zeilen[j] == "}":
            return "\n".join(zeilen[start:j + 1])
    raise AssertionError("Funktion %s() hat kein abschliessendes '}'" % name)


class BelegFunktionenTest(unittest.TestCase):
    """Die zwei Wächter, gegen die Szenarien von unten gefahren werden."""

    @classmethod
    def setUpClass(cls):
        text = SMOKE.read_text(encoding="utf-8", errors="replace")
        cls.basis = Path(tempfile.mkdtemp(prefix="wb_beleg_"))
        cls.skript = cls.basis / "waechter.ps1"
        cls.skript.write_text(
            PARAM + "\n\n"
            + lade_funktionen(text, "Remove-Beleg") + "\n\n"
            + lade_funktionen(text, "Test-Frisch") + "\n\n"
            + HARNESS, encoding="utf-8")

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.basis, ignore_errors=True)

    def fahre(self, modus, ziel):
        fertig = subprocess.run(
            ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
             "-File", str(self.skript), "-Modus", modus, "-Ziel", str(ziel)],
            capture_output=True, text=True, timeout=120)
        return fertig.returncode, fertig.stdout.strip(), fertig.stderr

    def datei(self, inhalt="WbDev: Beleg", alter=None):
        pfad = self.basis / ("beleg_%s.log" % abs(hash((self.id(), inhalt))))
        pfad.write_text(inhalt, encoding="utf-8")
        if alter is not None:
            os.utime(pfad, (alter, alter))
        return pfad

    # -- Test-Frisch ------------------------------------------------------

    def test_ein_gerade_geschriebener_beleg_zaehlt(self):
        pfad = self.datei()
        rc, raus, err = self.fahre("frisch", pfad)
        self.assertEqual(rc, 0, err)
        self.assertEqual(raus, "TRUE",
                         "ein Beleg, der nach dem Sitzungsbeginn geschrieben wurde, "
                         "wird als frisch abgewiesen")

    def test_ein_beleg_von_gestern_zaehlt_nicht(self):
        """Der Kernfall: die Datei ist da und sieht richtig aus.

        Genau daran scheitert ein Gate, das nur auf Existenz und Zeilen
        schaut - es meldet die Werte des vorigen Laufs als Messung.
        """
        pfad = self.datei()
        alt = time.time() - 3600.0
        os.utime(pfad, (alt, alt))
        rc, raus, err = self.fahre("alt", pfad)
        self.assertEqual(rc, 0, err)
        self.assertEqual(raus, "FALSE",
                         "ein liegengebliebener Beleg von vor einer Stunde gilt als "
                         "Messung dieses Laufs")

    def test_ein_fehlender_beleg_zaehlt_nicht(self):
        rc, raus, err = self.fahre("fehlt", self.basis / "gibtsnicht.log")
        self.assertEqual(rc, 0, err)
        self.assertEqual(raus, "FALSE", "ohne Datei ist nichts gemessen")

    def test_ohne_sitzungsbeginn_ist_nichts_beweisbar(self):
        """Ein unbekannter Startzeitpunkt darf nicht als "frisch" gelten.

        Ohne diese Prüfung fiele ein nie gestarteter Lauf über die
        Zeitstempel-Vergleichsbedingung und jede alte Datei durch.
        """
        pfad = self.datei()
        rc, raus, err = self.fahre("ohneStart", pfad)
        self.assertEqual(rc, 0, err)
        self.assertEqual(raus, "FALSE",
                         "ohne bekannten Sitzungsbeginn gilt jeder Beleg als frisch")

    # -- Remove-Beleg -----------------------------------------------------

    def test_ein_schreibgeschuetzter_beleg_wird_entfernt(self):
        """Der gemessene Fall 1: Read-only-Flag.

        `Remove-Item -ErrorAction SilentlyContinue` lässt die Datei liegen;
        erst `-Force` nimmt sie mit. Genau darum prüft der Wächter nach.
        """
        pfad = self.datei()
        os.chmod(pfad, 0o444)
        try:
            if os.access(str(pfad), os.W_OK):
                self.skipTest("Read-only lässt sich hier nicht setzen")
            rc, raus, err = self.fahre("loeschen", pfad)
        finally:
            # Nur falls der Waechter sie NICHT entfernen konnte - im
            # Erfolgsfall gibt es die Datei nicht mehr, und ein chmod
            # darauf wuerde den Test mit FileNotFoundError beenden statt
            # mit dem eigentlichen Befund.
            if os.path.exists(pfad):
                os.chmod(pfad, 0o666)
        self.assertEqual(rc, 0, err)
        self.assertEqual(raus, "WEG",
                         "der Beleg blieb liegen und die Auswertung hätte seine "
                         "alten Zeilen gelesen")

    def test_ein_beleg_der_sich_nicht_loeschen_laesst_bricht_ab(self):
        """Der gemessene Fall 2: offenes Handle eines hängenden Editors.

        Hier hilft kein Löschen mehr. Der Lauf MUSS abbrechen - weiterzulaufen
        hieße, den alten Beleg als Ergebnis des neuen auszugeben, und genau
        so sieht ein grünes Gate aus, das nichts gemessen hat.
        """
        pfad = self.datei()
        rc, raus, err = self.fahre("gesperrt", pfad)
        self.assertEqual(rc, 1,
                         "ein nicht löschbarer Beleg lässt den Lauf weiterlaufen")
        self.assertIn("ABBRUCH", (raus + err).upper(),
                      "der Abbruch sagt dem Leser nicht, worum es geht")


class BlindesLoeschenTest(unittest.TestCase):
    """Der Rauchtest darf seinen Beleg nicht mehr blind löschen.

    `Remove-Item … -ErrorAction SilentlyContinue` ist der Fehler selbst:
    der Löschfehler verschwindet, und die Datei bleibt liegen.
    """

    def setUp(self):
        self.smoke = SMOKE.read_text(encoding="utf-8", errors="replace")
        self.release = RELEASE.read_text(encoding="utf-8", errors="replace")

    def test_kein_blindes_loeschen_der_sitzungslogs(self):
        blind = re.findall(r"Remove-Item \$LogFile[^\n]*", self.smoke)
        self.assertEqual(
            [], [z for z in blind if "SilentlyContinue" in z and "Force" not in z],
            "der Sitzungslog wird ohne Nachprüfung gelöscht - bleibt er liegen, "
            "liest die Auswertung den vorigen Lauf: " + repr(blind))

    def test_kein_blindes_loeschen_der_health_json(self):
        blind = [z for z in re.findall(r"Remove-Item \$HealthJson[^\n]*", self.smoke)
                 if "Force" not in z]
        self.assertEqual([], blind,
                         "die WbHealth.json wird ohne Nachprüfung gelöscht - "
                         "Health-Gate und Materialien-Pruefung speisen sich dann "
                         "aus dem vorigen Lauf")

    def test_die_auswertung_prueft_die_frische(self):
        for muster in (r"Test-Frisch \$datei \$BelegStart\[\$datei\]",
                       r"Test-Frisch \$HealthJson \$BelegStart\[\$HealthJson\]"):
            self.assertRegex(self.smoke, muster,
                             "die Auswertung liest den Beleg ohne Frischenachweis")

    def test_gate2_loescht_nicht_blind_und_prueft_die_frische(self):
        for zeile in re.findall(r"Remove-Item \$TestLog[^\n]*", self.release):
            self.assertIn("Force", zeile,
                          "Gate 2 loescht die Testlog ohne -Force: eine "
                          "schreibgeschuetzte Datei bliebe liegen - " + zeile)
        self.assertIn("if (-not $testLogFresh)", self.release,
                      "Gate 2 laesst einen Log gelten, der nicht aus diesem Lauf "
                      "stammt - der Abschluss-Marker steht auch im alten Log")
        self.assertIn("$testLogStart = Get-Date", self.release,
                      "Gate 2 fuehrt keinen Zeitstempel des Laufbeginns, gegen den "
                      "es die Testlog messen koennte")


if __name__ == "__main__":
    unittest.main()
