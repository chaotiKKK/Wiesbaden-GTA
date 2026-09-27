r"""Selbsttest des Plasmacutter-Gates (Tools/verify_cuttable.py).

Ein Gate, das nur einmal gruen war, ist kein Gate. Dieser Test baut die
Fehlerfaelle nach, die in der Bildfolge wirklich vorgekommen sind, und
verlangt, dass das Gate bei jedem einzelnen ROT wird - darunter zwei, die
eine Log-Pruefung allein nicht gesehen haette:

  * Kamera 80 Grad daneben: alle vier "Bild gespeichert"-Zeilen stehen im
    Log, trotzdem war kein Trenn-Stueck im Bild.
  * Bild ohne gluehende Kante unter dem richtigen Namen: das Log ist
    einwandfrei, nur die PNG ist die falsche.

WARUM ER IM PUSH-GATE MEISTENS UEBERSPRINGT: Saved\ ist nicht versioniert.
Im Commit-Worktree liegen beim Start der Python-Suiten keine Bilder - die
erzeugt Gate 4 erst danach. Ohne Belege kann der Test nichts bauen, also
ueberspringt er (skip) statt zu scheitern. Wer das Gate selbst pruefen
will, fahrt es im Arbeitsbaum, wo die Belege liegen:

    python -m unittest Tools.test_verify_cuttable_gate -v

Gate 4 prueft im Push NICHT sich selbst - es faehrt die Pruefungen mit
frischen Bildern. Dieser Test traegt die zweite Haelfte: dass die
Pruefungen ueberhaupt etwas koennen.
"""

import io
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

try:
    import PIL  # noqa: F401
    HAT_PIL = True
except ImportError:
    HAT_PIL = False

sys.path.insert(0, str(Path(__file__).resolve().parent))

WURZEL = Path(__file__).resolve().parent.parent
GATE = WURZEL / "Tools" / "verify_cuttable.py"
DIAG = WURZEL / "Saved" / "Diagnose"
LOG = WURZEL / "Saved" / "Logs" / "wb_cut_schnitt.log"
BILDER = ("schnitt_00_vorher.png", "schnitt_01_nah.png",
          "schnitt_02_schraeg.png", "schnitt_03_weit.png")

# (Name, Log-Ersetzung oder None, Bildaktion oder None, erwartet ROT)
#   Log-Ersetzung: (alt, neu) - der Fall enthaelt "alt" sonst geht er nicht.
#   Bildaktion: (quelle, ziel) wird kopiert, "__weg__" loescht die Zieldatei.
#   bild_ohne_glut legt das Vergleichsbild unter dem Namen des Nahbilds ab:
#   Log einwandfrei, Bild falsch - genau der Fall, den nur die Pixelpruefung
#   fangen kann.
FAELLE = (
    ("gut", None, None, False),
    ("kamera_daneben", ("Blickwinkel 0 Grad", "Blickwinkel 80 Grad"),
     None, True),
    ("kein_fall",
     ("geschnitten = 1, gefallen = 1, Glut an ja",
      "geschnitten = 1, gefallen = 0, Glut an nein"), None, True),
    ("kein_abschluss", ("WbCutShots: fertig - getrennt",
                        "WbCutShots: abgebrochen - getrennt"), None, True),
    ("glut_kuehlt_nicht", ("Glut noch 28.2 s", "Glut noch 1.0 s"),
     None, True),
    ("bild_ohne_glut", None,
     ("schnitt_00_vorher.png", "schnitt_01_nah.png"), True),
    ("bild_fehlt", None, ("__weg__", "schnitt_03_weit.png"), True),
    ("glut_im_vergleichsbild", None,
     ("schnitt_02_schraeg.png", "schnitt_00_vorher.png"), True),
)


def beleg_vollstaendig():
    """Passt der Log zu den Bildern - und ist er ueberhaupt fertig?

    GEMESSEN am 27.09.2026, beim ersten Push dieses Branches: die Python-
    Suiten laufen VOR Gate 4. Sie fanden die Reste eines frueheren, TEILWEISE
    gelaufenen Schnittlaufs im Gate-Worktree und hielten sie fuer einen
    gültigen Beleg - die Dateien existierten, mehr wurde nicht gefragt. Die
    Tests bauten daraus ihre Faelle und meldeten Falsches (2 Fehler).

    Ein Log zaehlt nur, wenn er ANFANG und ENDE hat. Genau so unterscheidet
    der echte Lauf einen Abbruch von einem Lauf, und genau daran liess sich
    der Fehler festmachen.
    """
    if not LOG.exists() or not all((DIAG / b).exists() for b in BILDER):
        return False
    try:
        text = LOG.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return False
    return ("Log file open" in text
            and "Log file closed" in text
            and "WbCutShots: fertig - getrennt" in text)


def beleg_vorhanden():
    return beleg_vollstaendig()


def gate(logpfad, diag):
    """Das Gate als Unterprozess - so, wie der Wrapper es aufruft."""
    args = [sys.executable, str(GATE), str(logpfad), "--kein-html",
            "--diag", str(diag)]
    proc = subprocess.Popen(args, cwd=str(WURZEL), stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT)
    ausgabe = proc.communicate()[0].decode("utf-8", "replace")
    return proc.returncode, ausgabe


@unittest.skipUnless(HAT_PIL, "Pillow fehlt - ohne die Bibliothek lassen sich "
                               "die PNGs nicht messen")
@unittest.skipUnless(beleg_vorhanden(),
                     "keine vollstaendigen Belege in Saved/Diagnose bzw. kein "
                     "abgeschlossener Lauf-Log - im Commit-Worktree normal, dort "
                     "faehrt Gate 4 den Lauf erst danach")
