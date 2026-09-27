"""Baut aus Bildern eine HTML-Uebersichtsseite (Kontaktbogen).

Wofuer: Messungen im Blender-/UE-Werkzeuggeschaeft sind nur dann
belastbar, wenn man das Bild daneben hat. Ein Kontaktbogen mit Bild UND
Messwert in einer Seite laesst sich anhaengen, im Chat zeigen und
weiterreichen, ohne dass jemand drei PNGs und eine TXT suchen muss.

Bilder werden als Data-URL eingebettet - so bleibt die Seite eine einzige
Datei und laesst sich verschieben, ohne dass die Verweise brechen
(Saved/ ist im Repo ohnehin nicht versioniert, ein gebrochener <img src>
waere stiller Fehlschlag: die Seite zeigt dann nur Rahmen).

Aufruf:
  python Tools/contact_sheet.py <ziel.html> --titel "..." \\
      --bild a.png --beschriftung "..." --bild b.png --beschriftung "..."
"""

import argparse
import base64
import mimetypes
import os
import sys


def data_url(pfad):
    typ = mimetypes.guess_type(pfad)[0] or "image/png"
    with open(pfad, "rb") as fh:
        return "data:%s;base64,%s" % (typ, base64.b64encode(fh.read()).decode("ascii"))


def main(argv):
    ap = argparse.ArgumentParser(description="Bilder als HTML-Kontaktbogen")
    ap.add_argument("ziel", help="zu schreibende HTML-Datei")
    ap.add_argument("--titel", default="Uebersicht")
    ap.add_argument("--bild", action="append", default=[],
                    help="Bilddatei, wiederholbar")
    ap.add_argument("--beschriftung", action="append", default=[],
                    help="Text unter dem Bild, paarweise zu --bild")
    ap.add_argument("--spalten", type=int, default=2)
    args = ap.parse_args(argv)

    beschriftungen = args.beschriftung
    if beschriftungen and len(beschriftungen) != len(args.bild):
        # Kuerzere Liste auffuellen - meistens wird nur das erste Bild
        # erklaert, der Rest ist selbsterklaerend.
        beschriftungen = (beschriftungen + [""] * len(args.bild))[:len(args.bild)]

    fehlend = [p for p in args.bild if not os.path.exists(p)]
    if fehlend:
        print("FEHLER: Bilddatei(en) fehlen: %s" % ", ".join(fehlend), file=sys.stderr)
        return 2

    karten = []
    for i, pfad in enumerate(args.bild):
        text = beschriftungen[i] if i < len(beschriftungen) else ""
        karten.append(
            "<figure><img src='%s' alt='%s'>"
            "<figcaption>%s</figcaption></figure>"
            % (data_url(pfad), os.path.basename(pfad), text))

    # Ohne %-Formatierung: die Seite enthaelt Prozentzeichen (img{width:100%}),
    # und ein nachtraegliches %-Formatieren von HTML ist eine Fehlerquelle,
    # die sich nicht beim Lesen zeigt.
    stil = (
        "body{background:#1b1d21;color:#dfe3e8;"
        "font:14px/1.5 'Segoe UI',sans-serif;margin:0;padding:14px}"
        "h1{font-size:17px;margin:0 0 12px}"
        ".gitter{display:grid;gap:12px;grid-template-columns:repeat("
        + str(max(1, args.spalten)) + ",1fr)}"
        "figure{margin:0}"
        "img{width:100%;border-radius:4px;display:block;background:#000}"
        "figcaption{color:#9aa4b0;font-size:12px;padding-top:4px;"
        "white-space:pre-wrap}"
    )
    html = (
        '<!DOCTYPE html><html lang=de><head><meta charset=utf-8>'
        '<title>' + args.titel + '</title><style>' + stil + '</style></head>'
        '<body><h1>' + args.titel + '</h1>'
        '<div class=gitter>' + "".join(karten) + '</div></body></html>'
    )

    os.makedirs(os.path.dirname(os.path.abspath(args.ziel)) or ".", exist_ok=True)
    with open(args.ziel, "w", encoding="utf-8") as fh:
        fh.write(html)
    print("OK: %s (%d Bild(er), %d Byte)"
          % (args.ziel, len(args.bild), os.path.getsize(args.ziel)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
