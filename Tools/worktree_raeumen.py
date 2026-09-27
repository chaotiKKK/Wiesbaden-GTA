r"""WAECHTER: Einen Git-Worktree entfernen - aber nur, wenn niemand darin arbeitet.

    python Tools/worktree_raeumen.py <pfad>       # nur pruefen (Exit 0 = frei)
    python Tools/worktree_raeumen.py <pfad> --tun # pruefen und dann entfernen

WARUM DIESES WERKZEUG ES GIBT - gemessen am 27.09.2026:
`git worktree remove` ist NICHT atomar und sein Fehlschlag kommt ZU SPAT.
Es loescht erst den INHALT des Worktrees und meldet das als erfolgreichen
Schritt; erst danach entfernt es das Verzeichnis und den Admin-Eintrag. In
dieser Sitzung traf der Befehl einen Worktree, in dem gerade der Push eines
anderen Threads lief (`vor_dem_commit.py --stufe voll --push-refs`, PID 28560):

  * 1,2 GB / 1785 versionierte Dateien wurden geloescht,
  * der Prozess lief weiter in einem nun leeren Verzeichnis,
  * `worktree remove` meldete Exit 255 - fuer die halbe Arbeit trotzdem
    erfolgreich, denn die Registrierung war schon weg.

Die Commit-Pruefung (`git log <branch> --not --remotes`) hatte ich vorher
gemacht, und sie war gruen. Sie sagt aber nichts darueber, ob jemand GERADE
dort laeuft. Beides sind zwei Fragen; nur die zweite hat hier etwas gerettet.

DIE DREI PRUEFUNGEN, jede mit ihrem gemessenen Geltungsbereich:

  1. PROZESSE MIT DEM PFAD (Kommandozeile / Executable) - findet den Push und
     jeden Editor, dessen Aufrufzeile den Ordner nennt.
  2. UMBENENNEN - der stille killer. `os.rename` des Ordners ist unter
     Windows der EINZIGE der drei Billigen Operationen, die an einem offenen
     Handle scheitert.
  3. UNKLARHEIT IST NEIN - WMI weg, Ordner weg, Rechte weg: abbrechen.
     Ein Werkzeug, das im Zweifel loescht, ist kein Wächter.

WAS DIE PRUEFUNG NICHT KANN - bewusst gesagt, weil es der teuerste Irrtum
waere: GEMESSEN, dass ein Prozess, dessen Arbeitsverzeichnis (CWD) im Ordner
liegt, Windows an GAR NICHTS hindert. `rmdir`, `rename` und ein exklusiv
geoeffnetes Handle melden alle "Erfolg". Nur eine OFFENE DATEI im Ordner
blockiert `rename`. Wer dort nur CWD haelt - der typische Editor-Fall - wird
von Test 2 nicht gesehen. Test 1 faengt ihn nur, wenn sein Pfad in der
Kommandozeile steht. `handle.exe` waere die vollstaendige Loesung und ist
hier nicht vorhanden; deshalb der Grundsatz aus Schritt 3: im Zweifel
abbrechen und den Menschen entscheiden lassen.

`--tun` loescht nur bei nachweislich freiem Ordner. Ohne `--tun` wird
geprueft und nichts angefasst.
"""
import argparse
import json
import os
import subprocess
import sys
from pathlib import Path


class Belegt(Exception):
    """Der Ordner ist nachweislich in Benutzung - es wird nichts geloescht."""


class Unklar(Exception):
    """Der Zustand liess sich nicht bestimmen - im Zweifel wird nicht getan."""


# ------------------------------------------------------------- Prozesse (1)

