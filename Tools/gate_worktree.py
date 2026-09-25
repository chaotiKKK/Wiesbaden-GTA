r"""Das volle Gate beim Push in einem SAUBEREN Worktree - es prueft nur die Commits.

    python Tools/vor_dem_commit.py --stufe voll --push-refs   # so ruft der pre-push-Hook
    python Tools/gate_worktree.py <commit>                      # von Hand, ein Commit

WOFUER: Die volle Stufe lief im Arbeitsbaum - und dort liegt fast immer
fremde, laufende Arbeit (andere Agenten, offene Editoren). Zwei Folgen:

1. Das Gate pruefte nicht, was hinausgeht, sondern Commits PLUS fremde
   Baustellen: ein halbfertiger fremder Test liess den Push fallen, ein
   fremdes Loch fuellte eine Luecke im eigenen Commit.
2. Gate 1 und der Rauchtest beendeten JEDEN laufenden Unreal-Editor. Also
   wartete der Push-Waechter, bis der Baum sauber und kein Editor offen war -
   am 25.09.2026 ueber drei Stunden mit 79 fremden Aenderungen, ohne Aussicht.

Jetzt checkt das Gate den zu pushenden Commit in einen eigenen, dauerhaften
Worktree aus (Vorgabe: <Sicherung>\.gate-worktree\WiesbadenReal, eigene
Binaries/Intermediate - nach dem ersten Lauf baut er inkrementell) und
faehrt dort die volle Stufe. Fremde Aenderungen sind dort nicht, fremde
Editoren gehen ihn nichts an (Gate 1 und Rauchtest beenden nur Editoren
DIESES Projektordners).

WAS NICHT IN GIT STEHT, ABER GEBRAUCHT WIRD - die gebackene Stadt: die
World-Partition-Aktoren (26 GB), gebackene Chunks, Bake-Materialien, Rohdaten
und die Stadtkarten selbst (die Vorgabekarte WiesbadenCity_Alkis22 ist nicht
versioniert). Sie werden aus dem Hauptordner VERLINKT, nicht kopiert:

* ignorierte VERZEICHNISSE unter Content/ und Data/Raw/ -> Verzeichnis-
  Verbindung (mklink /J);
* unversionierte Stadtkarten Content/Maps/WiesbadenCity_*.umap -> Hardlink.

Einzelne ignorierte oder unversionierte DATEIEN sonst (etwa ein fremdes
SK_Sylvia.uasset oder __StadtNeubau-Kratzkarten) kommen NICHT mit - das waere
wieder fremde Arbeit im Gate.
"""
import fnmatch
import os
import subprocess
import sys
from pathlib import Path

NULL_SHA = "0" * 40
# Unversionierte Karten, die zur Stadt gehoeren (Bake-Ergebnisse, kein WIP).
STADTKARTEN = "Content/Maps/WiesbadenCity_*.umap"
# Unter diesen Wurzeln werden ignorierte Verzeichnisse verlinkt.
STADT_WURZELN = ("Content/", "Data/Raw/")


def saubere_umgebung():
    """Ohne GIT_* des laufenden Hooks (siehe vor_dem_commit.saubere_umgebung)."""
    umgebung = dict(os.environ)
    for name in [k for k in umgebung if k.startswith("GIT_")]:
        del umgebung[name]
    return umgebung


def git(cwd, *args, pruefen=True):
    fertig = subprocess.run(["git", *args], cwd=str(cwd), capture_output=True, text=True,
                            encoding="utf-8", errors="replace", env=saubere_umgebung())
    if pruefen and fertig.returncode != 0:
        raise RuntimeError("git %s: %s" % (" ".join(args), (fertig.stderr or fertig.stdout).strip()))
    return fertig.stdout


def push_shas(stdin_text):
    """Die lokalen Commits aus der pre-push-Eingabe ("<ref> <sha> <ref> <sha>" je Zeile).

    Loeschungen (lokaler Sha nur Nullen) pruefen nichts; jeder Commit nur einmal.
    """
    shas = []
    for zeile in stdin_text.splitlines():
        teile = zeile.split()
        if len(teile) < 4 or teile[1] == NULL_SHA:
            continue
        if teile[1] not in shas:
            shas.append(teile[1])
    return shas


def je_baum_einer(shas, baum_von):
    """Ein Commit je Dateibaum: die PR-Kette auf main traegt dieselben Baeume wie
    der Zweig - zweimal dasselbe zu pruefen kostete eine halbe Stunde fuer nichts."""
    gesehen, auswahl = set(), []
    for sha in shas:
        baum = baum_von(sha)
        if baum not in gesehen:
            gesehen.add(baum)
            auswahl.append(sha)
    return auswahl


def gate_projekt(projekt):
    """Projektordner im Gate-Worktree (WB_GATE_WORKTREE = anderer Stammordner).

    Das Layout <Stamm>\\<Projektname> ist Absicht: build_release.ps1 und
    smoke_test.ps1 rechnen mit -Root = Ordner UEBER dem Projekt.
    """
    projekt = Path(projekt)
    stamm = os.environ.get("WB_GATE_WORKTREE")
    stamm = Path(stamm) if stamm else projekt.parent / ".gate-worktree"
    return stamm / projekt.name


