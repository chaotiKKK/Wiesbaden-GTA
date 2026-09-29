# Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
"""Richtet in EINEM Lauf aus: Release-Bilder, Release-Texte, oeffentliches
Schaufenster und den oeffentlichen Release-Spiegel.

    python Tools/releases_ausrichten.py                 # Plan, schreibt nichts
    python Tools/releases_ausrichten.py --anwenden      # alles ausrichten

WARUM ES DAS GIBT. Nach jedem Merge ans private Repo sind vier Werkzeuge in
fester Reihenfolge zu fahren - und die falsche Reihenfolge richtet mehr
kaputt als sie repariert:

  1. `schaufenster.py`               erzeugt Seite + Bilder im oeffentlichen Repo
  2. `releases_bilder_ausrichten.py` Assets der privaten Releases
  3. `releases_texte_ausrichten.py`   Texte der privaten Releases
  4. `releases_oeffentlich.py`         Spiegel im oeffentlichen Repo

Schritt 1 muss VOR 2 bis 4 laufen, weil deren Bildnamen aus `origin/main`
kommen: wer die Assets vorher ausrichtet, loescht dem Release genau die
Bilder, die der gerade gemergte Zweig hinzugefuegt hat - am 27.09.2026 so
geschehen, zwei Bilder weg, weil die Seite noch nicht auf main stand.

Dieses Werkzeug exportiert die Seite und die Bilder aus EINEM Ref (`--ref`,
Vorgabe `origin/main`), reicht beiden Ausrichtern genau diese Dateien und
ruft am Ende das Gate auf, das den Zustand misst. Stimmt das Gate nicht, ist
der Lauf auch nicht "fertig" - sein Exit-Code ist dann der des Gates.

    Exit 0  alles ausgerichtet (oder: der Plan fand nichts zu tun)
    Exit 1  beim Ausrichten ist etwas gescheitert
    Exit 2  Voraussetzung fehlt: Ref ohne Seite, Ziel ist nicht der Klon des
            oeffentlichen Schaufensters, oder der Klon hat fremde Arbeit
    Exit 3  das Gate am Ende war rot - ausdruecklich NICHT "fertig"

Im Plan-Modus wird das Schaufenster in ein TEMP-Verzeichnis erzeugt und mit
dem Klon verglichen; der Klon bleibt unberuehrt. Deshalb kann der Plan
sagen, was sich im Schaufenster aendern wuerde, obwohl im Anwendungs-Modus
dort direkt geschrieben wird.
"""
import argparse
import io
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile

REPO = pathlib.Path(__file__).resolve().parents[1]
TOOLS = REPO / "Tools"
sys.path.insert(0, str(TOOLS))

import schaufenster  # noqa: E402
from vor_dem_commit import drucke  # noqa: E402

OEFFENTLICHES_REPO = "chaotiKKK/wiesbaden-real-meilensteine"
# Steht als "Stand TT.MM.JJJJ" in der Fusszeile jeder Seite.
STAND_MUSTER = re.compile(r"Stand\s+(\d{2}\.\d{2}\.\d{4})")
SEITENPFAD = "docs/meilensteine.md"
BILDERPFAD = "docs/meilensteine/bilder"

# Austauschbar, damit der Selbsttest weder gh noch git noch das Netz braucht.
LAUF = subprocess.run


class Abbruch(RuntimeError):
    """Eine Voraussetzung fehlt - nicht dasselbe wie ein gescheiterter Lauf."""


# ---------------------------------------------------------------------------
# Bausteine
# ---------------------------------------------------------------------------

