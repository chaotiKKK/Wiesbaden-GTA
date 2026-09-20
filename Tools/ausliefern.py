"""Sicher ausliefern, waehrend fremde Arbeit im Baum liegt.

WOFUER: In diesem Baum arbeiten mehrere Agenten und der Nutzer gleichzeitig.
Wer dann `git add -A` tippt, committet fremde, halbfertige Arbeit mit - und
merkt es erst, wenn sie auf main liegt. Dieses Werkzeug committet GENAU die
gewuenschten Pfade, laesst alles andere unberuehrt liegen und sagt vor dem
Push, ob trotzdem etwas Fremdes im Commit gelandet ist.

    python Tools/ausliefern.py zeigen Source/Foo.cpp Tools/bar.py
    python Tools/ausliefern.py commit -F nachricht.txt Source/Foo.cpp
    python Tools/ausliefern.py pruefen
    python Tools/ausliefern.py push

SECHS FALLEN, die hier fest verdrahtet sind - jede hat schon einmal Schaden
angerichtet:

1. `git add <pfad>` BRICHT AB, wenn einer der Pfade bereits geloescht ist
   ("did not match any files"), und dann ist NICHTS gestaged. Wer danach
   `git commit` ruft, committet den vorherigen Index-Stand - im Ernstfall eine
   einzelne Loeschung statt neun Dateien. Genau das ist am 20.09.2026 passiert
   und fiel nur auf, weil hinterher jemand `git show --stat` gelesen hat.
   Darum: `git commit --only -- <pfade>`, das Loeschungen mitnimmt, und
   danach ein Ist-Soll-Vergleich der Dateiliste des Commits.
2. `git add -A` und `git add .` fassen den GANZEN Baum an. Dieses Werkzeug
   benutzt sie nirgends und ruehrt den Index fremder Dateien nicht an;
   `--only` committet an einem bereits gefuellten Index vorbei.
3. Ein VERZEICHNIS als Pfad nimmt alles darin mit - auch fremde Dateien, die
   zufaellig dort liegen. Darum wird jeder Pfad vorher auf konkrete Dateien
   aufgeloest und die Liste ausgegeben, bevor irgendetwas passiert.
4. Ein Tippfehler im Pfad committet lautlos gar nichts. Ein Pfad ohne jede
   Aenderung ist deshalb ein Fehler, kein Achselzucken.
5. `git commit --only` kennt nur Pfade, die git schon kennt. Eine NEUE Datei
   laesst es mit "did not match any file(s) known to git" abblitzen - auch
   wenn sie danebensteht. Sie wird deshalb vorher mit `add -N` bekannt
   gemacht, was keinen Inhalt staged. (Aufgefallen, als dieses Werkzeug sich
   selbst ausliefern sollte.)
6. Nach `--amend` oder einem Rebase stimmen die gemerkten Commit-Nummern nicht
   mehr. Unbekannte Commits werden darum gemeldet, statt als "geprueft" zu
   gelten - ein Werkzeug, das im Zweifel schweigt, ist schlimmer als keines.

Das Merkbuch liegt unter `.git/ausliefern-journal.json`: je Commit die Pfade,
die gewollt waren. Es ist nicht versioniert - es gehoert zu diesem Klon.
"""
import argparse
import json
import os
import subprocess
import sys

JOURNAL = "ausliefern-journal.json"


class Fehler(Exception):
    """Abbruch mit lesbarem Grund - keine Rueckverfolgung noetig."""


# --------------------------------------------------------------------------
# Git-Grundlagen
# --------------------------------------------------------------------------

def git(*args, wurzel=None, pruefen=True):
    """Ruft git und liefert die Ausgabe. Fehlertext bleibt lesbar."""
    ergebnis = subprocess.run(
        ["git", *args], cwd=wurzel, capture_output=True, text=True,
        encoding="utf-8", errors="replace")
    if pruefen and ergebnis.returncode != 0:
        raise Fehler("git %s fehlgeschlagen:\n%s%s"
                     % (" ".join(args), ergebnis.stdout, ergebnis.stderr))
    return ergebnis.stdout


def git_z(*args, wurzel=None):
    """Wie git(), aber fuer -z-Ausgaben: Liste ohne leere Eintraege."""
    roh = subprocess.run(["git", *args], cwd=wurzel, capture_output=True, check=True).stdout
    return [t.decode("utf-8", "surrogateescape") for t in roh.split(b"\0") if t]


