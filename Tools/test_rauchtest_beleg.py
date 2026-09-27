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
        """Jeder Beleg laeuft durch Test-Frisch, bevor gelesen wird.

        Die Form hat sich mit dem Ausbau geaendert (Lese-Beleg statt eines
        foreach im Rumpf) - dieser Test hat den Umbau bemerkt und ist mit
        gezogen. Er bleibt, weil genau diese Verdrahtung die Luecke war.
        """
        for muster in (r"Lese-Beleg \$CarLog\s+\$BelegStart\[\$CarLog\]",
                       r"Lese-Beleg \$HeliLog\s+\$BelegStart\[\$HeliLog\]",
                       r"Test-Frisch \$HealthJson \$BelegStart\[\$HealthJson\]"):
            self.assertRegex(self.smoke, muster,
                             "die Auswertung liest den Beleg ohne Frischenachweis")
        # Und nirgends ein rohes Lesen der Sitzungslogs am Rumpf vorbei.
        roh = [z for z in re.findall(r"Get-Content \$[A-Za-z]+Log\b[^\n]*", self.smoke)
               if "Lese-Beleg" not in z]
        self.assertEqual([], roh, "ein Sitzungslog wird am Beleg-Waechter vorbei gelesen: %r" % roh)

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


class AbbruchErkennungTest(unittest.TestCase):
    """Kann der Rauchtest noch ROT werden, wenn eine Sitzung frueh stirbt?

    ``Test-Frisch`` beweist nur die AKTUALITAET eines Belegs, nicht dessen
    VOLLSTAENDIGKEIT. Ein Log, der nach dem Sitzungsbeginn geschrieben wurde
    und nach zwei Zeilen endet, besteht den Aktualitaetsnachweis - er ist
    schliesslich genau der, den der Lauf selbst geschrieben hat. Ob der
    Rauchtest darauf ROT wird, entscheidet sich einzig in den Pruefungen
    weiter unten, und die lagen bisher im Skript-Rumpf: nur mit einem
    startenden Editor erreichbar, also nie getestet.

    Diese Klasse fuettert genau diese Pruefungen mit abgeschnittenen Belegen.
    Sie sind deshalb aus der .ps1 herausgezogen und nicht nachgebaut.

    Der wichtigste Fall ist ``sitzung2_gestorben``: dort ist die
    Warteschranke der Sitzung (``MinCount 5`` fuer "WbDev Flug t=") ERFUELLT,
    die Abbruchschleife greift also ganz normal - und trotzdem fehlen die
    Gierproben. Genau daran darf es nicht gruen werden.

    Ebenfalls geprueft und nicht nebenssaechlich: der vollstaendige Lauf
    muss ALLE PRUEFUNGEN GRUEN schaffen. Ohne diese Kontrollgruppe waere
    jede rote Zeile oben bedeutungslos - ein Test, der nur "wird rot"
    behauptet, behauptet es auch dann, wenn gar nichts geprueft wird.
    """

    # Schranken kommen aus dem param-Block von smoke_test.ps1, nicht aus
    # festen Zahlen hier: sonst prueft der Harness mit den Werten von heute
    # gegen einen Code, dessen Schranken jemand verschoben hat - und meldet
    # weiter "gruen" fuer einen Lauf, den der echte Rauchtest fallen laesst.
    SCHRANKEN = ("MaxSpielMs", "MaxPrimComponents", "MaxInstances", "CalibRefMs",
                 "CalibIter", "CalibSamples", "MaxLoadFactor", "AbsoluteMaxSpielMs")

    # Die Logzeilen sind KOPIEN aus dem echten Format, nicht erfunden. Das ist
    # keine Feinheit: mit "Kursaenderung 5 Grad" statt "+5 Grad" fand der
    # Regex des Rauchtests 0 Messpunkte - die Kontrollgruppe schlug an und
    # das bliebe eine falsche Behauptung. Das Vorzeichen gehoert in die
    # Zeichenklasse des Pruefmusters, also auch in die Beispieldaten.
    CAR_VOLL = (
        "Log file open, 09/27/26 10:00:00\n"
        "Material-Bilanz: 0 Abschnitte ohne Material\n"
        "Straenge im Mittel: Spiel 18.4 ms\n"
        "Last-Inventar (Spiel-Strang): 8832 Primitive-Komponenten aktiv, "
        "565 Instanz-Komponenten mit 565557 Instanzen\n"
        "WbDev Fahrt t=1: Tempo 12 km/h, Drehzahl 900 U/min, Kursaenderung +5 Grad, Gang 1\n"
        "WbDev Fahrt t=2: Tempo 34 km/h, Drehzahl 2600 U/min, Kursaenderung +42 Grad, Gang 2\n"
        "WbDev Fahrt t=3: Tempo 41 km/h, Drehzahl 3200 U/min, Kursaenderung +38 Grad, Gang 3\n"
        "Log file closed, 09/27/26 10:00:20\n")
    HELI_VOLL = (
        "WbTeleport 2 ausgefuehrt: Distanz 125000 cm\n"
        "WbResetVehicle ausgefuehrt: Nick/Roll vorher (12.0/-45.0) -> nachher (0.2/0.3)\n"
        "WbDev Gierprobe t=1: Kurs 12 Grad (Gierrate 24.5 Grad/s)\n"
        "WbDev Gierprobe t=2: Kurs 40 Grad (Gierrate 18.0 Grad/s)\n"
        "WbDev Flug t=1: Hoehe 100 m, Vario +2.0 m/s, Fahrt 40 km/h\n"
        "WbDev Flug t=2: Hoehe 130 m, Vario +3.5 m/s, Fahrt 42 km/h\n"
        "WbDev Flug t=3: Hoehe 160 m, Vario +1.5 m/s, Fahrt 40 km/h\n"
        "Log file closed, 09/27/26 10:00:31\n")
    HEALTH_VOLL = ('{"healthy": true, "warnings": [], "perf": '
                   '{"meshSectionsWithoutMaterial": 0, "meshSectionsTotal": 1200}}')

    @classmethod
    def setUpClass(cls):
        text = SMOKE.read_text(encoding="utf-8", errors="replace")
        schranken = []
        for name in cls.SCHRANKEN:
            treffer = re.search(r"\$%s\s*=\s*([-\d.]+)" % name, text)
            if not treffer:
                raise AssertionError("Schranke $%s nicht im param-Block gefunden" % name)
            schranken.append("$%s = %s" % (name, treffer.group(1)))
        # Die Last-Messung auf eine winzige Runde zu schrumpfen ist KEINE
        # Verkleinerung: sie reproduziert genau den unbelasteten Fall, der auch
        # der Referenzwert $CalibRefMs beschreibt (Faktor wird auf 1.0
        # gehalten). Sonst wuerde jeder Test hier Sekunden brauchen.
        schranken.append("$CalibIter = 20000")
        schranken.append("$CalibSamples = 1")

        cls.basis = Path(tempfile.mkdtemp(prefix="wb_abruch_"))
        cls.skript = cls.basis / "auswertung.ps1"
        harness = (cls.HARNESS
                   .replace("__CAR__", cls.CAR_VOLL)
                   .replace("__HELI__", cls.HELI_VOLL)
                   .replace("__HEALTH__", cls.HEALTH_VOLL))
        cls.skript.write_text(
            "param([string]$Modus, [string]$Dir)\n\n"
            + "\n".join(schranken) + "\n\n"
            + "$Checks = New-Object System.Collections.ArrayList\n"
            + lade_funktionen(text, "Add-Check") + "\n\n"
            + lade_funktionen(text, "Test-Frisch") + "\n\n"
            + lade_funktionen(text, "Lese-Beleg") + "\n\n"
            + lade_funktionen(text, "Measure-LoadFactor") + "\n\n"
            + lade_funktionen(text, "Pruefe-Helilog") + "\n\n"
            + lade_funktionen(text, "Pruefe-Fahrlog") + "\n\n"
            + lade_funktionen(text, "Pruefe-Health") + "\n\n"
            + harness, encoding="utf-8")

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.basis, ignore_errors=True)

    def fahre(self, modus):
        ordner = Path(tempfile.mkdtemp(prefix="wb_belege_", dir=str(self.basis)))
        fertig = subprocess.run(
            ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
             "-File", str(self.skript), "-Modus", modus, "-Dir", str(ordner)],
            capture_output=True, text=True, encoding="utf-8", errors="replace",
            timeout=180)
        self.assertEqual(fertig.returncode, 0,
                         "Harness abgebrochen:\n%s%s" % (fertig.stdout, fertig.stderr))
        ergebnis = {}
        for zeile in fertig.stdout.splitlines():
            if "|" in zeile:
                name, zustand = zeile.rsplit("|", 1)
                ergebnis[name.strip()] = zustand.strip() == "OK"
        self.assertTrue(ergebnis, "der Harness hat keine Pruefung gemeldet:\n%s" % fertig.stdout)
        return ergebnis

    def test_ein_vollstaendiger_lauf_ist_vollstaendig_gruen(self):
        """Die Kontrollgruppe. Ohne sie waere alles Folgende wertlos."""
        pruefungen = self.fahre("voll")
        rot = [n for n, ok in pruefungen.items() if not ok]
        self.assertEqual([], rot, "der vollstaendige Lauf meldet %r rot" % rot)
        for erwartet in ("Fahren", "Perf-Regression", "Teleport", "ResetVehicle",
                         "HeliFly", "HeliYaw", "Materialien", "Health-Gate"):
            self.assertIn(erwartet, pruefungen,
                          "die Kontrollgruppe prueft %r nicht mit - sie zeigt "
                          "nicht, dass die Pruefungen einen Unterschied machen" % erwartet)

    def test_sitzung1_stirbt_nach_einem_fahrt_messpunkt(self):
        pruefungen = self.fahre("sitzung1_gestorben")
        self.assertFalse(pruefungen["Fahren"],
                         "ein Log mit EINEM von drei Fahrt-Messpunkten gilt als "
                         "erfolgreiche Fahrt - so sieht der Fall aus, in dem "
                         "der Editor nach dem Start stirbt")
        self.assertFalse(pruefungen["Perf-Regression"],
                         "ohne den 8-s-Diagnoseblock ist Perf unbewiesen und "
                         "muss als Fehler gelten, nicht als Erfolg")

    def test_sitzung1_stirbt_vor_dem_acht_sekunden_block(self):
        pruefungen = self.fahre("sitzung1_kein_block")
        self.assertFalse(pruefungen["Perf-Regression"],
                         "der 8-s-Block fehlt, die Pruefung meldet trotzdem Erfolg")

    def test_sitzung2_stirbt_trotz_erfuellter_warteschranke(self):
        """Der Kernfall: MinCount 5 ist erreicht, der Lauf stirbt normal.

        Genau hier liegt die Frage - der Rauchtest darf daraus kein Gruen
        machen, nur weil die Wartebedingung zufaellig einmal erfuellt war.
        """
        pruefungen = self.fahre("sitzung2_gestorben")
        self.assertTrue(pruefungen["HeliFly"],
                        "der Kontrollteil dieser Sitzung ist vollstaendig - "
                        "wenn selbst er rot waere, wuerde der Test nichts "
                        "ueber den Abbruch sagen")
        self.assertFalse(pruefungen["HeliYaw"],
                         "die Gierproben fehlen, weil die Sitzung nach dem "
                         "Flugprofil starb - das wird als Erfolg gemeldet")

    def test_kein_log_ueberhaupt(self):
        pruefungen = self.fahre("keine_logs")
        belege = [n for n in pruefungen if n.startswith("Beleg:")]
        self.assertEqual(2, len(belege), "beide Sitzungen muessen als Beleg fehlen: %r" % pruefungen)
        for name in belege:
            self.assertFalse(pruefungen[name], "%s wird als vorhanden gemeldet" % name)
        self.assertFalse(pruefungen["Fahren"])
        self.assertFalse(pruefungen["Teleport"])

    def test_keine_health_json(self):
        pruefungen = self.fahre("keine_health")
        self.assertFalse(pruefungen["Health-Gate"],
                         "ohne WbHealth.json gilt das Gesundheits-Gate als bestanden")
        self.assertFalse(pruefungen["Materialien"],
                         "ohne WbHealth.json gilt die Materialpruefung als bestanden")

    # Das Harness. Es schreibt die Belege in $Dir und ruft genau die Kette
    # auf, die auch der echte Lauf benutzt: Lese-Beleg -> Pruefe-*.
    HARNESS = r"""
$carLog  = Join-Path $Dir "smoke_car.log"
$heliLog = Join-Path $Dir "smoke_heli.log"
$healthLog = Join-Path $Dir "WbHealth.json"
$health  = $null
$healthErr = ""

$CAR_VOLL = @'
__CAR__
'@
$HELI_VOLL = @'
__HELI__
'@
$HEALTH_VOLL = '__HEALTH__'

$start = (Get-Date).AddSeconds(-30)     # der Beleg gilt als aus diesem Lauf

switch ($Modus) {
  "voll" {
    Set-Content -Path $carLog    -Value $CAR_VOLL    -Encoding UTF8
    Set-Content -Path $heliLog   -Value $HELI_VOLL   -Encoding UTF8
    Set-Content -Path $healthLog -Value $HEALTH_VOLL -Encoding UTF8
  }
  "sitzung1_gestorben" {
    # Log existiert und ist aktuell, stirbt aber nach dem ERSTEN Messpunkt.
    Set-Content -Path $carLog -Encoding UTF8 -Value @(
      "Log file open, 09/27/26 10:00:00"
      "Material-Bilanz: 0 Abschnitte ohne Material"
      "WbDev Fahrt t=1: Tempo 12 km/h, Drehzahl 900 U/min, Kursaenderung +5 Grad, Gang 1")
    Set-Content -Path $heliLog -Value $HELI_VOLL -Encoding UTF8
    Set-Content -Path $healthLog -Value $HEALTH_VOLL -Encoding UTF8
  }
  "sitzung1_kein_block" {
    # Alle drei Fahrt-Messpunkte da, aber der Editor starb VOR dem
    # 8-s-Diagnoseblock: Fahren ist gruen, Perf unbewiesen.
    Set-Content -Path $carLog -Encoding UTF8 -Value @(
      "Log file open, 09/27/26 10:00:00"
      "Material-Bilanz: 0 Abschnitte ohne Material"
      "WbDev Fahrt t=1: Tempo 12 km/h, Drehzahl 900 U/min, Kursaenderung +5 Grad, Gang 1"
      "WbDev Fahrt t=2: Tempo 34 km/h, Drehzahl 2600 U/min, Kursaenderung +42 Grad, Gang 2"
      "WbDev Fahrt t=3: Tempo 41 km/h, Drehzahl 3200 U/min, Kursaenderung +38 Grad, Gang 3")
    Set-Content -Path $heliLog -Value $HELI_VOLL -Encoding UTF8
    Set-Content -Path $healthLog -Value $HEALTH_VOLL -Encoding UTF8
  }
  "sitzung2_gestorben" {
    Set-Content -Path $carLog  -Value $CAR_VOLL  -Encoding UTF8
    # Fuenf "WbDev Flug t=" - die Warteschranke MinCount 5 ist ERFUELLT, die
    # Abbruchschleife greift ganz normal. Die Gierproben fehlen.
    Set-Content -Path $heliLog -Encoding UTF8 -Value @(
      "WbTeleport 2 ausgefuehrt: Distanz 125000 cm"
      "WbResetVehicle ausgefuehrt: Nick/Roll vorher (12.0/-45.0) -> nachher (0.2/0.3)"
      "WbDev Flug t=1: Hoehe 100 m, Vario +2.0 m/s, Fahrt 40 km/h"
      "WbDev Flug t=2: Hoehe 130 m, Vario +3.5 m/s, Fahrt 42 km/h"
      "WbDev Flug t=3: Hoehe 160 m, Vario +1.5 m/s, Fahrt 40 km/h"
      "WbDev Flug t=4: Hoehe 175 m, Vario +1.2 m/s, Fahrt 41 km/h"
      "WbDev Flug t=5: Hoehe 185 m, Vario +1.1 m/s, Fahrt 40 km/h")
    Set-Content -Path $healthLog -Value $HEALTH_VOLL -Encoding UTF8
  }
  "keine_logs" {
    # Der Editor kam gar nicht hoch - keine der beiden Logdateien existiert.
  }
  "keine_health" {
    Set-Content -Path $carLog  -Value $CAR_VOLL  -Encoding UTF8
    Set-Content -Path $heliLog -Value $HELI_VOLL -Encoding UTF8
  }
}

$car  = Lese-Beleg $carLog  $start "Sitzung 1 (Fahrzeug)"
$heli = Lese-Beleg $heliLog $start "Sitzung 2 (Heli)"

if (Test-Path $healthLog) {
  if (Test-Frisch $healthLog $start) {
    try { $health = Get-Content $healthLog -Raw | ConvertFrom-Json }
    catch { $healthErr = $_.Exception.Message }
  } else { $healthErr = "WbHealth.json ist vom vorigen Lauf uebernommen" }
} else { $healthErr = "keine WbHealth.json geschrieben" }

Pruefe-Helilog $heli
Pruefe-Fahrlog $car
Pruefe-Health $health $healthErr

foreach ($c in $Checks) { Write-Host ("{0}|{1}" -f $c.Name, $(if ($c.Ok) { "OK" } else { "FAIL" })) }
"""


if __name__ == "__main__":
    unittest.main()
