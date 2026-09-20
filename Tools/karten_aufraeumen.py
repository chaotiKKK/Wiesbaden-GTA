"""Alte Karten wegraeumen - mit Rueckfrage, nie stillschweigend.

WOFUER: Jeder Bake legt eine neue Stadtkarte an und laesst die alte stehen.
Das ist richtig so (ein misslungener Bau darf die gespielte Stadt nicht
ueberschreiben), aber es summiert sich: JE Karte 1,9 GB externe Actors. Dazu
kommen die Schmierlevel, die `rebuild_city.py` beim Bauen anlegt.

Behalten werden GENAU zwei: die neue Karte und ihre Vorgaengerin. Die
Vorgaengerin bleibt, weil sie der Rueckweg ist, wenn sich die neue Karte erst
im Spiel als schlecht erweist.

    python Tools/karten_aufraeumen.py                 # nur zeigen
    python Tools/karten_aufraeumen.py --loeschen      # fragt nach
    python Tools/karten_aufraeumen.py --loeschen --ja # ohne Rueckfrage

VIER SICHERUNGEN, jede aus einem dokumentierten Vorfall:

1. **Die Rueckfrage laesst sich nicht wegoptimieren.** Ohne Terminal (Bake,
   Dienst, Protokoll-Umleitung) wird NICHT geloescht, sondern der Befehl
   ausgegeben. Ein `input()` im `-unattended`-Editor haette dort ewig
   gewartet, ohne dass es jemand sieht - darum raeumt der Bake auch nicht
   selbst auf, sondern legt nur einen Vorschlag ab.
2. **Die gespielte Karte ist unantastbar.** Sie steht in `GameDefaultMap`
   (`Config/DefaultEngine.ini`) und wird auch mit `--ja` nie geloescht.
3. **"FERTIG" ist kein Beleg.** Alkis10 und Alkis11 meldeten einen
   erfolgreichen Bake und waren im Spiel nur Gras. Die Abnahme nennt dafuer
   eine unabhaengige Kennzahl: eine volle Stadt wiegt 1,8-1,9 GB externe
   Actors, ein Leerbake 1,4 GB. Wiegt die NEUE Karte zu wenig, wird nichts
   geloescht - dann ist sie der Fehler, nicht die alte.
4. **Verfolgte Dateien werden benannt.** Ein Teil der Karten liegt in git.
   Dieses Werkzeug loescht nur auf der Platte und sagt, was danach als
   Loeschung im Arbeitsbaum steht - committen ist eine eigene Entscheidung
   (Tools/ausliefern.py).
"""
import argparse
import json
import os
import shutil
import sys
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from karte import standard_karte  # noqa: E402  EINE Quelle: DefaultEngine.ini

WURZEL = Path(__file__).resolve().parent.parent
KARTEN = WURZEL / "Content" / "Maps"
EXTERNE = (WURZEL / "Content" / "__ExternalActors__" / "Maps",
           WURZEL / "Content" / "__ExternalObjects__" / "Maps")
VORSCHLAG = WURZEL / "Saved" / "Diagnose" / "bake_vorschlag.json"

# Untergrenze aus Tools/bake_abnahme.py: "eine volle Stadt wiegt 1,8-1,9 GB,
# ein Leerbake 1,4 GB". 1,7 GB liegt sicher zwischen beiden.
VOLLE_STADT_MIN_BYTES = 1.7 * 1024 ** 3


class Fehler(Exception):
    """Abbruch mit lesbarem Grund."""


def groesse(pfad):
    """Bytes eines Pfads - Datei oder Verzeichnis."""
    if pfad.is_file():
        return pfad.stat().st_size
    summe = 0
    for wurzel, _, dateien in os.walk(pfad):
        for name in dateien:
            try:
                summe += os.path.getsize(os.path.join(wurzel, name))
            except OSError:
                pass
    return summe


def lesbar(bytes_):
    for einheit in ("B", "KB", "MB", "GB"):
        if bytes_ < 1024 or einheit == "GB":
            return "%.1f %s" % (bytes_, einheit)
        bytes_ /= 1024.0
    return "%.1f GB" % bytes_