def git(*args, cwd=None, text=True):
    # GEMESSEN: `encoding=` setzt subprocess in den Textmodus - auch dann,
    # wenn man `text=False` mitgibt. Fuer `git archive` (Binärdaten) darf
    # deshalb nur eine der beiden Angaben kommen.
    kwargs = {"text": True, "encoding": "utf-8", "errors": "replace"} if text \
        else {"text": False}
    fertig = LAUF(["git", *args], cwd=str(cwd or REPO), capture_output=True,
                  **kwargs)
    if fertig.returncode != 0:
        raise Abbruch("`git %s` scheitert: %s"
                      % (" ".join(args), (fertig.stderr or b"" if not text
                                          else "").strip()[:200]))
    return fertig.stdout


def werkzeug(name, *args):
    """Ein Schritt als Unterprozess; seine Ausgabe wird unveraendert
    durchgereicht. Ein Ausrichter, der seinen Plan nicht erklaeren kann, ist
    hier kein Helfer."""
    befehl = [sys.executable, str(TOOLS / name), *args]
    print("\n--- %s %s" % (name, " ".join(args)), flush=True)
    fertig = LAUF(befehl, cwd=str(REPO), capture_output=True, text=True,
                  encoding="utf-8", errors="replace")
    for zeile in ((fertig.stdout or "") + (fertig.stderr or "")).rstrip().splitlines():
        drucke("   " + zeile)
    return fertig.returncode


def exportieren(ref):
    """(wurzel, text, bilder) aus EINEM Ref in ein TEMP-Verzeichnis.

    `git archive` statt `git show`: damit sind die Bilder garantiert DERSELBE
    Stand wie die Seite. Ein Bild, das die Seite nennt, aber im Ref nicht
    liegt, faellt dann beim Entpacken auf und nicht erst beim Upload.
    """
    wurzel = pathlib.Path(tempfile.mkdtemp(prefix="wb_ausrichten_"))
    roh = git("archive", ref, SEITENPFAD, BILDERPFAD, text=False)
    with tarfile.open(fileobj=io.BytesIO(roh)) as band:
        band.extractall(wurzel, filter="data")
    seite = wurzel / SEITENPFAD
    bilder = wurzel / BILDERPFAD
    if not seite.is_file() or not bilder.is_dir():
        raise Abbruch(f"{SEITENPFAD} oder {BILDERPFAD} gibt es in {ref} nicht - "
                      f"`git fetch origin`?")
    return wurzel, seite.read_text(encoding="utf-8"), bilder


def stand_text(ref):
    """Die Fusszeile aus dem DATUM DES REFS, nicht aus dem heutigen Tag.

    GEMESSEN am 28.09.2026: mit `date.today()` meldete der Plan
    "geaendert: index.html" fuer einen Klon, der am Vortag aus demselben
    Ref gebaut worden war - der einzige Unterschied war "Stand 28.09.2026"
    gegen "Stand 27.09.2026", und `git` haette nichts zu committen gehabt.
    Der Stand ist eine Aussage ueber den INHALT, nicht ueber den Tag, an dem
    jemand nachgesehen hat. Deshalb wird das Commit-Datum des Refs genommen:
    derselbe Ref ergibt damit immer dieselbe Seite - der Plan wird wieder
    still, sobald wirklich nichts zu tun ist.
    """
    stamp = git("log", "-1", "--format=%cs", ref).strip()[:10]
    try:
        jahr, monat, tag = (int(x) for x in stamp.split("-"))
        return "Stand %02d.%02d.%04d" % (tag, monat, jahr)
    except ValueError:
        raise Abbruch("kann das Datum von %s nicht lesen (%r) - git log -1 "
                      "--format=%%cs" % (ref, stamp))