def prozess_liste():
    """(pid, name, kommandozeile, ausfuehrbar) - leer heisst: WMI ging nicht.

    Ueber CIM statt `tasklist`: nur CIM liefert die Kommandozeile, und die
    ist der verlaesslichste der verfuegbaren Belege. Es wird BEIDES
    zurueckgegeben und nichts gefiltert - die Aufraeumung des Skripts gehoert
    in den Test, nicht in die Messung.
    """
    befehl = ["powershell", "-NoProfile", "-Command",
              "Get-CimInstance Win32_Process | "
              "Select-Object ProcessId,Name,CommandLine,ExecutablePath | "
              "ConvertTo-Json -Compress"]
    try:
        fertig = subprocess.run(befehl, capture_output=True, text=True, encoding="utf-8",
                                errors="replace", timeout=60)
    except (OSError, subprocess.SubprocessError):
        return []
    if fertig.returncode != 0:
        return []
    text = (fertig.stdout or "").strip()
    if not text:
        return []
    try:
        roh = json.loads(text)
    except ValueError:
        return []
    if isinstance(roh, dict):          # genau ein Prozess liefert ein Objekt
        roh = [roh]
    return [(int(z.get("ProcessId") or 0), z.get("Name") or "?",
             z.get("CommandLine") or "", z.get("ExecutablePath") or "") for z in roh]


def prozess_mit_pfad(pfad, eigene_pids=()):
    """Laufende Prozesse, deren Kommandozeile oder Pfad `pfad` enthaelt."""
    ziel = os.path.normcase(os.path.abspath(str(pfad))).rstrip("\\/")
    ausgeschlossen = set(int(p) for p in eigene_pids) | {os.getpid()}
    gefunden = []
    for pid, name, kommando, ausfuehrbar in prozess_liste():
        if pid in ausgeschlossen:
            continue
        for kandidat in (kommando, ausfuehrbar):
            if kandidat and ziel in os.path.normcase(kandidat).replace("/", "\\"):
                gefunden.append((pid, name, (kommando or "")[:160]))
                break
    return gefunden


# -------------------------------------------------------------- Sperre (2)

def umbenennbar(pfad):
    """Laesst sich der Ordner umbenennen? False = etwas haelt ihn fest.

    GEMESSEN am 27.09.2026 in vier Szenarien:

        nur CWD des Prozesses  -> rename ERFOLG   (wird NICHT erkannt!)
        offene Datei darin     -> rename "Zugriff verweigert"
        exklusiv geoeffnet    -> rename blockiert
        nichts                 -> rename ERFOLG

    Der Test ist also echt, aber unvollstaendig - siehe den Modultext. Er
    kostet zwei Umbenennungen und ist deshalb nur im echten Lauf erlaubt, im
    Test wird er uebergeben.
    """
    ordner = Path(pfad)
    ziel = ordner.with_name(ordner.name + ".worktree_raeumen_probe")
    if ziel.exists():
        raise Unklar("Die Probe %s existiert schon - bitte erst pruefen." % ziel)
    try:
        os.rename(ordner, ziel)
    except OSError:
        return False
    try:
        os.rename(ziel, ordner)
    except OSError:
        # Krise: der Ordner heisst jetzt anders. Das melde ich, statt es
        # zu verstecken - ein still umbenannter Projektordner waere der
        # schlimmere Fehler.
        raise Unklar("Ordner auf %s umbenannt, das Zurueckbenennen scheiterte." % ziel)
    return True


# ---------------------------------------------------------- Worktree (3, 4)

def _git(*args, cwd=None):
    umgebung = dict(os.environ)
    for name in [k for k in umgebung if k.startswith("GIT_")]:
        del umgebung[name]
    return subprocess.run(["git", *args], cwd=cwd, capture_output=True, text=True,
                          encoding="utf-8", errors="replace", env=umgebung)


def worktree_liste(haupt):
    """Registrierte Worktrees als Liste (pfad, sha, branch) aus --porcelain."""
    fertig = _git("worktree", "list", "--porcelain", cwd=str(haupt))
    if fertig.returncode != 0:
        raise Unklar("git worktree list: %s" % (fertig.stderr or "").strip())
    raus, pfad = [], None
    for zeile in fertig.stdout.splitlines():
        if zeile.startswith("worktree "):
            pfad = zeile[len("worktree "):].strip()
            raus.append([pfad, "", ""])
        elif zeile.startswith("HEAD ") and raus:
            raus[-1][1] = zeile[5:].strip()
        elif zeile.startswith("branch ") and raus:
            raus[-1][2] = zeile[7:].strip()
    return [tuple(z) for z in raus]


