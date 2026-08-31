"""Listet alle Fassadenmaterialien von ambientCG und holt ihre Vorschaubilder.

Warum ueberhaupt:

Die vier bisher verdrahteten Fassaden heissen Facade006/009/018A/020B und
tragen im Projekt die Namen Wohnhaus, Altbau, Buerohaus, Nachkrieg. Angesehen
hatte sie niemand. Tatsaechlich sind alle vier moderne Hochhaeuser - eines
davon eine Glas-Vorhangfassade, die als PUTZ verdrahtet war. Wiesbaden bekam
damit eine Skyline aus Spiegelglas.

Dieses Werkzeug laedt nur die kleinen Vorschaubilder, damit die Auswahl nach
dem INHALT getroffen werden kann und nicht nach der Nummer.

Aufruf:  py Tools/survey_facades.py
"""

import io
import json
import os
import re
import urllib.request

PROJECT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(PROJECT, "Data", "Raw", "FacadeVorschau")

# Ohne User-Agent antwortet ambientCG mit HTTP 403.
AGENT = "WiesbadenReal/1.0 (Stadtprojekt, einmaliger Materialabruf)"


def fetch(url, timeout=60):
    request = urllib.request.Request(url, headers={"User-Agent": AGENT})
    with urllib.request.urlopen(request, timeout=timeout) as r:
        return r.read()


def main():
    os.makedirs(OUT, exist_ok=True)

    url = ("https://ambientcg.com/api/v2/full_json"
           "?type=Material&limit=300&include=displayData,previewData"
           "&q=facade")
    data = json.loads(fetch(url).decode("utf-8", "replace"))
    assets = data.get("foundAssets", [])
    print("Fassaden gefunden: %d" % len(assets))

    index = []
    for asset in assets:
        asset_id = asset.get("assetId", "")
        if not asset_id.lower().startswith("facade"):
            continue

        tags = asset.get("tags") or []
        preview = ""
        previews = asset.get("previewImage") or {}
        for key in ("256-PNG", "200-JPG", "512-PNG", "1024-PNG"):
            if previews.get(key):
                preview = previews[key]
                break
        if not preview:
            continue

        path = os.path.join(OUT, "%s.png" % asset_id)
        if not os.path.exists(path):
            try:
                with io.open(path, "wb") as f:
                    f.write(fetch(preview, timeout=60))
            except Exception as exc:
                print("  %s: Vorschau fehlgeschlagen (%s)" % (asset_id, exc))
                continue

        index.append({"id": asset_id, "tags": tags})
        print("  %-14s %s" % (asset_id, ", ".join(tags[:8])))

    with io.open(os.path.join(OUT, "index.json"), "w", encoding="utf-8") as f:
        json.dump(index, f, indent=2, ensure_ascii=False)
    print("FERTIG: %d Vorschauen in %s" % (len(index), OUT))


main()
