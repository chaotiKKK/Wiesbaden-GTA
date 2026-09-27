"""Selbsttest des Ausricht-Werkzeugs (Tools/releases_ausrichten.py).

    python -m unittest discover -s Tools -p "test_releases_ausrichten.py"

Geprueft wird die ORCHESTRIERUNG, nicht die Werkzeuge darunter (die haben
eigene Tests): Reihenfolge, Argumente, Plan schreibt nicht, Abbruchgruende,
und dass der Exit-Code des Gates durchgereicht wird. Kein git, kein gh, kein
Netz - `LAUF` ist im Test eine Attrappe, `git archive` liefert ein gebautes
Band.
"""
import contextlib
import io
import os
import pathlib
import shutil
import sys
import tarfile
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import releases_ausrichten as ra  # noqa: E402

SEITE = """# Meilensteine

| # | Meilenstein | Zeitraum | Stand |
|---|---|---|---|
| 1 | [Eins](#1-eins) | 02.09. | fertig |

---

## 1. Eins

*02.09.2026*

Ein Absatz.

![Ein Bild](meilensteine/bilder/01-eins.jpg)
"""


class Fertig:
    def __init__(self, code=0, raus="", fehler=""):
        self.returncode, self.stdout, self.stderr = code, raus, fehler


class Attrappe:
    """Ersetzt jeden Unterprozess. Git kennt sie, die Werkzeuge kennt sie als
    Aufrufe - und die kartei, was zur Kontrolle wieder zurueckgibt."""

    def __init__(self, remote="https://github.com/chaotiKKK/wiesbaden-real-meilensteine.git",
                 schmutz=(), band=None, gate=0, werkzeug_fehler=None,
                 datum="2026-09-27"):
        self.remote = remote
        self.schmutz = list(schmutz)
        self.band = band
        self.datum = datum
        self.gate = gate
        self.werkzeug_fehler = werkzeug_fehler or {}
        self.aufrufe = []
        self.klon = pathlib.Path(tempfile.mkdtemp(prefix="wb_klon_"))
        (self.klon / "bilder").mkdir()
        (self.klon / ".git").mkdir()   # klon_pruefen will .git sehen

    def __call__(self, befehl, cwd=None, capture_output=False, text=False,
                 encoding=None, errors=None):
        aufgerufen = [str(b) for b in befehl]
        # Ein Werkzeugaufruf ist [python.exe, <Pfad zum .py>, ...argumente].
        ist_werkzeug = aufgerufen[0].lower().endswith("python.exe")
        # Nur der Basisname: die Aufrufe werden ueber den Dateinamen
        # wiedergefunden, nicht ueber den vollen Pfad.
        name = os.path.basename(aufgerufen[1]) if ist_werkzeug else aufgerufen[0]
        self.aufrufe.append((name, aufgerufen, str(cwd)))
        if "archive" in aufgerufen:
            return Fertig(0, self.band or _band())
        if "remote" in aufgerufen and "get-url" in aufgerufen:
            return Fertig(0, self.remote + "\n")
        if "status" in aufgerufen:
            return Fertig(0, "\n".join(self.schmutz))
        if "rev-parse" in aufgerufen:
            return Fertig(0, "72439dc\n")
        if "log" in aufgerufen:
            return Fertig(0, self.datum + "\n")
        if "diff" in aufgerufen and "--quiet" in aufgerufen:
            return Fertig(1)            # es gibt etwas zu committen
        if name in self.werkzeug_fehler:
            return Fertig(self.werkzeug_fehler[name], "", "kaputt")
        return Fertig(0)

    def werkzeug(self, name):
        return [a[0] for a in self.aufrufe if a[0] == name]


def _band():
    """Ein echtes tar mit Seite und einem Bild."""
    quelle = pathlib.Path(tempfile.mkdtemp(prefix="wb_band_"))
    (quelle / "docs" / "meilensteine" / "bilder").mkdir(parents=True)
    (quelle / "docs" / "meilensteine.md").write_text(SEITE, encoding="utf-8", newline="\n")
    (quelle / "docs" / "meilensteine" / "bilder" / "01-eins.jpg").write_bytes(b"x")
    puffer = io.BytesIO()
    with tarfile.open(fileobj=puffer, mode="w") as band:
        band.add(quelle / "docs", arcname="docs")
    shutil.rmtree(quelle, ignore_errors=True)
    return puffer.getvalue()


