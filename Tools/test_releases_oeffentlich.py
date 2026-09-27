"""Selbsttest des oeffentlichen Release-Spiegels (Tools/releases_oeffentlich.py).

    python -m unittest discover -s Tools -p "test_releases_oeffentlich.py"

Das Werkzeug schreibt in ein oeffentliches Repo. Zwei Dinge muessen deshalb
festgenagelt sein: es fasst das private Repo nicht an (jeder `gh`-Aufruf
traegt `--repo`), und ein Netzfehler wird wiederholt, ein echter Fehler
dafuer nicht. Alles andere laeuft gegen eine Attrappe.
"""
import json
import pathlib
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import releases_oeffentlich as ro  # noqa: E402

SEITE = """# Meilensteine

| # | Meilenstein | Zeitraum | Stand |
|---|---|---|---|
| 2 | [Zwei](#2-zwei) | 01.09. | offen |
| 1 | [Eins](#1-eins) | 02.09. | fertig |

---

## 2. Zwei

*01.09.2026*

![Zwei](meilensteine/bilder/02-zwei.jpg)

---

## 1. Eins

*02.09.2026*

![Eins](meilensteine/bilder/01-eins.jpg)
"""


class Attrappe:
    """gh-Attrappe mit einem oeffentlichen Repo als Spielbrett."""

    def __init__(self, vorhanden=None, netzfehler_einmal=False):
        self.releases = dict(vorhanden or {})
        self.aufrufe = []
        self.netzfehler_einmal = netzfehler_einmal
        self.genutzt = False

    def __call__(self, *args, repo=None, versuche=3):
        self.aufrufe.append((args, repo))
        if self.netzfehler_einmal and not self.genutzt:
            self.genutzt = True
            return 1, "", ("Post https://api.github.com/x: dial tcp 1.2.3.4:443: "
                           "connectex: Ein Verbindungsversuch ist fehlgeschlagen")
        if args[:2] == ("release", "list"):
            return 0, json.dumps([{"tagName": t} for t in self.releases]), ""
        if args[:2] == ("release", "view"):
            daten = self.releases.get(args[2])
            if daten is None:
                return 1, "", "release not found"
            return 0, json.dumps(daten), ""
        if args[:2] == ("release", "create"):
            self.releases[args[2]] = {"assets": [], "body": ""}
            return 0, "", ""
        if args[:2] == ("release", "upload"):
            self.releases[args[2]]["assets"].append(
                {"name": pathlib.Path(args[3]).name})
            return 0, "", ""
        if args[:2] == ("release", "delete-asset"):
            self.releases[args[2]]["assets"] = [
                a for a in self.releases[args[2]]["assets"] if a["name"] != args[3]]
            return 0, "", ""
        if args[:2] == ("release", "edit"):
            notiz = pathlib.Path(args[-1]).read_text(encoding="utf-8")
            self.releases[args[2]]["body"] = notiz
            return 0, "", ""
        return 1, "", f"unbekannt: {args}"