class CuttableGateFaelltTest(unittest.TestCase):
    """Jeder Fehlerfall muss das Gate zu ROT bringen - und der echte Lauf bleibt gruen."""

    def setUp(self):
        self.quelle = str(WURZEL / "Saved" / "Diagnose")
        with io.open(str(LOG), "r", encoding="utf-8", errors="replace") as fh:
            self.log = fh.read()
        # Der Beleg muss zu DEN BILDERN passen, die gleich kopiert werden.
        # Sonst baut der Test seine Faelle aus einem Log, der zu einem
        # anderen Lauf gehoert - und meldet damit etwas Falsches.
        self.assertIn("WbCutShots: fertig - getrennt", self.log,
                      "der Log ist kein vollstaendiger Lauf")
        self.schnipsel = tempfile.mkdtemp(prefix="wb_cuttest_")
        self.addCleanup(shutil.rmtree, self.schnipsel, ignore_errors=True)

    def test_der_beleg_muss_vollstaendig_sein(self):
        """Der Fund vom 27.09.2026 als Test: ein HALBER Lauf ist kein Beleg.

        Genau daran scheiterte der erste Push dieses Branches - die Suiten
        liefen vor Gate 4 und fanden die Reste eines abgebrochenen Laufs.
        """
        self.assertTrue(beleg_vollstaendig(),
                        "der Beleg wurde als vollstaendig angenommen, ist es aber nicht")
        text = LOG.read_text(encoding="utf-8", errors="replace")
        for merkmal in ("Log file open", "Log file closed",
                        "WbCutShots: fertig - getrennt"):
            self.assertIn(merkmal, text, merkmal)

    def test_ein_abgebrochener_log_ist_kein_beleg(self):
        """Ein Log ohne Ende darf NICHT als Beleg durchgehen."""
        alt = LOG.read_text(encoding="utf-8", errors="replace")
        try:
            LOG.write_text(alt.replace("WbCutShots: fertig - getrennt", "abgebrochen"),
                           encoding="utf-8")
            self.assertFalse(beleg_vollstaendig(),
                             "ein abgebrochener Lauf gilt als Beleg")
        finally:
            LOG.write_text(alt, encoding="utf-8")
        self.assertTrue(beleg_vollstaendig(), "der Test hat den Log nicht wiederhergestellt")

    def test_fehlende_bilder_sind_kein_beleg(self):
        alt = (DIAG / BILDER[0]).read_bytes()
        try:
            (DIAG / BILDER[0]).unlink()
            self.assertFalse(beleg_vorhanden(), "fehlendes Bild gilt als Beleg")
        finally:
            (DIAG / BILDER[0]).write_bytes(alt)

    def baue_fall(self, ersetzung, bildaktion):
        diag = tempfile.mkdtemp(dir=self.schnipsel)
        for bild in BILDER:
            shutil.copy(os.path.join(self.quelle, bild), os.path.join(diag, bild))
        if bildaktion:
            quelle, ziel = bildaktion
            if quelle == "__weg__":
                os.remove(os.path.join(diag, ziel))
            else:
                shutil.copy(os.path.join(diag, quelle), os.path.join(diag, ziel))
        text = self.log
        if ersetzung:
            alt, neu = ersetzung
            self.assertIn(alt, text,
                          "der Lauf-Log enthaelt %r nicht - der Fall laesst "
                          "sich nicht bauen, die Erwartung stimmt nicht mehr "
                          "mit dem Lauf" % (alt,))
            text = text.replace(alt, neu)
        logpfad = os.path.join(diag, "lauf.log")
        with io.open(logpfad, "w", encoding="utf-8") as fh:
            fh.write(text)
        return logpfad, diag

    def pruefe_fall(self, ersetzung, bildaktion, erwartet_rot):
        logpfad, diag = self.baue_fall(ersetzung, bildaktion)
        code, ausgabe = gate(logpfad, diag)
        if erwartet_rot:
            self.assertNotEqual(code, 0, "das Gate blieb gruen:\n%s" % ausgabe)
        else:
            self.assertEqual(code, 0, "der echte Lauf wurde abgewiesen:\n%s" % ausgabe)

    def test_01_echter_lauf_bleibt_gruen(self):
        self.pruefe_fall(None, None, False)

    def test_02_kamera_80_grad_daneben(self):
        self.pruefe_fall(("Blickwinkel 0 Grad", "Blickwinkel 80 Grad"), None, True)

    def test_03_nichts_gefallen_keine_glut(self):
        self.pruefe_fall(("geschnitten = 1, gefallen = 1, Glut an ja",
                          "geschnitten = 1, gefallen = 0, Glut an nein"), None, True)

    def test_04_abbruch_ohne_abschlusszeile(self):
        self.pruefe_fall(("WbCutShots: fertig - getrennt",
                          "WbCutShots: abgebrochen - getrennt"), None, True)

    def test_05_glut_kuehlt_nicht_ab(self):
        self.pruefe_fall(("Glut noch 28.2 s", "Glut noch 1.0 s"), None, True)

    def test_06_bild_ohne_glut_unter_richtigem_namen(self):
        self.pruefe_fall(None, ("schnitt_00_vorher.png", "schnitt_01_nah.png"), True)

    def test_07_bild_fehlt(self):
        self.pruefe_fall(None, ("__weg__", "schnitt_03_weit.png"), True)

    def test_08_glut_im_vergleichsbild(self):
        self.pruefe_fall(None, ("schnitt_02_schraeg.png", "schnitt_00_vorher.png"), True)


if __name__ == "__main__":
    unittest.main(verbosity=2)