class AusrichtenTest(unittest.TestCase):
    def setUp(self):
        self.alt = (ra.LAUF, os.environ.get("WB_SCHAUFENSTER"))
        self.attrappe = Attrappe()
        ra.LAUF = self.attrappe
        # Der Klon wird gesucht, nicht geraten: hier zeigt die
        # Umgebungsvariable auf die Attrappe.
        os.environ["WB_SCHAUFENSTER"] = str(self.attrappe.klon)
        self.puffer = io.StringIO()

    def tearDown(self):
        ra.LAUF = self.alt[0]
        if self.alt[1] is None:
            os.environ.pop("WB_SCHAUFENSTER", None)
        else:
            os.environ["WB_SCHAUFENSTER"] = self.alt[1]
        shutil.rmtree(self.attrappe.klon, ignore_errors=True)

    def laufen(self, *argv):
        with contextlib.redirect_stdout(self.puffer):
            code = ra.hauptprogramm(list(argv))
        return code, self.puffer.getvalue()

    # -- Der Plan schreibt nicht ------------------------------------------

    def test_der_plan_ruft_alle_fuenf_werkzeuge_und_schreibt_nicht(self):
        code, ausgabe = self.laufen("--ziel", str(self.attrappe.klon))
        self.assertEqual(code, 0, ausgabe)
        for name in ("releases_bilder_ausrichten.py", "releases_texte_ausrichten.py",
                     "releases_oeffentlich.py", "release_abgleich.py"):
            self.assertTrue(self.attrappe.werkzeug(name), f"{name} lief nicht")
        self.assertNotIn("--anwenden", " ".join(
            x for a in self.attrappe.aufrufe for x in a[1]))
        self.assertNotIn("commit", " ".join(x for a in self.attrappe.aufrufe for x in a[1]))

    def test_die_reihenfolge_ist_schaufenster_vor_den_ausrichtern(self):
        """GEMESSEN am 27.09.2026: die Assets zuerst auszurichten loescht dem
        Release die Bilder, die der gerade gemergte Zweig hinzugefuegt hat."""
        self.laufen("--anwenden", "--ziel", str(self.attrappe.klon))
        kommandos = [a[1] for a in self.attrappe.aufrufe]
        add = next(i for i, k in enumerate(kommandos) if k[:2] == ["git", "add"])
        ausrichter = next(i for i, a in enumerate(self.attrappe.aufrufe)
                          if a[0] == "releases_bilder_ausrichten.py")
        self.assertLess(add, ausrichter,
                        "erst committet und geschoben, dann ausgerichtet - "
                        "die Bildnamen der Releases muessen aus dem neuen "
                        "Schaufenster kommen")

    def test_im_plan_bekommt_der_ausrichter_die_seite_aus_dem_ref(self):
        self.laufen("--ziel", str(self.attrappe.klon))
        aufruf = next(a[1] for a in self.attrappe.aufrufe
                      if a[0] == "releases_bilder_ausrichten.py")
        seiten_index = aufruf.index("--seite")
        self.assertTrue(aufruf[seiten_index + 1].endswith("docs/meilensteine.md")
                        or aufruf[seiten_index + 1].endswith("docs\\meilensteine.md"),
                        aufruf[seiten_index + 1])

    # -- Anwenden ----------------------------------------------------------

    def test_anwenden_reicht_anwenden_und_den_kurzen_sha(self):
        self.laufen("--anwenden", "--ziel", str(self.attrappe.klon))
        aufruf = next(a[1] for a in self.attrappe.aufrufe
                      if a[0] == "releases_texte_ausrichten.py")
        self.assertIn("--anwenden", aufruf)
        self.assertEqual(aufruf[aufruf.index("--sha") + 1], "72439dc")

    def test_anwenden_schiebt_das_schaufenster(self):
        self.laufen("--anwenden", "--ziel", str(self.attrappe.klon))
        kommandos = [a[1] for a in self.attrappe.aufrufe]
        self.assertTrue(any(k[:2] == ["git", "commit"] for k in kommandos))
        self.assertTrue(any(k[:3] == ["git", "push", "origin"] for k in kommandos))

    def test_ohne_schaufenster_fasst_den_klon_nicht_an(self):
        self.laufen("--anwenden", "--ohne-schaufenster",
                    "--ziel", str(self.attrappe.klon))
        kommandos = [a[1] for a in self.attrappe.aufrufe]
        self.assertFalse(any(k[:2] == ["git", "add"] for k in kommandos))
        self.assertFalse(any(k[:2] == ["git", "commit"] for k in kommandos))

    # -- Abbruchgruende ----------------------------------------------------

    def test_ein_fremdes_ziel_bricht_ab(self):
        self.attrappe.remote = "https://github.com/chaotiKKK/Wiesbaden-GTA.git"
        code, ausgabe = self.laufen("--anwenden", "--ziel", str(self.attrappe.klon))
        self.assertEqual(code, 2, ausgabe)
        self.assertIn("nicht geschrieben", ausgabe)
        self.assertEqual(self.attrappe.werkzeug("releases_bilder_ausrichten.py"), [],
                         "es wurde trotzdem ausgerichtet")

    def test_ein_schmutziger_klon_bricht_ab(self):
        """`git add -A` haette die fremde Arbeit mitveroeffentlicht."""
        self.attrappe.schmutz = [" M index.html", "?? notiz.txt"]
        code, ausgabe = self.laufen("--anwenden", "--ziel", str(self.attrappe.klon))
        self.assertEqual(code, 2, ausgabe)
        self.assertIn("nicht mitveroeffentlicht", ausgabe)
        self.assertEqual(self.attrappe.werkzeug("releases_bilder_ausrichten.py"), [])

    def test_ein_fehlender_klon_bricht_ab(self):
        # GEMESST: `C:\gibt\es\nicht` existiert auf diesem Rechner
        # wirklich - ein scheinbar unmoeglicher Testpfad ist hier keine
        # Garantie. Also ein Pfad aus dem TEMP-Ordner, den es sicher nicht gibt.
        fehlt = pathlib.Path(tempfile.gettempdir()) / "wb_klon_den_es_nicht_gibt"
        self.assertFalse(fehlt.exists())
        code, ausgabe = self.laufen("--anwenden", "--ziel", str(fehlt))
        self.assertEqual(code, 2, ausgabe)
        self.assertIn("git clone", ausgabe)

    def test_ohne_gate_kein_gate(self):
        self.attrappe.gate = 1
        code, ausgabe = self.laufen("--kein-gate", "--ziel", str(self.attrappe.klon))
        self.assertEqual(code, 0, ausgabe)
        self.assertEqual(self.attrappe.werkzeug("release_abgleich.py"), [])

    # -- Der Beweis -------------------------------------------------------

    def test_ein_rotes_gate_macht_den_lauf_unfertig(self):
        self.attrappe.werkzeug_fehler["release_abgleich.py"] = 1
        code, ausgabe = self.laufen("--anwenden", "--ziel", str(self.attrappe.klon))
        self.assertEqual(code, 3, ausgabe)
        self.assertIn("NICHT fertig", ausgabe)

    def test_ein_fehlgeschlagener_ausrichter_bricht_sofort_ab(self):
        self.attrappe.werkzeug_fehler["releases_bilder_ausrichten.py"] = 1
        code, ausgabe = self.laufen("--anwenden", "--ziel", str(self.attrappe.klon))
        self.assertEqual(code, 1, ausgabe)
        self.assertEqual(self.attrappe.werkzeug("releases_oeffentlich.py"), [],
                         "nach dem Fehler liefen die Schritte weiter")


