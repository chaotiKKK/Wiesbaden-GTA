"""Waechter ueber die Zeilenenden des Baumes (.gitattributes).

WOZU: AGENTS.md wurde einmal als 3078-Zeilen-Diff committet, ohne dass sich
ein Zeichen am Inhalt geaendert haette - ein Werkzeug hatte die Datei mit
anderen Zeilenenden neu geschrieben. Die .gitattributes verhindert das, indem
Git beim Einchecken auf LF normalisiert. Diese Pruefung haelt fest, dass die
Regel wirkt und vollstaendig bleibt.

Sie faellt vor allem dann, wenn jemand eine NEUE Binaer-Endung einbringt (ein
.psd, ein .wav-Format, ein neues Unreal-Format) ohne Zeile in .gitattributes.
Git erkennt Binaerdateien zwar an ihren Null-Bytes selbst - aber eine
zerstoerte .uasset faellt erst auf, wenn der Editor sie nicht mehr laedt.

Aufruf (aus der Projektwurzel):
    python -m unittest discover -s Tools -p "test_zeilenenden.py"
"""
import subprocess
import unittest
from pathlib import Path

WURZEL = Path(__file__).resolve().parent.parent

# So erkennt Git selbst, ob eine Datei binaer ist: ein Null-Byte in den ersten
# 8000. Dieselbe Regel hier, damit die Pruefung dasselbe sieht wie Git.
SPAEHWEITE = 8000


def git(*args):
    return subprocess.run(
        ["git", *args], cwd=WURZEL, capture_output=True, text=True, check=True
    ).stdout


def verfolgte_dateien():
    roh = subprocess.run(
        ["git", "ls-files", "-z"], cwd=WURZEL, capture_output=True, check=True
    ).stdout
    return [p.decode("utf-8", "surrogateescape") for p in roh.split(b"\0") if p]


def vorhandene_dateien():
    """Verfolgte Pfade, die es auf der Platte wirklich gibt.

    `git ls-files` liefert auch Pfade, die im Arbeitsbaum geloescht sind (im
    Index stehen sie weiter, bis der Loeschvorgang committet wird). Am
    26.09.2026 waren fuenf Sebbo-Assets genau so beschaffen - und diese
    Zeilenenden-Pruefung starb daran mit FileNotFoundError, statt ihr
    eigentliches Thema zu pruefen. Ein Loeschen ist eine Entscheidung des
    Menschen und wird hier weder verhindert noch bewertet; die Liste wird nur
    zurueckgegeben, damit der Aufrufer sie melden kann.
    """
    return [p for p in verfolgte_dateien() if (WURZEL / p).is_file()]


def fehlende_dateien():
    return [p for p in verfolgte_dateien() if not (WURZEL / p).is_file()]


def ist_binaer(pfad):
    with open(WURZEL / pfad, "rb") as f:
        return b"\0" in f.read(SPAEHWEITE)


def attribute(pfade, name):
    """git check-attr fuer viele Pfade auf einmal - einzeln waere es zu langsam."""
    if not pfade:
        return {}
    eingabe = "\0".join(pfade) + "\0"
    roh = subprocess.run(
        ["git", "check-attr", "--stdin", "-z", name],
        cwd=WURZEL, input=eingabe.encode(), capture_output=True, check=True,
    ).stdout
    stuecke = roh.split(b"\0")
    ergebnis = {}
    # Ausgabe ist <pfad>\0<attribut>\0<wert>\0 je Eintrag.
    for i in range(0, len(stuecke) - 2, 3):
        ergebnis[stuecke[i].decode("utf-8", "surrogateescape")] = stuecke[i + 2].decode()
    return ergebnis


class RegelVorhandenTest(unittest.TestCase):
    def test_gitattributes_ist_verfolgt(self):
        self.assertIn(".gitattributes", verfolgte_dateien(),
                      ".gitattributes fehlt - ohne sie legt Git die Bytes roh ab")

    def test_grundregel_normalisiert_auf_lf(self):
        text = (WURZEL / ".gitattributes").read_text(encoding="utf-8")
        self.assertIn("* text=auto eol=lf", text,
                      "Die Grundregel fehlt: ohne 'text' normalisiert Git nichts")