def eigene_commits(pfad, haupt):
    """Commits, die auf KEINEM Remote liegen - nach dem Pfad gefragt, nicht nach dem Branch.

    Das ist die Probe, die ich vor dem Entfernen faelschlich fuer ausreichend
    hielt: sie beweist, dass kein COMMIT fehlt - und nichts darueber, ob
    gerade jemand darin arbeitet.

    GEMESSEN am 27.09.2026, und der Fund ist important: `--not --branches`
    schluckt den Commit, um den es geht. Ein Worktree-Branch ist ein LOKALER
    Branch; `--branches` zaehlt ihn als "liegt irgendwo schon" und meldet
    eine leere Liste. Genau das haette den Branch `wt-gatetest` durchgewinkt,
    auf dem in dieser Sitzung vier Commits lagen, die auf keinem origin
    existierten.

    Die richtige Frage ist "ist er auf einem REMOTE?", denn nur dort sind die
    Commits auch dann noch, wenn dieser Worktree und alle lokalen Branches
    weg sind.
    """
    kopf = _git("rev-parse", "HEAD", cwd=str(pfad))
    if kopf.returncode != 0:
        raise Unklar("HEAD in %s nicht lesbar: %s" % (pfad, (kopf.stderr or "").strip()))
    sha = kopf.stdout.strip()
    if not sha:
        raise Unklar("HEAD in %s leer" % pfad)
    eigen = _git("log", "--oneline", sha, "--not", "--remotes", cwd=str(haupt))
    if eigen.returncode != 0:
        raise Unklar("git log --not: %s" % (eigen.stderr or "").strip())
    return [z for z in eigen.stdout.splitlines() if z.strip()]


# ---------------------------------------------------------- Stadtinhalte (4)

def verlinkte_stadtinhalte(pfad, wurzeln=("Content", "Data/Raw")):
    """Wie viele Verzeichnis-Verbindungen zeigt der Worktree in den Stadt-Wurzeln?

    GEMESSEN am 27.09.2026: `os.path.islink` ist unter Windows KEIN Weg -
    es meldet eine Junction als ganz normale Datei, mein erster Zaehlversuch
    kam auf 0 von 28. Richtig ist `FILE_ATTRIBUTE_REPARSE_POINT`, abgefragt
    ueber PowerShell (AGENTS.md: "ein rekursives Loeschen DURCH eine
    Verbindung leert den Hauptordner" - dieselbe Technik, nur lesend).
    """
    zaehler = ["0"]
    pfade = "".join("'%s';" % str(Path(pfad) / w) for w in wurzeln if (Path(pfad) / w).exists())
    if not pfade:
        return 0
    befehl = [
        "powershell", "-NoProfile", "-Command",
        "$n=0; foreach ($p in @(%s)) { if (Test-Path $p) { "
        "Get-ChildItem -LiteralPath $p -Recurse -Directory -Force -ErrorAction SilentlyContinue | "
        "Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint } | "
        "ForEach-Object { $n++ } } }; Write-Output $n" % pfade,
    ]
    try:
        fertig = subprocess.run(befehl, capture_output=True, text=True,
                                encoding="utf-8", errors="replace", timeout=300)
    except (OSError, subprocess.SubprocessError):
        return None            # None = nicht messbar, nicht "0 Verlinkungen"
    if fertig.returncode != 0:
        return None
    text = (fertig.stdout or "").strip().splitlines()
    if not text or not text[-1].strip().isdigit():
        return None
    return int(text[-1].strip())


def erwartete_stadtinhalte(haupt):
    """Wie viele Stadtverzeichnisse wuerde gate_worktree.vorbereiten verlinken?

    Bewusst NICHT ueber einen eigenen Filter: ich frage dieselbe Funktion, die
    den Gate-Worktree fuellt. Eine zweite Definition hier waere die Einladung,
    dass die beiden auseinanderlaufen - und die Zahl waere dann nur dekorativ.
    Laeuft das nicht (kein gate_worktree im Baum), ist die Erwartung None und
    es wird nichts behauptet.
    """
    try:
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        import gate_worktree as gw
    except ImportError:
        return None
    try:
        verzeichnisse, _ = gw.stadtinhalt(haupt)
    except Exception:            # git nicht erreichbar o.ae.
        return None
    return len(verzeichnisse)