class VergleichTest(unittest.TestCase):
    """`gleich` - der Vergleich, ohne den jeder Plan 'geaendert' meldet."""

    def setUp(self):
        self.wurzel = pathlib.Path(tempfile.mkdtemp(prefix="wb_gleich_"))
        self.addCleanup(shutil.rmtree, self.wurzel, ignore_errors=True)

    def test_zeilenenden_werden_nicht_als_unterschied_gezaehlt(self):
        a = self.wurzel / "a.html"
        b = self.wurzel / "b.html"
        a.write_bytes(b"<p>hi</p>\n")
        b.write_bytes(b"<p>hi</p>\r\n")
        self.assertTrue(ra.gleich(a, b))

    def test_ein_wirklicher_unterschied_zaehlt(self):
        a = self.wurzel / "a.html"
        b = self.wurzel / "b.html"
        a.write_bytes(b"<p>hi</p>\n")
        b.write_bytes(b"<p>hoch</p>\n")
        self.assertFalse(ra.gleich(a, b))

    def test_bilder_wortgleich_geprueft(self):
        a = self.wurzel / "a.jpg"
        b = self.wurzel / "b.jpg"
        a.write_bytes(b"\xff\xd8\xff")
        b.write_bytes(b"\xff\xd8\xff")
        self.assertTrue(ra.gleich(a, b))
        b.write_bytes(b"\xff\xd8\xfe")
        self.assertFalse(ra.gleich(a, b))