def repo_wurzel(start=None):
    pfad = start or os.path.dirname(os.path.abspath(__file__))
    return git("rev-parse", "--show-toplevel", wurzel=pfad).strip()


def mitten_drin(wurzel):
    """Merge, Rebase oder Cherry-Pick im Gang? Dann nicht anfassen."""
    git_dir = os.path.join(wurzel, git("rev-parse", "--git-dir", wurzel=wurzel).strip())
    for marke, name in (("MERGE_HEAD", "Merge"), ("CHERRY_PICK_HEAD", "Cherry-Pick"),
                        ("REBASE_HEAD", "Rebase"), ("rebase-merge", "Rebase"),
                        ("rebase-apply", "Rebase")):
        if os.path.exists(os.path.join(git_dir, marke)):
            return name
    return None


# --------------------------------------------------------------------------
# Pfade aufloesen
# --------------------------------------------------------------------------

def status_dateien(wurzel):
    """Geaenderte Dateien mit ihrem Statuscode: Pfad -> "XY"."""
    daten = {}
    for zeile in git_z("status", "--porcelain", "-z", "--untracked-files=all", wurzel=wurzel):
        if len(zeile) > 3 and zeile[2] == " ":
            daten[zeile[3:]] = zeile[:2]
    return daten


def geaenderte_dateien(wurzel):
    """Alle geaenderten Dateien des Baums, relativ zur Wurzel.

    Deckt Index UND Arbeitsbaum ab, damit eine bereits per `git rm` gestagete
    Loeschung nicht durchrutscht.
    """
    dateien = set()
    for zeile in git_z("status", "--porcelain", "-z", "--untracked-files=all", wurzel=wurzel):
        # Format: "XY <pfad>"; bei Umbenennungen folgt der alte Pfad als
        # eigener Eintrag, der hier egal ist.
        if len(zeile) > 3 and zeile[2] == " ":
            dateien.add(zeile[3:])
    return dateien


def aufloesen(wurzel, pfade):
    """Macht aus Pfaden (Datei oder Verzeichnis) die konkret betroffenen Dateien.

    Rueckgabe: (zuordnung, ohne_aenderung)
      zuordnung     - gewuenschter Pfad -> sortierte Liste betroffener Dateien
      ohne_aenderung- Pfade, unter denen sich nichts geaendert hat
    """
    alle = geaenderte_dateien(wurzel)
    zuordnung = {}
    leer = []
    for pfad in pfade:
        norm = pfad.replace("\\", "/").rstrip("/")
        treffer = sorted(d for d in alle if d == norm or d.startswith(norm + "/"))
        if treffer:
            zuordnung[pfad] = treffer
        else:
            leer.append(pfad)
    return zuordnung, leer


def commit_dateien(wurzel, commit):
    """Dateiliste eines Commits (ohne Merge-Sonderfaelle)."""
    return set(git_z("show", "--name-only", "--format=", "-z", commit, wurzel=wurzel))


# --------------------------------------------------------------------------
# Merkbuch
# --------------------------------------------------------------------------

def journal_pfad(wurzel):
    git_dir = git("rev-parse", "--git-dir", wurzel=wurzel).strip()
    if not os.path.isabs(git_dir):
        git_dir = os.path.join(wurzel, git_dir)
    return os.path.join(git_dir, JOURNAL)