def waehle_stadtinhalt(ignoriert, unversioniert):
    """Was aus dem Hauptordner verlinkt wird (datenrein, getestet).

    ignoriert:     `git ls-files -o -i --exclude-standard --directory` (Verzeichnisse mit '/')
    unversioniert: `git ls-files -o --exclude-standard`
    Rueckgabe (Verzeichnisse, Dateien) - jeweils relativ, ohne Schraegstrich am Ende.
    """
    verzeichnisse = sorted(
        e.rstrip("/") for e in ignoriert
        if e.endswith("/") and e.startswith(STADT_WURZELN) and e not in STADT_WURZELN)
    # Nur die oberste Ebene: steckt ein Eintrag in einem anderen, genuegt der aeussere.
    verzeichnisse = [v for v in verzeichnisse
                     if not any(v != a and v.startswith(a + "/") for a in verzeichnisse)]
    dateien = sorted(e for e in unversioniert if fnmatch.fnmatch(e, STADTKARTEN))
    return verzeichnisse, dateien


def stadtinhalt(projekt):
    ignoriert = git(projekt, "ls-files", "-o", "-i", "--exclude-standard", "--directory",
                    "--", "Content", "Data/Raw").splitlines()
    unversioniert = git(projekt, "ls-files", "-o", "--exclude-standard", "--", "Content/Maps").splitlines()
    return waehle_stadtinhalt(ignoriert, unversioniert)


def _verbindung(ziel, quelle):
    """Verzeichnis-Verbindung (Windows, ohne Adminrechte)."""
    fertig = subprocess.run(["cmd", "/c", "mklink", "/J", str(ziel), str(quelle)],
                            capture_output=True, text=True, encoding="utf-8", errors="replace")
    if fertig.returncode != 0:
        raise RuntimeError("mklink /J %s: %s" % (ziel, (fertig.stderr or fertig.stdout).strip()))


def verlinken(projekt, wt, verzeichnisse, dateien):
    """Stadtinhalt in den Worktree haengen; Vorhandenes bleibt (zweiter Lauf = nichts zu tun)."""
    neu = 0
    for rel in verzeichnisse:
        quelle, ziel = Path(projekt) / rel, Path(wt) / rel
        if os.path.lexists(ziel):
            continue
        ziel.parent.mkdir(parents=True, exist_ok=True)
        _verbindung(ziel, quelle)
        neu += 1
    for rel in dateien:
        quelle, ziel = Path(projekt) / rel, Path(wt) / rel
        if os.path.lexists(ziel):
            continue
        ziel.parent.mkdir(parents=True, exist_ok=True)
        os.link(quelle, ziel)   # Hardlink: gleicher Datentraeger, kein Kopieren von GB
        neu += 1
    return neu


def vorbereiten(projekt, sha):
    """Worktree auf genau diesen Commit bringen; Rueckgabe: sein Projektordner."""
    projekt = Path(projekt)
    wt = gate_projekt(projekt)
    if not (wt / ".git").exists():
        wt.parent.mkdir(parents=True, exist_ok=True)
        git(projekt, "worktree", "prune")
        git(projekt, "worktree", "add", "--detach", str(wt), sha)
        print("Gate-Worktree angelegt: %s" % wt, flush=True)
    else:
        git(wt, "checkout", "--detach", "--force", sha)
        # Reste frueherer Laeufe weg; ignorierte Pfade (Build, Verlinktes) und
        # die verlinkten Stadtkarten bleiben.
        git(wt, "clean", "-fd", "-e", STADTKARTEN)
    kopf = git(wt, "rev-parse", "HEAD").strip()
    if kopf != git(projekt, "rev-parse", sha).strip():
        raise RuntimeError("Gate-Worktree steht auf %s statt %s" % (kopf, sha))
    verzeichnisse, dateien = stadtinhalt(projekt)
    neu = verlinken(projekt, wt, verzeichnisse, dateien)
    rest = [z for z in git(wt, "status", "--porcelain").splitlines()
            if not fnmatch.fnmatch(z[3:], STADTKARTEN)]
    if rest:
        raise RuntimeError("Gate-Worktree nicht sauber: %s" % "; ".join(rest[:5]))
    print("Gate-Worktree %s auf %s (%d Stadtinhalte verlinkt, %d neu)."
          % (wt, sha[:10], len(verzeichnisse) + len(dateien), neu), flush=True)
    return wt


def pruefen(projekt, sha):
    """Volle Stufe im Worktree fahren - mit SEINER Fassung der Gates (der des Commits)."""
    wt = vorbereiten(projekt, sha)
    fertig = subprocess.run([sys.executable, str(wt / "Tools" / "vor_dem_commit.py"), "--stufe", "voll"],
                            cwd=str(wt), env=saubere_umgebung())
    return fertig.returncode


def push_pruefen(projekt, stdin_text):
    """Einstieg fuer den pre-push-Hook: jeden zu pushenden Dateibaum einmal pruefen."""
    shas = push_shas(stdin_text)
    if not shas:
        print("Nur Loeschungen im Push - nichts zu pruefen.")
        return 0
    auswahl = je_baum_einer(shas, lambda s: git(projekt, "rev-parse", s + "^{tree}").strip())
    for sha in auswahl:
        print("Volles Gate im sauberen Worktree fuer %s ..." % sha[:10], flush=True)
        rot = pruefen(projekt, sha)
        if rot:
            print("\nGate ROT fuer %s - der Push wird abgewiesen." % sha[:10])
            print("Wenn das so gewollt ist: git push --no-verify")
            return 1
    print("Gate gruen fuer %d Commit(s) im sauberen Worktree." % len(auswahl))
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Aufruf: python Tools/gate_worktree.py <commit>")
    sys.exit(pruefen(Path(__file__).resolve().parent.parent, sys.argv[1]))
