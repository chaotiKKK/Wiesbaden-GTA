# Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
"""GATE: Release-Assets und Release-Texte gegen docs/meilensteine.md.

    python Tools/release_abgleich.py                    # Gate-Lauf
    python Tools/release_abgleich.py --quelle arbeit    # nur die Seite im Arbeitszweig
    python Tools/release_abgleich.py --ref origin/branch

WAS GEPRUEFT WIRD - und warum getrennt, was "gemessen" heisst:

1. Die Seite IM ARBEITSBAUM ist in sich stimmig: jeder Bildverweis zeigt auf
   eine Datei, jeder Meilenstein-Abschnitt hat einen Release-Tag, jeder Tag
   einen Abschnitt, die Ueberschrift passt zum Titel, den die
   Release-Texte benutzen, und jede Zeile der Uebersichtstabelle hat ein
   Ziel. Das kostet nichts und braucht kein Netz.

2. Die Releases auf GitHub passen zu der Seite, aus der sie ERZEUGT wurden -
   das ist per Definition `origin/main`, denn `releases_texte_ausrichten.py`
   schreibt den Stand von dort in den Fuss jedes Release-Texts. Wuerde das
   Gate stattdessen die Seite des Arbeitszweigs nehmen, waere es bei jedem
   Push dieses Zweigs rot, obwohl nichts kaputt ist: die Seite ist ja noch
   nicht veroeffentlicht. Der Unterschied wird deshalb als HINWEIS
   gemeldet, nicht als Fehler, samt der Handlung, die danach faellt.

3. Release-Assets = genau die Bilder, die die Seite im Abschnitt zeigt.
   Release-Text = genau der Abschnitt, den `releases_texte_ausrichten.py`
   heute erzeugen wuerde. Der Erzeuger wird dafuer wirklich aufgerufen
   (nicht nachgebaut): eine zweite Kopie seiner Regeln waere eine Stelle,
   an der beide alt werden koennen, ohne dass es jemand merkt.

EXIT-CODES - getrennt, damit "nichts gemessen" nie als "alles in Ordnung"
gelesen werden kann (dieselbe Lehre wie in verify_anchor.cmd):
    0  Alles stimmt
    1  Abweichung gefunden (jede einzelne ist im Ausdruck benannt)
    2  Die Seite im Arbeitszweig ist in sich nicht stimmig
    3  Nicht messbar: gh fehlt, ist nicht angemeldet oder antwortet nicht
    4  Die Release-Seite (--ref) laesst sich nicht lesen
Notausgang, wenn GitHub gerade nicht erreichbar ist und es trotzdem raus
muss: `git push --no-verify` oder `WB_KEINE_GATES=1`.
"""
import argparse
import contextlib
import difflib
import json
import os
import pathlib
import re
import subprocess
import sys
import tempfile
import urllib.error
import urllib.request

REPO = pathlib.Path(__file__).resolve().parents[1]
TOOLS = REPO / "Tools"
sys.path.insert(0, str(TOOLS))

import releases_bilder_ausrichten as rba  # noqa: E402
import releases_texte_ausrichten as rta  # noqa: E402

SEITENPFAD = "docs/meilensteine.md"
BILDERPFAD = "docs/meilensteine/bilder"
# Im Selbsttest auf ein TEMP-Verzeichnis gestellt - das Gate darf dort nichts
# vom echten Projekt lesen, sondern nur von dem, was der Test hingelegt hat.
SEITE_ARBEIT = REPO / SEITENPFAD
BILDER_ARBEIT = REPO / BILDERPFAD
STAND_ZEILE = re.compile(r"^Stand im Code: ([0-9a-f]{7,40})", re.M)
UEBERSCHRIFT = re.compile(r"^## (\d+)\.\s+(.*)$", re.M)
BILDVERWEIS = re.compile(r"meilensteine/bilder/([^\s)]+)")
TABELLENZEILE = re.compile(r"^\|\s*(\d+)\s*\|\s*\[([^\]]+)\]\(([^)]*)\)\s*\|", re.M)


class NichtMessbar(Exception):
    """Die Release-Seite war nicht abfragbar - das ist KEIN Erfolg."""


# ---------------------------------------------------------------------------
# gh: der einzige Weg nach draussen, austauschbar fuer den Selbsttest
# ---------------------------------------------------------------------------

