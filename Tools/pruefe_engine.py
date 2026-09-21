r"""Waechter: nennt irgendein Werkzeug eine ANDERE Engine als die kanonische?

    python Tools/pruefe_engine.py            # prueft, Exit 1 bei Abweichung
    python Tools/pruefe_engine.py --liste    # zeigt jede Fundstelle

WOFUER: Auf diesem Rechner liegen zwei Engines 5.8 nebeneinander - die
installierte (5.8.2) und eine aeltere Kopie im Projektordner (5.8.1). Beide
heissen "UE_5.8", beide existieren, beide bauen. Mischt man sie, stirbt der
Build in einem ENGINE-Header (`GenericPlatform.h`: C2953
"SelectIntPointerType" bereits definiert) - das sieht nach kaputtem
Engine-Quelltext aus und ist keiner.

Am 21.09.2026 kostete genau das einen halben Release-Lauf: `build_release.ps1`
leitete seinen Engine-Pfad aus dem Projektordner ab und erwischte die Kopie.
Die Pipeline pruefte mit `Test-Path`, ob `Build.bat` da ist - und sie IST da.
**Ein Pfad, der existiert und trotzdem falsch ist, faellt keiner
Vorhandenseins-Pruefung auf.** Nur dem Vergleich mit dem, was sonst baut.

Dieser Waechter zieht genau diesen Vergleich, in Sekunden, bevor ein
Compiler ihn in zehn Minuten und einer irrefuehrenden Fehlermeldung zieht.

DREI REGELN:

1. **Gesucht wird in dem, was git verfolgt** - nicht im Arbeitsbaum. Was
   nicht versioniert ist, kann keinen anderen ueberraschen.
2. **Der Ordnername entscheidet nicht.** Beide heissen "UE_5.8"; getrennt
   werden sie erst durch die Patch-Nummer aus `Engine/Build/Build.version`.
   Die kanonische Engine wird darum zusaetzlich gegen `EngineAssociation`
   des .uproject geprueft.
3. **Erklaerender Text darf beide nennen.** AGENTS.md und NEUER_PC.md
   BESCHREIBEN die Falle - sie zu verbieten hiesse, die Warnung zu
   verbieten. Solche Dateien stehen namentlich in AUSNAHMEN, mit Grund.
"""
import argparse
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from engine import KANONISCH, pruefen  # noqa: E402

WURZEL = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Plausibilitaets-Untergrenze fuer die Dateiliste (siehe verfolgte_dateien).
MINDESTENS_DATEIEN = 100

# Dateien, die ABSICHTLICH beide Engines nennen. Jede braucht einen Grund -
# eine Ausnahmeliste ohne Begruendung waechst, bis sie nichts mehr prueft.
AUSNAHMEN = {
    # Fliesstext, kein Kommentarzeichen davor. Diese Datei BESCHREIBT die
    # Falle fuer den naechsten Agenten; sie zu verbieten hiesse, vor der
    # Falle zu schweigen.
    "AGENTS.md": "beschreibt die Falle im Fliesstext",

    # Eine Umzugstabelle MUSS den alten Pfad nennen - er steht dort auf
    # der Quellseite und wird gerade auf den kanonischen abgebildet.
    # (Die Abbildung zeigte bis zum 21.09.2026 in die falsche Richtung.)
    "Tools/fix_paths_neuer_pc.mjs": "Umzugstabelle: alter Pfad ist die Quelle",

    # Pruefdaten. Dieser Test MUSS eine fremde Engine enthalten - sonst
    # kann er nicht zeigen, dass der Waechter sie findet. Fiel selbst
    # erst auf, nachdem er committet war: siehe verfolgte_dateien().
    "Tools/test_pruefe_engine.py": "Testdaten: die fremde Engine ist der Pruefgegenstand",

    # Die eine Quelle selbst. Ihr Docstring stellt BEIDE Engines
    # nebeneinander - das ist der ganze Zweck der Datei. Ein Docstring
    # ist kein Kommentar im Sinne von ist_kommentar(); er beginnt nicht
    # mit einem Kommentarzeichen.
    "Tools/engine.py": "die kanonische Quelle stellt beide Engines gegenueber",
}

