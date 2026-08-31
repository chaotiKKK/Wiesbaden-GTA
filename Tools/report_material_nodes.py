"""Prueft, welche Knotenarten in den Stadt-Materialien vorkommen.

Warum nicht ueber die Python-Schnittstelle der Engine: In UE 5.8 ist die
Knotenliste eines Materials von Python aus nicht lesbar ("Property
'Expressions' ... is protected"), und `MaterialEditingLibrary.get_statistics`
liefert im Kommandozeilenlauf durchweg Nullen, weil dort keine Vorschau-Shader
uebersetzt werden. Beide Wege sind geprueft und scheiden aus.

Deshalb hier der direkte Weg ueber die Datei: Der Klassenname jedes benutzten
Knotens steht in der Namenstabelle des .uasset. Gezaehlt werden kann damit
nicht - Unreal legt Namen als Zeichenkette plus getrennt gespeicherte laufende
Nummer ab, die Zeichenkette steht also genau einmal in der Datei, egal wie oft
der Knoten vorkommt. Die Aussage ist deshalb "kommt vor" / "kommt nicht vor",
und mehr wird hier auch nicht gebraucht.

Das beantwortet die eine Frage, auf die es hier ankommt: Ist das berechnete
Rauschen wirklich ueberall durch Texturabtastung ersetzt? Gemessen kostete das
Zeichnen mit den normalen Materialien 142-146 ms je Bild gegenueber 8,8 ms mit
einfarbigen - MaterialExpressionNoise wertet dreioktaviges Simplex-Rauschen je
Bildpunkt aus.

Aufruf:  py Tools/report_material_nodes.py [Materialordner]
"""

import os
import sys

MAT_DIR = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "Content", "Materials", "City")

# Laufende Nummer erzwingen, sonst zaehlt "TextureSample" auch die
# "TextureSampleParameter2D"-Knoten mit.
KINDS = [
    ("Noise", b"MaterialExpressionNoise"),
    ("Texturabtastung", b"MaterialExpressionTextureSample"),
    ("Weltposition", b"MaterialExpressionWorldPosition"),
    ("Rauschtextur", b"T_WbNoise"),
]

WITH_NOISE = []
rows = []
for name in sorted(os.listdir(MAT_DIR)):
    if not name.endswith(".uasset"):
        continue
    with open(os.path.join(MAT_DIR, name), "rb") as f:
        blob = f.read()
    marks = [pattern in blob for _, pattern in KINDS]
    if marks[0]:
        WITH_NOISE.append(name[:-7])
    if any(marks):
        rows.append((name[:-7], marks))

print("WBNODES %-30s %s" % ("Material", "  ".join(label for label, _ in KINDS)))
for name, marks in rows:
    print("WBNODES %-30s %s" % (
        name, "  ".join(("ja " if m else "-  ").ljust(len(label))
                        for m, (label, _) in zip(marks, KINDS))))

if WITH_NOISE:
    print("WBNODES ACHTUNG: berechnetes Rauschen noch in %d Material(ien): %s"
          % (len(WITH_NOISE), ", ".join(WITH_NOISE)))
else:
    print("WBNODES Kein berechnetes Rauschen mehr - alles abgetastet.")
