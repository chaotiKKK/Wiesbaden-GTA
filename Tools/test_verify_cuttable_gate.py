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
erzeugt Gate 4 erst danach. Ohne Belege kann die erste Klasse nichts
bauen, also ueberspringt sie (skip) statt zu scheitern. Wer das Gate
selbst pruefen will, fahrt es im Arbeitsbaum, wo die Belege liegen:

    python -m unittest Tools.test_verify_cuttable_gate -v

Die zweite Klasse (GatePrueftSichSelbstTest) hat diesen Ausweg nicht:
sie baut Log und Bilder selbst und laeuft ueberall. Sie kam am
27.09.2026 dazu, nachdem der fuenfte Fehlschlag eines Pushes gezeigt
hatte, dass ein Selbsttest, der auf fremde Belege wartet, im entscheiden-
den Moment gar nicht laeuft - oder schlimmer: gegen die Belege eines
anderen Laufs laeuft und dabei den echten Lauf beurteilt.

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


def _hintergrund(im):
    """Dunkler Abendhimmel mit Struktur.

    Ein einfarbig schwarzes Bild ist winzig (< 10 000 Byte) und das Gate
    weist es als "zu klein fuer ein Bild" ab. Die Struktur haelt die PNG
    gross und ist selbst weit unter der Glutschwelle.
    """
    px = im.load()
    for y in range(im.size[1]):
        for x in range(im.size[0]):
            px[x, y] = (10 + (x * y) % 23, 12, 14)
    return px


def bild_dunkel(diag, datei):
    """Ein Bild ganz ohne Glut - der Normalfall fuer das Vergleichsbild."""
    from PIL import Image
    im = Image.new("RGB", (1280, 720))
    _hintergrund(im)
    im.save(os.path.join(diag, datei))


def bild_mit_schnittkante(diag, datei):
    """Legt eines der drei Winkelbilder an: dunkel mit gluehender Kante."""
    from PIL import Image
    im = Image.new("RGB", (1280, 720))
    px = _hintergrund(im)
    for y in range(300, 360):
        for x in range(200, 1080):
            px[x, y] = (255, 150, 60)
    im.save(os.path.join(diag, datei))


# Ein vollstaendiger, gültiger Lauf als Text - die Zeilen, die das Gate liest.
SYNTHETISCHER_LAUF = [
    "LogWbVehicles: WbCutShots: eigene Kamera (FOV 90.0) als ViewTarget gesetzt.",
    "LogWbVehicles: WbCutShots t=1.0: Trenn-Stueck bei (100, 200, 0) aufgestellt, "
    "Glutzeit 30.0 s (Spielwert 6 s).",
    "LogWbVehicles: WbCutShots: Bild schnitt_00_vorher gespeichert. ungeklafft, "
    "nah an der Kante. | Kamera (15, 125, 35), 119.0 cm vom Ziel, "
    "Blickwinkel 0.0 Grad",
    "LogWbVehicles: WbCutShots t=2.0: geschnitten = 1, gefallen = 1, Glut an ja.",
    "LogWbVehicles: WbCutShots: Bild schnitt_01_nah gespeichert. nah an der "
    "Kante, Glut noch 28.2 s, Licht 2821 Candela, Winkel 1, Bild schnitt_01_nah | "
    "Kamera (-115382, -121703, 11140), 119 cm vom Ziel, Blickwinkel 0 Grad",
    "LogWbVehicles: WbCutShots: Bild schnitt_02_schraeg gespeichert. schraeg von "
    "oben, Glut noch 26.8 s, Licht 2681 Candela, Winkel 2, Bild schnitt_02_schraeg | "
    "Kamera (-115222, -121753, 11255), 209 cm vom Ziel, Blickwinkel 0 Grad",
    "LogWbVehicles: WbCutShots: Bild schnitt_03_weit gespeichert. weit von der "
    "Seite, Glut noch 25.4 s, Licht 2541 Candela, Winkel 3, Bild schnitt_03_weit | "
    "Kamera (-115527, -121843, 11235), 341 cm vom Ziel, Blickwinkel 0 Grad",
    "LogWbVehicles: WbCutShots: fertig - getrennt 1",
]


