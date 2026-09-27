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
import shutil
import subprocess
import sys
import time
from pathlib import Path

NULL_SHA = "0" * 40
# Unversionierte Karten, die zur Stadt gehoeren (Bake-Ergebnisse, kein WIP).
STADTKARTEN = "Content/Maps/WiesbadenCity_*.umap"
# Unter diesen Wurzeln werden ignorierte Verzeichnisse verlinkt.
STADT_WURZELN = ("Content/", "Data/Raw/")
# Ordner, deren INHALT Belege sind. Sie sind nicht versioniert und ueberleben
# genau das "git clean -fd" OHNE -x, mit dem vorbereiten() den Worktree
# zurueckstellt - der Worktree sammelt sie also ueber beliebig viele
# Push-Laeufe an.
BELEGORDNER = ("Saved/Logs", "Saved/Diagnose")
# Zeitmarke "dieser Push-Lauf hat angefangen", in Sekunden seit 1970. Sie
# liegt in Saved/ selbst und NICHT in einem Belegordner - belege_raeumen()
# wuerde sie sonst gleich wieder mitloeschen. Damit koennen die Python-Suiten
# einen Beleg nicht nur auf Vollstaendigkeit, sondern auf AKTUALITAET
# pruefen (Tools/test_verify_cuttable_gate.py).
BELEG_MARKE = "Saved/.gate_lauf_beginn"


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


LOCK_SKRIPT = Path(__file__).resolve().parent / "engine_run_lock.ps1"
LOCK_BELEGT = 3