# KEIN Backslash im Muster - die Zeile wird vor der Suche normalisiert.
#
# Die erste Fassung trennte Laufwerk und Weg mit einer Klasse aus beiden
# Schraegstrichen. Auf dem Weg durch die Shell verlor sie eine Ebene und
# wurde zu 'nur Vorwaerts-Schraegstrich' - danach fand das Muster KEINEN
# einzigen Windows-Pfad mehr und meldete zufrieden 'alle Werkzeuge zeigen
# auf dieselbe Engine'. Ein Waechter, der nichts findet, sieht genauso aus
# wie einer, der nichts zu finden hat. Darum hier gar kein Backslash mehr,
# und ein Test, der eine bekannte Abweichung finden MUSS.
# Die beiden Anfuehrungszeichen kommen aus chr(): eines davon woertlich in
# die Zeile zu schreiben wuerde den umschliessenden String beenden.
_ANFUEHRUNG = chr(34) + chr(39)
MUSTER = re.compile(
    "[A-Za-z]:/[^" + _ANFUEHRUNG + "<>|]*?UE_[0-9]+[.][0-9]+", re.IGNORECASE)


# Ein KOMMENTAR darf jede Engine nennen - er warnt ja gerade vor ihr.
# Verbieten hiesse, die Warnung zu verbieten. Gesucht wird darum nur, was
# ausgefuehrt wird. Das ersetzt den grossen Teil der Ausnahmeliste: eine
# Liste von Dateinamen veraltet, eine Regel nicht.
# OHNE Regex - und das hat einen gemessenen Grund.
#
# Die erste Fassung benutzte ein Muster mit Wortgrenze hinter REM. Auf dem
# Weg durch die Shell wurde daraus REM + Zeichen 0x08 (Backspace): ein
# unsichtbares Byte, das grep brav als 'REM' anzeigt und gegen das keine
# REM-Zeile je passt. Der Waechter hielt danach JEDE Kommentarzeile fuer
# Code. Sichtbar wurde es erst an repr(muster.pattern).
#
# Ein Vergleich ohne Sonderzeichen kann so nicht kaputtgehen.
KOMMENTAR_ZEICHEN = ("#", "::", "//", "--", "<!--", "*", "/*")


def ist_kommentar(zeile):
    """Kommentarzeile? Sie darf jede Engine nennen - sie warnt ja vor ihr."""
    s = zeile.lstrip()
    if s[:3].upper() == "REM" and (len(s) == 3 or not s[3].isalnum()):
        return True
    return s.startswith(KOMMENTAR_ZEICHEN)


def _norm(pfad):
    """Fuer den Vergleich: Trenner vereinheitlichen, Gross/Klein egal.

    MEHRFACHE Trenner werden zusammengezogen. In JS- und Shell-Quelltext
    steht ein Windows-Backslash doppelt; ohne diesen Schritt wurde daraus
    "C://Program Files//..." und der Waechter meldete die KANONISCHE
    Engine als Abweichler. Ein Waechter, der Richtiges anmeckert, wird
    genauso schnell abgeschaltet wie einer, der nichts findet.
    """
    p = pfad.replace(chr(92), "/")
    while "//" in p:
        p = p.replace("//", "/")
    return p.rstrip("/").lower()


def verfolgte_dateien(zusaetzlich=None):
    """Verfolgte UND vorgemerkte Dateien.

    `git ls-files` allein sieht nur, was schon versioniert ist. Eine NEUE
    Datei faellt damit erst auf, NACHDEM sie committet wurde - dieser
    Waechter meldete seine eigene Testdatei genau einen Commit zu spaet.
    Fuer einen Pre-Commit-Hook waere das wertlos: er soll ja gerade das
    pruefen, was gleich hineinwandert.
    """
    namen = []

    # VORGEMERKTE DATEIEN KOENNEN UEBERGEBEN WERDEN.
    #
    # Der Aufrufer weiss es oft besser als ein eigener git-Aufruf: laeuft
    # dieser Waechter aus einem pre-commit-Hook, committet git gerade einen
    # TEMPORAEREN Index (`git commit --only` legt einen an). Wer ihn sehen
    # will, braucht GIT_INDEX_FILE - und genau das darf hier nicht geerbt
    # werden, weil ein Unterprozess sonst in den laufenden Commit schreibt.
    #
    # Aufloesung: der Hook-Laeufer liest die Liste MIT der Umgebung und
    # reicht sie hier ALS ARGUMENT herein. Die Abdichtung bleibt, die Liste
    # stimmt trotzdem.
    abfragen = [["ls-files", "-z"]]
    if zusaetzlich is None:
        abfragen.append(["diff", "--cached", "--name-only", "-z"])
    else:
        namen += list(zusaetzlich)

    for args in abfragen:
        roh = subprocess.run(["git", *args], cwd=WURZEL, capture_output=True)
        if roh.returncode != 0:
            # NICHT still weitermachen. Schlaegt git fehl (nicht im PATH,
            # kein Repo, exportierter Baum), waere die Dateiliste leer - und
            # der Waechter meldete "alle Werkzeuge zeigen auf dieselbe
            # Engine", weil er keine einzige angesehen hat. Genau davor
            # warnt der Kopf dieser Datei dreimal; die Warnung galt bisher
            # fuer das Muster, nicht fuer die Beschaffung.
            raise RuntimeError(
                "git %s fehlgeschlagen (Exit %d): %s"
                % (" ".join(args), roh.returncode,
                   roh.stderr.decode("utf-8", "replace").strip()[:200]))
        namen += [t.decode("utf-8", "surrogateescape")
                  for t in roh.stdout.split(bytes([0])) if t]
    # Reihenfolge stabil halten, Doppelte entfernen.
    gesehen, eindeutig = set(), []
    for n in namen:
        if n not in gesehen:
            gesehen.add(n)
            eindeutig.append(n)

    # UNTERGRENZE. Dieses Repo hat ueber 1500 verfolgte Dateien. Eine
    # zweistellige Liste ist kein kleines Repo, sondern ein kaputter Aufruf -
    # und ein Waechter, der zu wenig sieht, meldet trotzdem gruen.
    if len(eindeutig) < MINDESTENS_DATEIEN:
        raise RuntimeError(
            "nur %d verfolgte Dateien gefunden (erwartet mindestens %d) - "
            "der Aufruf stimmt nicht, nicht das Repo."
            % (len(eindeutig), MINDESTENS_DATEIEN))
    return eindeutig


