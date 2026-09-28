"""Selbsttest des Release-Abgleichs (Tools/release_abgleich.py).

    python -m unittest discover -s Tools -p "test_release_abgleich.py"

Das Gate redet mit GitHub; der Test redet mit einer Attrappe. Alles andere -
Seite, Bilddateien, TAGS, SEITEN_TITEL - liegt in einem TEMP-Verzeichnis, und
die Seite aus dem Ref wird eingespeist statt aus git geholt. Der Test fasst
das echte Projekt nicht an.

Die wichtigste Zusicherung steht weiter unten: **wenn die Releases nicht
abfragbar sind, ist das Gate ROT und nicht gruen.** Ein Gate, das bei fehlendem
Netz "alles in Ordnung" sagt, ist schlimmer als keines.
"""
import io
import json
import contextlib
import pathlib
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import release_abgleich as ra  # noqa: E402

SEITE = """# Meilensteine

| # | Meilenstein | Zeitraum | Stand |
|---|---|---|---|
| 2 | [Zwei](#2-zwei) | 01.09. | offen |
| 1 | [Eins](#1-eins) | 02.09. | fertig |

---

## 2. Zwei

*01.09.2026*

Der zweite Meilenstein mit einem Bild.

![Zwei](meilensteine/bilder/02-zwei.jpg)

---

## 1. Eins

*02.09.2026*

Der erste Meilenstein mit einem Bild.

![Eins](meilensteine/bilder/01-eins.jpg)
"""

TAGS = {1: "meilenstein-01-eins", 2: "meilenstein-02-zwei"}
TITEL = {1: "Eins", 2: "Zwei"}


class Attrappe:
    """Eine gh-Attrappe: sie kennt die Releases, die der Test gebaut hat.

    ZWO Repos, weil das Gate beide liest: das private (ohne `--repo`) und das
    oeffentliche Schaufenster (`--repo chaotiKKK/wiesbaden-real-meilensteine`).
    Ohne diesen Unterschied wuerde jeder Test am oeffentlichen Repo scheitern,
    das der Test gar nicht kennt.
    """

    OEFF = "chaotiKKK/wiesbaden-real-meilensteine"

    def __init__(self, releases, fehler=None):
        self.repos = {"privat": dict(releases), self.OEFF: dict(releases)}
        self.fehler = fehler
        self.aufrufe = []

    def __call__(self, *args):
        self.aufrufe.append(args)
        if self.fehler is not None:
            return self.fehler
        if "--repo" in args:
            name = args[args.index("--repo") + 1]
        else:
            name = "privat"
        ziel = self.repos[name]
        if args[:2] == ("release", "list"):
            return 0, json.dumps([{"tagName": t} for t in ziel]), ""
        if args[:2] == ("release", "view"):
            tag = args[2]
            daten = ziel.get(tag)
            if daten is None:
                return 1, "", "release not found"
            return 0, json.dumps(daten), ""
        return 1, "", f"unbekannter Aufruf: {args}"