def gh_echt(*args):
    """(returncode, stdout, stderr) von `gh ...` im Projektordner."""
    try:
        fertig = subprocess.run(["gh", *args], cwd=REPO, capture_output=True,
                                text=True, encoding="utf-8", errors="replace")
    except FileNotFoundError:
        return 127, "", "gh nicht gefunden"
    return fertig.returncode, fertig.stdout, fertig.stderr


GH_LAUF = gh_echt  # der Selbsttest setzt das auf eine Attrappe


def gh_json(*args):
    code, raus, fehler = GH_LAUF(*args)
    if code == 127:
        raise NichtMessbar("gh ist nicht installiert "
                           "(https://cli.github.com - `gh auth login`)")
    if code != 0:
        raise NichtMessbar("`gh %s` endete mit %d: %s"
                           % (" ".join(args), code, (fehler or "").strip()[:200]))
    try:
        return json.loads(raus or "null")
    except ValueError as fehler:
        raise NichtMessbar("`gh %s` lieferte kein JSON (%s)"
                           % (" ".join(args), fehler))


def gh_text(*args):
    code, raus, fehler = GH_LAUF(*args)
    if code == 127:
        raise NichtMessbar("gh ist nicht installiert")
    if code != 0:
        raise NichtMessbar("`gh %s` endete mit %d: %s"
                           % (" ".join(args), code, (fehler or "").strip()[:200]))
    return raus


# ---------------------------------------------------------------------------
# Die Seite lesen - aus dem Arbeitsbaum oder aus einem Ref
# ---------------------------------------------------------------------------

def seite_aus_ref(ref):
    """docs/meilensteine.md aus einem Git-Ref, oder None wenn es sie nicht gibt."""
    fertig = subprocess.run(["git", "show", f"{ref}:{SEITENPFAD}"], cwd=REPO,
                            capture_output=True, text=True, encoding="utf-8",
                            errors="replace")
    if fertig.returncode != 0:
        return None
    return fertig.stdout


def ref_hat_datei(ref, pfad):
    fertig = subprocess.run(["git", "cat-file", "-e", f"{ref}:{pfad}"], cwd=REPO,
                            capture_output=True)
    return fertig.returncode == 0


def seiten_daten(text):
    """-> (titel je Nummer, Bilder je Nummer, Nummern der Uebersichtstabelle)."""
    titel, bilder, aktuell = {}, {}, None
    for zeile in text.splitlines():
        kopf = UEBERSCHRIFT.match(zeile)
        if kopf:
            aktuell = int(kopf.group(1))
            titel[aktuell] = kopf.group(2).strip()
            bilder.setdefault(aktuell, [])
            continue
        if aktuell is None:
            continue
        for name in BILDVERWEIS.findall(zeile):
            if name not in bilder[aktuell]:
                bilder[aktuell].append(name)
    tabelle = {int(m.group(1)) for m in TABELLENZEILE.finditer(text)}
    return titel, bilder, tabelle


# ---------------------------------------------------------------------------
# Pruefung 1: die Seite in sich
# ---------------------------------------------------------------------------

def seite_pruefen(text, datei_da):
    """Befunde der Seite. `datei_da(name)` sagt, ob ein Bild existiert."""
    titel, bilder, tabelle = seiten_daten(text)
    befunde = []

    for num, namen in sorted(bilder.items()):
        for name in namen:
            if not datei_da(name):
                befunde.append(f"M{num:02d}: Bildverweis ohne Datei: {name}")

    for num in sorted(titel):
        if num not in rba.TAGS:
            befunde.append(
                f"M{num:02d}: kein Release-Tag in releases_bilder_ausrichten.py "
                f"(TAGS) - ein neuer Meilenstein braucht dort einen Eintrag")
        elif num not in rta.SEITEN_TITEL:
            befunde.append(
                f"M{num:02d}: kein Titel in releases_texte_ausrichten.py "
                f"(SEITEN_TITEL) - der Release-Text kann nicht erzeugt werden")
        elif rta.SEITEN_TITEL[num] != titel[num]:
            befunde.append(
                f"M{num:02d}: Ueberschrift '{titel[num]}' passt nicht zum "
                f"Release-Titel '{rta.SEITEN_TITEL[num]}'")

    for num in sorted(rba.TAGS):
        if num not in titel:
            befunde.append(
                f"Release-Tag {rba.TAGS[num]} gehoert zu M{num:02d}, "
                f"das auf der Seite fehlt")

    for num in sorted(tabelle):
        if num not in titel:
            befunde.append(
                f"Uebersichtstabelle nennt M{num:02d}, es gibt keinen Abschnitt")

    return befunde, titel, bilder