def teile_einer_karte(name):
    """Alle Pfade, die zu einer Karte gehoeren - .umap UND externe Actors."""
    teile = []
    umap = KARTEN / (name + ".umap")
    if umap.exists():
        teile.append(umap)
    for basis in EXTERNE:
        ordner = basis / name
        if ordner.exists():
            teile.append(ordner)
    return teile


def alle_karten():
    """Kartennamen unter Content/Maps (ohne Endung), sortiert."""
    if not KARTEN.exists():
        return []
    return sorted(p.stem for p in KARTEN.glob("*.umap"))


def art(name):
    """Grobe Einordnung fuer die Ausgabe."""
    if name.startswith("__StadtNeubau"):
        return "Schmierlevel des Bakes"
    if name.startswith("__"):
        return "Schmierlevel anderer Werkzeuge"
    return "Stadtkarte"


def vorschlag_lesen():
    """Was der letzte Bake hinterlassen hat: neue Karte und Vorgaengerin."""
    if not VORSCHLAG.exists():
        return None
    try:
        with open(VORSCHLAG, encoding="utf-8") as f:
            daten = json.load(f)
    except (OSError, ValueError):
        return None
    if not daten.get("neu"):
        return None
    return daten


def kurzname(pfad_oder_name):
    """'/Game/Maps/WiesbadenCity_Alkis17' -> 'WiesbadenCity_Alkis17'."""
    return str(pfad_oder_name).replace("\\", "/").rstrip("/").split("/")[-1]


def plan(neu=None, vorgaenger=None):
    """Was bleibt, was geht - ohne etwas anzufassen.

    Rueckgabe: (behalten, weg, hinweise) - weg ist eine Liste
    (name, art, bytes, [pfade]).
    """
    hinweise = []
    live = kurzname(standard_karte())

    daten = vorschlag_lesen()
    if neu is None:
        neu = kurzname(daten["neu"]) if daten else live
        if daten:
            hinweise.append("Neue Karte aus dem Bake-Vorschlag: %s" % neu)
        else:
            hinweise.append("Kein Bake-Vorschlag gefunden - die gespielte Karte "
                            "gilt als die neue: %s" % neu)
    if vorgaenger is None and daten and daten.get("vorgaenger"):
        vorgaenger = kurzname(daten["vorgaenger"])

    if vorgaenger is None:
        # Ohne Vorschlag: die juengste Stadtkarte, die nicht die neue ist.
        andere = [n for n in alle_karten() if art(n) == "Stadtkarte" and n != neu]
        andere.sort(key=lambda n: (KARTEN / (n + ".umap")).stat().st_mtime, reverse=True)
        vorgaenger = andere[0] if andere else None
        if vorgaenger:
            hinweise.append("Vorgaengerin nicht angegeben - juengste andere "
                            "Stadtkarte gewaehlt: %s" % vorgaenger)

    # Sicherung 2: die gespielte Karte bleibt IMMER.
    behalten = {n for n in (neu, vorgaenger, live) if n}

    weg = []
    for name in alle_karten():
        if name in behalten:
            continue
        teile = teile_einer_karte(name)
        weg.append((name, art(name), sum(groesse(p) for p in teile), teile))
    weg.sort(key=lambda e: -e[2])
    return sorted(behalten), weg, hinweise


def neue_karte_ist_voll(neu):
    """Sicherung 3: wiegt die neue Karte wie eine volle Stadt?

    Rueckgabe: (ok, bytes, grund). Ohne externe Actors gilt sie als NICHT
    geprueft - dann wird nicht geloescht.
    """
    ordner = [basis / neu for basis in EXTERNE if (basis / neu).exists()]
    if not ordner:
        return False, 0, ("keine externen Actors gefunden - entweder ist die "
                          "Karte nicht gebacken oder der Name stimmt nicht")
    bytes_ = sum(groesse(o) for o in ordner)
    if bytes_ < VOLLE_STADT_MIN_BYTES:
        return False, bytes_, ("nur %s externe Actors - eine volle Stadt wiegt "
                               "1,8-1,9 GB, ein Leerbake 1,4 GB" % lesbar(bytes_))
    return True, bytes_, ""


def verfolgt(pfade):
    """Welche der Pfade liegen in git? (nur zum Melden)"""
    import subprocess
    try:
        roh = subprocess.run(["git", "ls-files", "-z", "--", *[str(p) for p in pfade]],
                             cwd=WURZEL, capture_output=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError):
        return []
    return [t.decode("utf-8", "surrogateescape") for t in roh.split(b"\0") if t]


