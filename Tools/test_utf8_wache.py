# coding: utf-8
"""Wache: die Gate-Tools muessen auch unter PYTHONUTF8=1 bestehen.

Ab Python 3.15 (PEP 686) ist UTF-8 der Vorgabe-Modus: stdio dekodiert
dann UTF-8/strict statt Locale, implizite open()-Aufrufe ebenfalls. Die
volle Suite unter PYTHONUTF8=1 hat den Mechanismus am 29.09.2026 live
gezeigt: ein subprocess-Reader-Thread starb STILL an UnicodeDecodeError,
als ein Test die deutsche mklink-Ausgabe las (OEM-"ue" in "Verbindung
erstellt fuer ...") - kein Fehler, kein Abbruch, nur ein Test, der nie
endete. Genau diese Fehlerklasse soll hier dauerhaft bewacht werden:
PYTHONUTF8=1 simuliert auf 3.14 exakt die 3.15-Vorgaben, also laeuft
diese Wache die Gate-Tool-Suiten in einem KINDPROZESS mit gesetztem
PYTHONUTF8=1 und besteht nur, wenn sie dort gruen enden.

Der Kindprozess ist noetig, weil PYTHONUTF8 beim Prozessstart gelesen
wird - im laufenden Interpreter laesst sie sich nicht zuverlaessig
setzen. Die drei Messungen:
  1. Ist PYTHONUTF8=1 sichtbar durchgereicht? (die 3.15-Umgebung)
  2. Stoppt die Wache ihren eigenen Unterlauf? (keine Endlosschleife)
  3. Bestehen die Gate-Tool-Suiten in dieser Umgebung?

Der Suite-Lauf dauert einige Minuten (345 Tests, gemessen ~6 min) - er
liegt bewusst in dieser einen Datei und nicht in discover's Standard-
Runde doppelt: discover startet ihn einmal, der Kindprozess sonst nie.
"""
import os
import subprocess
import sys
import unittest

TOOLSPFAD = os.path.dirname(os.path.abspath(__file__))
PROJEKT = os.path.dirname(TOOLSPFAD)

# Marker fuer den Unterlauf: setzt die Wache im Kindprozess, damit dieser
# NICHT wieder sich selbst startet (der Suite-Lauf discover'd auch diese
# Datei). Der Kindprozess beweist per Skip, dass er den Marker sah.
INNER_MARKER = "WB_UTF8_WACHE_INNER"

# Die Gate-Werkzeuge mit ihren Suiten. Alle Module muessen getrackt und
# in Tools/ liegen; test_releases_bilder_ausrichten existiert (noch)
# nicht - bewusst nicht geraten, sondern ergaenzen, wenn er kommt.
GATE_MODULEE = [
    "Tools.test_vor_dem_commit",
    "Tools.test_gate_worktree",
    "Tools.test_push_ref_wache",
    "Tools.test_release_abgleich",
    "Tools.test_releases_texte_ausrichten",
    "Tools.test_releases_ausrichten",
    "Tools.test_releases_oeffentlich",
    "Tools.test_schaufenster",
    "Tools.test_platten_waechter",
]

KIND_TIMEOUT = 1800    # Sekunden; die Suite brauchte gemessen ~370 s
ZUSCHNITT = 3000       # Fehlermeldung auf das Suite-Ende kuerzen


def child_umgebung(extra=None):
    umgebung = dict(os.environ)
    umgebung.pop(INNER_MARKER, None)     # nie von oben erben
    if extra:
        umgebung.update(extra)
    return umgebung


class WacheTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Der Unterlauf (Marker gesetzt) springt hier ab - er hat seine
        # Aufgabe (Existenz + Marker sichtbar) erfuellt, sobald er
        # ueberhaupt laeuft. Ohne den Abbruch wuerde der Suite-Lauf
        # unten im Kind wieder diese Datei starten: Endlosschleife.
        if os.environ.get(INNER_MARKER):
            raise unittest.SkipTest(
                "%s gesetzt - Unterlauf der Wache gestoppt" % INNER_MARKER)
        for modul in GATE_MODULEE:
            pfad = os.path.join(TOOLSPFAD, modul.split(".", 1)[1] + ".py")
            if not os.path.isfile(pfad):
                raise unittest.SkipTest("Gate-Modul fehlt: %s" % pfad)

    def test_pythonutf8_gesetzt(self):
        """Die 3.15-Umgebung: PYTHONUTF8=1 muss messbar sein.

        Im normalen Gate-Lauf ist die Variable bewusst NICHT gesetzt -
        dieser Test beweist nur, dass der Kindprozess sie wirklich
        erbt, und macht fehlende Durchreichung sichtbar, statt still
        eine Locale-Messung als 3.15-Messung auszugeben.
        """
        umgebung = child_umgebung({"PYTHONUTF8": "1"})
        # errors="replace": Kind-Ausgabe dekodier-tolerant lesen (PEP 686 -
        # ohne Handler dekodiert der Textmodus ab 3.15 strict).
        fertig = subprocess.run(
            [sys.executable, "-c", "import os; print(os.environ.get('PYTHONUTF8'))"],
            capture_output=True, text=True, errors="replace",
            env=umgebung, cwd=PROJEKT, timeout=120)
        self.assertEqual(fertig.returncode, 0, fertig.stderr)
        self.assertEqual(fertig.stdout.strip(), "1",
                         "PYTHONUTF8 erreichte den Kindprozess nicht")

    def test_unterlauf_gestoppt(self):
        """Der Suite-Lauf im Kind startet NICHT wieder die Wache."""
        umgebung = child_umgebung({"PYTHONUTF8": "1", INNER_MARKER: "1"})
        fertig = subprocess.run(
            [sys.executable, "-m", "unittest", "-v", "Tools.test_utf8_wache"],
            capture_output=True, text=True, errors="replace",
            env=umgebung, cwd=PROJEKT, timeout=600)
        self.assertEqual(fertig.returncode, 0,
                         "Unterlauf schlug fehl statt zu skippen: %s"
                         % (fertig.stdout + fertig.stderr)[-ZUSCHNITT:])
        self.assertIn("Unterlauf der Wache gestoppt", fertig.stdout + fertig.stderr,
                      "Der Unterlauf bewies den Marker nicht - die Wache "
                      "wuerde sich im Suite-Lauf selbst starten")

    def test_gate_werkzeuge_unter_utf8(self):
        """Die Gate-Tool-Suiten bestehen unter PYTHONUTF8=1.

        Genau der Lauf, der am 29.09.2026 den stillen Decode-Tod im
        mklink-Test zeigte - jetzt als Beweis, nicht als einmaliger
        Befund. Scheitert er, steht im Anhang das Suite-Ende (die
        tracebacks der UTF-8-Opfer).
        """
        if os.environ.get("PYTHONUTF8") == "1":
            # Schon unter PYTHONUTF8=1 gemessen: der native Lauf MISST dann
            # bereits die 3.15-Vorgabe - ein Kindlauf waere doppelte Arbeit
            # (+6 min Suite-Zeit ohne neuen Befund).
            raise unittest.SkipTest(
                "Laeuft bereits unter PYTHONUTF8=1 - dieser Lauf misst die "
                "3.15-Vorgabe bereits selbst, ein Kindlauf waere doppelt")
        umgebung = child_umgebung({"PYTHONUTF8": "1"})
        fertig = subprocess.run(
            [sys.executable, "-m", "unittest", *GATE_MODULEE],
            capture_output=True, text=True, errors="replace",
            env=umgebung, cwd=PROJEKT, timeout=KIND_TIMEOUT)
        kombiniert = fertig.stdout + fertig.stderr
        self.assertEqual(
            fertig.returncode, 0,
            "Gate-Suiten unter PYTHONUTF8=1 nicht gruen - Ende:\n%s"
            % kombiniert[-ZUSCHNITT:])
        self.assertIn("\nOK", kombiniert,
                      "Kein gruennes Suite-Ende unter PYTHONUTF8=1:\n%s"
                      % kombiniert[-ZUSCHNITT:])


if __name__ == "__main__":
    unittest.main()
