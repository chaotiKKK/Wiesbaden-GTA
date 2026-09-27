r"""Selbsttest des Plasmacutter-Gates (Tools/verify_cuttable.py).

Ein Gate, das nur einmal gruen war, ist kein Gate. Dieser Test baut die
Fehlerfaelle nach, die in der Bildfolge wirklich vorgekommen sind, und
verlangt, dass das Gate bei jedem einzelnen ROT wird - darunter zwei, die
eine Log-Pruefung allein nicht gesehen haette:

  * Kamera 80 Grad daneben: alle vier "Bild gespeichert"-Zeilen stehen im
    Log, trotzdem war kein Trenn-Stueck im Bild.
  * Bild ohne gluehende Kante unter dem richtigen Namen: das Log ist
    einwandfrei, nur die PNG ist die falsche.

WARUM DIE ERSTE KLASSE IM PUSH-GATE UEBERSPRINGT: Saved\ ist nicht versioniert, und die
Python-Suiten fahren VOR Gate 4 - sie koennen dessen Beleg gar nicht sehen.
Sie verlangt deshalb nicht nur, dass der Log vollstaendig ist (Anfang
und Ende), sondern dass er und die vier Bilder NACH der Zeitmarke des
Push-Laufs geschrieben wurden. Er laeuft also gegen die Belege des geplanten
Commits oder gar nicht.

Das ist keine Feinheit. GEMESSEN am 27.09.2026: `Saved/` steht in .gitignore,
der Gate-Worktree wird wiederverwendet, und `git clean -fd` OHNE -x laesst
ignorierte Dateien stehen. Ein vollstaendiger Log vom VORRIGEN Push erfuellt
alle drei Vollstaendigkeitsbedingungen und wurde als eigener Beleg gelesen -
sechs Tests, die einen anderen Commit pruefen und dabei gruen melden.

Wer das Gate selbst pruefen will, fährt es im Arbeitsbaum, wo die Belege
liegen (dort gibt es keine Zeitmarke, und der eigene Lauf gilt als Beleg):

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
import time
import unittest
from pathlib import Path
from unittest import mock

try:
    import PIL  # noqa: F401
    HAT_PIL = True
except ImportError:
    HAT_PIL = False

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gate_worktree  # noqa: E402

WURZEL = Path(__file__).resolve().parent.parent
GATE = WURZEL / "Tools" / "verify_cuttable.py"
DIAG = WURZEL / "Saved" / "Diagnose"
LOG = WURZEL / "Saved" / "Logs" / "wb_cut_schnitt.log"
MARKE = WURZEL / gate_worktree.BELEG_MARKE
BELEG_NAME = gate_worktree.BELEG_MARKE
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


def im_gate_worktree():
    """Liegt DIESER Projektordner an der Stelle, an der das Gate laeuft?

    Nur noetig, wenn die Zeitmarke fehlt - dann steht kein Startzeitpunkt zum
    Vergleich bereit. Die Marke wird von vorbereiten() in jedem Lauf gesetzt,
    der normale Push braucht diese Frage also nie.

    `gate_projekt()` ist bewusst der Vergleichspunkt: es ist der Ort, an dem
    der Push sein Gate fahren laesst, unabhaengig davon, woher der Aufruf kam
    (WB_GATE_WORKTREE oder der Stammordner neben dem Hauptbaum).
    """
    try:
        return gate_worktree.gate_projekt(WURZEL).resolve() == WURZEL.resolve()
    except (OSError, RuntimeError, subprocess.SubprocessError):
        # Unklarheit heisst hier: der Arbeitsbaum-Fall. Wer sich nicht sicher
        # ist, verliert lieber die Belege des eigenen Laufs als die eines
        # fremden.
        return False


def beleg_vollstaendig():
    """Passt der Log zu den Bildern, ist er fertig - und ist er von HEUTE?

    GEMESSEN am 27.09.2026, beim ersten Push dieses Branches: die Python-
    Suiten laufen VOR Gate 4. Sie fanden die Reste eines frueheren, TEILWEISE
    gelaufenen Schnittlaufs im Gate-Worktree und hielten sie fuer einen
    gültigen Beleg - die Dateien existierten, mehr wurde nicht gefragt. Die
    Tests bauten daraus ihre Faelle und meldeten Falsches (2 Fehler).

    Ein Log zaehlt nur, wenn er ANFANG und ENDE hat. Genau so unterscheidet
    der echte Lauf einen Abbruch von einem Lauf, und genau daran liess sich
    der Fehler festmachen.

    Vollstaendigkeit genuegte aber nicht - das ist der naechste Befund auf
    derselben Linie: der Worktree wird WIEDERVERWENDET, und "git clean -fd"
    OHNE -x laesst ignorierte Dateien stehen, `Saved/` steht in .gitignore.
    Ein vollstaendiger Log vom VORRIGEN Push erfuellt alle drei Bedingungen
    oben und wurde trotzdem als eigener Beleg gelesen. Ein Beleg aus einem
    anderen Commit wird nicht als falsch markiert, sondern als eigener - die
    schlimmere Halfte: gruen, ohne gemessen zu haben.

    Deshalb der zweite Nachweis: Gate 4 setzt die Zeitmarke BELEG_MARKE, und
    Beleg und Bilder muessen danach geschrieben worden sein. Eine Sekunde
    Toleranz, weil die Dateisysteme die Zeitstempel runden.

    OHNE Zeitmarke wird nach dem Herkunftsort unterschieden, nicht stillschwei-
    gend nach dem alten Verfahren entschieden:

    * im Gate-Worktree: ueberspringen. Eine Marke fehlt dort nur, wenn
      jemand am vorbereiten() vorbeigelaufen ist - dann ist die Aktualitaet
      eben nicht beweisbar, und ein Beleg ohne Beweis ist kein Beleg.
    * im Arbeitsbaum: der Fall, den dieser Test eigentlich meint. Hier fährt
      man Gate 4 von Hand, im eigenen Lauf, und es gibt nichts zu vergleichen.
    """
    if not LOG.exists() or not all((DIAG / b).exists() for b in BILDER):
        return False
    try:
        text = LOG.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return False
    if not ("Log file open" in text
            and "Log file closed" in text
            and "WbCutShots: fertig - getrennt" in text):
        return False

    try:
        beginn = float(MARKE.read_text(encoding="utf-8").strip())
    except (OSError, ValueError):
        # Keine Marke. Im Worktree ist das ein Grund zu ueberspringen, im
        # Arbeitsbaum der normale Handbetrieb - siehe Docstring.
        return not im_gate_worktree()

    grenze = beginn - 1.0
    try:
        if LOG.stat().st_mtime < grenze:
            return False
        for bild in BILDER:
            if (DIAG / bild).stat().st_mtime < grenze:
                return False
    except OSError:
        return False
    return True


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

class SkipBedingungTest(unittest.TestCase):
    """Wann dieser Selbsttest laeuft und wann er ueberspringt.

    Die Bedingung entscheidet, ob sechs Tests ueberhaupt etwas pruefen. Sie
    wird bei @skipUnless einmal beim Import ausgewertet - also gerade NICHT
    je Test, weshalb hier die Funktion selbst geprueft wird.
    """

    VOLLSTAEENDIG = ("Log file open, 09/27/26 10:00:00\n"
                    "WbCutShots: fertig - getrennt 1\n"
                    "Log file closed, 09/27/26 10:00:42\n")

    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="wb_beleg_"))
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)
        self.diag = self.tmp / "Diagnose"
        self.diag.mkdir()
        for b in BILDER:
            (self.diag / b).write_bytes(b"\x89PNG\r\n\x1a\n")
        self.log = self.tmp / "Logs" / "wb_cut_schnitt.log"
        self.log.parent.mkdir()
        self.log.write_text(self.VOLLSTAEENDIG, encoding="utf-8")
        self.marke = self.tmp / BELEG_NAME
        self.patches = [
            mock.patch.object(sys.modules[__name__], "LOG", self.log),
            mock.patch.object(sys.modules[__name__], "DIAG", self.diag),
            mock.patch.object(sys.modules[__name__], "MARKE", self.marke),
        ]
        for p in self.patches:
            p.start()
        self.addCleanup(lambda: [p.stop() for p in self.patches])

    def setze_marke(self, sekunden):
        self.marke.parent.mkdir(parents=True, exist_ok=True)
        self.marke.write_text("%.3f" % sekunden, encoding="utf-8")

    def im_worktree(self, ja):
        return mock.patch(__name__ + ".im_gate_worktree", return_value=ja)

    def test_der_geplante_lauf_zaehlt(self):
        """Marke von eben, Log und Bilder danach geschrieben: das ist der
        Lauf, den der Push plant - genau dagegen sollen die Tests laufen."""
        self.setze_marke(time.time() - 30)
        self.assertTrue(beleg_vollstaendig(),
                        "Belege aus dem geplanten Lauf gelten nicht als Beleg")

    def test_ein_vollstaendiger_log_vom_vorigen_push_zaehlt_nicht(self):
        """Der Kernfall, zweite Etappe. Alles ist vorhanden, der Log hat Anfang
        und Ende, die Bilder sind da - nur ist alles vom vorigen Lauf. Genau
        daran scheitert eine Vollstaendigkeitspruefung."""
        alt = time.time() - 3600
        self.setze_marke(alt + 1800)
        for p in (self.log,) + tuple(self.diag / b for b in BILDER):
            os.utime(p, (alt, alt))
        self.assertFalse(beleg_vollstaendig(),
                         "ein vollstaendiger Beleg vom VORRIGEN Push gilt als "
                         "Beleg des geplanten - der Push prueft dann einen "
                         "anderen Commit")

    def test_ein_altes_bild_allein_genuegt_nicht(self):
        """Der Log ist frisch, eines der vier Bilder ist alt: dann hat der Lauf
        die Folge nicht vollstaendig erzeugt."""
        self.setze_marke(time.time() - 30)
        alt = time.time() - 3600
        os.utime(self.diag / BILDER[2], (alt, alt))
        self.assertFalse(beleg_vollstaendig(),
                         "ein altes Bild unter richtigen Namen gilt als frisch")

    def test_ohne_marke_ueberspringt_es_im_worktree(self):
        """Ohne Zeitmarke ist die Aktualitaet nicht beweisbar - und im
        Gate-Worktree heisst das ueberspringen, nicht auf die alte Pruefung
        zurueckfallen."""
        alt = time.time() - 3600
        for p in (self.log,) + tuple(self.diag / b for b in BILDER):
            os.utime(p, (alt, alt))
        with self.im_worktree(True):
            self.assertFalse(beleg_vollstaendig(),
                             "ohne Zeitmarke faellt der Worktree auf die alte "
                             "Vollstaendigkeitspruefung zurueck")

    def test_ohne_marke_gilt_im_arbeitsbaum_der_eigene_lauf(self):
        """Im Arbeitsbaum fährt man Gate 4 von Hand. Es gibt nichts zu
        vergleichen, und der eigene Lauf ist der Beleg - sonst waere die
        Bedienungsanleitung im Docstring wertlos."""
        with self.im_worktree(False):
            self.assertTrue(beleg_vollstaendig(),
                            "im Arbeitsbaum muss der eigene Lauf als Beleg gelten")

    def test_eine_kaputte_marke_ueberspringt_im_worktree(self):
        self.marke.parent.mkdir(parents=True, exist_ok=True)
        self.marke.write_text("kein Datum", encoding="utf-8")
        with self.im_worktree(True):
            self.assertFalse(beleg_vollstaendig(),
                             "eine unlesbare Zeitmarke gilt als gueltiger Startzeitpunkt")

    def test_ein_abgebrochener_lauf_zaehlt_nicht(self):
        """Die alte Bedingung bleibt: Anfang und Ende muessen da sein."""
        self.setze_marke(time.time() - 30)
        self.log.write_text("Log file open, 09/27/26 10:00:00\n"
                            "WbCutShots: abgebrochen - getrennt\n", encoding="utf-8")
        self.assertFalse(beleg_vollstaendig(),
                         "ein abgebrochener Lauf gilt als vollstaendiger Beleg")


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
