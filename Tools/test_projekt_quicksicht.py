"""Waechter ueber docs/projekt-quicksicht.md: die Seite muss zum Baum passen.

WOZU: Die Quicksicht ist die Seite, die ein neuer Agent zuerst liest. Ihr
erster Entwurf nannte Code-Ordner, die es nicht gibt (Player/AI/Network), und
liess fuenf echte weg - geprueft hatte das niemand, weil eine Doku-Seite
keinen Compiler hat. Diese Pruefung haelt zwei Aussagen der Seite am Baum
fest:

* die Ordnerliste `Source/WiesbadenReal/` nach Domaene (...) gegen die
  tatsaechlichen Unterordner;
* welche Gates vor dem Commit und welche vor dem Push laufen - gegen
  Tools/vor_dem_commit.py (dort entscheidet `stufe == "voll"`) und gegen die
  Hooks in Tools/git-hooks/, die die Stufen aufrufen.

Die Gates werden per AST aus vor_dem_commit.py gelesen, nicht aus dessen
Kommentaren: Kommentare driften genauso wie die Quicksicht.

Aufruf (aus der Projektwurzel):
    python -m unittest discover -s Tools -p "test_projekt_quicksicht.py"
"""
import ast
import re
import unittest
from pathlib import Path

WURZEL = Path(__file__).resolve().parent.parent
QUICKSICHT = WURZEL / "docs" / "projekt-quicksicht.md"
SOURCE = WURZEL / "Source" / "WiesbadenReal"
VOR_DEM_COMMIT = WURZEL / "Tools" / "vor_dem_commit.py"
HOOKS = WURZEL / "Tools" / "git-hooks"

# "Python-Suiten" ist kein numeriertes Gate, laeuft aber in derselben Liste.
PYTHON_SUITEN = "Python-Suiten"


def gate_kennungen(text):
    """Alle Gate-Kennungen in einem Text: 'Gate 2+3' -> {'2', '3'}."""
    kennungen = set()
    for treffer in re.finditer(r"Gate\s+([0-9A-Z](?:\+[0-9A-Z])*)\b", text):
        kennungen.update(treffer.group(1).split("+"))
    if PYTHON_SUITEN in text:
        kennungen.add(PYTHON_SUITEN)
    return kennungen


def ist_voll_bedingung(knoten):
    """True fuer `if stufe == "voll":`."""
    return (isinstance(knoten, ast.Compare)
            and isinstance(knoten.left, ast.Name) and knoten.left.id == "stufe"
            and len(knoten.ops) == 1 and isinstance(knoten.ops[0], ast.Eq)
            and isinstance(knoten.comparators[0], ast.Constant)
            and knoten.comparators[0].value == "voll")


def gates_aus_vor_dem_commit(quelltext):
    """(schnell, nur_voll): Gate-Kennungen je Stufe laut vor_dem_commit.py.

    Ein Gate ist jeder Aufruf `lauf.fahre(...)`/`lauf.fahre_gate(...)` mit
    einem Gate-Namen als erstem Argument. Liegt er im Rumpf eines
    `if stufe == "voll":`, laeuft er nur vor dem Push; sonst in beiden Stufen.
    """
    baum = ast.parse(quelltext)
    schnell, nur_voll = set(), set()

    def besuche(knoten, unter_voll):
        if isinstance(knoten, ast.If):
            voll = ist_voll_bedingung(knoten.test)
            for kind in knoten.body:
                besuche(kind, unter_voll or voll)
            for kind in knoten.orelse:
                besuche(kind, unter_voll)
            return
        if (isinstance(knoten, ast.Call)
                and isinstance(knoten.func, ast.Attribute)
                and knoten.func.attr in ("fahre", "fahre_gate")
                and knoten.args
                and isinstance(knoten.args[0], ast.Constant)
                and isinstance(knoten.args[0].value, str)):
            kennungen = gate_kennungen(knoten.args[0].value)
            (nur_voll if unter_voll else schnell).update(kennungen)
        for kind in ast.iter_child_nodes(knoten):
            besuche(kind, unter_voll)

    besuche(baum, False)
    return schnell, nur_voll - schnell


def abschnitt(text, anfang, ende):
    """Text ab `anfang` bis vor `ende` (oder bis zum Absatzende)."""
    start = text.index(anfang)
    stopp = text.find(ende, start + len(anfang)) if ende else -1
    if stopp < 0:
        stopp = text.find("\n\n", start)
    return text[start:stopp if stopp >= 0 else len(text)]


