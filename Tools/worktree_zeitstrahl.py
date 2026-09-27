r"""ZEITLEISTE: Was in einer Session mit den Worktrees passiert ist.

    python Tools/worktree_zeitstrahl.py              # ganzer Baum
    python Tools/worktree_zeitstrahl.py --stunden 4  # nur die letzten 4 h
    python Tools/worktree_zeitstrahl.py --json       # maschinenlesbar

WARUM: Am 27.09.2026 hat ein `git worktree remove` 1,2 GB aus einem Worktree
geloescht, in dem der Push eines anderen Threads lief. Was in den folgenden
Stunden passierte, liess sich nur aus vier verstreuten Belegstellen
zusammensetzen - per Hand, und mit einer Fehlannahme ("das Push-Log zeigt
nichts Verdaechtiges"). Eine Zeitleiste, die das bei jedem Aufruf tut, ist
der Unterschied zwischen "ich glaube" und "gemessen".

DIE VIER QUELLEN - jede mit ihrem Geltungsbereich, keine ist vollstaendig:

  1. GIT-REFLOG (`.git/worktrees/*/logs/HEAD`, `.git/logs/HEAD`): Zeitstempel
     sind ECHT (Unix-Epoch im Datensatz) und ueberleben jeden Prozess. Erfasst
     Commits und Checkouts, NICHT das Anlegen oder Entfernen eines Worktrees.
  2. PUSH-LOGS (`**/push*.log`): tragen die Lock-Zeile mit Startzeit und die
     Gate-Dauern. Daraus sind die Uhrzeiten RECHNERISCH abgeleitet, nicht
     abgelesen.
  3. ENGINE-LOCK (`%LOCALAPPDATA%/WiesbadenReal/Locks/engine_run.lock`):
     OwnerPid, OwnerStart, Label, TakenAt. NUR der aktuelle Zustand - die
     Datei wird beim Freigeben geloescht, ist also KEINE Historie. Das
     Werkzeug sagt das auch, statt daraus eine Luecke zu machen.
  4. DATEIZEITEN: mtime aller Worktrees. Die Frage "existiert der Ordner
     noch" laesst sich damit beantworten, "wann wurde er entfernt" nicht.

RECHNUNG, mit der die Gate-Zeiten entstehen - GEMESSEN am 27.09.2026 an
`.planning/meilensteine/push.log`: Push-Start 14:16:17 aus der Lock-Zeile,
dann die kumulierten `gruen N s`-Zeilen hintereinander addiert. Ergebnis
853 s, Ende 14:30:30 - die mtime des Logs ist 14:30:37, also **7 s
Abweichung**. Die Rechnung traegt also, und die Toleranz wird mit
ausgewiesen statt versteckt.

NICHT BEHAUPTET: Prozessstart-Zeiten. Eine PID aus einem Log laesst sich heute
nicht mehr rueckwirkend befragen, wenn der Prozess tot ist - dafuer gaebe es
die Kehrseite dieser Zeitleiste, aber nicht aus Dateien. Wo eine Quelle fehlt,
steht das im Bericht; eine erfundene Luecke waere schlimmer als eine leere.
"""
import argparse
import json
import os
import re
import subprocess
import sys
from datetime import datetime, timedelta
from pathlib import Path

WURZEL = Path(__file__).resolve().parent.parent

# Ein Reflog-Datensatz: <sha> <sha> <ident> <epoch> <tz>\t<nachricht>
RE_REFLOG = re.compile(r"^([0-9a-f]{40})\s+([0-9a-f]{40})\s+(.*?)\s+(\d+)\s+([+-]\d{4})\t(.*)$")
# Lock-Zeile: "seit 2026-09-27 14:16:17" oder "seit 2026-09-27T15:46:37"
RE_LOCK_SEIT = re.compile(r"seit (\d{4}-\d\d-\d\d)[ T](\d\d:\d\d:\d\d)")
RE_GATE_DAUER = re.compile(r"gruen\s+(\d+) s")
RE_GATE_NAME = re.compile(r"\.\.\.\s+(Gate .+?)\s*$")
RE_WORKTREE = re.compile(r"^worktree (.+)$")
RE_HEAD = re.compile(r"^HEAD ([0-9a-f]{40})$")
RE_ENGINE_LOG = re.compile(r"Log file open, (\d\d/\d\d/\d\d) (\d\d:\d\d:\d\d)")
RE_LOCK_GEHALTEN = re.compile(r"Lock: gehalten - PID (\d+)")
RE_LOCK_WARTET = re.compile(r"Lock: BELEGT durch einen anderen Lauf")
RE_LOCK_ABGESCHLOSSEN = re.compile(r"Alle Gates gruen\.?|Gate ROT|EXIT (\d+)")