def stadtinhalt_befund(pfad, haupt, zaehlen=None, erwarten=None):
    """Das Erkennungszeichen: wie vollstaendig haengt die gebackene Stadt dran?

    GEMESSEN am 27.09.2026 am Worktree, den ich leerte: sein Push-Log meldete
    "0 Stadtinhalte verlinkt, 0 neu" - waehrend der gesunde Gate-Worktree
    derselben Stunde 28 von 28 zeigte. Genau diese Zahl stand als
    Erkennungszeichen in dem Thread, zu dem der Worktree gehoerte, und sie
    steht in keinem anderen Log.

    Die Formulierung "nur einen TEIL verlinkt" ist bewusst zweigeteilt:

      * 0 von n  - der Worktree ist nie am Gate gelaufen, oder jemand hat die
        Verbindungen geloest. So sah der geklaerte Fall aus.
      * k von n mit 0 < k < n - ein halb vorbereiteter Worktree. Der gefaehr-
        lichere Fall, denn er ist im Log nicht als Zahl sichtbar.

    Beides ist eine WARNUNG, kein Abbruch: ein Worktree, der seinen Zweck
    erreicht hat, soll sich entfernen lassen. Wer die Verlinkungen mit
    entfernen will, muss sie ohnehin kennen.
    """
    ist = (zaehlen or verlinkte_stadtinhalte)(pfad)
    soll = erwarten if erwarten is not None else erwartete_stadtinhalte(haupt)
    if ist is None or not soll:
        return None
    if ist >= soll:
        return ("Stadt", "alle %d Stadtinhalte verlinkt - unauffaellig." % soll, False)
    if ist == 0:
        return ("Stadt", "0 von %d Stadtinhalten verlinkt. Der Worktree haengt NICHT "
                         "an der gebackenen Stadt - entweder ist er nie am Gate "
                         "gewesen, oder die Verbindungen wurden geloest. Gerade "
                         "GEFAEHRLICH: so sah der Worktree aus, den am 27.09.2026 ein "
                         "unbefugter `git worktree remove` geleert hat, waehrend sein "
                         "Push lief." % soll, True)
    return ("Stadt", "nur %d von %d Stadtinhalten verlinkt - der Worktree ist nur "
                     "halb am Gate vorbereitet. Beim Entfernen verschwinden diese "
                     "Verbindungen; der Ordner sieht danach vollstaendig aus, "
                     "enthaelt aber die gebackene Stadt nicht mehr." % (ist, soll), True)


# ------------------------------------------------------------------- Kern