class AbgleichTest(unittest.TestCase):
    def setUp(self):
        self.wurzel = pathlib.Path(tempfile.mkdtemp(prefix="wb_release_"))
        self.addCleanup(shutil.rmtree, self.wurzel, ignore_errors=True)
        self.bilder = self.wurzel / "docs" / "meilensteine" / "bilder"
        self.bilder.mkdir(parents=True)
        self.seite = self.wurzel / "docs" / "meilensteine.md"
        self.seite_schreiben(SEITE)
        for name in ("01-eins.jpg", "02-zwei.jpg"):
            (self.bilder / name).write_bytes(b"x")

        self.alt = (ra.SEITE_ARBEIT, ra.BILDER_ARBEIT, ra.GH_LAUF, ra.HTTP_LAUF,
                    ra.rba.TAGS, ra.rta.SEITEN_TITEL, ra.seite_aus_ref,
                    ra.ref_hat_datei)
        ra.SEITE_ARBEIT = self.seite
        ra.BILDER_ARBEIT = self.bilder
        # Kein echter Abruf im Test: HTTP 200 gilt als "oeffentlich".
        ra.HTTP_LAUF = lambda url: (200, "123")
        ra.rba.TAGS = dict(TAGS)
        ra.rta.SEITEN_TITEL = dict(TITEL)
        # Die Seite aus dem Ref ist im Test dieselbe wie im Arbeitsbaum, nur
        # ohne das, was der Test spaeter abweichend macht.
        ra.seite_aus_ref = lambda ref: self.ref_seite
        ra.ref_hat_datei = lambda ref, pfad: (self.bilder / pathlib.Path(pfad).name).exists()
        self.ref_seite = SEITE
        self.attrappe = None
        self.raus = ""
        self._oeffentlich = None

    def tearDown(self):
        (ra.SEITE_ARBEIT, ra.BILDER_ARBEIT, ra.GH_LAUF, ra.HTTP_LAUF,
         ra.rba.TAGS, ra.rta.SEITEN_TITEL, ra.seite_aus_ref,
         ra.ref_hat_datei) = self.alt

    # -- Bausteine ---------------------------------------------------------

    def seite_schreiben(self, text):
        self.seite.write_text(text, encoding="utf-8", newline="\n")

    def releases(self, text=SEITE, muendungen=True):
        """Releases, die zu `text` passen - der gruene Zustand."""
        raus = {}
        for num, tag in TAGS.items():
            raus[tag] = {
                "assets": [{"name": n} for n in
                           ra.seiten_daten(text)[1].get(num, [])],
                "body": ra.erwarteter_text(num, text),
            }
        if not muendungen:
            for tag in raus:
                raus[tag]["body"] = raus[tag]["body"].replace(
                    "## 1. Eins", "# Eins")
        return raus

    def oeffentlich_veraendern(self, aenderung):
        """Ruft die Aenderung auf den oeffentlichen Releases auf."""
        self._oeffentlich = aenderung

    def laufen(self, argv=None, releases=None, fehler=None):
        self.attrappe = Attrappe(releases if releases is not None else {},
                                 fehler=fehler)
        if self._oeffentlich is not None:
            self._oeffentlich(self.attrappe.repos[Attrappe.OEFF])
        ra.GH_LAUF = self.attrappe
        puffer = io.StringIO()
        with contextlib.redirect_stdout(puffer):
            code = ra.hauptprogramm(argv or [])
        self.raus = puffer.getvalue()
        return code

    # -- 1. Alles stimmt ----------------------------------------------------

    def test_ein_stimmiger_zustand_ist_gruen(self):
        self.assertEqual(self.laufen(releases=self.releases()), 0,
                         self.raus)

    def test_der_gruene_zustand_prueft_echt(self):
        """Gegen die Zirkularitaet: der erwartete Text enthaelt wirklich den
        Absatz der Seite. Sonst waere jeder Test gruen, weil beide Seiten
        dasselbe leere Ding erzaehlen."""
        erwartet = ra.erwarteter_text(1, SEITE)
        self.assertIn("Der erste Meilenstein mit einem Bild.", erwartet)
        self.assertIn("01-eins.jpg", erwartet)
        self.assertIn("Stand im Code:", erwartet)

    def test_ohne_maßnahme_wird_gar_nichts_geprueft(self):
        """--quelle arbeit fasst die Releases nicht an - und sagt das."""
        code = self.laufen(["--quelle", "arbeit"])
        self.assertEqual(code, 0, self.raus)
        self.assertEqual(self.attrappe.aufrufe, [],
                         "die Releases wurden abgefragt, obwohl nur die "
                         "Seite geprueft werden sollte")
        self.assertIn("NICHT abgeglichen", self.raus)

    # -- 2. Abweichungen bei den Assets ------------------------------------

    def test_ein_fehlendes_bild_im_release_ist_rot(self):
        rel = self.releases()
        rel[TAGS[1]]["assets"] = []
        self.assertEqual(self.laufen(releases=rel), 1, self.raus)
        self.assertIn("Bild fehlt im Release: 01-eins.jpg", self.raus)

    def test_ein_zusaetzliches_bild_im_release_ist_rot(self):
        rel = self.releases()
        rel[TAGS[1]]["assets"].append({"name": "01-alt.jpg"})
        self.assertEqual(self.laufen(releases=rel), 1, self.raus)
        self.assertIn("nicht auf der Seite: 01-alt.jpg", self.raus)

    def test_ein_fehlendes_release_ist_rot(self):
        rel = self.releases()
        del rel[TAGS[2]]
        self.assertEqual(self.laufen(releases=rel), 1, self.raus)
        self.assertIn("Release fehlt ganz", self.raus)

    # -- 3. Abweichungen im Text -------------------------------------------

    def test_ein_veralteter_release_text_ist_rot(self):
        rel = self.releases()
        rel[TAGS[1]]["body"] = rel[TAGS[1]]["body"].replace(
            "Der erste Meilenstein mit einem Bild.", "Ein alter Satz.")
        self.assertEqual(self.laufen(releases=rel), 1, self.raus)
        self.assertIn("Release-Text weicht von der Seite ab", self.raus)
        # Der Diff zeigt, was im Release STATT des Seitentexts steht.
        self.assertIn("-Ein alter Satz.", self.raus)

    def test_der_stand_im_code_allein_macht_nicht_rot(self):
        """Er wandert mit jedem Commit auf main - waere er Teil des
        Vergleichs, waere das Gate nach jedem Merge rot, ohne dass ein Text
        veraltet waere."""
        rel = self.releases()
        rel[TAGS[1]]["body"] = rel[TAGS[1]]["body"].replace(
            "0" * 40, "a1b2c3d4e5f6a7b8c9d0")
        self.assertEqual(self.laufen(releases=rel), 0, self.raus)

    def test_ein_text_ohne_stand_im_code_ist_rot(self):
        rel = self.releases()
        text = rel[TAGS[1]]["body"]
        rel[TAGS[1]]["body"] = text.split("---")[0]
        self.assertEqual(self.laufen(releases=rel), 1, self.raus)
        self.assertIn("nennt keinen Stand im Code", self.raus)

    def test_ein_leerer_text_ist_rot(self):
        rel = self.releases()
        rel[TAGS[1]]["body"] = ""
        self.assertEqual(self.laufen(releases=rel), 1, self.raus)
        self.assertIn("Release-Text ist leer", self.raus)

    # -- 4. Die Seite in sich ----------------------------------------------

    def test_ein_bildverweis_ohne_datei_ist_rot(self):
        (self.bilder / "01-eins.jpg").unlink()
        code = self.laufen(["--quelle", "arbeit"])
        self.assertEqual(code, 2, self.raus)
        self.assertIn("Bildverweis ohne Datei: 01-eins.jpg", self.raus)

    def test_ein_neuer_meilenstein_ohne_tag_ist_rot(self):
        """Der Fall, den die beiden Ausricht-Werkzeuge stillschweigend
        ueberspringen wuerden: ein Abschnitt ohne TAGS-Eintrag faellt dort
        einfach aus dem Vergleich heraus."""
        self.seite_schreiben(SEITE + """
---

## 3. Drei

*03.09.2026*

![Drei](meilensteine/bilder/03-drei.jpg)
""")
        (self.bilder / "03-drei.jpg").write_bytes(b"x")
        self.ref_seite = self.seite.read_text(encoding="utf-8")
        code = self.laufen(["--quelle", "arbeit"])
        self.assertEqual(code, 2, self.raus)
        self.assertIn("kein Release-Tag", self.raus)

    def test_ein_tag_ohne_abschnitt_ist_rot(self):
        ra.rba.TAGS = {**TAGS, 3: "meilenstein-03-drei"}
        code = self.laufen(["--quelle", "arbeit"])
        self.assertEqual(code, 2, self.raus)
        self.assertIn("gehoert zu M03, das auf der Seite fehlt", self.raus)

    def test_ein_titel_widerspricht_der_ueberschrift_ist_rot(self):
        ra.rta.SEITEN_TITEL = {**TITEL, 1: "Eins (anders)"}
        code = self.laufen(["--quelle", "arbeit"])
        self.assertEqual(code, 2, self.raus)
        self.assertIn("passt nicht zum Release-Titel", self.raus)

    def test_eine_tabellenzeile_ohne_abschnitt_ist_rot(self):
        self.seite_schreiben(SEITE.replace(
            "| 1 | [Eins](#1-eins) | 02.09. | fertig |",
            "| 1 | [Eins](#1-eins) | 02.09. | fertig |\n"
            "| 7 | [Sieben](#7-sieben) | 03.09. | offen |"))
        code = self.laufen(["--quelle", "arbeit"])
        self.assertEqual(code, 2, self.raus)
        self.assertIn("Uebersichtstabelle nennt M07", self.raus)

    # -- 5. Nicht messbar ist nicht gruen ----------------------------------

    def test_ohne_gh_ist_das_gate_rot_und_nicht_gruen(self):
        code = self.laufen(fehler=(127, "", "gh nicht gefunden"))
        self.assertEqual(code, 3, self.raus)
        self.assertIn("NICHT 'alles in Ordnung'", self.raus)
        self.assertIn("push --no-verify", self.raus)

    def test_ein_netzfehler_ist_rot(self):
        code = self.laufen(fehler=(1, "", "dial tcp: lookup api.github.com"))
        self.assertEqual(code, 3, self.raus)
        self.assertIn("nicht abfragbar", self.raus)

    def test_kein_json_ist_rot(self):
        # Eine FREIE Funktion als gh, nicht die Attrappe: `__call__` am
        # Instanzattribut nimmt Python beim Aufruf nicht her - die Attrappe
        # wuerde weiterlaufen und der Test pruefte die falsche Sache.
        def kaputt(*args):
            return 0, "kein json", ""

        ra.GH_LAUF = kaputt
        puffer = io.StringIO()
        with contextlib.redirect_stdout(puffer):
            code = ra.hauptprogramm([])
        self.raus = puffer.getvalue()
        self.assertEqual(code, 3, self.raus)
        self.assertIn("kein JSON", self.raus)

    def test_ein_ref_ohne_seite_ist_rot(self):
        ra.seite_aus_ref = lambda ref: None
        puffer = io.StringIO()
        with contextlib.redirect_stdout(puffer):
            code = ra.hauptprogramm([])
        self.raus = puffer.getvalue()
        self.assertEqual(code, 4, self.raus)

    def test_ein_fehlender_ref_kann_auftragsgemaess_egal_sein(self):
        ra.seite_aus_ref = lambda ref: None
        puffer = io.StringIO()
        with contextlib.redirect_stdout(puffer):
            code = ra.hauptprogramm(["--ref-fehlt-ist-ok"])
        self.raus = puffer.getvalue()
        self.assertEqual(code, 0, self.raus)
        self.assertIn("auftragsgemaess", self.raus)

    # -- 6. Oeffentlichkeit: Bilder ohne GitHub-Konto ----------------------

    def test_ein_link_ins_private_repo_ist_rot(self):
        """Der Fehler, den der Textvergleich allein NICHT sieht: der Text
        kann bytegleich dem Erzeuger entsprechen und trotzdem ins private
        Repo zeigen."""
        rel = self.releases()
        for tag in rel:
            rel[tag]["body"] = (rel[tag]["body"] + "\n\nFoto: "
                                "[das Turmbild](https://github.com/chaotiKKK/"
                                "Wiesbaden-GTA/blob/main/x.jpg)\n")
        code = self.laufen(releases=rel)
        self.assertEqual(code, 1, self.raus)
        self.assertIn("verlinkt ins private Repo", self.raus)

    def test_ein_bildlink_außerhalb_des_schaufensters_ist_rot(self):
        rel = self.releases()
        for tag in rel:
            rel[tag]["body"] = (rel[tag]["body"] + "\n\n![x](https://example.org/a.jpg)\n")
        code = self.laufen(releases=rel)
        self.assertEqual(code, 1, self.raus)
        self.assertIn("zeigt nicht ins oeffentliche Schaufenster", self.raus)

    def test_ein_totes_oeffentliches_bild_ist_rot(self):
        """Der Text ist richtig, das Bild liegt aber nicht im Schaufenster -
        das faellt nur, wenn man es wirklich abruft."""
        ra.HTTP_LAUF = lambda url: (404, None)
        code = self.laufen(releases=self.releases())
        self.assertEqual(code, 1, self.raus)
        self.assertIn("ohne Konto nicht abrufbar (HTTP 404)", self.raus)

    def test_ein_netzfehler_beim_abruf_ist_nicht_messbar(self):
        def kaputt(url):
            raise ra.NichtMessbar("Timeout")

        ra.HTTP_LAUF = kaputt
        code = self.laufen(releases=self.releases())
        self.assertEqual(code, 3, self.raus)
        self.assertIn("nicht abfragbar", self.raus)

    def test_der_abruf_erfolgt_ohne_anmeldung_und_oeffentlich(self):
        """Der Aufruf darf keine Anmeldedaten mitschicken - sonst wuerde
        200 beweisen, dass der Link mit Konto geht, und nicht das, was
        geprueft werden soll. Und jeder abgerufene Weg muss im oeffentlichen
        Repo liegen."""
        gesehen = []

        def merker(url):
            gesehen.append(url)
            return 200, "1"

        ra.HTTP_LAUF = merker
        self.laufen(releases=self.releases())
        self.assertTrue(gesehen, "es wurde gar nichts abgerufen")
        erlaubt = ("https://raw.githubusercontent.com/chaotiKKK/wiesbaden-real-meilensteine/",
                   "https://github.com/chaotiKKK/wiesbaden-real-meilensteine/releases/download/")
        for url in gesehen:
            self.assertNotIn("Wiesbaden-GTA", url)
            self.assertTrue(url.startswith(erlaubt), "falsche Basis: %s" % url)

    # -- 6b. Der oeffentliche Spiegel --------------------------------------

    def test_ein_fehlendes_oeffentliches_release_ist_rot(self):
        """Ohne das Spiegel-Release gibt es fuer Menschen ohne Konto keinen
        Download-Weg - die Bildlinks im Text sind dann die einzigen, und die
        zeigen keine Datei zum Speichern."""
        self.oeffentlich_veraendern(
            lambda ziel: ziel.pop(TAGS[1], None))
        code = self.laufen(releases=self.releases())
        self.assertEqual(code, 1, self.raus)
        self.assertIn("kein oeffentliches Release", self.raus)

    def test_ein_bild_fehlt_im_oeffentlichen_release_ist_rot(self):
        def leeren(ziel):
            ziel[TAGS[1]]["assets"] = []

        self.oeffentlich_veraendern(leeren)
        code = self.laufen(releases=self.releases())
        self.assertEqual(code, 1, self.raus)
        self.assertIn("Bild fehlt im oeffentlichen Release: 01-eins.jpg", self.raus)

    def test_ein_toter_oeffentlicher_download_ist_rot(self):
        """Nur die Download-Wege sind tot, die Bildlinks sind gesund: der Fall,
        den ein reiner Textvergleich nie sieht."""
        def teilweise(url):
            return (404, None) if "/releases/download/" in url else (200, "1")

        ra.HTTP_LAUF = teilweise
        code = self.laufen(releases=self.releases())
        self.assertEqual(code, 1, self.raus)
        self.assertIn("oeffentlicher Download nicht erreichbar (HTTP 404)", self.raus)

    def test_der_spiegel_wird_im_oeffentlichen_repo_gelesen(self):
        self.laufen(releases=self.releases())
        mit_repo = [a for a in self.attrappe.aufrufe if "--repo" in a]
        self.assertTrue(mit_repo, "das oeffentliche Repo wurde nie gelesen")
        for args in mit_repo:
            self.assertEqual(args[args.index("--repo") + 1],
                             Attrappe.OEFF)

    # -- 7. Der Hinweis, kein Fehler ---------------------------------------

    def test_ein_neues_bild_im_zweig_ist_ein_hinweis_kein_fehler(self):
        """Der Arbeitszweig ist juenger als die veroeffentlichte Seite - das
        darf den Push nicht blockieren, aber es soll gesagt werden."""
        (self.bilder / "01-neu.jpg").write_bytes(b"x")
        neu = SEITE.replace(
            "![Eins](meilensteine/bilder/01-eins.jpg)",
            "![Eins](meilensteine/bilder/01-eins.jpg)\n"
            "![Neu](meilensteine/bilder/01-neu.jpg)")
        self.seite_schreiben(neu)
        code = self.laufen(releases=self.releases(SEITE))
        self.assertEqual(code, 0, self.raus)
        self.assertIn("HINWEIS M01: 01-neu.jpg", self.raus)
        self.assertIn("releases_bilder_ausrichten.py --anwenden", self.raus)