def fundstellen(dateien=None, zusaetzlich=None):
    """[(datei, zeilennr, gefundener_pfad, ist_abweichler)] ueber alle Dateien."""
    kanon = _norm(KANONISCH)
    treffer = []
    for rel in (dateien if dateien is not None else verfolgte_dateien(zusaetzlich)):
        voll = os.path.join(WURZEL, rel)
        try:
            with open(voll, "rb") as f:
                roh = f.read()
        except OSError:
            continue
        if b"\0" in roh[:8192]:          # Binaerdatei - kein Text zum Pruefen
            continue
        try:
            text = roh.decode("utf-8")
        except UnicodeDecodeError:
            text = roh.decode("latin-1", "replace")
        for nr, zeile in enumerate(text.splitlines(), 1):
            if ist_kommentar(zeile):
                continue
            for gefunden in MUSTER.findall(zeile.replace(chr(92), '/')):
                treffer.append((rel.replace("\\", "/"), nr, gefunden,
                                _norm(gefunden) != kanon))
    return treffer


def abweichler(dateien=None, zusaetzlich=None):
    """Nur die Abweichler, ohne die begruendeten Ausnahmen."""
    return [t for t in fundstellen(dateien, zusaetzlich)
            if t[3] and t[0] not in AUSNAHMEN]


def hauptprogramm(argv=None):
    p = argparse.ArgumentParser(description="Engine-Pfade im Repo pruefen.")
    p.add_argument("--liste", action="store_true", help="jede Fundstelle zeigen")
    p.add_argument("--dateien", nargs="*", metavar="PFAD",
                   help="vorgemerkte Dateien vom Aufrufer, statt git zu fragen")
    a = p.parse_args(argv)

    ok, meldung = pruefen()
    print(("Kanonische Engine: " if ok else "FEHLER an der kanonischen Engine: ") + meldung)
    if not ok:
        return 1

    alle = fundstellen(zusaetzlich=a.dateien)
    schlimm = [t for t in alle if t[3] and t[0] not in AUSNAHMEN]

    if a.liste:
        for datei, nr, pfad, abweicht in sorted(alle):
            marke = "ABWEICHER" if (abweicht and datei not in AUSNAHMEN) else \
                    ("Ausnahme " if abweicht else "ok       ")
            print("  %s %s:%d  %s" % (marke, datei, nr, pfad))

    print("Gefunden: %d Nennungen in %d Dateien, %d begruendete Ausnahme(n)."
          % (len(alle), len({t[0] for t in alle}), len(AUSNAHMEN)))

    if schlimm:
        print("\nFEHLER: %d Nennung(en) einer ANDEREN Engine:" % len(schlimm))
        for datei, nr, pfad, _ in sorted(schlimm):
            print("  %s:%d  %s" % (datei, nr, pfad))
        print("\nKanonisch ist: %s" % KANONISCH)
        print("Entweder den Pfad dort berichtigen, oder - besser - das Skript")
        print("ruft Tools/engine.cmd bzw. Tools/engine.py und traegt gar keinen.")
        return 1

    print("Alle Werkzeuge zeigen auf dieselbe Engine.")
    return 0


if __name__ == "__main__":
    sys.exit(hauptprogramm())
