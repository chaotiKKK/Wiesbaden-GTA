#!/usr/bin/env python3
"""Gleicht die Bild-Assets der GitHub-Releases mit der Meilenstein-Seite ab.

Soll-Menge je Release = die Bilder, die docs/meilensteine.md im Abschnitt des
Meilensteins zeigt. Fehlende Assets werden aus docs/meilensteine/bilder/
hochgeladen, Assets mit veraltetem Namen (alte Reihenfolge-Praefixe wie
05-kaefer-* in Release 4) werden entfernt - sie sind Dubletten der Seite.

Aufruf:
    python Tools/releases_bilder_ausrichten.py            # nur Plan ausgeben
    python Tools/releases_bilder_ausrichten.py --anwenden # ausfuehren
"""
import argparse
import json
import pathlib
import re
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parents[1]
SEITE = REPO / "docs" / "meilensteine.md"
BILDER = REPO / "docs" / "meilensteine" / "bilder"

TAGS = {
    1: "meilenstein-01-wahrzeichen",
    2: "meilenstein-02-nerobergbahn",
    3: "meilenstein-03-ton",
    4: "meilenstein-04-kaefer-fahrphysik",
    5: "meilenstein-05-sonne-nacht-wetter",
    6: "meilenstein-06-buslinien",
    7: "meilenstein-07-ka52-hubschrauber",
    8: "meilenstein-08-stadt-aus-amtlichen-daten",
    9: "meilenstein-09-strassen-markierungen",
    10: "meilenstein-10-sebbo-hauptsitz",
    11: "meilenstein-11-dennos-laden",
    12: "meilenstein-12-ampeln",
    13: "meilenstein-13-stadtverkehr",
    14: "meilenstein-14-sebbo-spielfigur",
}


def gh(*args: str) -> str:
    r = subprocess.run(
        ["gh", *args], cwd=REPO, capture_output=True, text=True, encoding="utf-8"
    )
    if r.returncode != 0:
        raise RuntimeError(f"gh {' '.join(args)} fehlgeschlagen: {r.stderr.strip()}")
    return r.stdout


def bilder_pro_meilenstein() -> dict[int, list[str]]:
    """Liest die Seite und sammelt je Abschnitt (## N. ...) die Bilddateinamen."""
    out: dict[int, list[str]] = {}
    aktuell = None
    for zeile in SEITE.read_text(encoding="utf-8").splitlines():
        kopf = re.match(r"^## (\d+)\.\s", zeile)
        if kopf:
            aktuell = int(kopf.group(1))
            out.setdefault(aktuell, [])
            continue
        if aktuell is None:
            continue
        for name in re.findall(r"meilensteine/bilder/([^\s)]+)", zeile):
            if name not in out[aktuell]:
                out[aktuell].append(name)
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--anwenden", action="store_true", help="Aenderungen ausfuehren")
    args = ap.parse_args()

    soll = bilder_pro_meilenstein()
    fehler = False
    for num, tag in sorted(TAGS.items()):
        wollen = soll.get(num, [])
        ist = json.loads(gh("release", "view", tag, "--json", "assets"))["assets"]
        ist_namen = [a["name"] for a in ist]
        fehlend = [w for w in wollen if w not in ist_namen]
        ueberzaehlig = [i for i in ist_namen if i not in wollen]
        if not fehlend and not ueberzaehlig:
            print(f"M{num:02d} {tag}: passt ({len(wollen)} Bilder)")
            continue
        print(f"M{num:02d} {tag}:")
        for f in fehlend:
            print(f"   + {f}")
        for u in ueberzaehlig:
            print(f"   - {u} (veralteter Name, Dublette der Seite)")
        if not args.anwenden:
            continue
        for f in fehlend:
            quelle = BILDER / f
            if not quelle.exists():
                print(f"   FEHLER: {quelle} fehlt lokal", file=sys.stderr)
                fehler = True
                continue
            gh("release", "upload", tag, str(quelle), "--clobber")
        for u in ueberzaehlig:
            gh("release", "delete-asset", tag, u, "--yes")
    if args.anwenden and fehler:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