class HilfsfunktionTest(unittest.TestCase):
    """Die zwei Bausteine, die ohne GitHub auskommen."""

    def test_ohne_stand_bleibt_der_inhalt_stehbar(self):
        text = ("## 1. Eins\n\nEin Satz.\n\n---\n\n"
                "Stand im Code: 0123456789abcdef · alle Meilensteine: [x](y)\n")
        geprueft = ra.ohne_stand(text)
        self.assertIn("Ein Satz.", geprueft)
        self.assertNotIn("## 1. Eins", geprueft)
        self.assertIn("<SHA>", geprueft)

    def test_stand_im_code_liest_eine_kommennummer(self):
        self.assertEqual(
            ra.stand_im_code("Stand im Code: 72439dc · alle Meilensteine: [x](y)"),
            "72439dc")
        self.assertIsNone(ra.stand_im_code("ohne Fuss"))

    def test_der_hinweis_nennt_beide_richtungen(self):
        zeilen = ra.arbeitsvergleich({1: ["a.jpg", "neu.jpg"]},
                                     {1: ["a.jpg", "alt.jpg"]})
        self.assertEqual(len(zeilen), 2, zeilen)
        self.assertTrue(any("neu.jpg" in z for z in zeilen))
        self.assertTrue(any("alt.jpg" in z for z in zeilen))



