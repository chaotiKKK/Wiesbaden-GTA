r"""Die Release-Gates fahren, BEVOR ein Commit entsteht - nicht erst beim Paket.

    python Tools/vor_dem_commit.py                # schnelle Stufe (Vorgabe)
    python Tools/vor_dem_commit.py --stufe voll   # zusaetzlich Gates 2 und 3
    python Tools/vor_dem_commit.py --gestaged     # nur was git vorgemerkt hat

WOFUER: Die Gates gab es schon, aber sie liefen erst in `build_release.cmd` -
also erst, wenn jemand ein Paket wollte. Ein Fehler von heute fiel damit
Tage spaeter auf, verteilt ueber mehrere Commits, und blockierte ausgerechnet
den Lauf, der Stunden dauert.

WARUM ZWEI STUFEN - und das ist eine gemessene Entscheidung, keine Meinung:

    Gate 0  Engine-Pfade        1 s
    Python-Suiten             46 s
    Gate 1  Kompilieren         2 s ohne C++-Aenderung, Minuten mit
    Gate 2  Unit-Tests          Minuten (startet den Unreal-Editor)
    Gate 3  Rauchtest           Minuten (mehrere Editor-Sitzungen)

Ein Hook, der vor JEDEM Commit eine Viertelstunde braucht, wird binnen eines
Tages mit --no-verify umgangen; dann prueft er gar nichts mehr. Darum:

* **schnell** laeuft vor jedem Commit. Gate 1 nur, wenn wirklich C++ dabei
  ist - wer nur ein Python-Werkzeug aendert, wartet nicht auf einen Compiler.
* **voll** laeuft vor dem PUSH. Dort ist die Wartezeit vertretbar, und nichts
  verlaesst den Rechner ungeprueft. Die Blockade wandert damit vom
  Paketieren an die Stelle, an der sie noch billig ist.

Notausgang: `git commit --no-verify` oder `WB_KEINE_GATES=1`. Er ist
absichtlich da - ein Wachposten ohne Tuer wird eingerissen, nicht benutzt.
"""
import argparse
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

WURZEL = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.path.join(WURZEL, "Tools")

# Endungen, die einen Kompilierlauf noetig machen. Alles andere kann den
# Compiler nicht kaputt machen und soll ihn darum nicht kosten.
CPP_ENDUNGEN = (".cpp", ".h", ".cs", ".inl")


def saubere_umgebung():
    """Umgebung OHNE die GIT_*-Variablen des laufenden Hooks.

    GEMESSEN am 21.09.2026, und es war knapp: git setzt fuer
    `git commit --only` ein TEMPORAERES GIT_INDEX_FILE und vererbt es an
    jeden Unterprozess. Die Testsuiten legen Wegwerf-Repos an und rufen dort
    `git add -A` - das schrieb prompt in den Index des laufenden Commits.
    Ergebnis: drei Suiten fielen um, und eine Wegwerfdatei stand im Index des
    echten Commits. Waeren die Gates gruen gewesen, waere sie mitgekommen.

    Ein Hook darf nicht in den Commit hineinwirken, den er pruefen soll.
    """
    umgebung = dict(os.environ)
    for name in [k for k in umgebung if k.startswith("GIT_")]:
        del umgebung[name]
    return umgebung


def gestagte_dateien():
    """Was git fuer diesen Commit vorgemerkt hat."""
    roh = subprocess.run(["git", "diff", "--cached", "--name-only", "-z"],
                         cwd=WURZEL, capture_output=True)
    return [t.decode("utf-8", "surrogateescape")
            for t in roh.stdout.split(b"\0") if t]


def geaenderte_dateien():
    """Alles, was im Baum anders ist als HEAD - fuer Laeufe ausserhalb eines Hooks."""
    roh = subprocess.run(["git", "diff", "HEAD", "--name-only", "-z"],
                         cwd=WURZEL, capture_output=True)
    return [t.decode("utf-8", "surrogateescape")
            for t in roh.stdout.split(b"\0") if t]


