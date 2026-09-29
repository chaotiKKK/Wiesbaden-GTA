#!/usr/bin/env python3
"""Erzeugt die GitHub-Release-Beschreibungen aus docs/meilensteine.md.

Jeder Release-Text ist der Abschnitt der Seite mit absoluten Bildlinks
(sonst zeigen die Bilder auf "main" in ein anderes Verzeichnis) plus einem
Fuss mit dem Stand im Code. So kann die Seite der Single Source of Truth
bleiben, ohne dass ein Release aus dem Tritt faellt.

Aufruf:
    python Tools/releases_texte_ausrichten.py             # Plan/Diff ausgeben
    python Tools/releases_texte_ausrichten.py --anwenden  # zurueckschreiben
"""
import argparse
import difflib
import pathlib
import re
import subprocess
import sys


def drucke(text, file=None):
    """Print ohne Unicode-Absturz (Gleiche Hilfe wie vor_dem_commit.drucke).

    Die Diff-Zeilen zeigen Seiten-Inhalt mit errors="replace" in sich
    (U+FFFD) - eine cp1252-Konsole darf daran nicht sterben, sonst stirbt
    der Bericht statt des Befunds.
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

REPO = pathlib.Path(__file__).resolve().parents[1]
SEITE = REPO / "docs" / "meilensteine.md"
# Wird von --seite ueberschrieben. Wichtig: dieses Modul hat sein eigenes SEITE,
# das pfad_setzen() im Bildmodul nicht erreicht - ohne den Zuweis hier laeuft
# die Textquelle still ueber den lokalen Stand, waehrend --bilder auf main zeigt.
#
# DIE LINKS MUESSEN OEFFENTLICH SEIN. Das Spiel-Repo ist privat: jeder Link
# dorthin endet fuer jemanden ohne GitHub-Konto in 404 (bzw. in der
# Anmeldemaske), und ein Release, dessen Bilder nur eingeloggte Besucher
# sehen, ist genau die tote-release, die man an zwei Tagen nicht mehr
# auffaellt. Die Bilder liegen deshalb im oeffentlichen Schaufenster-Repo
# (Tools/schaufenster.py erzeugt es aus derselben Seite), und die
# Fusszeile verweist auf die oeffentliche Seite statt auf die Quelldatei.
#
# `PRIVAT` ist die Kontrolle darauf: erlaubt ist `wiesbaden-real-meilensteine`
# (oeffentlich), verboten `Wiesbaden-GTA` (privat). Wird eine alte Zeile
# zurueckgebaut, bricht der Erzeuger ab, statt wieder still Links zu bauen,
# die nur mit Konto funktionieren.
OEFFENTLICH = "https://raw.githubusercontent.com/chaotiKKK/wiesbaden-real-meilensteine/main"
SCHaufenSTER = "https://chaotikkk.github.io/wiesbaden-real-meilensteine/"
# Die MEILENSTEIN-Releases liegen als Spiegel auch im oeffentlichen Repo, weil
# die Download-Liste im privaten Repo fuer Fremde unsichtbar ist (alle 37
# Assets lieferten am 27.09.2026 anonym 404). `releases_oeffentlich.py`
# spiegelt sie dorthin - dieselbe Textfunktion, dieselben Bilder.
OEFFENTLICHE_RELEASES = "https://github.com/chaotiKKK/wiesbaden-real-meilensteine/releases"
PRIVAT = re.compile(r"github\.com/chaotiKKK/Wiesbaden-GTA", re.I)
PFAD = "docs/meilensteine.md"

sys.path.insert(0, str(REPO / "Tools"))
from releases_bilder_ausrichten import TAGS, pfad_setzen  # noqa: E402

SEITEN_TITEL = {
    1: "Wahrzeichen",
    2: "Nerobergbahn",
    3: "Ton: Mischpult und echte Aufnahmen",
    4: "Der Käfer: echte Fahrphysik",
    5: "Echte Sonne, Nacht und Wetter",
    6: "ESWE-Buslinien 6 und 3",
    7: "Ka-52-Hubschrauber",
    8: "Wiesbaden aus amtlichen Daten",
    9: "Straßen, Gehwege und Markierungen",
    10: "Sebbo-Hauptsitz",
    11: "Dennos Laden: Café, Friseur und Lieferungen",
    12: "Ampeln mit echten Signalprogrammen",
    13: "Stadtverkehr und Passanten",
    14: "Neue Spielfigur Sebbo",
}


def abs_url(name: str) -> str:
    return f"{OEFFENTLICH}/bilder/{name}"


def abschnitte() -> dict[int, str]:
    """Abschnitt je Meilenstein: Fliesstext + Bildtabellen, ohne Ueberschrift."""
    roh = SEITE.read_text(encoding="utf-8")
    teile = re.split(r"^## ", roh, flags=re.M)
    out: dict[int, str] = {}
    for teil in teile[1:]:
        num = int(teil.split(".", 1)[0])
        koerper = teil.split("\n", 1)[1]
        # Der Trenner "---" vor der naechsten Ueberschrift gehoert nicht dazu.
        koerper = re.sub(r"\n*---\s*$", "", koerper.rstrip())
        # Bildlinks (relativ oder absolut) werden in einem Rutsch auf das
        # oeffentliche Schaufenster gezeigt - die Regex fasst beide Formen.
        koerper = re.sub(
            r"\]\([^)]*meilensteine/bilder/([^\s)]+)\)",
            lambda m: f"]({abs_url(m.group(1))})",
            koerper,
        )
        # Ein Link auf die Quelldatei im privaten Repo waere fuer jeden ohne
        # Konto tot - auch der Fuss zeigt deshalb auf das Schaufenster.
        koerper = koerper.replace(f"]({PFAD})", f"]({SCHaufenSTER})")
        out[num] = koerper.strip()
    return out


def download_zeile(num: int) -> str:
    """Wo es dieselben Bilder zum Herunterladen gibt - ohne GitHub-Konto."""
    tag = TAGS[num]
    return (f"\nBilder und GIFs zum Herunterladen, auch ohne GitHub-Konto: "
            f"{OEFFENTLICHE_RELEASES}/tag/{tag}\n")


def release_text(num: int, sha: str) -> str:
    text = (
        f"## {num}. {SEITEN_TITEL[num]}\n\n"
        + abschnitte()[num]
        + download_zeile(num)
        + f"\n---\n\nStand im Code: {sha} · alle Meilensteine: "
        + f"[Schaufenster]({SCHaufenSTER})\n"
    )
    if PRIVAT.search(text):
        raise RuntimeError(
            "Abbruch: der Release-Text enthaelt einen Link ins private Repo "
            f"({PRIVAT.search(text).group(0)}) - ohne GitHub-Konto waere er tot.")
    return text


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--anwenden", action="store_true")
    ap.add_argument("--seite", help="andere Meilenstein-Seite als Quelle")
    ap.add_argument("--bilder", help="anderes Bilderverzeichnis als Quelle")
    ap.add_argument("--sha", help="Stand im Code (Vorgabe: origin/main)")
    args = ap.parse_args()
    pfad_setzen(args.seite, args.bilder)
    if args.seite:
        global SEITE
        SEITE = pathlib.Path(args.seite).resolve()

    # errors="replace": dekodier-tolerant halten. Ohne Handler dekodiert
    # der Textmodus ab Python 3.15 (PEP 686) UTF-8/strict - ein einziges
    # Nicht-UTF-8-Byte wuerde den Reader-Thread still sterben lassen.
    sha = args.sha or subprocess.run(
        ["git", "rev-parse", "origin/main"],
        cwd=REPO, capture_output=True, text=True, errors="replace", check=True,
    ).stdout.strip()

    for num, tag in sorted(TAGS.items()):
        neu = release_text(num, sha)
        # errors="replace": gh-Ausgabe dekodier-tolerant lesen (PEP 686 -
        # ohne Handler dekodiert der Textmodus ab 3.15 strict).
        alt = subprocess.run(
            ["gh", "release", "view", tag, "--json", "body", "-q", ".body"],
            cwd=REPO, capture_output=True, text=True, encoding="utf-8",
            errors="replace",
        ).stdout
        # Der bestehende Text hat keine "#"-Ueberschrift - dieselbe Form halten.
        alt_ohne = re.sub(r"^## \d+\..*\n\n", "", alt.rstrip())
        neu_ohne = re.sub(r"^## \d+\..*\n\n", "", neu.rstrip())
        if alt_ohne.strip() == neu_ohne.strip():
            drucke(f"M{num:02d} {tag}: Text passt")
            continue
        drucke(f"M{num:02d} {tag}: Text weicht ab")
        for zeile in difflib.unified_diff(
            alt_ohne.splitlines(), neu_ohne.splitlines(),
            "ist", "soll", lineterm="", n=0,
        ):
            drucke("   " + zeile)
        if args.anwenden:
            # Ueber eine Datei schreiben: "--notes" mit Umlauten und Zeilenumbruechen
            # zerlegt die Shell, und diealten Texte enthalten doppelte CRs.
            notiz = REPO / "Saved" / f"release_{tag}.md"
            if args.seite:
                notiz = pathlib.Path(args.seite).resolve().parent / f"release_{tag}.md"
            notiz.parent.mkdir(parents=True, exist_ok=True)
            notiz.write_text(neu, encoding="utf-8", newline="\n")
            subprocess.run(
                ["gh", "release", "edit", tag, "--notes-file", str(notiz)],
                cwd=REPO, check=True,
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