def motor_sperre(name, warte_s=None, lock_pfad=None, schlaf=time.sleep, uhr=time.monotonic):
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
    hinter dem ersten anstehen, nicht rot werden. Abfrage alle 15 s, eine
    Zeile je Minute; nach warte_s (Vorgabe WB_GATE_LOCK_WARTEN, sonst 3600 s)
    gibt der Lauf auf. Rueckgabe True = gehalten.
    """
    if warte_s is None:
        warte_s = float(os.environ.get("WB_GATE_LOCK_WARTEN", "3600"))
    befehl = ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(LOCK_SKRIPT),
              "-Modus", "Nehmen", "-Name", name]
    if lock_pfad:
        befehl += ["-LockPfad", str(lock_pfad)]
    frist = uhr() + warte_s
    naechste_meldung = uhr()
    while True:
        # WarteSekunden 0: das Skript prueft und legt atomar an, gewartet wird
        # hier - sonst schriebe es alle 2 s eine Zeile ins Hook-Protokoll.
        fertig = subprocess.run(befehl, capture_output=True, text=True,
                                encoding="utf-8", errors="replace")
        text = ((fertig.stdout or "") + (fertig.stderr or "")).strip().splitlines()
        if fertig.returncode == 0:
            print("Engine-Lock: %s" % (text[-1] if text else "gehalten"), flush=True)
            return True
        if fertig.returncode != LOCK_BELEGT or uhr() >= frist:
            for zeile in text[:3]:
                print(zeile, flush=True)
            print("Engine-Lock nicht bekommen - dieser Lauf faesst den Gate-Worktree nicht an.",
                  flush=True)
            return False
        if uhr() >= naechste_meldung:
            belegt = next((z for z in text if "BELEGT" in z), text[0] if text else "belegt")
            print("%s - warte (hoechstens noch %.0f min) ..." % (belegt, (frist - uhr()) / 60.0),
                  flush=True)
            naechste_meldung = uhr() + 60.0
        schlaf(15.0)


def belege_raeumen(wt, haupt):
    """Leert die Belegordner des Gate-Worktrees - die Reste haben keinen Besitzer.

    GEMESSEN am 27.09.2026: `git clean -fd` OHNE `-x` laesst ignorierte Dateien
    stehen (an einem Wegwerf-Repo mit derselben Konfiguration nachgemessen), und
    `Saved/` steht in .gitignore. Der Worktree wird zudem WIEDERVERWENDET, nur
    auf den neuen Commit gestellt. Damit lagen beim Start der Python-Suiten die
    Belege des VORRIGEN Push-Laufs im Baum - und die Suites lesen sie, weil sie
    vor Gate 4 laufen:

      * `test_verify_cuttable_gate` sollte laut eigenem Docstring im
        Commit-Worktree ueberspringen (es gibt dort keine Bilder). Es tat das
        aber nicht, sondern FUHR gegen die vollstaendigen Belege des vorigen
        Laufs und meldete sie als eigenen Beleg.
      * Dasselbe galt fuer `smoke_car.log`, `smoke_heli.log` und
        `WbHealth.json` - see Tools/smoke_test.ps1, wo genau darum jetzt
        Remove-Beleg und Test-Frisch stehen.

    Ein Beleg aus einem anderen Commit hat fuer diesen Commit keinen Wert. Er
    wird aber nicht als falsch markiert, sondern als eigener - das ist die
    schlimmere Halfte: ein gruenes Gate, das nichts gemessen hat.

    Zwei Sicherheitsguertel, weil dies die erste Stelle ist, die AUSSERHALB von
    git etwas loescht:

    * `Saved/` wird nie verlinkt (STADT_WURZELN = Content/, Data/Raw/). Ein
      Verzeichniswechsel (junction) an dieser Stelle zeigte auf fremde Daten -
      und `EchterWorktreeTest` dokumentiert, dass ein rekursives Loeschen
      DURCH eine Verbindung den Hauptordner leert. Im Ernstfall die 26 GB
      gebackene Stadt.
    * Ist der "Worktree" ausnahmsweise der Hauptordner selbst, wird gar nicht
      geloescht, sondern abgebrochen.
    """
    wt = Path(wt)
    haupt = Path(haupt)
    if wt.resolve() == haupt.resolve():
        raise RuntimeError(
            "Der Gate-Worktree IST der Hauptordner (%s) - die Belege des "
            "Arbeitsbaums werden nicht geloescht." % wt)
    entfernt, blockiert = 0, []
    for rel in BELEGORDNER:
        ordner = wt / rel
        if os.path.isjunction(ordner) or os.path.islink(ordner):
            raise RuntimeError(
                "%s ist eine Verknuepfung - dort wird nicht geloescht. Eine "
                "Verbindung zeigt auf fremde Daten, und ein Loeschen durch sie "
                "leert den Ordner, auf den sie zeigt." % ordner)
        if not ordner.is_dir():
            continue
        for eintrag in ordner.iterdir():
            try:
                if eintrag.is_dir() and not os.path.islink(eintrag):
                    shutil.rmtree(eintrag)
                else:
                    eintrag.unlink()
                entfernt += 1
            except OSError as fehler:
                # Ein haengender Editor haelt seinen Log offen. Genau der Fall,
                # an dem die Belege liegen bleiben - weiterlaufen hiesse, sie
                # fuer die eigenen zu halten.
                blockiert.append("%s (%s)" % (eintrag.name, fehler.strerror or fehler))
    if blockiert:
        raise RuntimeError(
            "Belege des vorigen Laufs nicht entfernbar: %s. Der Lauf wuerde "
            "sie als eigene lesen - erst den haengenden Prozess beenden, dann "
            "erneut." % "; ".join(blockiert[:5]))
    return entfernt


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
    # VOR den Gates: die Belege eines anderen Commits duerfen keinen als
    # eigene gelten. Siehe belege_raeumen().
    weggeraeumt = belege_raeumen(wt, projekt)
    # Die Zeitmarke kommt NACH dem Raeumen, sonst loescht der naechste Lauf
    # sie wieder weg. Sie ist der Anker fuer "aus diesem Lauf" - siehe
    # BELEG_MARKE.
    marke = wt / BELEG_MARKE
    marke.parent.mkdir(parents=True, exist_ok=True)
    marke.write_text("%.3f" % time.time(), encoding="utf-8")
    rest = [z for z in git(wt, "status", "--porcelain").splitlines()
            if not fnmatch.fnmatch(z[3:], STADTKARTEN)]
    if rest:
        raise RuntimeError("Gate-Worktree nicht sauber: %s" % "; ".join(rest[:5]))
    print("Gate-Worktree %s auf %s (%d Stadtinhalte verlinkt, %d neu, "
          "%d Beleg(e) des vorigen Laufs entfernt)."
          % (wt, sha[:10], len(verzeichnisse) + len(dateien), neu, weggeraeumt),
          flush=True)
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
    # Lock VOR dem ersten Checkout und ueber alle Commits (siehe motor_sperre).
    if not motor_sperre("push_gate"):
        print("\nEngine-Lock belegt - der Push wird abgewiesen, der laufende Gate-Lauf bleibt heil.")
        print("Spaeter erneut pushen, oder: git push --no-verify")
        return 1
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
    if not motor_sperre("gate_worktree"):
        sys.exit(LOCK_BELEGT)
    sys.exit(pruefen(Path(__file__).resolve().parent.parent, sys.argv[1]))