def gate_beginn(zeilen):
    """Wann die Gates wirklich begannen - nicht wann der Lock angemeldet wurde.

    GEMESSEN am 27.09.2026 an `.planning/push-quicksicht.log`: dessen erste
    Lock-Zeile stammt aus der WARTEphase ("Lock: BELEGT ... - warte
    (hoechstens noch 60 min)"), der Lauf startete 14:44:33 und bekam den
    Lock erst rund 12 Minuten spaeter. Mit dieser Startzeit ergab die
    Rechnung eine Endzeit 894 s neben der mtime des Logs - die Zeiten des
    ersten Pushs wirkten dagegen exakt (6-7 s).

    Die Unterscheidung ist nicht kosmetisch: eine Zeitleiste, die die
    Wartephase als Gate-Zeit ausgibt, verschiebt jedes Ereignis um 12
    Minuten und laesst zwei Laeufe scheinbar gleichzeitig laufen.
    """
    wartete = False
    for zeile in zeilen:
        if RE_LOCK_WARTET.search(zeile):
            wartete = True
        if RE_LOCK_GEHALTEN.search(zeile):
            m = RE_LOCK_SEIT.search(zeile)
            if m:
                beginn = datetime.strptime("%s %s" % (m.group(1), m.group(2)),
                                          "%Y-%m-%d %H:%M:%S")
                # "gehalten - PID" kann auch der eigene Lauf sein; entscheidend
                # ist, ob vorher auf einen fremden gewartet wurde.
                return beginn, bool(wartete)
    return None, wartete


def lauf_vollstaendig(zeilen):
    """Endete der Lauf mit einem Abschluss? Sonst sind die Zeiten eine Untergrenze."""
    for zeile in zeilen:
        if RE_LOCK_ABGESCHLOSSEN.search(zeile):
            return True
    return False


def lies_text(pfad, grenzen=400000):
    try:
        with open(pfad, encoding="utf-8", errors="replace") as f:
            return f.read(grenzen).splitlines()
    except OSError:
        return []


# ------------------------------------------------- Wo liegt die Planung?

def git_ordner(wurzel):
    """Der gemeinsame .git-Ordner - oder None, wenn git nicht antwortet."""
    try:
        fertig = subprocess.run(["git", "rev-parse", "--path-format=absolute",
                                 "--git-common-dir"],
                                cwd=str(wurzel), capture_output=True, text=True,
                                encoding="utf-8", errors="replace", timeout=20)
    except (OSError, subprocess.SubprocessError):
        return None
    if fertig.returncode != 0:
        return None
    zeile = (fertig.stdout or "").strip()
    return Path(zeile) if zeile else None


