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

DER STAMMORDNER HAENGT AM HAUPT-ARBEITSORDNER, nicht am aufrufenden Ordner.
GEMESSEN am 27.09.2026: er wurde aus `projekt.parent` gebildet, und in einem
verlinkten Worktree ist das dessen Elternordner. Ein Push von dort legte
`.gate-worktree\.gate-worktree\WiesbadenReal` an - mit 0 verlinkten
Stadtinhalten, ohne .uproject, und Gate 0 wurde nach 11 Minuten Lauf rot.
Die Quelle ist jetzt `git worktree list` (erster Eintrag = Hauptordner); ein
Aufruf aus dem Gate-Worktree selbst wird abgewiesen.

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
import time
from pathlib import Path

def drucke(text, file=None):
    """Print ohne Unicode-Absturz (Gleiche Hilfe wie vor_dem_commit.drucke).

    Gate-Meldungen tragen Unterprozess-Text mit errors="replace" in sich
    (U+FFFD, fremde Schriftzeichen) - eine cp1252-Konsole darf daran
    nicht sterben, sonst stirbt der Bericht statt des Fehlers.
    """
    ziel = file if file is not None else sys.stdout
    try:
        print(text, file=ziel, flush=True)
    except UnicodeEncodeError:
        fehler = getattr(ziel, "errors", None) or "strict"
        if fehler != "strict":
            raise
        roh = text.encode(ziel.encoding or "ascii", "replace")
        kanal = getattr(ziel, "buffer", None)
        if kanal is None:
            print(roh.decode(ziel.encoding or "ascii"), file=ziel, flush=True)
            return
        kanal.write(roh + b"\n")
        kanal.flush()


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


def haupt_ordner(projekt):
    """Der HAUPT-Arbeitsordner des Repos, von wo immer aufgerufen wird.

    GEMESSEN am 27.09.2026: der Stammordner wurde aus `projekt.parent`
    gebildet, und in einem verlinkten Worktree ist das dessen Elternordner -
    nicht der des Hauptbaums. Ein Push aus einem Worktree legte deshalb
    .gate-worktree\\.gate-worktree\\WiesbadenReal an: 0 verlinkte Stadtinhalte,
    kein .uproject, Gate 0 rot nach 11 Minuten Gate-Lauf.

    Die Quelle ist `git worktree list`: dort stehen die Pfade mit dem
    ERSTEN Eintrag als Haupt-Arbeitsordner, unabhaengig davon, von wo aus
    aufgerufen wird. Bewusst NICHT `rev-parse --git-common-dir`: das zeigt im
    Hauptbaum auf <Projekt>/.git, dessen Elternordner der Projektordner
    selbst ist - der Stammordner landete dann INNEN im Projekt statt neben
    ihm. Der Unterschied um eine Ebene ist hier der ganze Fehler.
    """
    projekt = Path(projekt)
    liste = git(projekt, "worktree", "list", "--porcelain")
    pfade = [z[len("worktree "):].strip() for z in liste.splitlines()
             if z.startswith("worktree ")]
    if not pfade:
        raise RuntimeError("git worktree list lieferte keinen Pfad fuer %s" % projekt)
    return Path(pfade[0]).resolve()


def gate_projekt(projekt):
    """Projektordner im Gate-Worktree (WB_GATE_WORKTREE = anderer Stammordner).

    Das Layout <Stamm>\\<Projektname> ist Absicht: build_release.ps1 und
    smoke_test.ps1 rechnen mit -Root = Ordner UEBER dem Projekt.

    Der Stammordner haengt am HAUPT-Arbeitsordner, nicht am aufrufenden
    Ordner - siehe haupt_ordner() und den Befund vom 27.09.2026.
    """
    projekt = Path(projekt)
    stamm = os.environ.get("WB_GATE_WORKTREE")
    stamm = Path(stamm) if stamm else haupt_ordner(projekt).parent / ".gate-worktree"
    return stamm / haupt_ordner(projekt).name


def ist_im_gate_worktree(projekt):
    """Liegt dieser Ordner SELBST schon unter dem Stammordner?

    Das ist die Vorbedingung, an der das Verschachteln sichtbar wird: ruft
    jemand gate_worktree.py aus dem Gate-Worktree heraus auf, entstuende
    ein zweiter Ordner darunter. Ein gate_projekt()-Vergleich allein genuegt
    NICHT - der ergibt fuer den Haupt-Ordner wieder sich selbst.

    Kann der Pfad nicht ermittelt werden (kein Repo, Liste leer), ist die
    Antwort NEIN: der Wächter darf keinen Push verweigern, weil er selbst
    nicht nachsehen konnte.
    """
    try:
        return Path(projekt).resolve() == Path(gate_projekt(projekt)).resolve()
    except (OSError, RuntimeError):
        return False


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