def klon_pruefen(ziel):
    """Der Schaufenster-Klon muss genau der sein - und sauber.

    Zwei Fehler, die beide still waeren: ein Klon, der auf ein anderes Repo
    zeigt (dort waere jeder Schreibzugriff gelandet), und ein schmutziger
    Klon (`git add -A` haette die fremde Arbeit mitveroeffentlicht).
    """
    if not ziel.is_dir():
        raise Abbruch("Schaufenster-Klon fehlt: %s\n  einmal klonen: git clone "
                      "https://github.com/%s.git %s"
                      % (ziel, OEFFENTLICHES_REPO, ziel))
    if not (ziel / ".git").exists():
        raise Abbruch("%s ist kein Git-Klon" % ziel)
    remote = git("remote", "get-url", "origin", cwd=ziel).strip()
    if OEFFENTLICHES_REPO not in remote:
        raise Abbruch("%s zeigt auf %s, nicht auf %s - dort wird nicht "
                      "geschrieben" % (ziel, remote, OEFFENTLICHES_REPO))
    schmutz = [z for z in git("status", "--porcelain", cwd=ziel).splitlines() if z.strip()]
    if schmutz:
        raise Abbruch("der Klon hat %d uncommittete Aenderung(en), darunter:\n  %s\n"
                      "  fremde Arbeit wird nicht mitveroeffentlicht"
                      % (len(schmutz), "\n  ".join(schmutz[:6])))


def gleich(a, b):
    """Gleiche Datei? Zeilenenden werden behandelt.

    GEMESSEN: der Klon kennt kein `.gitattributes`, sein Arbeitsbaum hat fuer
    index.html und README.md CRLF, `schaufenster.erzeugen` schreibt LF. Ohne
    diesen Vergleich meldet der Plan bei JEDEM Lauf "geaendert: index.html",
    obwohl git danach nichts zu committen haette.
    """
    if a.is_dir() or b.is_dir():
        return a.is_dir() and b.is_dir()
    if a.suffix.lower() in (".jpg", ".jpeg", ".gif", ".png", ".webp"):
        return a.read_bytes() == b.read_bytes()
    try:
        return (a.read_text(encoding="utf-8").replace("\r\n", "\n")
                == b.read_text(encoding="utf-8").replace("\r\n", "\n"))
    except (UnicodeDecodeError, OSError):
        return a.read_bytes() == b.read_bytes()


def stand_hinweis(neu, alt):
    """Steht der Unterschied NUR in der Stand-Zeile, wird das gesagt.

    Sonst sucht man stundenlang nach einem Encoding- oder Zeilenendefehler,
    der gar nicht existiert: am 28.09.2026 war der ganze Unterschied
    "Stand 28.09.2026" gegen "Stand 27.09.2026".
    """
    try:
        a = STAND_MUSTER.search(neu.read_text(encoding="utf-8"))
        b = STAND_MUSTER.search(alt.read_text(encoding="utf-8"))
    except (UnicodeDecodeError, OSError):
        return ""
    if not a or not b or a.group(1) == b.group(1):
        return ""
    ohne_a = STAND_MUSTER.sub("Stand <DATUM>", neu.read_text(encoding="utf-8"))
    ohne_b = STAND_MUSTER.sub("Stand <DATUM>", alt.read_text(encoding="utf-8"))
    if ohne_a.replace("\r\n", "\n") != ohne_b.replace("\r\n", "\n"):
        return ""
    return " (nur der Stand: %s -> %s)" % (b.group(1), a.group(1))


def schaufenster_plan(ziel, text, bilder, stand):
    """Erzeugt das Schaufenster in ein TEMP-Verzeichnis und vergleicht mit dem
    Klon: (anzahl_bilder, [unterschiede])."""
    temp = pathlib.Path(tempfile.mkdtemp(prefix="wb_schaufenster_"))
    try:
        anzahl = schaufenster.erzeugen(text, str(bilder), str(temp), stand)
        unterschied = []
        for pfad in sorted(temp.rglob("*")):
            if pfad.is_dir():
                continue
            zielpfad = ziel / pfad.relative_to(temp)
            if not zielpfad.exists():
                unterschied.append("neu: %s" % pfad.relative_to(temp))
            elif not gleich(pfad, zielpfad):
                unterschied.append("geaendert: %s%s"
                                   % (pfad.relative_to(temp), stand_hinweis(pfad, zielpfad)))
        for pfad in sorted((ziel / "bilder").glob("*")):
            if not (temp / "bilder" / pfad.name).exists():
                unterschied.append("entfaellt: bilder/%s" % pfad.name)
        return anzahl, unterschied
    finally:
        shutil.rmtree(temp, ignore_errors=True)


