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

REPO = pathlib.Path(__file__).resolve().parents[1]
SEITE = REPO / "docs" / "meilensteine.md"
REPO_URL = "https://github.com/chaotiKKK/Wiesbaden-GTA"
PFAD = "docs/meilensteine.md"

sys.path.insert(0, str(REPO / "Tools"))
from releases_bilder_ausrichten import TAGS  # noqa: E402

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
    return f"{REPO_URL}/blob/main/docs/meilensteine/bilder/{name}?raw=true"


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
        koerper = koerper.replace("](meilensteine/bilder/", f"]({REPO_URL}/blob/main/docs/meilensteine/bilder/")
        koerper = re.sub(
            r"\]\([^)]*meilensteine/bilder/([^\s)]+)\)",
            lambda m: f"]({abs_url(m.group(1))})",
            koerper,
        )
        koerper = koerper.replace(
            f"]({PFAD})", f"]({REPO_URL}/blob/main/{PFAD})"
        )
        out[num] = koerper.strip()
    return out


def release_text(num: int, sha: str) -> str:
    return (
        f"## {num}. {SEITEN_TITEL[num]}\n\n"
        + abschnitte()[num]
        + f"\n\n---\n\nStand im Code: {sha} · alle Meilensteine: "
        + f"[{PFAD}]({REPO_URL}/blob/main/{PFAD})\n"
    )


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--anwenden", action="store_true")
    args = ap.parse_args()

    sha = subprocess.run(
        ["git", "rev-parse", "origin/main"],
        cwd=REPO, capture_output=True, text=True, check=True,
    ).stdout.strip()

    for num, tag in sorted(TAGS.items()):
        neu = release_text(num, sha)
        alt = subprocess.run(
            ["gh", "release", "view", tag, "--json", "body", "-q", ".body"],
            cwd=REPO, capture_output=True, text=True, encoding="utf-8",
        ).stdout
        # Der bestehende Text hat keine "#"-Ueberschrift - dieselbe Form halten.
        alt_ohne = re.sub(r"^## \d+\..*\n\n", "", alt.rstrip())
        neu_ohne = re.sub(r"^## \d+\..*\n\n", "", neu.rstrip())
        if alt_ohne.strip() == neu_ohne.strip():
            print(f"M{num:02d} {tag}: Text passt")
            continue
        print(f"M{num:02d} {tag}: Text weicht ab")
        for zeile in difflib.unified_diff(
            alt_ohne.splitlines(), neu_ohne.splitlines(),
            "ist", "soll", lineterm="", n=0,
        ):
            print("   " + zeile)
        if args.anwenden:
            # Ueber eine Datei schreiben: "--notes" mit Umlauten und Zeilenumbruechen
            # zerlegt die Shell, und diealten Texte enthalten doppelte CRs.
            notiz = REPO / "Saved" / f"release_{tag}.md"
            notiz.parent.mkdir(parents=True, exist_ok=True)
            notiz.write_text(neu, encoding="utf-8", newline="\n")
            subprocess.run(
                ["gh", "release", "edit", tag, "--notes-file", str(notiz)],
                cwd=REPO, check=True,
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
