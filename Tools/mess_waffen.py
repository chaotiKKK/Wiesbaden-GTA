"""Misst die importierten Waffen-Assets und schreibt das Ergebnis in eine Datei.

Warum eine eigene Datei und nicht das Log: Python-Prints und unreal.log sind
in -run=pythonscript-Laeufen nicht zuverlaessig im Log (gemessen 26.09.2026,
die ###WAFFENIMP###-Marken des Importskripts fehlten komplett). Die Datei ist
der Beleg.

Erwartet: groesste Kante ~24..144 cm (Bau-Bericht). 100-fach-Werte bedeuten
einen UnitScaleFactor-Fehler im Import.

Headless: UnrealEditor-Cmd <proj> -run=pythonscript -script=<dieses Skript>
"""
import unreal

ZIEL = r"C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Saved\Diagnose\waffen_mass.txt"
NAMEN = [
    ("SM_Waffe_Pistole", 27.9),
    ("SM_Waffe_Gewehr", 66.6),
    ("SM_Waffe_MG", 86.4),
    ("SM_Waffe_Laserpistole", 24.5),
    ("SM_Waffe_Lichtschwert", 143.5),
    ("SM_Waffe_Raketenwerfer", 114.2),
    ("SM_Waffe_Granatwerfer", 48.3),
    ("SM_Waffe_Plasmacutter", 32.8),
]


def groesse(mesh):
    # get_bounding_box ist eine METHODE; die Box kennt in UE 5.8 nur
    # .min/.max - kein .size, kein get_size() (gemessen 26.09.2026 mit
    # Tools/probe_box_api.py; davor schlug dieser Aufruf bei allen acht
    # Waffen fehl).
    box = mesh.get_bounding_box()
    lo = box.min
    hi = box.max
    return (hi.x - lo.x, hi.y - lo.y, hi.z - lo.z)


zeilen = []
for name, soll in NAMEN:
    mesh = unreal.EditorAssetLibrary.load_asset("/Game/Waffen/Meshes/" + name)
    if mesh is None:
        zeilen.append("%-24s FEHLT (nicht importiert)" % name)
        continue
    try:
        x, y, z = groesse(mesh)
        kante = max(x, y, z)
        note = "ok" if abs(kante - soll) / soll <= 0.08 else "ABWEICHUNG (Soll %.1f)" % soll
        zeilen.append("%-24s %8.2f x %8.2f x %8.2f cm   %s" % (name, x, y, z, note))
    except Exception as fehler:
        zeilen.append("%-24s Messfehler: %s" % (name, fehler))

with open(ZIEL, "w", encoding="utf-8") as f:
    f.write("Masse der importierten Waffen-Meshes (cm)\n")
    f.write("\n".join(zeilen) + "\n")

unreal.SystemLibrary.quit_editor()