def schaufenster_schreiben(ziel, text, bilder, stand, ref, kurz):
    """Erzeugt, committet und schiebt. (geaendert, notiz)"""
    schaufenster.erzeugen(text, str(bilder), str(ziel), stand)
    LAUF(["git", "add", "-A"], cwd=str(ziel), capture_output=True, text=True,
         encoding="utf-8", errors="replace")
    if LAUF(["git", "diff", "--cached", "--quiet"], cwd=str(ziel),
            capture_output=True).returncode == 0:
        return 0, "nichts zu committen (die Zeilenenden waren der einzige Unterschied)"
    fertig = LAUF(["git", "commit", "-m",
                   "Schaufenster: neu aus %s (%s)" % (ref, kurz)],
                  cwd=str(ziel), capture_output=True, text=True,
                  encoding="utf-8", errors="replace")
    if fertig.returncode != 0:
        raise Abbruch("commit im Klon: %s" % (fertig.stderr or "").strip()[:200])
    schieb = LAUF(["git", "push", "origin", "HEAD"], cwd=str(ziel),
                  capture_output=True, text=True, encoding="utf-8",
                  errors="replace")
    if schieb.returncode != 0:
        raise Abbruch("push: %s" % (schieb.stderr or "").strip()[:200])
    return 1, "gepusht (%s)" % kurz


# ---------------------------------------------------------------------------
# Lauf
# ---------------------------------------------------------------------------

def klon_finden(ausdruecklich):
    """Wo liegt der Klon des oeffentlichen Schaufensters?

    Ein fester Standardpfad waere geraten: auf diesem Rechner liegt der
    Klon eine Ebene ueber dem Projekt (`C:\\freebuff\\...`), auf einem
    anderen vielleicht daneben oder im Benutzerverzeichnis. Deshalb wird
    gesucht - erst die ausdrueckliche Angabe, dann eine Umgebungsvariable
    (`WB_SCHAUFENSTER`), dann alle Ebenen ueber dem Projekt hoch, dann das
    Benutzerverzeichnis. Findet sich nichts, sagt die Meldung, WO gesucht
    wurde, statt einen Pfad zu erfinden, der dann zufaellig stimmt.
    """
    if ausdruecklich:
        kandidaten = [pathlib.Path(ausdruecklich)]
    else:
        kandidaten = []
        if os.environ.get("WB_SCHAUFENSTER"):
            kandidaten.append(pathlib.Path(os.environ["WB_SCHAUFENSTER"]))
        eltern = REPO.parents
        kandidaten += [p / "wiesbaden-real-meilensteine" for p in eltern]
        kandidaten.append(pathlib.Path.home() / "wiesbaden-real-meilensteine")
    for kandidat in kandidaten:
        if (kandidat / ".git").is_dir():
            return kandidat.resolve()
    raise Abbruch("Kein Klon des oeffentlichen Schaufensters gefunden. Gesucht:\n  %s\n"
                  "  einmal klonen: git clone https://github.com/%s.git <ziel>\n"
                  "  oder Ziel mit --ziel, Ort mit der Umgebungsvariable "
                  "WB_SCHAUFENSTER benennen"
                  % ("\n  ".join(str(k) for k in kandidaten), OEFFENTLICHES_REPO))


