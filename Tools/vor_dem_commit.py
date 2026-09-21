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
    Gate 1  Kompilieren         2 s ohne C++-Aenderung, Minuten mit
    Python-Suiten              31 s   (gemessen 21.09.2026, 172 Tests)
    Gate 2  Unit-Tests          Minuten (startet den Unreal-Editor)
    Gate 3  Rauchtest           Minuten (mehrere Editor-Sitzungen)

Ein Hook, der vor JEDEM Commit eine Viertelstunde braucht, wird binnen eines
Tages mit --no-verify umgangen; dann prueft er gar nichts mehr. Darum:

* **schnell** laeuft vor jedem Commit und enthaelt nur, was zur
  Release-Pipeline gehoert: Gate 0 immer, Gate 1 nur, wenn wirklich C++
  dabei ist - wer nur ein Python-Werkzeug aendert, wartet nicht auf einen
  Compiler.
* **voll** laeuft vor dem PUSH. Dort ist die Wartezeit vertretbar, und nichts
  verlaesst den Rechner ungeprueft. Die Blockade wandert damit vom
  Paketieren an die Stelle, an der sie noch billig ist.

DIE PYTHON-SUITEN LIEGEN AUF DER VOLLEN STUFE, und zwar aus zwei Gruenden:

1. Sie waren 31 s von 32 s der schnellen Stufe. Alles andere dort kostet
   zusammen eine Sekunde - der Hook bestand praktisch nur aus ihnen.
2. Sie sind kein Gate der Release-Pipeline. build_release.ps1 faehrt Gate 0
   bis 3 und ruft sie nirgends auf; vor dem Commit standen sie als Zugabe.

Sie sind VERSCHOBEN, NICHT GESTRICHEN: build_release.cmd kennt sie nicht,
darum faehrt die volle Stufe sie selbst. Nichts verlaesst den Rechner, ohne
dass sie gelaufen sind.

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


def gate0_befehl(dateien):
    """Die Befehlszeile fuer Gate 0 - mit den vorgemerkten Dateien.

    WARUM UEBERGEBEN STATT FRAGEN LASSEN: Gate 0 startet mit
    saubere_umgebung(), also OHNE GIT_*. Das muss so bleiben - ein
    Unterprozess wuerde sonst in den Index des laufenden Commits schreiben
    (gemessen am 21.09.2026). Ohne GIT_INDEX_FILE sieht ein eigener
    `git diff --cached` aber den ECHTEN Index, und der ist bei
    `git commit --only` leer - genau der Weg, den ausliefern.py benutzt.
    Neu vorgemerkte Dateien entgingen dem Gate damit vollstaendig.

    gestagte_dateien() liest die Liste absichtlich MIT der Umgebung und ist
    darum richtig. Sie wird hier als Argument weitergereicht: die
    Abdichtung bleibt wirksam, die Liste stimmt trotzdem.
    """
    befehl = [sys.executable, os.path.join(TOOLS, "pruefe_engine.py")]
    if dateien:
        befehl.append("--dateien")
        befehl.extend(dateien)
    return befehl


def gates_fahren(stufe, dateien):
    lauf = Lauf()
    print("Gates vor dem Commit (Stufe: %s)" % stufe)

    lauf.fahre("Gate 0  Engine-Pfade", gate0_befehl(dateien))

    # Die Python-Suiten gehoeren zur vollen Stufe, nicht vor jeden Commit.
    #
    # GEMESSEN am 21.09.2026: sie sind 31 s von 32 s der schnellen Stufe.
    # Alles andere dort kostet zusammen eine Sekunde. Sie sind ausserdem
    # KEIN Gate der Release-Pipeline - build_release.ps1 faehrt Gate 0 bis 3
    # und ruft sie nirgends auf; vor dem Commit standen sie als Zugabe.
    # Darum laufen sie jetzt dort, wo die langsamen Gates schon liegen.
    #
    # Sie laufen weiter, bevor etwas den Rechner verlaesst: der pre-push-Hook
    # faehrt die volle Stufe. Verschoben, nicht gestrichen.
    if stufe == "voll":
        lauf.fahre("Python-Suiten",
                   [sys.executable, "-m", "unittest", "discover",
                    "-s", "Tools", "-p", "test_*.py"])
    else:
        lauf.ueberspringe("Python-Suiten",
                          "Stufe schnell - sie laufen vor dem Push")

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
