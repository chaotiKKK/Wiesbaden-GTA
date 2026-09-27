"""Selbsttest des Release-Text-Erzeugers (Tools/releases_texte_ausrichten.py).

    python -m unittest discover -s Tools -p "test_releases_texte_ausrichten.py"

DER EINE SATZ, UM DEN ES GEHT: ein Release-Text muss ohne GitHub-Konto
lesbar sein. Das Spiel-Repo ist privat - jeder Link dorthin endet fuer
Fremde in 404. Deshalb zeigt der Erzeuger seine Bilder auf dem
oeffentlichen Schaufenster und haelt nach, dass kein Link ins private Repo
zurueckkehrt.
"""
import pathlib
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import releases_texte_ausrichten as rta  # noqa: E402

SEITE = """# Meilensteine

| # | Meilenstein | Zeitraum | Stand |
|---|---|---|---|
| 1 | [Eins](#1-eins) | 02.09. | fertig |

Siehe [docs/meilensteine.md](docs/meilensteine.md) fuer alles.

---

## 1. Eins

*02.09.2026*

Ein Absatz mit [einem Link](https://github.com/chaotiKKK/Wiesbaden-GTA) mitten drin.

![Ein Bild](meilensteine/bilder/01-eins.jpg)

| | |
|---|---|
| ![Tabellebild](meilensteine/bilder/02-zwei.jpg) | ![Zweiter Pfad](/x/docs/meilensteine/bilder/03-drei.gif) |
"""


class TextTest(unittest.TestCase):
    def setUp(self):
        self.wurzel = pathlib.Path(tempfile.mkdtemp(prefix="wb_releasetext_"))
        self.addCleanup(shutil.rmtree, self.wurzel, ignore_errors=True)
        self.seite = self.wurzel / "meilensteine.md"
        self.seite.write_text(SEITE, encoding="utf-8", newline="\n")
        self.alt = (rta.SEITE, dict(rta.SEITEN_TITEL))
        rta.SEITE = self.seite
        rta.SEITEN_TITEL = {1: "Eins"}
        self.addCleanup(self.zurueck)

    def zurueck(self):
        rta.SEITE, rta.SEITEN_TITEL = self.alt

    def text(self):
        return rta.release_text(1, "72439dc")

    def test_kein_link_ins_private_repo(self):
        """Der Kern. Der private Repo-Name darf nicht vorkommen - weder als
        Bildlink noch als Fusszeilen-Link noch als Nebensatz."""
        with self.assertRaises(RuntimeError) as fehler:
            self.text()
        self.assertIn("private Repo", str(fehler.exception))

    def test_ohne_fremden_link_ist_der_text_oeffentlich(self):
        self.seite.write_text(SEITE.replace(
            "Ein Absatz mit [einem Link](https://github.com/chaotiKKK/Wiesbaden-GTA) "
            "mitten drin.", "Ein ganz normaler Absatz."), encoding="utf-8", newline="\n")
        text = self.text()
        self.assertNotIn("Wiesbaden-GTA", text)
        self.assertIn("raw.githubusercontent.com/chaotiKKK/wiesbaden-real-meilensteine",
                      text)

    def test_alle_drei_bildpfade_werden_oeffentlich(self):
        """Relativer Pfad, absoluter Pfad und Tabellenzelle - alle drei Formen
        kommen in der Seite vor, alle drei muessen oeffentlich werden."""
        self.seite.write_text(SEITE.replace(
            "Ein Absatz mit [einem Link](https://github.com/chaotiKKK/Wiesbaden-GTA) "
            "mitten drin.", "Ein ganz normaler Absatz."), encoding="utf-8", newline="\n")
        text = self.text()
        for name in ("01-eins.jpg", "02-zwei.jpg", "03-drei.gif"):
            self.assertIn(f"{rta.OEFFENTLICH}/bilder/{name}", text)
        self.assertNotIn("docs/meilensteine/bilder/", text)

    def test_der_fuss_zeigt_auf_das_schaufenster(self):
        self.seite.write_text(SEITE.replace(
            "Ein Absatz mit [einem Link](https://github.com/chaotiKKK/Wiesbaden-GTA) "
            "mitten drin.", "Ein ganz normaler Absatz."), encoding="utf-8", newline="\n")
        text = self.text()
        self.assertIn(f"[Schaufenster]({rta.SCHaufenSTER})", text)
        self.assertIn("Stand im Code: 72439dc", text)

    def test_die_ueberschrift_steht_davor(self):
        self.seite.write_text(SEITE.replace(
            "Ein Absatz mit [einem Link](https://github.com/chaotiKKK/Wiesbaden-GTA) "
            "mitten drin.", "Ein ganz normaler Absatz."), encoding="utf-8", newline="\n")
        self.assertTrue(self.text().startswith("## 1. Eins\n\n"))

    def test_der_oeffentliche_repo_name_ist_erlaubt(self):
        """Gegenprobe zum PRIVAT-Muster: `wiesbaden-real-meilensteine` heisst
        sich aehnlich, ist aber oeffentlich und muss durchgelassen werden."""
        self.assertIsNone(rta.PRIVAT.search(
            f"{rta.OEFFENTLICH}/bilder/01-eins.jpg"))
        self.assertIsNone(rta.PRIVAT.search(rta.SCHaufenSTER))


if __name__ == "__main__":
    unittest.main()