def journal_lesen(wurzel):
    pfad = journal_pfad(wurzel)
    if not os.path.exists(pfad):
        return {}
    try:
        with open(pfad, encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        # Ein kaputtes Merkbuch darf die Auslieferung nicht blockieren - es
        # fuehrt nur dazu, dass die Commits als ungeprueft gelten.
        return {}


def journal_schreiben(wurzel, daten):
    with open(journal_pfad(wurzel), "w", encoding="utf-8") as f:
        json.dump(daten, f, indent=1, ensure_ascii=False, sort_keys=True)


# --------------------------------------------------------------------------
# Befehl: zeigen
# --------------------------------------------------------------------------

def befehl_zeigen(wurzel, pfade):
    zuordnung, leer = aufloesen(wurzel, pfade)
    gewollt = {d for liste in zuordnung.values() for d in liste}
    alle = geaenderte_dateien(wurzel)
    fremd = sorted(alle - gewollt)

    print("WIRD COMMITTET (%d Datei(en)):" % len(gewollt))
    for pfad in pfade:
        for datei in zuordnung.get(pfad, []):
            zusatz = "" if datei == pfad.replace("\\", "/") else "   <- %s" % pfad
            print("   %s%s" % (datei, zusatz))
    if leer:
        print("\nOHNE AENDERUNG (das ist ein Fehler, kein Hinweis):")
        for pfad in leer:
            print("   %s" % pfad)
    print("\nBLEIBT LIEGEN - fremde oder spaetere Arbeit (%d):" % len(fremd))
    for datei in fremd[:40]:
        print("   %s" % datei)
    if len(fremd) > 40:
        print("   ... und %d weitere" % (len(fremd) - 40))
    return 1 if leer else 0


# --------------------------------------------------------------------------
# Befehl: commit
# --------------------------------------------------------------------------

def befehl_commit(wurzel, pfade, nachricht=None, nachrichtdatei=None, trocken=False):
    hindernis = mitten_drin(wurzel)
    if hindernis:
        raise Fehler("%s laeuft gerade - erst abschliessen, dann ausliefern." % hindernis)

    zuordnung, leer = aufloesen(wurzel, pfade)
    if leer:
        raise Fehler("Diese Pfade haben keine Aenderung (Tippfehler?):\n   %s"
                     % "\n   ".join(leer))
    erwartet = sorted({d for liste in zuordnung.values() for d in liste})

    print("Committe %d Datei(en):" % len(erwartet))
    for datei in erwartet:
        print("   %s" % datei)
    if trocken:
        print("\n(Trockenlauf - nichts committet.)")
        return 0

    # `git commit --only` kennt nur Pfade, die git schon kennt - eine NEUE
    # Datei laesst es mit "did not match any file(s) known to git" abblitzen.
    # Gefunden, als dieses Werkzeug sich selbst ausliefern sollte. `add -N`
    # macht den Pfad bekannt, ohne Inhalt zu stagen; den holt `--only` dann aus
    # dem Arbeitsbaum. Fremde unverfolgte Dateien bleiben unberuehrt.
    status = status_dateien(wurzel)
    neue = [d for d in erwartet if status.get(d, "").startswith("?")]
    if neue:
        git("add", "-N", "--", *neue, wurzel=wurzel)

    args = ["commit", "--only"]
    if nachrichtdatei:
        args += ["-F", nachrichtdatei]
    elif nachricht:
        args += ["-m", nachricht]
    else:
        raise Fehler("Es fehlt eine Nachricht (-m oder -F).")
    # "--" trennt Pfade von Optionen; ohne das deutet git einen Pfad wie
    # "-Foo" als Schalter.
    args += ["--", *erwartet]
    git(*args, wurzel=wurzel)

    kopf = git("rev-parse", "HEAD", wurzel=wurzel).strip()
    tatsaechlich = commit_dateien(wurzel, kopf)

    fehlend = sorted(set(erwartet) - tatsaechlich)
    zuviel = sorted(tatsaechlich - set(erwartet))
    print("\nCommit %s: %d Datei(en)." % (kopf[:8], len(tatsaechlich)))
    if fehlend or zuviel:
        # Genau hier haette der Vorfall vom 20.09. aufhoeren muessen.
        if fehlend:
            print("FEHLT im Commit (%d):" % len(fehlend))
            for d in fehlend:
                print("   %s" % d)
        if zuviel:
            print("FREMD im Commit (%d):" % len(zuviel))
            for d in zuviel:
                print("   %s" % d)
        raise Fehler("Der Commit stimmt nicht mit den gewuenschten Pfaden ueberein. "
                     "Nicht pushen; mit 'git reset --soft HEAD~1' zuruecknehmen.")

    journal = journal_lesen(wurzel)
    journal[kopf] = erwartet
    journal_schreiben(wurzel, journal)
    print("Ist-Soll stimmt ueberein, im Merkbuch vermerkt.")
    return 0


# --------------------------------------------------------------------------
# Befehl: pruefen
# --------------------------------------------------------------------------

def zu_pushende_commits(wurzel, gegen):
    return git("rev-list", "%s..HEAD" % gegen, wurzel=wurzel).split()


def befehl_pruefen(wurzel, gegen=None, still=False):
    """Prueft die unveroeffentlichten Commits gegen das Merkbuch.

    Rueckgabe: (anzahl_beanstandet, bericht als Liste von Zeilen)
    """
    if gegen is None:
        zweig = git("rev-parse", "--abbrev-ref", "HEAD", wurzel=wurzel).strip()
        gegen = git("rev-parse", "--abbrev-ref", "%s@{upstream}" % zweig,
                    wurzel=wurzel, pruefen=False).strip()
        if not gegen:
            raise Fehler("Kein Gegenstueck in der Ferne. Mit --gegen origin/main angeben.")

    commits = zu_pushende_commits(wurzel, gegen)
    journal = journal_lesen(wurzel)
    zeilen = ["%d Commit(s) vor %s:" % (len(commits), gegen)]
    beanstandet = 0

    for commit in commits:
        kurz = git("show", "-s", "--format=%h %s", commit, wurzel=wurzel).strip()
        dateien = commit_dateien(wurzel, commit)
        if commit not in journal:
            # Falle 5: nicht von diesem Werkzeug gebaut (oder nach --amend
            # umnummeriert). Nicht durchwinken, sondern benennen.
            zeilen.append("   ? %s  - nicht ueber dieses Werkzeug (%d Datei(en))"
                          % (kurz, len(dateien)))
            beanstandet += 1
            continue
        gewollt = set(journal[commit])
        fremd = sorted(dateien - gewollt)
        fehlend = sorted(gewollt - dateien)
        if fremd or fehlend:
            zeilen.append("   ! %s" % kurz)
            for d in fremd:
                zeilen.append("       FREMD : %s" % d)
            for d in fehlend:
                zeilen.append("       FEHLT : %s" % d)
            beanstandet += 1
        else:
            zeilen.append("   + %s  (%d Datei(en), wie gewollt)" % (kurz, len(dateien)))

    if not still:
        print("\n".join(zeilen))
    return beanstandet, zeilen


# --------------------------------------------------------------------------
# Befehl: push
# --------------------------------------------------------------------------

def befehl_push(wurzel, gegen=None, trotzdem=False, zusatz=None):
    beanstandet, _ = befehl_pruefen(wurzel, gegen)
    if beanstandet and not trotzdem:
        print("\nNICHT GEPUSHT: %d Commit(s) beanstandet (siehe oben)." % beanstandet)
        print("Wenn das so gewollt ist: nochmal mit --trotzdem.")
        return 2
    if beanstandet:
        print("\n--trotzdem gesetzt: pushe trotz %d Beanstandung(en)." % beanstandet)
    ausgabe = git("push", *(zusatz or []), wurzel=wurzel)
    print(ausgabe or "Push durch.")
    return 0


# --------------------------------------------------------------------------

def hauptprogramm(argv=None):
    p = argparse.ArgumentParser(
        description="Genau die gewollten Pfade ausliefern, fremde Arbeit stehen lassen.")
    unter = p.add_subparsers(dest="befehl", required=True)

    z = unter.add_parser("zeigen", help="Was wuerde committet, was bleibt liegen")
    z.add_argument("pfade", nargs="+")

    c = unter.add_parser("commit", help="Genau diese Pfade committen")
    c.add_argument("pfade", nargs="+")
    c.add_argument("-m", dest="nachricht", help="Commit-Nachricht")
    c.add_argument("-F", dest="nachrichtdatei", help="Datei mit der Nachricht")
    c.add_argument("--trocken", action="store_true", help="nur zeigen, nichts tun")

    pr = unter.add_parser("pruefen", help="Unveroeffentlichte Commits gegen das Merkbuch")
    pr.add_argument("--gegen", help="Vergleichspunkt, z. B. origin/main")

    pu = unter.add_parser("push", help="Pruefen und nur bei sauberem Befund pushen")
    pu.add_argument("--gegen", help="Vergleichspunkt, z. B. origin/main")
    pu.add_argument("--trotzdem", action="store_true", help="trotz Beanstandung pushen")
    pu.add_argument("--zusatz", nargs="*", help="weitere Argumente fuer git push")

    a = p.parse_args(argv)
    wurzel = repo_wurzel()

    try:
        if a.befehl == "zeigen":
            return befehl_zeigen(wurzel, a.pfade)
        if a.befehl == "commit":
            return befehl_commit(wurzel, a.pfade, a.nachricht, a.nachrichtdatei, a.trocken)
        if a.befehl == "pruefen":
            beanstandet, _ = befehl_pruefen(wurzel, a.gegen)
            return 1 if beanstandet else 0
        if a.befehl == "push":
            return befehl_push(wurzel, a.gegen, a.trotzdem, a.zusatz)
    except Fehler as fehler:
        print("ABBRUCH: %s" % fehler, file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(hauptprogramm())