class NetzAussetzerTest(unittest.TestCase):
    """Netzfehler werden wiederholt, Befunde nie (28.09.2026: 4 von 6 Laeufen
    brachen an kurzen Aussetzern ab, jeder kostete einen 25-Minuten-Push)."""

    def setUp(self):
        self.pausen = []

    def schlaf(self, s):
        self.pausen.append(s)

    def folge(self, *ergebnisse):
        reste = list(ergebnisse)

        def versuch():
            e = reste.pop(0)
            if isinstance(e, Exception):
                raise e
            return e
        return versuch

    def test_aussetzer_dann_erfolg(self):
        v = self.folge((1, "", "Post \"https://api.github.com/graphql\": i/o timeout"),
                       (0, "[]", ""))
        e = ra.mit_wiederholung(v, lambda e: e[0] != 0 and ra.ist_netzfehler(e[2]),
                                schlaf=self.schlaf)
        self.assertEqual(e, (0, "[]", ""))
        self.assertEqual(self.pausen, [3])

    def test_befund_wird_nicht_wiederholt(self):
        v = self.folge((1, "", "release not found"), (0, "[]", ""))
        e = ra.mit_wiederholung(v, lambda e: e[0] != 0 and ra.ist_netzfehler(e[2]),
                                schlaf=self.schlaf)
        self.assertEqual(e[0], 1)
        self.assertEqual(self.pausen, [])

    def test_dauerhaft_weg_bleibt_nicht_gemessen(self):
        v = self.folge(OSError("timed out"), OSError("timed out"), OSError("timed out"))
        with self.assertRaises(OSError):
            ra.mit_wiederholung(v, lambda e: isinstance(e, Exception), schlaf=self.schlaf)
        self.assertEqual(self.pausen, [3, 10])

    def test_http_404_ist_ein_befund(self):
        # Genau der Weg in anonym_erreichen: HTTPError wird zum Ergebnis,
        # also kein Netzfehler, also kein zweiter Versuch.
        v = self.folge((404, None), (200, "1"))
        self.assertEqual(ra.mit_wiederholung(v, lambda e: isinstance(e, Exception),
                                             schlaf=self.schlaf), (404, None))
        self.assertEqual(self.pausen, [])

    def test_der_echte_bildabruf_wiederholt_den_aussetzer(self):
        # Nicht nur der Helfer: anonym_erreichen muss ihn auch benutzen.
        import urllib.error
        from unittest import mock

        class Antwort:
            status = 200
            headers = {"Content-Length": "7"}

            def __enter__(self):
                return self

            def __exit__(self, *a):
                return False

        aufrufe = []

        def urlopen(anfrage, timeout):
            aufrufe.append(1)
            if len(aufrufe) == 1:
                raise urllib.error.URLError("timed out")
            return Antwort()

        with mock.patch.object(ra, "NETZ_PAUSEN", (0, 0)), \
                mock.patch.object(ra.urllib.request, "urlopen", urlopen):
            self.assertEqual(ra.anonym_erreichen("https://x/bild.jpg"), (200, "7"))
        self.assertEqual(len(aufrufe), 2)

    def test_netzzeichen(self):
        self.assertTrue(ra.ist_netzfehler('Post "https://api.github.com/graphql": EOF'))
        self.assertTrue(ra.ist_netzfehler("<urlopen error timed out>"))
        self.assertFalse(ra.ist_netzfehler("HTTP 404: Not Found (release)"))
        self.assertFalse(ra.ist_netzfehler(""))

if __name__ == "__main__":
    unittest.main()
