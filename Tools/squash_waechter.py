r"""Squash-Falle beim Push melden: ein Branch lebt nach seinem Squash-Merge weiter.

WAS PASSIERT: GitHub fasst einen PR beim "Squash and merge" zu EINEM neuen
Commit auf main zusammen. Die Commits des Branches kommen dabei nicht nach
main. Arbeitet man auf dem Branch weiter und oeffnet einen neuen PR, rechnet
GitHub vom alten gemeinsamen Vorfahren aus: der Squash-Commit auf main und die
Branch-Commits aendern dieselben Dateien auf verschiedenen Wegen. Ergebnis:
der PR zeigt tausende Dateien, die laengst auf main sind, und meldet
Konflikte, die "zu komplex fuer den Web-Editor" sind - obwohl inhaltlich
nichts zu entscheiden ist.

GEMESSEN am 27.09.2026 an PR #16: main hatte genau einen Commit, der dem
Branch fehlte - e9dcd96, der Squash von PR #12. Sein Dateibaum war BITGLEICH
mit b72a30d, dem Kopf von PR #12, und der steckte schon im Branch. Der PR
zeigte 1.333 Dateien und liess sich nicht mergen.

DIE SIGNATUR: ein Commit auf main, der dem Branch fehlt, hat exakt den
Dateibaum eines Commits, der nur im Branch liegt. Dann enthaelt der Branch
alles, was dieser main-Commit bringt, und

    git merge -s ours <squash-commit>

vermerkt ihn verlustfrei - keine Datei aendert sich, nur die Historie. Danach
rechnet GitHub ab dem Squash, die Scheinkonflikte sind weg.

NUR EIN HINWEIS, KEIN GATE: die Falle ist kein Fehler am Commit, und das
Gegenmittel ist eine Entscheidung (ours-Merge, Rebase oder neuer Branch).
Ein Gate, das den Push dafuer verweigert, wird umgangen. Der Waechter faellt
auch nie selbst: jeder Fehler wird zu einer Zeile, nie zu einem Abbruch.

Aufruf von Hand:
    python Tools/squash_waechter.py [<branch-oder-commit>] [<basis>]
"""
import subprocess
import sys
from pathlib import Path

NULL_SHA = "0" * 40
# Mehr als so viele Commits auf main seit dem Abzweig: der Branch ist alt,
# aber das ist eine andere Geschichte - der Waechter bleibt billig.
GRENZE = 2000


def _git(cwd, *args):
    fertig = subprocess.run(["git", *args], cwd=str(cwd), capture_output=True,
                            text=True, encoding="utf-8", errors="replace")
    return fertig.returncode, fertig.stdout


def basis_ref(cwd):
    """Der Hauptzweig auf dem Server: origin/HEAD, sonst origin/main."""
    rc, aus = _git(cwd, "symbolic-ref", "-q", "refs/remotes/origin/HEAD")
    if rc == 0 and aus.strip():
        return aus.strip()
    rc, _ = _git(cwd, "rev-parse", "-q", "--verify", "refs/remotes/origin/main^{commit}")
    return "refs/remotes/origin/main" if rc == 0 else None


def _commits_mit_baum(cwd, bereich):
    rc, aus = _git(cwd, "log", "-n", str(GRENZE), "--format=%H %T %s", bereich)
    if rc != 0:
        return []
    zeilen = []
    for zeile in aus.splitlines():
        teile = zeile.split(" ", 2)
        if len(teile) >= 2:
            zeilen.append((teile[0], teile[1], teile[2] if len(teile) > 2 else ""))
    return zeilen


def befunde(cwd, spitze, basis):
    """Squash-Commits auf `basis`, deren Inhalt `spitze` schon traegt.

    Liefert [(squash_sha, squash_betreff, branch_sha)] - leer, wenn keine
    Falle vorliegt. Verglichen wird der GANZE Dateibaum: nur dann ist der
    ours-Merge garantiert verlustfrei.
    """
    nur_basis = _commits_mit_baum(cwd, "%s..%s" % (spitze, basis))
    if not nur_basis:
        return []
    nur_branch = {baum: sha for sha, baum, _ in _commits_mit_baum(cwd, "%s..%s" % (basis, spitze))}
    return [(sha, betreff, nur_branch[baum])
            for sha, baum, betreff in nur_basis if baum in nur_branch]


