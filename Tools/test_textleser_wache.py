# coding: utf-8
"""Wache: subprocess-Textleser ohne errors= werden abgelehnt (PEP 686).

Ab Python 3.15 (PEP 686) dekodiert der Textmodus UTF-8/strict - ein
einziges Fremd-Byte laesst den Leser still sterben (gemessen am
mklink-Reader-Thread unter PYTHONUTF8=1, 29.09.2026). Die Laufzeit-
Wache (test_utf8_wache.py) misst die bestehenden Suiten in der
3.15-Umgebung; diese Wache hier greift FRUEHER: sie liest jede
getrackte Datei in Tools/ als AST und lehnt jeden subprocess-Aufruf
ab, der Textmodus ohne errors= verlangt - als statische Pruefung im
normalen Suite-Lauf, bevor der Code ueberhaupt laeuft.

Textmodus heisst hier: text=True, universal_newlines=True oder eine
gesetzte encoding-Option. Gehaertet heisst: errors= mit von None
verschiedenem Wert (errors="replace" ist der Projekt-Stil seit
804bbf2). Ein Aufruf ohne jede Text-Option (bytes) bleibt erlaubt;
text=False auch. Wo der Wert keine Konstante ist, entscheidet die
Wache grosszuegig fuer den Autor - sie jagt die stille STRICT-Falle,
keine dynamischen Styles.

Ausnahmen stehen in AUSNAHMEN - mit Begruendung und ohne Verjaehrung:
wer eine Datei committet oder haertet, traegt sie dort aus. Der
Zaehl-Test haelt den Bestand sichtbar, damit das Ausnehmen nicht
still geschieht.
"""
import ast
import os
import subprocess
import tempfile
import unittest

TOOLSPFAD = os.path.dirname(os.path.abspath(__file__))
PROJEKT = os.path.dirname(TOOLSPFAD)

# Diese Dateien duerfen (noch) strict-Textleser tragen, mit Grund.
# test_gate_worktree.py traegt fremde uncommittete Hunks (import
# threading / LockAusgabeTest) - 7 strict-Leser, Stand 29.09.2026.
# Sobald der fremde Thread committet hat: haerten und HIER AUSNEHMEN.
AUSNAHMEN = {
    "Tools/test_gate_worktree.py":
        "fremde uncommittete WIP im Arbeitsbaum - nicht anfassen; "
        "nach dem Commit haerten und diesen Eintrag loeschen",
}

# getestete Funktionen, kein Lambda im Testpfad: run/Popen/check_output/
# check_call/popen decken subprocess komplett ab, os.popen mit.
AUFRUFE = {"run", "Popen", "check_output", "check_call", "popen"}


def _ist_wahr(wert):
    """Constant True -> True; Constant False/None -> False; sonst True
    (dynamischer Wert: grosszuegig als Textmodus lesen)."""
    if isinstance(wert, ast.Constant):
        return bool(wert.value)
    return True


def _ist_gehaertet(kws):
    """errors= muss vorhanden und nicht None sein."""
    if "errors" not in kws:
        return False
    wert = kws["errors"]
    if isinstance(wert, ast.Constant) and wert.value is None:
        return False
    return True


def strict_textleser(pfad):
    """[(zeile, funktion)] - subprocess-Textleser ohne errors= in einer Datei."""
    with open(pfad, "rb") as fh:
        baum = ast.parse(fh.read(), filename=pfad)
    treffer = []
    for knoten in ast.walk(baum):
        if not isinstance(knoten, ast.Call):
            continue
        name = getattr(knoten.func, "id", None) \
            or getattr(knoten.func, "attr", None)
        if name not in AUFRUFE:
            continue
        kws = {k.arg: k.value for k in knoten.keywords if k.arg}
        textmodus = (
            ("text" in kws and _ist_wahr(kws["text"]))
            or ("universal_newlines" in kws and _ist_wahr(kws["universal_newlines"]))
            or ("encoding" in kws and _ist_wahr(kws["encoding"]))
        )
        if not textmodus or _ist_gehaertet(kws):
            continue
        treffer.append((knoten.lineno, name))
    return treffer


def getrackte_werkzeuge():
    """Getrackte Tools/*.py relativ zum Projekt - fail-closed: schlaegt
    git fehl, bricht der Test sichtbar, statt still nichts zu pruefen."""
    fertig = subprocess.run(
        ["git", "ls-files", "Tools/*.py"], cwd=PROJEKT,
        capture_output=True, text=True, errors="replace", check=True)
    dateien = [z for z in fertig.stdout.splitlines() if z.strip()]
    if not dateien:
        raise AssertionError("git ls-files lieferte keine Dateien - "
                             "die Wache wuerde still nichts pruefen")
    return dateien