class StandTest(unittest.TestCase):
    """Die Stand-Zeile - GEMESSEN am 28.09.2026 die Ursache der einzigen
    Plan-Meldung, die es bis dahin gab."""

    def setUp(self):
        self.alt = ra.LAUF
        self.attrappe = Attrappe()
        ra.LAUF = self.attrappe
        self.wurzel = pathlib.Path(tempfile.mkdtemp(prefix="wb_stand_"))
        self.addCleanup(shutil.rmtree, self.wurzel, ignore_errors=True)

    def tearDown(self):
        ra.LAUF = self.alt

    def test_der_stand_kommt_aus_dem_ref_und_nicht_aus_heute(self):
        """GEMESSEN: mit `date.today()` meldete der Plan jeden Tag
        "geaendert: index.html", obwohl git nichts zu committen hatte - der
        Klon war am Vortag aus demselben Ref gebaut worden."""
        self.assertEqual(ra.stand_text("origin/main"), "Stand 27.09.2026")
        # Ein anderer Tag aendert daran nichts: der Stand gehoert zum Inhalt.
        self.attrappe.datum = "2026-09-27"
        self.assertEqual(ra.stand_text("origin/main"), "Stand 27.09.2026")

    def test_ein_unlesbares_datum_bricht_ab_statt_zurueckzufallen(self):
        self.attrappe.datum = "gestern"
        with self.assertRaises(ra.Abbruch):
            ra.stand_text("origin/main")

    def _paare(self, neu, alt):
        a = self.wurzel / "neu.html"
        b = self.wurzel / "alt.html"
        a.write_text(neu, encoding="utf-8", newline="\n")
        b.write_text(alt, encoding="utf-8", newline="\n")
        return a, b

    def test_der_plan_nennt_den_stand_als_ursache(self):
        a, b = self._paare("<p class=\"mono\">Stand 28.09.2026</p>\n<p>x</p>\n",
                           "<p class=\"mono\">Stand 27.09.2026</p>\n<p>x</p>\n")
        self.assertIn("nur der Stand: 27.09.2026 -> 28.09.2026", ra.stand_hinweis(a, b))

    def test_bei_echtem_inhaltsunterschied_gibt_es_keinen_hinweis(self):
        a, b = self._paare("<p>Stand 28.09.2026 neu</p>\n", "<p>Stand 27.09.2026 alt</p>\n")
        self.assertEqual(ra.stand_hinweis(a, b), "")

    def test_bei_gleichem_stand_gibt_es_keinen_hinweis(self):
        a, b = self._paare("<p>Stand 28.09.2026</p>\n", "<p>ganz anders</p>\n")
        self.assertEqual(ra.stand_hinweis(a, b), "")


if __name__ == "__main__":
    unittest.main()
