#!/usr/bin/env python3
"""Benennt die Meilenstein-Bilder von den alten Reihenfolge-Praefixen auf die
Meilenstein-Nummer um und zieht die Verweise in docs/meilensteine.md nach.

Die Praefixe stammten aus der Reihenfolge, in der die Aufnahmen entstanden sind
(01-stadt-* gehoert zu Meilenstein 8, 05-kaefer-* zu Meilenstein 4 usw.). Zwei
Bildergruppen teilten sich dadurch das Praefix 01 (Stadt und Wahrzeichen).
"""
import pathlib
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parents[1]
BILDER = REPO / "docs" / "meilensteine" / "bilder"
SEITE = REPO / "docs" / "meilensteine.md"

# (alter Name, neuer Name) - die Reihenfolge ist ohne Bedeutung, nur die
# Zuordnung zaehlt. Unveraenderte Dateien stehen nicht in der Liste.
UMS = {
    "01-stadt-marktkirche.jpg": "08-stadt-marktkirche.jpg",
    "01-stadt-nerotal.jpg": "08-stadt-nerotal.jpg",
    "02-strassen-laterne.jpg": "09-strassen-laterne.jpg",
    "02-strassen-ring.jpg": "09-strassen-ring.jpg",
    "04-verkehr-kurve.jpg": "13-verkehr-kurve.jpg",
    "04-verkehr-t6.jpg": "13-verkehr-t6.jpg",
    "05-kaefer-bremsspuren.jpg": "04-kaefer-bremsspuren.jpg",
    "05-kaefer-garagenhof.jpg": "04-kaefer-garagenhof.jpg",
    "05-kaefer-nacht.jpg": "04-kaefer-nacht.jpg",
    "06-ka52-flug.jpg": "07-ka52-flug.jpg",
    "06-ka52-hof.jpg": "07-ka52-hof.jpg",
    "07-bus-haltestelle.jpg": "06-bus-haltestelle.jpg",
    "07-bus-mitfahrt.jpg": "06-bus-mitfahrt.jpg",
    "09-wetter-gewitter.jpg": "05-wetter-gewitter.jpg",
    "09-wetter-nacht.jpg": "05-wetter-nacht.jpg",
    "09-wetter-regen.jpg": "05-wetter-regen.jpg",
    "12-hq-logo.jpg": "10-hq-logo.jpg",
    "12-hq-turm.jpg": "10-hq-turm.jpg",
    "13-sebbo-ducken.jpg": "14-sebbo-ducken.jpg",
    "13-sebbo-gehen.jpg": "14-sebbo-gehen.jpg",
    "13-sebbo-rennen-blende.jpg": "14-sebbo-rennen-blende.jpg",
}


def main() -> int:
    text = SEITE.read_text(encoding="utf-8")
    umbenannt = 0
    for alt, neu in UMS.items():
        quelle = BILDER / alt
        ziel = BILDER / neu
        if not quelle.exists():
            if ziel.exists():
                print(f"uebersprungen (Ziel da): {alt}")
                continue
            print(f"FEHLT: {alt}", file=sys.stderr)
            return 1
        if ziel.exists():
            print(f"FEHLER: Ziel existiert schon: {neu}", file=sys.stderr)
            return 1
        # Verweise zuerst im Text umschreiben, dann die Datei verschieben.
        if alt not in text:
            print(f"WARNUNG: kein Verweis auf {alt} in der Seite")
        text = text.replace(f"meilensteine/bilder/{alt}", f"meilensteine/bilder/{neu}")
        subprocess.run(["git", "mv", str(quelle), str(ziel)], cwd=REPO, check=True)
        umbenannt += 1
    # newline="\n": ohne das schreibt Windows CRLF und die ganze Datei
    # erscheint im Diff als umformatiert.
    SEITE.write_text(text, encoding="utf-8", newline="\n")
    print(f"{umbenannt} Bilder umbenannt, {len(UMS)} Eintraege in der Seite nachgezogen")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