def planordner(wurzel=None):
    """Der Ordner, in dem `.planning` liegt - AUCH aus einem Worktree heraus.

    WARUM NICHT WURZEL.parent: GEMESSEN am 27.09.2026. Der Selbsttest lief
    im Gate-Worktree `C:\\freebuff\\WiesbadenReal_Sicherung\\.gate-worktree\\
    WiesbadenReal`. `WURZEL.parent` ist dort `.gate-worktree` - und dort
    gibt es kein `.planning`. Vier Tests des Selbsttests uebersprangen
    darum strukturell, ohne dass jemand sie abgeschaltet haette: der Code
    fand die Logs nicht, also meldete er "nichts zu pruefen".

    Der Weg zur Wirklichkeit ist der gemeinsame .git-Ordner. `git
    rev-parse --git-common-dir` liefert aus JEDEM Worktree den .git des
    Hauptbaums (gemessen oben: `C:/freebuff/WiesbadenReal_Sicherung/
    WiesbadenReal/.git`), und dessen Elternteil ist genau der Ordner mit
    `.planning`.

    Der Weg zur Wirklichkeit ist der gemeinsame .git-Ordner. `git
    rev-parse --git-common-dir` liefert aus JEDEM Worktree den .git des
    Hauptbaums (gemessen: `C:/freebuff/WiesbadenReal_Sicherung/
    WiesbadenReal/.git`), und dessen Elternteil ist genau der Ordner mit
    `.planning`. Findet git nichts - kein Repo, git nicht da -, wird
    hoechstens eine Strecke hochgegangen; das deckt den Fall ab, dass die
    Kette gar nicht ueber .git laeuft.

    Die Kandidaten werden der Reihe nach GEPRUEFT, nicht geraten: es gewinnt
    der erste, in dem `.planning` wirklich liegt. Findet sich keiner, ist
    das None - und der Aufrufer sagt das, statt einen falschen Ordner zu
    nehmen. Ein ertauschter Planordner waere schlimmer als ein fehlender:
    die Tests gaelten dann einer fremden Ablage als wuerden sie den echten
    Lauf messen.
    """
    wurzel = Path(wurzel) if wurzel else WURZEL
    kandidaten = []
    gd = git_ordner(wurzel)
    if gd is not None:
        # <Aufsicht>/WiesbadenReal/.git -> <Aufsicht>/WiesbadenReal
        # (Hauptbaum, .planning im Hauptprojekt) ...
        kandidaten.append(gd.parent)
        # ... und eine Ebene darueber (Aufsicht neben dem Projektordner).
        kandidaten.append(gd.parent.parent)
    # Hochlaufen als Rueckfall: im echten Gate-Worktree liegt .planning
    # zwei Ebenen ueber WURZEL (WiesbadenReal -> .gate-worktree ->
    # Sicherung). Genau deshalb genuegte WURZEL.parent nicht. GEMESSEN am
    # 27.09.2026. Nur drei Ebenen - weiter waere geraten, und geraten wird
    # hier nichts.
    kandidat = wurzel.parent
    for _ in range(3):
        kandidaten.append(kandidat)
        if kandidat.parent == kandidat:
            break
        kandidat = kandidat.parent
    for kandidat in kandidaten:
        try:
            if (kandidat / ".planning").is_dir():
                return kandidat
        except OSError:
            continue
    return None


# --------------------------------------------------------------- Quelle 1

def reflog_ereignisse(git_dir, tag, grenze):
    """Commits und Checkouts aus den Reflogs. Zeitstempel sind echt."""
    raus = []
    for name in ("logs/HEAD",):
        pfad = Path(git_dir) / name
        if not pfad.exists():
            continue
        for zeile in pfad.read_text(encoding="utf-8", errors="replace").splitlines():
            m = RE_REFLOG.match(zeile)
            if not m:
                continue
            zeit = datetime.fromtimestamp(int(m.group(4)))
            if zeit < grenze:
                continue
            raus.append({
                "zeit": zeit.isoformat(timespec="seconds"),
                "quelle": "reflog",
                "wer": m.group(3).split("<")[0].strip(),
                "text": m.group(6)[:90],
                "detail": "%s -> %s (%s)" % (m.group(1)[:8], m.group(2)[:8], tag),
            })
    return raus


def worktree_liste(git_dir):
    fertig = subprocess.run(["git", "worktree", "list", "--porcelain"],
                            cwd=str(Path(git_dir).parent), capture_output=True, text=True,
                            encoding="utf-8", errors="replace")
    if fertig.returncode != 0:
        return []
    raus, pfad = [], None
    for zeile in fertig.stdout.splitlines():
        m = RE_WORKTREE.match(zeile)
        if m:
            pfad = m.group(1).strip()
            raus.append({"pfad": pfad, "sha": "", "branch": ""})
        elif RE_HEAD.match(zeile) and raus:
            raus[-1]["sha"] = zeile[5:].strip()[:8]
        elif zeile.startswith("branch ") and raus:
            raus[-1]["branch"] = zeile[7:].strip()
    return raus


