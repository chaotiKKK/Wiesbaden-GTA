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


def bild_mit_cutter_glut(diag, oben):
    """Legt schnitt_00_vorher.png selbst an: Streifen wie der Plasmacutter.

    GEMESSEN am 27.09.2026 am echten Vergleichsbild des Push-Laufs: ein
    senkrechter Streifen x 560..599, y 550..719, RGB 175/124/80 - die Glut
    der Waffe, die der Ego-Kamera im Bild steht. Unten macht sie 0,74 % des
    Bildes aus, oben 0,00 %.

    `oben` legt denselben Streifen in die obere Bildhaelfte: dort ist er die
    Schnittkante, und das Gate muss ROT werden. Sonst haette die Messung
    durch den Beschnitt keine Zaehne mehr.
    """
    from PIL import Image
    im = Image.new("RGB", (1280, 720))
    px = im.load()
    for y in range(720):
        for x in range(1280):
            # Leichte Struktur statt Einheitsschwarz: sonst ist die PNG so klein,
            # dass das Gate sie als "zu klein fuer ein Bild" abweist.
            px[x, y] = (10 + (x * y) % 23, 12, 14)
    y0 = 100 if oben else 550
    for y in range(y0, 720):
        for x in range(560, 600):
            px[x, y] = (175, 124, 80)
    im.save(os.path.join(diag, "schnitt_00_vorher.png"))


def beleg_vorhanden():
    return LOG.exists() and all((DIAG / b).exists() for b in BILDER)


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
                     "keine Belege in Saved/Diagnose bzw. kein Lauf-Log - "
                     "im Commit-Worktree normal, dort fahrt Gate 4 den Lauf "
                     "erst danach")
class CuttableGateFaelltTest(unittest.TestCase):
    """Jeder Fehlerfall muss das Gate zu ROT bringen - und der echte Lauf bleibt gruen."""

    def setUp(self):
        self.quelle = str(WURZEL / "Saved" / "Diagnose")
        with io.open(str(LOG), "r", encoding="utf-8", errors="replace") as fh:
            self.log = fh.read()
        self.schnipsel = tempfile.mkdtemp(prefix="wb_cuttest_")
        self.addCleanup(shutil.rmtree, self.schnipsel, ignore_errors=True)

    def baue_fall(self, ersetzung, bildaktion):
        diag = tempfile.mkdtemp(dir=self.schnipsel)
        for bild in BILDER:
            shutil.copy(os.path.join(self.quelle, bild), os.path.join(diag, bild))
        if bildaktion:
            if callable(bildaktion):
                bildaktion(diag)
            else:
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

    def test_09_cutter_glut_unten_ist_kein_schnittfehler(self):
        # DER Fall, der Gate 4 am 27.09.2026 rot gemacht hat: 0,68 % Glut im
        # Vergleichsbild, davon 2115 Pixel die eigene Waffe unten mittig.
        self.pruefe_fall(None, lambda d: bild_mit_cutter_glut(d, False), False)

    def test_10_glut_oben_im_vergleichsbild_bleibt_rot(self):
        # Gegenprobe: der Beschnitt darf die Pruefung nicht entwaffnen.
        self.pruefe_fall(None, lambda d: bild_mit_cutter_glut(d, True), True)


if __name__ == "__main__":
    unittest.main(verbosity=2)
