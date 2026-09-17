"""Baut die Ansagetexte fuer die Linie-6-Halteansagen aus den echten Haltenamen
(line6.json "stop_names") und legt sie als UTF-8-JSON ab. Die eigentliche Sprach-
Ausgabe macht danach make_bus_announcements.ps1 ueber die Windows-Stimme "Hedda"
(de-DE) - der Prefix "Naechster Halt:" wird HIER (sauberes UTF-8) gesetzt, damit
im PowerShell-Skript keine Umlaut-Literale noetig sind.
"""
import json

HERE = __file__.replace("\\", "/").rsplit("/", 2)[0]  # .../WiesbadenReal
LINE = HERE + "/Data/Raw/Bus/line6.json"
OUT = HERE + "/Tools/announce_text.json"

with open(LINE, encoding="utf-8") as f:
    data = json.load(f)
names = data.get("stop_names", [])
if not names:
    raise SystemExit("line6.json hat keine stop_names - erst fetch_line6_stop_names.py laufen lassen.")

items = []
for i, name in enumerate(names):
    # "/" trennt im OSM-Namen zwei Bezeichner (z. B. "Schwalbacher Straße/LuisenForum")
    # -> als Sprechpause ", " lesen, damit es nicht als "Schrägstrich" verhaspelt.
    spoken = name.replace("/", ", ")
    items.append({
        "idx": i,
        "file": "A_%02d.wav" % i,
        "asset": "A_%02d" % i,
        "name": name,
        "text": "Nächster Halt: %s." % spoken,   # "Nächster Halt: ..."
    })

with open(OUT, "w", encoding="utf-8") as f:
    json.dump(items, f, ensure_ascii=False, indent=1)

print("Ansagetexte geschrieben: %s (%d Halte)" % (OUT, len(items)))
for it in items:
    print("  %s | %s" % (it["file"], it["text"]))