def bericht(behalten, weg, hinweise):
    for h in hinweise:
        print("  Hinweis: %s" % h)
    print("\nBLEIBT (%d):" % len(behalten))
    for name in behalten:
        print("   %-34s %s" % (name, lesbar(sum(groesse(p) for p in teile_einer_karte(name)))))
    if not weg:
        print("\nNICHTS WEGZURAEUMEN.")
        return 0
    gesamt = sum(e[2] for e in weg)
    print("\nWIRD GELOESCHT (%d, zusammen %s):" % (len(weg), lesbar(gesamt)))
    for name, welche, bytes_, teile in weg:
        print("   %-34s %10s   %s" % (name, lesbar(bytes_), welche))
        for p in teile:
            print("        %s" % p.relative_to(WURZEL))
    return gesamt


def loeschen(weg):
    entfernt = 0
    for name, _, _, teile in weg:
        for p in teile:
            try:
                if p.is_dir():
                    shutil.rmtree(p)
                else:
                    p.unlink()
                entfernt += 1
            except OSError as fehler:
                print("   FEHLER bei %s: %s" % (p, fehler))
    return entfernt


def hauptprogramm(argv=None):
    p = argparse.ArgumentParser(
        description="Alte Karten wegraeumen - behalten werden die neue und ihre Vorgaengerin.")
    p.add_argument("--neu", help="Name der neuen Karte (sonst aus dem Bake-Vorschlag)")
    p.add_argument("--vorgaenger", help="Name der Vorgaengerin (sonst aus dem Vorschlag)")
    p.add_argument("--loeschen", action="store_true", help="wirklich loeschen")
    p.add_argument("--ja", action="store_true", help="ohne Rueckfrage (fuer Skripte)")
    a = p.parse_args(argv)

    try:
        behalten, weg, hinweise = plan(a.neu, a.vorgaenger)
    except Fehler as fehler:
        print("ABBRUCH: %s" % fehler, file=sys.stderr)
        return 2

    gesamt = bericht(behalten, weg, hinweise)
    if not weg:
        return 0

    inhalte = [p for _, _, _, teile in weg for p in teile]
    in_git = verfolgt(inhalte)
    if in_git:
        print("\n%d dieser Dateien liegen in git - sie stehen danach als "
              "Loeschung im Arbeitsbaum:" % len(in_git))
        for d in in_git[:10]:
            print("   %s" % d)
        if len(in_git) > 10:
            print("   ... und %d weitere" % (len(in_git) - 10))

    if not a.loeschen:
        print("\nNichts geloescht. Zum Ausfuehren: --loeschen")
        return 0

    # Sicherung 3: die neue Karte muss wie eine volle Stadt wiegen.
    neu = kurzname(a.neu) if a.neu else (
        kurzname(vorschlag_lesen()["neu"]) if vorschlag_lesen() else kurzname(standard_karte()))
    ok, _, grund = neue_karte_ist_voll(neu)
    if not ok:
        print("\nNICHT GELOESCHT: die neue Karte %s ist nicht abgenommen - %s." % (neu, grund))
        print("Erst die Abnahme fahren: python Tools/bake_abnahme.py --neu %s" % neu)
        return 2

    # Sicherung 1: ohne Terminal wird nicht geloescht.
    if not a.ja:
        if not sys.stdin.isatty():
            print("\nNICHT GELOESCHT: kein Terminal fuer die Rueckfrage.")
            print("Von Hand bestaetigen oder im Skript --ja setzen:")
            print("   python Tools/karten_aufraeumen.py --loeschen --ja")
            return 2
        print("\n%s in %d Karte(n) werden geloescht. Das laesst sich nicht "
              "rueckgaengig machen." % (lesbar(gesamt), len(weg)))
        antwort = input("Loeschen? Zum Bestaetigen 'ja' eintippen: ").strip().lower()
        if antwort != "ja":
            print("Abgebrochen - nichts geloescht.")
            return 1

    entfernt = loeschen(weg)
    print("\n%d Eintrag/Eintraege geloescht, %s frei." % (entfernt, lesbar(gesamt)))
    return 0


if __name__ == "__main__":
    sys.exit(hauptprogramm())