def synthetischer_lauf(diag):
    """Baut im Zielordner Log und vier Bilder - einen Lauf, den das Gate akzeptiert.

    WARUM EIGENE BELEGE: der Selbsttest hing an den Bildern eines ECHTEN
    Laufs. Im Commit-Worktree gibt es keine, dort hat sich Gate 4 also nie
    selbst geprueft. Zwei Gate-Laeufe spaeter tat es das - gegen die
    Belege eines fremden Laufs, den ein anderer Thread zur selben Zeit
    gefahren hatte (der fuenfte Fehlschlag im Push vom 27.09.2026:
    test_01_echter_lauf_bleibt_gruen). Das Gate prueft sich damit
    abhaengig davon, ob gerade jemand ein Bild ablegt - das ist genau die
    Abhaengigkeit, die ein Selbsttest nicht haben darf. Mit eigenen
    Belegen laeuft er ueberall, im Commit-Worktree genauso wie hier.
    """
    for datei in BILDER[1:]:
        bild_mit_schnittkante(diag, datei)
    bild_dunkel(diag, "schnitt_00_vorher.png")
    logpfad = os.path.join(diag, "lauf.log")
    with io.open(logpfad, "w", encoding="utf-8") as fh:
        fh.write("\n".join(SYNTHETISCHER_LAUF) + "\n")
    return logpfad


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
    px = _hintergrund(im)
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


@unittest.skipUnless(HAT_PIL, "Pillow fehlt - ohne die Bibliothek lassen sich "
                              "die PNGs nicht messen")