def hauptprogramm(argv=None):
    ap = argparse.ArgumentParser(
        description="Release-Bilder, -Texte, Schaufenster und Spiegel in einem Lauf ausrichten.")
    ap.add_argument("--anwenden", action="store_true", help="schreiben statt planen")
    ap.add_argument("--ref", default="origin/main",
                    help="Stand, aus dem ausgerichtet wird (Vorgabe: origin/main)")
    ap.add_argument("--ziel", default=None,
                    help="Klon des oeffentlichen Schaufensters (Vorgabe: gesucht)")
    ap.add_argument("--ohne-schaufenster", action="store_true",
                    help="Schaufenster und Push auslassen")
    ap.add_argument("--kein-gate", action="store_true", help="Gate am Ende auslassen")
    args = ap.parse_args(argv)

    ziel = None
    print("Ausrichtung aller Release-Ausgaben")
    print("  Quelle (Ref): %s" % args.ref)
    print("  Modus:        %s" % ("anwenden" if args.anwenden else "Plan (schreibt nichts)"))

    try:
        if not args.ohne_schaufenster:
            ziel = klon_finden(args.ziel)
            print("  Schaufenster: %s" % ziel)
            klon_pruefen(ziel)
        else:
            print("  Schaufenster: ausgelassen (--ohne-schaufenster)")
        wurzel, text, bilder = exportieren(args.ref)
        kurz = git("rev-parse", "--short", args.ref).strip()
    except Abbruch as grund:
        print("\n  ABBRUCH: %s" % grund)
        return 2

    stand = stand_text(args.ref)
    anwenden = ["--anwenden"] if args.anwenden else []
    try:
        # --- 1. Schaufenster zuerst: alle anderen zitieren seine Bildnamen --
        if args.ohne_schaufenster:
            print("\n--- Schaufenster: ausgelassen (--ohne-schaufenster)")
        else:
            anzahl, unterschied = schaufenster_plan(ziel, text, bilder, stand)
            print("\n--- Schaufenster (%s): %d Bilder, %d Datei(en) weichen ab"
                  % (args.ref, anzahl, len(unterschied)))
            for zeile in unterschied[:12]:
                print("   " + zeile)
            if len(unterschied) > 12:
                print("   ... und %d weitere" % (len(unterschied) - 12))
            if args.anwenden and unterschied:
                _, notiz = schaufenster_schreiben(ziel, text, bilder, stand,
                                                  args.ref, kurz)
                print("   " + notiz)
            elif unterschied:
                print("   (Plan: wuerde erzeugen und pushen)")

        # --- 2 und 3. Die privaten Releases ---------------------------------
        schritte = (
            ("releases_bilder_ausrichten.py", anwenden
             + ["--seite", str(wurzel / SEITENPFAD), "--bilder", str(bilder)]),
            ("releases_texte_ausrichten.py", anwenden
             + ["--seite", str(wurzel / SEITENPFAD), "--bilder", str(bilder),
                "--sha", kurz]),
            # 4. Der Spiegel liest die Seite selbst aus dem Ref - dieselbe Quelle.
            ("releases_oeffentlich.py", anwenden + ["--ref", args.ref, "--sha", kurz]),
        )
        for name, parameter in schritte:
            code = werkzeug(name, *parameter)
            if code != 0 and args.anwenden:
                print("\n  %s ist gescheitert - abgebrochen, die restlichen "
                      "Schritte laufen nicht mehr." % name)
                return 1
    except Abbruch as grund:
        print("\n  ABBRUCH: %s" % grund)
        return 2
    finally:
        shutil.rmtree(wurzel, ignore_errors=True)

    # --- 5. Der Beweis: das Gate -------------------------------------------
    if args.kein_gate:
        print("\nGate ausgelassen (--kein-gate).")
        return 0
    print("\n--- Gate (der Beweis fuer diesen Lauf)")
    if werkzeug("release_abgleich.py") != 0:
        print("\n  Gate rot - dieser Lauf ist damit NICHT fertig.")
        return 3
    print("\n  Alles ausgerichtet und gemessen. Fertig.")
    return 0


if __name__ == "__main__":
    raise SystemExit(hauptprogramm())