@contextlib.contextmanager
def seite_als(text):
    """Die Seite temporaer als Quelle des Release-Text-Erzeugers.

    `releases_texte_ausrichten.abschnitte()` liest sein Modul-globales SEITE.
    Das Gate ruft genau diesen Erzeuger auf - eine zweite Kopie seiner
    Regeln hier waere eine Stelle, an der Erzeuger und Pruefer still
    auseinanderlaufen, ohne dass ein Test es faellt.
    """
    alt = rta.SEITE
    datei = tempfile.NamedTemporaryFile("w", suffix=".md", delete=False,
                                        encoding="utf-8", newline="\n")
    try:
        datei.write(text)
        datei.close()
        rta.SEITE = pathlib.Path(datei.name)
        yield
    finally:
        rta.SEITE = alt
        with contextlib.suppress(OSError):
            os.unlink(datei.name)


def erwarteter_text(num, text):
    """Der Release-Text, den der Erzeuger HEUTE schreiben wuerde."""
    with seite_als(text):
        return rta.release_text(num, "0" * 40)


def arbeitsvergleich(bilder_arbeit, bilder_main):
    """Bilder, die nur auf einer der beiden Seiten stehen - ein HINWEIS.

    Kein Fehler: die Releases spiegeln `origin/main`, und ein Arbeitszweig,
    der ein neues Bild einbindet, ist genau dann richtig und nicht falsch.
    Der Hinweis nennt aber die Handlung, die nach dem Merge faellt.
    """
    zeilen = []
    for num in sorted(set(bilder_arbeit) | set(bilder_main)):
        for name in bilder_arbeit.get(num, []):
            if name not in bilder_main.get(num, []):
                zeilen.append(f"  HINWEIS M{num:02d}: {name} steht auf der Seite "
                              f"im Arbeitszweig, noch nicht im Release")
        for name in bilder_main.get(num, []):
            if name not in bilder_arbeit.get(num, []):
                zeilen.append(f"  HINWEIS M{num:02d}: {name} steht im Release, "
                              f"nicht mehr auf der Seite im Arbeitszweig")
    return zeilen


# ---------------------------------------------------------------------------
# Pruefung 2: die Releases auf GitHub
# ---------------------------------------------------------------------------

OEFFENTLICH_BASIS = rta.OEFFENTLICH
BILDLINK = re.compile(r"!\[[^\]]*\]\((https?://[^)\s]+)\)")
PRIVAT = rta.PRIVAT


def anonym_erreichen(url, sekunden=20):
    """(code, groesse) eines HTTP-HEAD OHNE Anmeldung.

    Bewusst ohne Token: die ganze Frage ist, ob jemand ohne GitHub-Konto
    das Bild sieht. Ein 404/403 ist ein BEFUND (der Link ist tot), ein
    Netzfehler ist "nicht messbar" - und damit nicht "in Ordnung".
    """
    anfrage = urllib.request.Request(url, method="HEAD", headers={
        "User-Agent": "WiesbadenReal-Gate6"})
    try:
        with urllib.request.urlopen(anfrage, timeout=sekunden) as antwort:
            return antwort.status, antwort.headers.get("Content-Length")
    except urllib.error.HTTPError as fehler:
        return fehler.code, None
    except Exception as fehler:            # Timeout, DNS, TLS, keine Route
        raise NichtMessbar("oeffentlicher Abruf %s: %s"
                           % (url, str(fehler)[:120]))


# Austauschbar, damit der Selbsttest NICHT ins Netz geht. Ein Test, der
# echte raw.githubusercontent-Abrufe macht, ist ein Test, der an einem
# schlechten Tag an einer Leitung scheitert - und dann faellt er als
# "Gate kaputt" durch, obwohl das Gate genau das_RIGHT_ tun sollte.
HTTP_LAUF = anonym_erreichen


def ohne_stand(text):
    """Release-Text ohne Ueberschrift und ohne den Stand-im-Code-Fuss.

    Der Stand im Code ist ein Zeitstempel auf origin/main und wandert mit
    jedem Commit dort - waere er Teil des Vergleichs, waere das Gate nach
    jedem Merge rot, ohne dass ein Text veraltet waere. Er wird deshalb
    getrennt geprueft: vorhanden und eine echte Commit-Nummer.
    """
    koerper = re.sub(r"^## \d+\..*\n\n", "", text.lstrip())
    return STAND_ZEILE.sub("Stand im Code: <SHA>", koerper).strip()


def stand_im_code(text):
    treffer = STAND_ZEILE.search(text)
    return treffer.group(1) if treffer else None