LOCK_SKRIPT = Path(__file__).resolve().parent / "engine_run_lock.ps1"
LOCK_BELEGT = 3
# Exit 4 des Skripts: die Platte liegt unter der Abbrechgrenze. Das ist kein
# belegter Lock, sondern ein Zustand, der verschwinden kann - GEMESSEN am
# 28.09.2026, 03:36: die Python-Suite eines Push-Laufs kippte bei 10,0 % frei
# auf den ABBRUCH-Weg, motor_sperre wertete das wie einen endgueltigen Fehler
# und der ganze Push starb, obwohl 20 Minuten spaeter alles geheilt war.
LOCK_PLATTE = 4


def motor_sperre(name, warte_s=None, lock_pfad=None, platten_grenze=None,
                 schlaf=time.sleep, uhr=time.monotonic):
    """Den maschinenweiten Engine-Lock fuer DIESEN Prozess nehmen.

    WARUM HIER UND NICHT ERST IN build_release.ps1: build_release nimmt den
    Lock vor SEINEM Gate 0 - aber der Push-Lauf faengt frueher an. Er setzt
    den geteilten Gate-Worktree per `checkout --force` + `clean` auf seinen
    Commit, faehrt Gate 0 und kompiliert (Gate 1), und erst "Gate 2+3" ruft
    build_release auf. Gemessen am 25.09.2026: ein paralleler
    `build_release -GatesOnly` im selben Worktree beendete in seinem Gate 1
    (Stop-ProjectEditors) den Gate-2-Editor des Push-Laufs, bevor dessen Lock
    ueberhaupt griff. Der Lock muss also die ganze Pipeline umschliessen - ab
    dem Checkout.

    Besitzer ist der Aufrufer dieses PowerShell-Kindes, also dieser Python-
    Prozess: der Lock lebt so lange wie der Push-Lauf und stirbt mit ihm
    (keine Freigabepflicht). Die verschachtelten Laeufe (build_release,
    smoke_test, Cleanup) finden ihn in ihrer Prozesskette und gelten als eigen.

    Ist der Lock belegt, wird GEWARTET statt abgewiesen - ein zweiter Push soll
    hinter dem ersten anstehen, nicht rot werden. Dasselbe gilt fuer Exit 4
    (Platte unter der Abbrechgrenze, LOCK_PLATTE): Raeumlaeufe und endende
    Cooks geben Platz frei, der Lauf wartet also auf Heilung, bis zur selben
    Frist. Abfrage alle 15 s, eine Zeile je Minute; nach warte_s (Vorgabe
    WB_GATE_LOCK_WARTEN, sonst 3600 s) gibt der Lauf auf. Rueckgabe True =
    gehalten.

    `platten_grenze` reicht -PlattenGrenze durch (0 = Gate aus). Nur fuer
    Tests da: die Suite soll plattenunabhaengig gruen bleiben, das echte
    Gate die Grenze aber behalten.
    """
    if warte_s is None:
        warte_s = float(os.environ.get("WB_GATE_LOCK_WARTEN", "3600"))
    befehl = ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(LOCK_SKRIPT),
              "-Modus", "Nehmen", "-Name", name]
    if lock_pfad:
        befehl += ["-LockPfad", str(lock_pfad)]
    if platten_grenze is not None:
        befehl += ["-PlattenGrenze", str(platten_grenze)]
    frist = uhr() + warte_s
    naechste_meldung = uhr()
    while True:
        # WarteSekunden 0: das Skript prueft und legt atomar an, gewartet wird
        # hier - sonst schriebe es alle 2 s eine Zeile ins Hook-Protokoll.
        fertig = subprocess.run(befehl, capture_output=True, text=True,
                                encoding="utf-8", errors="replace")
        text = ((fertig.stdout or "") + (fertig.stderr or "")).strip().splitlines()
        if fertig.returncode == 0:
            drucke("Engine-Lock: %s" % (text[-1] if text else "gehalten"))
            return True
        if fertig.returncode == LOCK_PLATTE:
            # Die Platte ist zu voll - kein belegter Lock, sondern ein
            # ZUSTAND. Genau wie bei BELEGT gilt: abwarten, bis zur selben
            # Frist; der naechste Raeumlauf oder ein endender Cook heilt es.
            if uhr() >= frist:
                for zeile in text[:2]:
                    drucke(zeile)
                drucke("Engine-Lock: Platte bleibt zu voll - dieser Lauf gibt auf.")
                return False
            if uhr() >= naechste_meldung:
                platte = next((z for z in text if "ABBRUCH" in z),
                              text[0] if text else "Platte zu voll")
                drucke("%s - warte auf Plattenplatz (hoechstens noch %.0f min) ..."
                       % (platte, (frist - uhr()) / 60.0))
                naechste_meldung = uhr() + 60.0
            schlaf(15.0)
            continue
        if fertig.returncode != LOCK_BELEGT or uhr() >= frist:
            for zeile in text[:3]:
                drucke(zeile)
            drucke("Engine-Lock nicht bekommen - dieser Lauf fasst den Gate-Worktree nicht an.")
            return False
        if uhr() >= naechste_meldung:
            belegt = next((z for z in text if "BELEGT" in z), text[0] if text else "belegt")
            drucke("%s - warte (hoechstens noch %.0f min) ..." % (belegt, (frist - uhr()) / 60.0))
            naechste_meldung = uhr() + 60.0
        schlaf(15.0)