def braucht_compiler(dateien):
    return any(d.lower().endswith(CPP_ENDUNGEN) for d in dateien)


class Lauf:
    """Ein Gate mit seiner gemessenen Dauer - Zahlen statt Eindruecke."""

    def __init__(self):
        self.ergebnisse = []

    def fahre(self, name, befehl, *, shell_cmd=False):
        print("  ... %s" % name, flush=True)
        start = time.time()
        if shell_cmd:
            fertig = subprocess.run(["cmd", "/c", befehl], cwd=WURZEL,
                                    capture_output=True, text=True,
                                    encoding="utf-8", errors="replace",
                                    env=saubere_umgebung())
        else:
            fertig = subprocess.run(befehl, cwd=WURZEL, capture_output=True,
                                    text=True, encoding="utf-8", errors="replace",
                                    env=saubere_umgebung())
        dauer = time.time() - start
        ok = fertig.returncode == 0
        self.ergebnisse.append((name, ok, dauer, fertig))
        print("      %s  %.0f s" % ("gruen" if ok else "ROT  ", dauer), flush=True)
        return ok

    def ueberspringe(self, name, grund):
        self.ergebnisse.append((name, None, 0.0, None))
        print("  ... %s\n      uebersprungen: %s" % (name, grund), flush=True)

    def bericht(self):
        rot = [e for e in self.ergebnisse if e[1] is False]
        gesamt = sum(e[2] for e in self.ergebnisse)
        print("\n  %d Gate(s) in %.0f s." % (len(self.ergebnisse), gesamt))
        for name, ok, _, fertig in rot:
            print("\nROT: %s" % name)
            text = ((fertig.stdout or "") + (fertig.stderr or "")).strip().splitlines()
            for zeile in text[-15:]:
                print("     " + zeile[:140])
        return len(rot)


def gates_fahren(stufe, dateien):
    lauf = Lauf()
    print("Gates vor dem Commit (Stufe: %s)" % stufe)

    lauf.fahre("Gate 0  Engine-Pfade",
               [sys.executable, os.path.join(TOOLS, "pruefe_engine.py")])
    lauf.fahre("Python-Suiten",
               [sys.executable, "-m", "unittest", "discover",
                "-s", "Tools", "-p", "test_*.py"])

    if braucht_compiler(dateien):
        lauf.fahre("Gate 1  Kompilieren", r"Tools\build_gate1.cmd", shell_cmd=True)
    else:
        lauf.ueberspringe("Gate 1  Kompilieren", "keine C++-Datei betroffen")

    if stufe == "voll":
        lauf.fahre("Gate 2+3  Tests und Rauchtest",
                   r"Tools\build_release.cmd -GatesOnly", shell_cmd=True)
    else:
        lauf.ueberspringe("Gate 2+3  Tests und Rauchtest",
                          "Stufe schnell - sie laufen vor dem Push")

    return lauf.bericht()


def hauptprogramm(argv=None):
    p = argparse.ArgumentParser(description="Release-Gates vor dem Commit fahren.")
    p.add_argument("--stufe", choices=("schnell", "voll"), default="schnell")
    p.add_argument("--gestaged", action="store_true",
                   help="nur vorgemerkte Dateien betrachten (fuer den Hook)")
    a = p.parse_args(argv)

    if os.environ.get("WB_KEINE_GATES") == "1":
        print("WB_KEINE_GATES=1 - Gates uebersprungen.")
        return 0

    dateien = gestagte_dateien() if a.gestaged else geaenderte_dateien()
    if a.gestaged and not dateien:
        print("Nichts vorgemerkt - nichts zu pruefen.")
        return 0

    rot = gates_fahren(a.stufe, dateien)
    if rot:
        print("\n%d Gate(s) ROT - der Commit wird abgewiesen." % rot)
        print("Wenn das so gewollt ist: git commit --no-verify")
        return 1
    print("Alle Gates gruen.")
    return 0


if __name__ == "__main__":
    sys.exit(hauptprogramm())