class BinaerdateienGeschuetztTest(unittest.TestCase):
    def test_keine_binaerdatei_gilt_als_text(self):
        binaer = [p for p in vorhandene_dateien() if ist_binaer(p)]
        self.assertGreater(len(binaer), 100, "Erwartet werden Hunderte Unreal-Assets")
        werte = attribute(binaer, "text")
        als_text = sorted(p for p in binaer if werte.get(p) not in ("unset", "unspecified"))
        # "unspecified" waere Git-Heuristik - erlaubt, aber nicht erwuenscht;
        # "set" waere gefaehrlich: Git wuerde die Datei umschreiben.
        gefaehrlich = sorted(p for p in binaer if werte.get(p) == "set")
        self.assertEqual(gefaehrlich, [],
                         "Diese Binaerdateien wuerde Git umschreiben und zerstoeren")
        endungen = sorted({Path(p).suffix.lower() for p in als_text})
        self.assertEqual(endungen, [],
                         f"Binaer-Endungen ohne eigene Zeile in .gitattributes: {endungen}")


class BestandSauberTest(unittest.TestCase):
    def test_keine_datei_ist_in_sich_gemischt(self):
        """Gemischte Dateien sind geladene Fallen - AGENTS.md war eine."""
        gemischt = []
        for p in vorhandene_dateien():
            if ist_binaer(p):
                continue
            b = (WURZEL / p).read_bytes()
            crlf = b.count(b"\r\n")
            lf = b.count(b"\n") - crlf
            if crlf and lf:
                gemischt.append((p, crlf, lf))
        self.assertEqual(gemischt, [], "In sich gemischte Zeilenenden")

    def test_im_index_steht_ueberall_lf(self):
        """Der entscheidende Punkt: der eingecheckte Blob ist LF.

        Nur deshalb ist es gleichgueltig, was ein Werkzeug auf die Platte
        schreibt. Stuende hier CRLF, waere der Churn wieder moeglich.
        """
        mit_crlf = []
        for p in verfolgte_dateien():
            # Binaerheit am BLOB pruefen, nicht an der Datei auf der Platte.
            # Dieser Test handelt vom Index, und der Index enthaelt auch Pfade,
            # die im Arbeitsbaum geloescht sind: am 26.09.2026 brach die
            # Pruefung mit FileNotFoundError ab, weil fuenf Sebbo-Assets
            # geloescht, aber noch eingecheckt waren. Die Zeilenenden dieser
            # Blobs sind trotzdem pruefbar - sie sind ja genau das Thema.
            blob = subprocess.run(
                ["git", "show", f":{p}"], cwd=WURZEL, capture_output=True, check=True
            ).stdout
            if b"\0" in blob[:SPAEHWEITE]:
                continue
            if b"\r\n" in blob:
                mit_crlf.append(p)
        self.assertEqual(mit_crlf, [], "Diese Blobs enthalten CRLF im Index")


class ChurnUnmoeglichTest(unittest.TestCase):
    """Die eigentliche Probe: der Fehler von damals, nachgestellt.

    Gemessen wird der Diff VOR und NACH der Umschrift. Auf "leer" zu pruefen
    waere falsch - die Datei darf ja gerade bearbeitet sein. Genau daran fiel
    eine erste Fassung dieses Tests, waehrend AGENTS.md einen Anhang trug.
    Entscheidend ist nicht, dass kein Diff da ist, sondern dass die
    Zeilenenden ihn NICHT VERAENDERN.
    """

    def _numstat(self, pfad):
        roh = subprocess.run(
            ["git", "diff", "--numstat", "--", pfad],
            cwd=WURZEL, capture_output=True, text=True, check=True,
        ).stdout.strip()
        return roh.split("\t")[:2] if roh else ["0", "0"]

    def _diff_um_umschrift(self, pfad, nach_crlf):
        voll = WURZEL / pfad
        urzustand = voll.read_bytes()
        vorher = self._numstat(pfad)
        try:
            roh = urzustand.replace(b"\r\n", b"\n")
            voll.write_bytes(roh.replace(b"\n", b"\r\n") if nach_crlf else roh)
            return vorher, self._numstat(pfad)
        finally:
            voll.write_bytes(urzustand)

    def test_umschrift_auf_lf_aendert_den_diff_nicht(self):
        vorher, nachher = self._diff_um_umschrift("AGENTS.md", nach_crlf=False)
        self.assertEqual(vorher, nachher,
                         "Umschrift auf LF hat den Diff veraendert - das ist der Churn")

    def test_umschrift_auf_crlf_aendert_den_diff_nicht(self):
        vorher, nachher = self._diff_um_umschrift("AGENTS.md", nach_crlf=True)
        self.assertEqual(vorher, nachher,
                         "Umschrift auf CRLF hat den Diff veraendert - das ist der Churn")


if __name__ == "__main__":
    unittest.main()