def vorbereiten(projekt, sha):
    """Worktree auf genau diesen Commit bringen; Rueckgabe: sein Projektordner."""
    projekt = Path(projekt)
    wt = gate_projekt(projekt)
    if not (wt / ".git").exists():
        wt.parent.mkdir(parents=True, exist_ok=True)
        git(projekt, "worktree", "prune")
        git(projekt, "worktree", "add", "--detach", str(wt), sha)
        drucke("Gate-Worktree angelegt: %s" % wt)
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
    drucke("Gate-Worktree %s auf %s (%d Stadtinhalte verlinkt, %d neu)."
           % (wt, sha[:10], len(verzeichnisse) + len(dateien), neu))
    return wt


def pruefen(projekt, sha):
    """Volle Stufe im Worktree fahren - mit SEINER Fassung der Gates (der des Commits)."""
    wt = vorbereiten(projekt, sha)
    fertig = subprocess.run([sys.executable, str(wt / "Tools" / "vor_dem_commit.py"), "--stufe", "voll"],
                            cwd=str(wt), env=saubere_umgebung())
    return fertig.returncode


def push_pruefen(projekt, stdin_text):
    """Einstieg fuer den pre-push-Hook: jeden zu pushenden Dateibaum einmal pruefen."""
    # GEMESSEN am 27.09.2026: ein Push AUS dem Gate-Worktree heraus legte
    # einen zweiten, verschachtelten Gate-Worktree an. haupt_ordner() macht
    # das Pfad-Problem zwar unweg, der Aufruf bleibt aber sinnlos: der
    # Gate-Worktree enthaelt die Stadtinhalte als VERLINKUNG, ein zweiter
    # Lauf darin prueft nichts Neues und nur durch den Zufall, dass die
    # Verlinkungen mitwandern. Deshalb wird er hier abgewiesen.
    if ist_im_gate_worktree(projekt):
        drucke("Dieser Ordner IST der Gate-Worktree (%s)." % projekt)
        drucke("Ein Push wird aus dem HAUPT-Arbeitsordner gepusht, nicht von hier:")
        drucke("  %s" % haupt_ordner(projekt))
        return 1
    shas = push_shas(stdin_text)
    if not shas:
        drucke("Nur Loeschungen im Push - nichts zu pruefen.")
        return 0
    auswahl = je_baum_einer(shas, lambda s: git(projekt, "rev-parse", s + "^{tree}").strip())
    # Lock VOR dem ersten Checkout und ueber alle Commits (siehe motor_sperre).
    if not motor_sperre("push_gate"):
        drucke("\nEngine-Lock belegt - der Push wird abgewiesen, der laufende Gate-Lauf bleibt heil.")
        drucke("Spaeter erneut pushen, oder: git push --no-verify")
        return 1
    for sha in auswahl:
        drucke("Volles Gate im sauberen Worktree fuer %s ..." % sha[:10])
        rot = pruefen(projekt, sha)
        if rot:
            drucke("\nGate ROT fuer %s - der Push wird abgewiesen." % sha[:10])
            drucke("Wenn das so gewollt ist: git push --no-verify")
            return 1
    drucke("Gate gruen fuer %d Commit(s) im sauberen Worktree." % len(auswahl))
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Aufruf: python Tools/gate_worktree.py <commit>")
    if not motor_sperre("gate_worktree"):
        sys.exit(LOCK_BELEGT)
    sys.exit(pruefen(Path(__file__).resolve().parent.parent, sys.argv[1]))