def releases_lesen():
    """-> {tag: {"assets": [namen], "body": text}} fuer alle Releases."""
    roh = gh_json("release", "list", "--limit", "100", "--json", "tagName")
    if not isinstance(roh, list):
        raise NichtMessbar("`gh release list` lieferte keine Liste")
    out = {}
    for eintrag in roh:
        tag = eintrag.get("tagName")
        if not tag:
            continue
        daten = gh_json("release", "view", tag, "--json", "assets,body")
        out[tag] = {
            "assets": [a.get("name", "") for a in (daten.get("assets") or [])],
            "body": daten.get("body") or "",
        }
    return out


def releases_pruefen(text):
    """Befunde der Releases gegen die Seite, aus der sie erzeugt wurden."""
    _, bilder, _ = seiten_daten(text)
    befunde = []
    vorhanden = releases_lesen()

    for num, tag in sorted(rba.TAGS.items()):
        wollen = bilder.get(num, [])
        if tag not in vorhanden:
            befunde.append(f"M{num:02d} {tag}: Release fehlt ganz")
            continue

        ist = vorhanden[tag]
        ist_namen = ist["assets"]
        fehlend = [w for w in wollen if w not in ist_namen]
        ueberzaehlig = [i for i in ist_namen if i not in wollen]
        doppelt = sorted({n for n in ist_namen if ist_namen.count(n) > 1})
        for name in fehlend:
            befunde.append(f"M{num:02d} {tag}: Bild fehlt im Release: {name}")
        for name in ueberzaehlig:
            befunde.append(f"M{num:02d} {tag}: Bild im Release, nicht auf der "
                           f"Seite: {name}")
        for name in doppelt:
            befunde.append(f"M{num:02d} {tag}: Bild doppelt im Release: {name}")

        koerper = ohne_stand(ist["body"])
        if not koerper:
            befunde.append(f"M{num:02d} {tag}: Release-Text ist leer")
        else:
            soll = ohne_stand(erwarteter_text(num, text))
            if koerper != soll:
                befunde.append(f"M{num:02d} {tag}: Release-Text weicht von der "
                               f"Seite ab")
                for zeile in list(difflib.unified_diff(
                        koerper.splitlines(), soll.splitlines(),
                        "ist", "soll", lineterm="", n=0))[:12]:
                    befunde.append("      " + zeile)
        if stand_im_code(ist["body"]) is None:
            befunde.append(f"M{num:02d} {tag}: Release-Text nennt keinen "
                           f"Stand im Code")

    befunde.extend(oeffentlichkeit_pruefen(vorhanden))
    return befunde


def oeffentlichkeit_pruefen(vorhanden):
    """Bilder der Releases MUESSEN ohne GitHub-Konto abrufbar sein.

    Zwei Pruefungen, weil sie zwei verschiedene Fehlerklassen fangen:
    der Textvergleich sieht nur, was der Erzeuger heute schreibt - ein
    Link ins private Repo kann bytegleich "korrekt" aussehen, ist fuer
    Fremde aber tot. Der HTTP-HEAD sagt wiederum nur etwas ueber die
    eine abgefragte Datei; ein Bild, das es im oeffentlichen Repo nicht
    gibt, faellt erst beim Abruf auf. Deshalb Text und Netz getrennt,
    und beides mit eigener Meldung.
    """
    befunde = []
    for tag, daten in sorted(vorhanden.items()):
        koerper = daten["body"]
        if PRIVAT.search(koerper):
            befunde.append(f"{tag}: Release-Text verlinkt ins private Repo "
                           f"({PRIVAT.search(koerper).group(0)}) - ohne "
                           f"GitHub-Konto ist das ein toter Link")
        links = BILDLINK.findall(koerper)
        fremd = [l for l in links if not l.startswith(OEFFENTLICH_BASIS + "/")]
        for link in fremd:
            befunde.append(f"{tag}: Bildlink zeigt nicht ins oeffentliche "
                           f"Schaufenster: {link[:110]}")
        # EINE echte Abrufprobe je Release: der Basispfad traegt fuer alle
        # Bilder, ein 404 an genau dieser Datei heisst "die Basis ist tot".
        if links:
            code, groesse = HTTP_LAUF(links[0])
            if code != 200:
                befunde.append(f"{tag}: erstes Bild ist ohne Konto nicht "
                               f"abrufbar (HTTP {code}): {links[0][:110]}")
    return befunde


# ---------------------------------------------------------------------------
# Hauptprogramm
# ---------------------------------------------------------------------------