class GatePrueftSichSelbstTest(unittest.TestCase):
    """Dieselben Faelle mit selbstgebauten Belegen - laeuft ueberall.

    Die Klasse darueber braucht die Belege eines echten Laufs und springt
    im Commit-Worktree weg, in dem Gate 4 laeuft. Diese hier baut ihre
    Belege selbst und prueft deshalb auch dort, wo das Gate tatsaechlich
    arbeitet. Sie ist die Antwort auf den fuenften Fehlschlag vom
    27.09.2026: test_01_echter_lauf_bleibt_gruen war nur aufgetreten,
    weil ein fremder Gate-Lauf kurz vorher Belege abgelegt hatte - der
    Selbsttest hing an etwas, das er nicht kontrolliert.
    """

    # (Name, Log-Ersetzung oder None, Bildaktion oder None, erwartet ROT)
    FAELLE = (
        ("gut", None, None, False),
        ("kamera_daneben", ("Blickwinkel 0 Grad", "Blickwinkel 80 Grad"),
         None, True),
        ("kein_fall", ("geschnitten = 1, gefallen = 1, Glut an ja",
                       "geschnitten = 1, gefallen = 0, Glut an nein"), None, True),
        ("kein_abschluss", ("WbCutShots: fertig - getrennt",
                            "WbCutShots: abgebrochen - getrennt"), None, True),
        ("glut_kuehlt_nicht", ("Glut noch 28.2 s", "Glut noch 1.0 s"),
         None, True),
        ("kein_licht", ("Licht 2821 Candela", "Licht 40 Candela"), None, True),
        ("bild_ohne_glut", None,
         ("schnitt_01_nah.png", "schnitt_00_vorher.png"), True),
        ("bild_fehlt", None, ("__weg__", "schnitt_03_weit.png"), True),
        ("cutter_glut_unten", None,
         lambda d: bild_mit_cutter_glut(d, False), False),
        ("glut_oben", None, lambda d: bild_mit_cutter_glut(d, True), True),
    )

    def setUp(self):
        self.schnipsel = tempfile.mkdtemp(prefix="wb_cutsyn_")
        self.addCleanup(shutil.rmtree, self.schnipsel, ignore_errors=True)

    def baue_fall(self, ersetzung, bildaktion):
        diag = tempfile.mkdtemp(dir=self.schnipsel)
        logpfad = synthetischer_lauf(diag)
        if bildaktion:
            if callable(bildaktion):
                bildaktion(diag)
            else:
                quelle, ziel = bildaktion
                if quelle == "__weg__":
                    os.remove(os.path.join(diag, ziel))
                else:
                    shutil.copy(os.path.join(diag, quelle), os.path.join(diag, ziel))
        with io.open(logpfad, "r", encoding="utf-8") as fh:
            text = fh.read()
        if ersetzung:
            alt, neu = ersetzung
            self.assertIn(alt, text, "der synthetische Lauf enthaelt %r nicht" % (alt,))
            text = text.replace(alt, neu)
        with io.open(logpfad, "w", encoding="utf-8") as fh:
            fh.write(text)
        return logpfad, diag

    def pruefe_fall(self, ersetzung, bildaktion, erwartet_rot):
        logpfad, diag = self.baue_fall(ersetzung, bildaktion)
        code, ausgabe = gate(logpfad, diag)
        if erwartet_rot:
            self.assertNotEqual(code, 0, "das Gate blieb gruen:\n%s" % ausgabe)
        else:
            self.assertEqual(code, 0, "der synthetische Lauf wurde abgewiesen:\n%s"
                             % ausgabe)

    def test_der_lauf_ohne_fehler_bleibt_gruen(self):
        self.pruefe_fall(*self.FAELLE[0][1:])

    def test_kamera_80_grad_daneben(self):
        self.pruefe_fall(*self.FAELLE[1][1:])

    def test_nichts_gefallen_keine_glut(self):
        self.pruefe_fall(*self.FAELLE[2][1:])

    def test_abbruch_ohne_abschlusszeile(self):
        self.pruefe_fall(*self.FAELLE[3][1:])

    def test_glut_kuehlt_nicht_ab(self):
        self.pruefe_fall(*self.FAELLE[4][1:])

    def test_licht_zu_schwach(self):
        self.pruefe_fall(*self.FAELLE[5][1:])

    def test_bild_ohne_glut_unter_richtigem_namen(self):
        self.pruefe_fall(*self.FAELLE[6][1:])

    def test_bild_fehlt(self):
        self.pruefe_fall(*self.FAELLE[7][1:])

    def test_cutter_glut_unten_ist_kein_schnittfehler(self):
        self.pruefe_fall(*self.FAELLE[8][1:])

    def test_glut_oben_im_vergleichsbild_bleibt_rot(self):
        self.pruefe_fall(*self.FAELLE[9][1:])

    def test_diese_klasse_wird_nicht_uebersprungen(self):
        """Der ganze Punkt: diese Klasse darf sich nicht selbst abschalten.

        Geprueft wird der Skip-Dekorator selbst, nicht irgendein Wort im
        Text: die Klasse darueber traegt @skipUnless(beleg_vorhanden()),
        und genau daran ist der fuenfte Fehlschlag vom 27.09.2026
        haengen geblieben.
        """
        typ = type(self)
        self.assertFalse(getattr(typ, "__unittest_skip__", False),
                         "die Klasse traegt einen Skip-Dekorator - sie waere "
                         "im Commit-Worktree wieder stumm")
        methoden = [m for m in dir(typ) if m.startswith("test_")]
        self.assertGreaterEqual(len(methoden), 10)
        for name in methoden:
            with self.subTest(test=name):
                self.assertFalse(
                    getattr(getattr(typ, name), "__unittest_skip__", False),
                    "%s traegt einen Skip-Dekorator" % name)


if __name__ == "__main__":
    unittest.main(verbosity=2)