class ProjektQuicksicht(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        if not QUICKSICHT.exists():
            raise unittest.SkipTest("docs/projekt-quicksicht.md fehlt in diesem Stand")
        cls.text = QUICKSICHT.read_text(encoding="utf-8")
        # Zeilenumbrueche im Fliesstext sind fuer die Suche bedeutungslos.
        cls.flach = re.sub(r"\s+", " ", cls.text)

    def test_source_ordner_stimmen(self):
        # `Source/` oder `Source/WiesbadenReal/` - der Inhalt zaehlt, nicht die
        # Schreibweise des Pfades.
        treffer = re.search(r"`Source/[^`]*` nach Domaene \(([^)]*)\)", self.flach)
        self.assertIsNotNone(treffer, "Ordnerliste '`Source/...` nach Domaene "
                             "(...)' nicht gefunden - Satz umformuliert?")
        genannt = {n.strip() for n in treffer.group(1).split(",") if n.strip()}
        echt = {p.name for p in SOURCE.iterdir() if p.is_dir()}
        self.assertEqual(
            genannt, echt,
            "Quicksicht-Ordnerliste passt nicht zu Source/WiesbadenReal/: "
            "fehlt in der Seite %s, gibt es nicht %s"
            % (sorted(echt - genannt), sorted(genannt - echt)))

    def test_hooks_und_einrichtung_existieren(self):
        for pfad in ("Tools/git-hooks/", "Tools/hooks_einrichten.py"):
            self.assertIn(pfad, self.flach, "Quicksicht nennt %s nicht mehr" % pfad)
        self.assertTrue((WURZEL / "Tools" / "hooks_einrichten.py").is_file())
        for hook in ("pre-commit", "pre-push"):
            self.assertTrue((HOOKS / hook).is_file(), "Hook %s fehlt" % hook)

    def test_hooks_rufen_die_stufen(self):
        # Die Quicksicht ordnet Gates den Hooks zu; das stimmt nur, solange
        # pre-commit die schnelle und pre-push die volle Stufe faehrt.
        commit = (HOOKS / "pre-commit").read_text(encoding="utf-8")
        push = (HOOKS / "pre-push").read_text(encoding="utf-8")
        self.assertRegex(commit, r"vor_dem_commit\.py\b.*--stufe schnell")
        self.assertRegex(push, r"vor_dem_commit\.py\b.*--stufe voll")

    def test_gates_je_hook_stimmen(self):
        schnell, nur_voll = gates_aus_vor_dem_commit(
            VOR_DEM_COMMIT.read_text(encoding="utf-8"))
        self.assertTrue(schnell and nur_voll, "keine Gates in vor_dem_commit.py "
                        "gefunden - hat sich der Aufruf lauf.fahre(...) geaendert?")

        vor_commit = gate_kennungen(abschnitt(self.flach, "pre-commit", "pre-push"))
        vor_push = gate_kennungen(abschnitt(self.flach, "pre-push", ". "))

        self.assertEqual(
            vor_commit, schnell,
            "Quicksicht 'pre-commit' passt nicht zu vor_dem_commit.py: fehlt %s, "
            "laeuft dort nicht vor dem Commit %s"
            % (sorted(schnell - vor_commit), sorted(vor_commit - schnell)))
        self.assertEqual(
            vor_push, nur_voll,
            "Quicksicht 'pre-push zusaetzlich' passt nicht zu vor_dem_commit.py: "
            "fehlt %s, laeuft dort nicht erst vor dem Push %s"
            % (sorted(nur_voll - vor_push), sorted(vor_push - nur_voll)))


class GateLeser(unittest.TestCase):
    """Der AST-Leser selbst - sonst waere ein stummer Leser 'alles gruen'."""

    BEISPIEL = '''
def fahre_alles(lauf, stufe):
    lauf.fahre_gate("Gate B  Besitz", True, 0.0)
    lauf.fahre("Gate 0  Engine-Pfade", [])
    if stufe == "voll":
        lauf.fahre("Python-Suiten", [])
    else:
        lauf.ueberspringe("Python-Suiten", "schnell")
    if dateien:
        lauf.fahre("Gate 1  Kompilieren", "x")
    if stufe == "voll":
        lauf.fahre("Gate 2+3  Tests und Rauchtest", "y")
'''

    def test_stufen_werden_getrennt(self):
        schnell, nur_voll = gates_aus_vor_dem_commit(self.BEISPIEL)
        self.assertEqual(schnell, {"B", "0", "1"})
        self.assertEqual(nur_voll, {PYTHON_SUITEN, "2", "3"})

    def test_kennungen(self):
        self.assertEqual(gate_kennungen("Gate 2+3 und Gate B"), {"2", "3", "B"})
        self.assertEqual(gate_kennungen("keine Gates"), set())


if __name__ == "__main__":
    unittest.main()
