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


def drucke(text, file=None):
    """Print ohne Unicode-Absturz (Gleiche Hilfe wie vor_dem_commit.drucke)."""
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
BILDER = REPO / "docs" / "meilensteine" / "bilder"

# Fuer den Lauf gegen einen anderen Stand (z. B. die Seite von origin/main,
# waehrend der Arbeitszweig noch nicht nachgezogen ist) lassen sich beide
# Pfade ueberschreiben - sonst nennt das Werkzeug Dateien, die der andere
# Thread gerade erst angelegt hat.
def pfad_setzen(seite: str | None, bilder: str | None) -> None:
    global SEITE, BILDER
    if seite:
        SEITE = pathlib.Path(seite).resolve()
    if bilder:
        BILDER = pathlib.Path(bilder).resolve()

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
    # errors="replace": gh-Ausgabe dekodier-tolerant lesen (PEP 686 - ohne
    # Handler dekodiert der Textmodus ab 3.15 strict und stirbt still).
    r = subprocess.run(
        ["gh", *args], cwd=REPO, capture_output=True, text=True, encoding="utf-8",
        errors="replace"
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
    ap.add_argument("--seite", help="andere Meilenstein-Seite als Quelle")
    ap.add_argument("--bilder", help="anderes Bilderverzeichnis als Quelle")
    args = ap.parse_args()
    pfad_setzen(args.seite, args.bilder)

    soll = bilder_pro_meilenstein()
    fehler = False
    for num, tag in sorted(TAGS.items()):
        wollen = soll.get(num, [])
        ist = json.loads(gh("release", "view", tag, "--json", "assets"))["assets"]
        ist_namen = [a["name"] for a in ist]
        fehlend = [w for w in wollen if w not in ist_namen]
        ueberzaehlig = [i for i in ist_namen if i not in wollen]
        if not fehlend and not ueberzaehlig:
            drucke(f"M{num:02d} {tag}: passt ({len(wollen)} Bilder)")
            continue
        drucke(f"M{num:02d} {tag}:")
        for f in fehlend:
            drucke(f"   + {f}")
        for u in ueberzaehlig:
            drucke(f"   - {u} (veralteter Name, Dublette der Seite)")
        if not args.anwenden:
            continue
        for f in fehlend:
            quelle = BILDER / f
            if not quelle.exists():
                drucke(f"   FEHLER: {quelle} fehlt lokal", file=sys.stderr)
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