def arbeitsbaum_seite():
    if not SEITE_ARBEIT.exists():
        raise SystemExit(f"FEHLER: {SEITENPFAD} fehlt im Arbeitsbaum")
    return SEITE_ARBEIT.read_text(encoding="utf-8")


def hauptprogramm(argv=None):
    ap = argparse.ArgumentParser(
        description="Release-Assets und -Texte gegen docs/meilensteine.md pruefen.")
    ap.add_argument("--ref", default="origin/main",
                    help="Ref, aus dem die Releases erzeugt wurden (Vorgabe: origin/main)")
    ap.add_argument("--quelle", choices=("main", "arbeit"), default="main",
                    help="welche Seite gegen die Releases gehalten wird "
                         "(Vorgabe: main = die Seite, aus der die Releases stammen)")
    ap.add_argument("--ref-fehlt-ist-ok", action="store_true",
                    help="fehlender Ref ist kein Fehler, sondern heisst: nur "
                         "die Seite im Arbeitszweig wurde geprueft")
    args = ap.parse_args(argv)

    print("Release-Abgleich: Seite, Assets und Texte")
    print(f"  Quelle der Releases: {args.quelle} ({args.ref})")

    # --- 1. Die Seite im Arbeitszweig: immer, ohne Netz ---------------------
    arbeit_text = arbeitsbaum_seite()
    befunde, titel_arbeit, bilder_arbeit = seite_pruefen(
        arbeit_text, lambda name: (BILDER_ARBEIT / name).exists())
    if befunde:
        print("\nSEITE IM ARBEITSBAUM:")
        for zeile in befunde:
            print("   " + zeile)
        print("\n  Exit 2. Notausgang: die Seite reparieren, nicht das Gate.")
        return 2
    anzahl_bilder = sum(len(v) for v in bilder_arbeit.values())
    print(f"  Seite im Arbeitszweig stimmig: {len(titel_arbeit)} Meilensteine, "
          f"{anzahl_bilder} Bildverweise, alle Dateien da")

    if args.quelle == "arbeit":
        print("\nNur die Seite geprueft (--quelle arbeit). "
              "Die Releases wurden NICHT abgeglichen.")
        return 0

    # --- 2. Die Seite, aus der die Releases stammen --------------------------
    main_text = seite_aus_ref(args.ref)
    if main_text is None:
        print(f"\n  Die Seite {SEITENPFAD} gibt es in {args.ref} nicht "
              f"(Ref nicht geholt?)")
        if args.ref_fehlt_ist_ok:
            print("  Exit 0 auftragsgemaess (--ref-fehlt-ist-ok).")
            return 0
        print("  Exit 4: ohne diese Seite ist der Abgleich der Releases "
              "nicht moeglich.")
        return 4
    main_befunde, _, bilder_main = seite_pruefen(
        main_text, lambda name: ref_hat_datei(args.ref, f"{BILDERPFAD}/{name}"))
    if main_befunde:
        print(f"\nSEITE IN {args.ref}:")
        for zeile in main_befunde:
            print("   " + zeile)
        print("\n  Exit 2: die veroeffentlichte Seite ist in sich kaputt.")
        return 2

    # --- 3. Die Releases selbst ---------------------------------------------
    try:
        rel_befunde = releases_pruefen(main_text)
    except NichtMessbar as grund:
        print(f"\n  Releases nicht abfragbar: {grund}")
        print("  Exit 3 - ausdruecklich NICHT 'alles in Ordnung'. "
              "Notausgang: git push --no-verify")
        return 3

    # --- 4. Was der Arbeitszweig gegenueber der veroeffentlichten Seite hat --
    unterschied = arbeitsvergleich(bilder_arbeit, bilder_main)
    for zeile in unterschied:
        print(zeile)
    if unterschied:
        print("\n  Nach dem Merge: python Tools/releases_bilder_ausrichten.py --anwenden")
        print("                   python Tools/releases_texte_ausrichten.py --anwenden")

    if rel_befunde:
        print("\nRELEASES:")
        for zeile in rel_befunde:
            print("   " + zeile)
        print("\n  Exit 1. Beheben mit:")
        print("    python Tools/releases_bilder_ausrichten.py --anwenden")
        print("    python Tools/releases_texte_ausrichten.py --anwenden")
        return 1

    print("\n  Alles stimmt: Seite, Assets und Texte passen zusammen.")
    return 0


if __name__ == "__main__":
    raise SystemExit(hauptprogramm())