def hat_konflikte(cwd, spitze, basis):
    """True, wenn ein Merge von `spitze` in `basis` Konflikte haette (None: unbekannt)."""
    rc, _ = _git(cwd, "merge-tree", "--write-tree", "--quiet", basis, spitze)
    return {0: False, 1: True}.get(rc)


def push_zweige(stdin_text):
    """(Ref, lokaler Sha) je gepushtem Branch aus der pre-push-Eingabe."""
    zweige = []
    for zeile in stdin_text.splitlines():
        teile = zeile.split()
        if len(teile) < 4 or teile[1] == NULL_SHA or not teile[2].startswith("refs/heads/"):
            continue
        zweige.append((teile[2], teile[1]))
    return zweige


def meldung(zweig, funde, konflikt, basis):
    """Die Hinweiszeilen fuer einen Branch - mit Handlung, nicht nur Befund."""
    name = zweig.replace("refs/heads/", "")
    kurz = basis.replace("refs/remotes/", "")
    zeilen = ["%s lebt nach einem Squash-Merge weiter:" % name]
    for sha, betreff, branch_sha in funde:
        zeilen.append("  %s auf %s (\"%s\") hat den Dateibaum von %s aus diesem Branch"
                      % (sha[:9], kurz, betreff[:60], branch_sha[:9]))
    if konflikt:
        zeilen.append("Ein PR dieses Branches zeigt dadurch SCHEINKONFLIKTE und laesst sich nicht mergen.")
    else:
        zeilen.append("Ein PR dieses Branches zeigt dadurch Dateien, die laengst auf %s sind." % kurz)
    zeilen.append("Verlustfrei vermerken (aendert keine Datei, nur die Historie):")
    for sha, _, _ in funde:
        zeilen.append("  git merge -s ours %s" % sha[:9])
    zeilen.append("Ist %s danach weitergelaufen: anschliessend normal `git merge %s`." % (kurz, kurz))
    return zeilen


def pruefen(cwd, stdin_text):
    """Alle Hinweiszeilen fuer einen Push - leer, wenn alles in Ordnung ist."""
    basis = basis_ref(cwd)
    if not basis:
        return []
    zeilen = []
    for zweig, sha in push_zweige(stdin_text):
        if zweig == basis.replace("refs/remotes/origin/", "refs/heads/"):
            continue  # der Hauptzweig selbst kann nicht hinter seinem Squash liegen
        funde = befunde(cwd, sha, basis)
        if funde:
            zeilen += meldung(zweig, funde, hat_konflikte(cwd, sha, basis), basis)
    return zeilen


def hinweis(cwd, stdin_text):
    """Einstieg fuer das Push-Gate: drucken, nie werfen, nie blockieren."""
    try:
        zeilen = pruefen(cwd, stdin_text)
    except Exception as e:  # ein Waechter darf den Push nie verhindern
        zeilen = ["Pruefung nicht ausgefuehrt (%s)" % e]
    if zeilen:
        print("  ... Squash-Falle (Hinweis, kein Gate)", flush=True)
        for zeile in zeilen:
            print("      %s" % zeile[:200], flush=True)
    return zeilen


if __name__ == "__main__":
    wurzel = Path(__file__).resolve().parent.parent
    spitze = sys.argv[1] if len(sys.argv) > 1 else "HEAD"
    basis = sys.argv[2] if len(sys.argv) > 2 else basis_ref(wurzel)
    if not basis:
        raise SystemExit("Kein origin/main - nichts zu vergleichen.")
    funde = befunde(wurzel, spitze, basis)
    if not funde:
        print("Keine Squash-Falle: %s traegt keinen Squash-Commit von %s doppelt." % (spitze, basis))
        sys.exit(0)
    for zeile in meldung(spitze, funde, hat_konflikte(wurzel, spitze, basis), basis):
        print(zeile)