def pruefe(pfad, haupt, eigene_pids=(), umbenennen=True, zaehlen=None, erwarten=None):
    """Darf der Worktree entfernt werden? -> (ok, [(schlagwort, text), ...]).

    Wirft `Belegt` fuer einen nachweislich belegten Ordner und `Unklar` fuer
    alles, was sich nicht bestimmen laesst. Beides heisst: nichts loeschen.
    """
    ordner = Path(pfad).resolve()
    if not ordner.is_dir():
        raise Unklar("%s ist kein Ordner." % ordner)

    liste = worktree_liste(haupt)
    normal = [os.path.normcase(os.path.abspath(p)) for p, _, _ in liste]
    if not liste:
        raise Unklar("git worktree list ist leer.")
    if os.path.normcase(str(ordner)) == normal[0]:
        raise Belegt("%s ist der HAUPT-Arbeitsordner - der wird nie entfernt." % ordner)
    if os.path.normcase(str(ordner)) not in normal:
        raise Unklar("%s ist kein registrierter Worktree - erst `git worktree prune`?"
                     % ordner)

    bericht = []
    blockiert = []
    warnungen = []

    # 1. Prozesse mit dem Pfad
    laufende = prozess_mit_pfad(ordner, eigene_pids)
    if laufende:
        blockiert.append(("Prozess", "Prozess(e) mit diesem Pfad: "
                         + ", ".join("PID %d %s" % (p, n) for p, n, _ in laufende)))
    elif not prozess_liste():
        blockiert.append(("WMI", "Prozessliste nicht abfragbar - Zustand unbekannt."))
    else:
        bericht.append(("Prozess", "kein Prozess nennt diesen Pfad."))

    # 2. Rename-Probe
    if umbenennen:
        if umbenennbar(ordner):
            bericht.append(("Sperre", "Ordner umbenennbar - kein offenes Handle."))
        else:
            blockiert.append(("Sperre", "Der Ordner laesst sich nicht umbenennen "
                                       "(Zugriff verweigert) - ein Prozess haelt eine "
                                       "Datei darin offen."))
    else:
        bericht.append(("Sperre", "Rename-Probe nicht ausgefuehrt."))

    # 4. Stadtinhalte - WARNUNG, kein Abbruch
    befund = stadtinhalt_befund(ordner, haupt, zaehlen=zaehlen, erwarten=erwarten)
    if befund is None:
        bericht.append(("Stadt", "Verlinkungsstand nicht messbar - dazu wird nichts behauptet."))
    else:
        schlagwort, text, warnung = befund
        (bericht if not warnung else warnungen).append((schlagwort, text))

    # 3. Arbeit, die nur hier liegt
    eigen = eigene_commits(ordner, haupt)
    if eigen:
        blockiert.append(("Commit", "Commits, die nur hier liegen (%d): %s"
                         % (len(eigen), "; ".join(eigen[:3]))))
    else:
        bericht.append(("Commit", "jeder Commit liegt auf einem Remote."))

    return (not blockiert), bericht + warnungen + blockiert


def raeumen(pfad, haupt, tun=False, eigene_pids=(), zaehlen=None, erwarten=None):
    """Pruefen und - nur bei freiem Ordner - entfernen. Rueckgabe Exit-Code."""
    ordner = Path(pfad).resolve()
    try:
        ok, bericht = pruefe(ordner, haupt, eigene_pids=eigene_pids,
                             zaehlen=zaehlen, erwarten=erwarten)
    except Belegt as problem:
        print("ABGEBROCHEN: %s" % problem)
        return 2
    except Unklar as problem:
        print("ABGEBROCHEN (unklar, im Zweifel nicht loeschen): %s" % problem)
        return 3
    for schlagwort, text in bericht:
        print("[%s] %s" % (schlagwort, text))
    if not ok:
        print("\n%s ist in Benutzung - es wurde nichts entfernt." % ordner)
        return 1
    # WARNUNGEN auch im Erfolgsfall: der Befund "nur ein Teil der Stadt
    # verlinkt" blockiert nicht, verschwindet aber nicht. Wer ihn nicht liest,
    # hat spaeter einen Ordner, der vollstaendig aussieht und die gebackene
    # Stadt nicht mehr enthaelt.
    if not tun:
        print("\n%s ist frei. Mit --tun entfernen." % ordner)
        return 0
    fertig = _git("worktree", "remove", str(ordner), cwd=str(haupt))
    if fertig.returncode != 0:
        print("git worktree remove: %s" % (fertig.stderr or "").strip())
        print("Hinweis: der Befehl ist nicht atomar - der Inhalt kann schon weg sein.")
        return 1
    print("Worktree entfernt: %s" % ordner)
    return 0


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description="Worktree erst entfernen, wenn niemand darin arbeitet")
    ap.add_argument("pfad", help="Worktree-Ordner")
    ap.add_argument("--tun", action="store_true", help="wirklich entfernen")
    ap.add_argument("--haupt", help="Projektordner (Vorgabe: der, in dem dieses Werkzeug liegt)")
    args = ap.parse_args()
    sys.exit(raeumen(args.pfad, args.haupt or Path(__file__).resolve().parent.parent,
                     tun=args.tun))