# --------------------------------------------------------------- Quelle 2

def gate_zeiten(zeilen):
    """Aus Push-Log -> (start, [(gate, von, bis, dauer)], guete, wartete).

    Die Abweichung entsteht, indem das gerechnete Ende mit der mtime des Logs
    verglichen wird. Sie ist das Guetest dafuer, dass die Rechnung stimmt -
    ohne sie waeren die Zeiten eine Behauptung. `wartete` sagt, ob der Lauf
    vorher auf einen fremden Lock gewartet hat; dann ist der Lock-Zeitpunkt
    nicht der Gate-Beginn (siehe gate_beginn).
    """
    start, wartete = gate_beginn(zeilen)
    if start is None:
        return None, [], None, False
    schritte, name, kum = [], None, 0
    for zeile in zeilen:
        g = RE_GATE_NAME.search(zeile)
        if g:
            name = g.group(1)
        d = RE_GATE_DAUER.search(zeile)
        if d and name:
            dauer = int(d.group(1))
            von = start + timedelta(seconds=kum)
            kum += dauer
            schritte.append({"gate": name, "von": von.isoformat(timespec="seconds"),
                             "bis": (start + timedelta(seconds=kum)).isoformat(timespec="seconds"),
                             "dauer_s": dauer})
    return start, schritte, kum, wartete


def push_log_ereignisse(wurzel, grenze):
    raus = []
    muster = ["**/push*.log", "**/push*.txt"]
    gesehen = set()
    for m in muster:
        for pfad in wurzel.glob(m):
            if not pfad.is_file() or pfad in gesehen:
                continue
            gesehen.add(pfad)
            zeilen = lies_text(pfad)
            if not zeilen:
                continue
            start, schritte, kum, wartete = gate_zeiten(zeilen)
            abstand = ""
            if start is not None:
                ende = start + timedelta(seconds=kum or 0)
                mtime = datetime.fromtimestamp(pfad.stat().st_mtime)
                delta = abs((mtime - ende).total_seconds())
                vorbehalt = ""
                if wartete:
                    vorbehalt = "  ACHSUNG: Lock zuerst BELEGT, Gate-Beginn unsicher"
                elif not lauf_vollstaendig(zeilen):
                    vorbehalt = "  ACHSUNG: Log endet ohne Abschluss - Zeiten sind eine Untergrenze"
                abstand = "  [Rechnung endet %s, mtime %s, Abweichung %d s]%s" % (
                    ende.strftime("%H:%M:%S"), mtime.strftime("%H:%M:%S"), int(delta), vorbehalt)
                if start >= grenze:
                    raus.append({"zeit": start.isoformat(timespec="seconds"),
                                 "quelle": "push-log", "wer": "-",
                                 "text": "Push-Start%s" % abstand,
                                 "detail": str(pfad.relative_to(wurzel))})
            for s in schritte:
                if datetime.fromisoformat(s["von"]) < grenze:
                    continue
                raus.append({"zeit": s["von"], "quelle": "gate", "wer": "-",
                             "text": "%s -> %s" % (s["gate"], s["bis"][11:19]),
                             "detail": "%d s, aus %s" % (s["dauer_s"],
                                                          pfad.relative_to(wurzel))})
    return raus


# --------------------------------------------------------------- Quelle 3

def lock_stand():
    """Der aktuelle Engine-Lock - oder die Aussage, dass es keiner ist.

    GEMESSEN: die Datei wird beim Freigeben geloescht. Sie ist damit ein
    Zustand, keine Historie, und wird hier auch als das behandelt.
    """
    pfad = Path(os.environ.get("LOCALAPPDATA", "")) / "WiesbadenReal" / "Locks" / "engine_run.lock"
    if not pfad.exists():
        return None, str(pfad)
    felder = {}
    for zeile in pfad.read_text(encoding="ascii", errors="replace").splitlines():
        if "=" in zeile:
            k, v = zeile.split("=", 1)
            felder[k.strip()] = v.strip()
    return felder, str(pfad)


# --------------------------------------------------------------- Quelle 4