class SpiegelTest(unittest.TestCase):
    def setUp(self):
        self.wurzel = pathlib.Path(tempfile.mkdtemp(prefix="wb_spiegel_"))
        self.addCleanup(shutil.rmtree, self.wurzel, ignore_errors=True)
        self.bilder = self.wurzel / "bilder"
        self.bilder.mkdir()
        for name in ("01-eins.jpg", "02-zwei.jpg"):
            (self.bilder / name).write_bytes(b"x")
        self.seite = self.wurzel / "meilensteine.md"
        self.seite.write_text(SEITE, encoding="utf-8", newline="\n")
        self.alt = (ro.gh, ro.rba.TAGS, ro.rta.SEITEN_TITEL, ro.rta.SEITE)
        ro.rba.TAGS = {1: "m1", 2: "m2"}
        ro.rta.SEITEN_TITEL = {1: "Eins", 2: "Zwei"}
        ro.rta.SEITE = self.seite
        self.meldung = []

    def tearDown(self):
        ro.gh, ro.rba.TAGS, ro.rta.SEITEN_TITEL, ro.rta.SEITE = self.alt

    def spiegeln(self, attrappe, anwenden):
        ro.gh = attrappe
        return ro.spiegeln("oeffentlich/test", self.seite, self.bilder,
                           "72439dc", anwenden, self.meldung)

    def ausgabe(self):
        return "\n".join(self.meldung)

    # -- Plan ---------------------------------------------------------------

    def test_der_plan_benennt_und_tut_nichts(self):
        a = Attrappe()
        self.spiegeln(a, False)
        text = self.ausgabe()
        self.assertIn("wuerde anlegen mit 1 Bildern", text)
        self.assertIn("+ 01-eins.jpg", text)
        self.assertNotIn("angelegt", text)
        schreibend = [x for x in a.aufrufe if x[0][1] == "release"
                      and x[0][0] in ("create", "upload", "edit", "delete-asset")]
        self.assertEqual(schreibend, [], "der Plan hat geschrieben")

    # -- Anlegen, Hochladen, Text ------------------------------------------

    def test_der_lauf_legt_an_laedt_hoch_und_schreibt_den_text(self):
        a = Attrappe()
        self.spiegeln(a, True)
        self.assertEqual(sorted(a.releases), ["m1", "m2"])
        for tag in ("m1", "m2"):
            self.assertEqual(len(a.releases[tag]["assets"]), 1, tag)
            self.assertIsInstance(a.releases[tag]["assets"][0], dict)
            self.assertIn("Stand im Code: 72439dc", a.releases[tag]["body"])
        self.assertIn("angelegt", self.ausgabe())

    def test_ein_zweiter_lauf_ist_ein_nichts_tun(self):
        a = Attrappe()
        self.spiegeln(a, True)
        self.meldung = []
        self.spiegeln(a, True)
        self.assertIn("passt (1 Bilder)", self.ausgabe())
        schreibend = [x for x in a.aufrufe
                      if x[0][0] in ("create", "upload", "edit", "delete-asset")]
        self.assertEqual(schreibend, [], "der zweite Lauf hat wieder geschrieben")

    def test_ein_altes_asset_wird_entfernt(self):
        a = Attrappe({"m1": {"assets": [{"name": "01-eins.jpg"}, {"name": "alt.jpg"}],
                      "body": "x"},
                      "m2": {"assets": [{"name": "02-zwei.jpg"}], "body": "x"}})
        self.spiegeln(a, True)
        self.assertEqual([x["name"] for x in a.releases["m1"]["assets"]],
                         ["01-eins.jpg"])
        self.assertIn("- alt.jpg (nicht auf der Seite)", self.ausgabe())

    def test_ein_veralteter_text_wird_ersetzt(self):
        a = Attrappe({"m1": {"assets": [{"name": "01-eins.jpg"}], "body": "alter Text"},
                      "m2": {"assets": [{"name": "02-zwei.jpg"}], "body": "alter Text"}})
        self.spiegeln(a, True)
        for tag in ("m1", "m2"):
            self.assertNotIn("alter Text", a.releases[tag]["body"])
            self.assertIn("Eins" if tag == "m1" else "Zwei", a.releases[tag]["body"])

    def test_das_private_repo_wird_nie_angefasst(self):
        """Jeder gh-Aufruf nennt das oeffentliche Repo. Ohne das waere ein
        Tippfehler hier ein Schreibzugriff auf das private Spiel-Repo."""
        a = Attrappe()
        self.spiegeln(a, True)
        self.assertTrue(a.aufrufe)
        for args, repo in a.aufrufe:
            self.assertEqual(repo, "oeffentlich/test", f"falsches Repo: {args}")

    def test_gh_haengt_das_repo_an_den_befehl(self):
        """Der Zusatz entsteht in gh() selbst - deshalb wird er hier
        nachgewiesen und nicht im Attrappen-Aufruf gesucht."""
        befehle = []

        class Fertig:
            returncode = 0
            stdout = "[]"
            stderr = ""

        alt = ro.subprocess.run

        def fangen(befehl, **kwargs):
            befehle.append(befehl)
            return Fertig()

        ro.subprocess.run = fangen
        self.addCleanup(lambda: setattr(ro.subprocess, "run", alt))
        ro.gh("release", "list", repo="oeffentlich/test")
        ro.gh("release", "list")
        self.assertIn("--repo", befehle[0])
        self.assertEqual(befehle[0][-2:], ["--repo", "oeffentlich/test"])
        self.assertNotIn("--repo", befehle[1],
                         "ohne --repo laeuft der Aufruf im cwd des Projekts - "
                         "das waere das private Repo")

    # -- Netzfehler --------------------------------------------------------

    def test_ein_netzfehler_wird_wiederholt(self):
        """Die Wiederholung sitzt in gh() - also wird hier gh() selbst
        geprueft, nicht das Spiegeln (dessen Attrappe den Retry ja umgeht).

        GEMESSEN im echten Lauf: nach acht von vierzehn Releases brach der
        Aufruf an einem `connectex` ab. Genau der Fall.
        """
        aufrufe = []

        class Fertig:
            def __init__(self, code, raus, fehler):
                self.returncode, self.stdout, self.stderr = code, raus, fehler

        def fangen(befehl, **kwargs):
            aufrufe.append(befehl)
            if len(aufrufe) == 1:
                return Fertig(1, "", "dial tcp 1.2.3.4:443: connectex: "
                                     "Ein Verbindungsversuch ist fehlgeschlagen")
            return Fertig(0, "[]", "")

        alt_run, alt_sleep = ro.subprocess.run, ro.time.sleep
        ro.subprocess.run = fangen
        ro.time.sleep = lambda _s: None
        self.addCleanup(lambda: setattr(ro.subprocess, "run", alt_run))
        self.addCleanup(lambda: setattr(ro.time, "sleep", alt_sleep))

        code, _, _ = ro.gh("release", "list", repo="oeffentlich/test")
        self.assertEqual(code, 0)
        self.assertEqual(len(aufrufe), 2, "es wurde nicht wiederholt")

    def test_der_fehler_text_wird_nicht_als_netzfehler_gezaehlt(self):
        """HTTP-Fehler sind keine Verbindungsprobleme: dreimal denselben
        404 zu schicken haette nur Zeit gekostet."""
        alt = ro.time.sleep
        ro.time.sleep = lambda _s: None
        self.addCleanup(lambda: setattr(ro.time, "sleep", alt))
        ro.gh = lambda *args, repo=None, versuche=3: (
            1, "", "release not found: http 404")
        with self.assertRaises(ro.Fehler):
            ro.spiegeln("oeffentlich/test", self.seite, self.bilder, "72439dc",
                        True, [])

    def test_netzfehler_erkennt_nur_echte_verbindungsprobleme(self):
        self.assertTrue(ro.netzfehler("dial tcp 1.2.3.4:443: connectex: fehlgeschlagen"))
        self.assertTrue(ro.netzfehler("net/http: TLS handshake timeout"))
        self.assertFalse(ro.netzfehler("release not found"))
        self.assertFalse(ro.netzfehler("HTTP 422: Validation Failed"))


if __name__ == "__main__":
    unittest.main()