class ScannerTest(unittest.TestCase):
    """Die Scanner-Funktion selbst - mit Snippets gegen beide Richtungen."""

    def schreibe(self, quelle):
        handle, pfad = tempfile.mkstemp(suffix=".py")
        os.close(handle)
        with open(pfad, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(quelle)
        self.addCleanup(os.unlink, pfad)
        return pfad

    def test_strict_ausdruck_wird_gefunden(self):
        pfad = self.schreibe(
            "import subprocess\n"
            "subprocess.run(['x'], capture_output=True, text=True)\n")
        treffer = strict_textleser(pfad)
        self.assertEqual(len(treffer), 1, treffer)
        self.assertEqual(treffer[0][1], "run")

    def test_encoding_allein_ist_strict(self):
        pfad = self.schreibe(
            "import subprocess\n"
            "subprocess.run(['x'], text=True, encoding='utf-8')\n")
        self.assertEqual(len(strict_textleser(pfad)), 1)

    def test_errors_none_ist_nicht_gehaertet(self):
        pfad = self.schreibe(
            "import subprocess\n"
            "subprocess.run(['x'], text=True, errors=None)\n")
        self.assertEqual(len(strict_textleser(pfad)), 1)

    def test_errors_replace_ist_gehaertet(self):
        pfad = self.schreibe(
            "import subprocess\n"
            "subprocess.run(['x'], text=True, errors='replace')\n")
        self.assertEqual(strict_textleser(pfad), [])

    def test_dynamisches_errors_gilt_als_gehaertet(self):
        pfad = self.schreibe(
            "import subprocess\n"
            "FEHLER = 'replace'\n"
            "subprocess.run(['x'], text=True, errors=FEHLER)\n")
        self.assertEqual(strict_textleser(pfad), [])

    def test_bytes_aufruf_bleibt_erlaubt(self):
        pfad = self.schreibe(
            "import subprocess\n"
            "subprocess.run(['x'], capture_output=True)\n"
            "subprocess.run(['x'], text=False)\n")
        self.assertEqual(strict_textleser(pfad), [])


class BestandTest(unittest.TestCase):
    """Der eigentliche Gate-Test ueber alle getrackten Tools/*.py."""

    def test_ausnahmen_existieren_noch(self):
        """Eine Ausnahme fuer eine verschwundene Datei ist Leichenpflege -
        sie waere still fuer immer ausgenommen, ohne dass sie existiert."""
        for pfad in AUSNAHMEN:
            self.assertTrue(os.path.isfile(os.path.join(PROJEKT, pfad)),
                            "Ausnahme gilt fuer fehlende Datei: %s" % pfad)

    def test_gate_werkzeuge_zaehlen_bekannte_treffer(self):
        """Jede Ausnahme haelt ihren gemessenen Bestand fest.

        GEMESSEN am 29.09.2026: der COMMITTETE Stand von
        test_gate_worktree.py traegt 6 strict-Leser, der Arbeitsstand 7
        (die uncommittete Fremd-WIP bringt den siebten bei Zeile ~941
        mit). Solange dieser Uebergang laeuft, gilt das Fenster 6..7;
        zugelassen sind beide Zaehlstaende, dokumentiert ist, welcher
        welcher ist. Aendert sich die Zahl DARUEBER HINAUS, scheitert
        dieser Test GEWOLLT: dann hat jemand committet oder ergaenzt,
        und die Ausnahme (und dieser Erwartungswert) muessen mitgehen -
        nicht still weitergelten. Endzustand: haerten, Ausnahme
        loeschen, diese Pruefung auf 0 ziehen.
        """
        datei = "Tools/test_gate_worktree.py"
        self.assertIn(datei, AUSNAHMEN,
                      "keine Ausnahme mehr noetig - diesen Test loeschen")
        treffer = strict_textleser(os.path.join(PROJEKT, datei))
        self.assertIn(
            len(treffer), (6, 7),
            "Bestand in %s verschoben (%d statt 6 oder 7): %s - "
            "Ausnahme und Erwartungsfenster anpassen oder haerten"
            % (datei, len(treffer), treffer))

    def test_kein_strict_textleser_in_den_werkzeugen(self):
        """Alle getrackten Tools/*.py dekodieren tolerant (oder stehen
        mit Begruendung in AUSNAHMEN). Scheitert der Test, steht in der
        Meldung Datei:Zeile und der Stil-Fix: errors=\"replace\"."""
        uebrig = {}
        for pfad in getrackte_werkzeuge():
            if pfad in AUSNAHMEN:
                continue
            treffer = strict_textleser(os.path.join(PROJEKT, pfad))
            if treffer:
                uebrig[pfad] = treffer
        self.assertEqual(
            uebrig, {},
            "Strict-Textleser ohne errors= (ab Python 3.15/PEP 686 stirbt "
            "der Leser still an einem Fremd-Byte) - mit errors=\"replace\" "
            "haerten:\n%s" % "\n".join(
                "%s: Zeile %d (%s)" % (pfad, z, n)
                for pfad, liste in sorted(uebrig.items())
                for z, n in liste))


if __name__ == "__main__":
    unittest.main()