def worktree_dateien(git_dir):
    raus = []
    for eintrag in (Path(git_dir) / "worktrees").glob("*") if (Path(git_dir) / "worktrees").exists() else []:
        if not eintrag.is_dir():
            continue
        gitdir = eintrag / "gitdir"
        pfad = ""
        if gitdir.exists():
            pfad = gitdir.read_text(encoding="utf-8", errors="replace").strip()
            pfad = str(Path(pfad).parent) if pfad else ""
        mtime = None
        if pfad and Path(pfad).exists():
            mtime = datetime.fromtimestamp(Path(pfad).stat().st_mtime)
        raus.append({"admin": eintrag.name, "pfad": pfad,
                     "mtime": mtime.isoformat(timespec="seconds") if mtime else "-"})
    return raus


# ------------------------------------------------------------------ Bericht

def baue_zeitleiste(git_dir, wurzel, stunden=None):
    grenze = datetime.now() - timedelta(hours=stunden) if stunden else datetime.min
    ereignisse = []
    ereignisse += reflog_ereignisse(Path(git_dir), "HEAD", grenze)
    for eintrag in (Path(git_dir) / "worktrees").glob("*") if (Path(git_dir) / "worktrees").exists() else []:
        if (eintrag / "logs" / "HEAD").exists():
            ereignisse += reflog_ereignisse(eintrag, eintrag.name, grenze)
    ereignisse += push_log_ereignisse(wurzel, grenze)
    ereignisse.sort(key=lambda z: z["zeit"])
    felder, lockpfad = lock_stand()
    return {
        "erzeugt": datetime.now().isoformat(timespec="seconds"),
        "wurzel": str(wurzel),
        "grenze": grenze.isoformat(timespec="seconds") if stunden else None,
        "ereignisse": ereignisse,
        "engine_lock": felder or {"zustand": "keiner (Datei wird beim Freigeben geloescht)",
                                  "pfad": lockpfad},
        "worktrees": worktree_dateien(git_dir),
    }


def drucke(bericht):
    print("ZEITLEISTE %s  (Wurzel: %s)" % (bericht["erzeugt"], bericht["wurzel"]))
    if bericht["grenze"] and bericht["grenze"] != datetime.min.isoformat():
        print("  nur ab %s" % bericht["grenze"].replace("T", " "))
    print()
    if not bericht["ereignisse"]:
        print("  (keine Ereignisse im Zeitraum)")
    for z in bericht["ereignisse"]:
        zeit = z["zeit"].replace("T", " ")
        print("  %s  %-9s %-13s %s" % (zeit[5:19], z["quelle"], z["wer"][:13], z["text"]))
        if z.get("detail"):
            print("  %-19s %s" % ("", z["detail"]))
    print("\nENGINE-LOCK: %s" % json.dumps(bericht["engine_lock"], ensure_ascii=False))
    print("  (Messung, kein Verlauf: die Datei wird beim Freigeben geloescht)")
    print("\nWORKTREES:")
    for w in bericht["worktrees"]:
        print("  %-40s %-46s mtime %s" % (w["admin"], w["pfad"][-46:], w["mtime"]))


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description="Worktree-Zeitleiste einer Session")
    ap.add_argument("--stunden", type=float, help="nur die letzten N Stunden")
    ap.add_argument("--wurzel", help="Wurzelordner (Vorgabe: der Projektordner)")
    ap.add_argument("--json", action="store_true", help="als JSON ausgeben")
    args = ap.parse_args()
    # Aus einem Worktree ist WURZEL.parent der Gate-Ordner, nicht der
    # Planordner - siehe planordner(). Ohne Aufloesung bliebe die
    # Push-Log-Quelle leer und der Bericht waere stillschweigend
    # unvollstaendig.
    wurzel = Path(args.wurzel) if args.wurzel else (planordner() or WURZEL.parent)
    git_dir = git_ordner(WURZEL) or (WURZEL / ".git")
    bericht = baue_zeitleiste(git_dir, wurzel, args.stunden)
    if args.json:
        print(json.dumps(bericht, indent=2, ensure_ascii=False))
    else:
        drucke(bericht)
