"""Einmal-Probe: welche API liefert die Asset-Box eines StaticMesh?

Ergebnis landet in Saved/Diagnose/probe_box_api.txt (Log taugt nicht).
"""
import unreal

ZIEL = r"C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Saved\Diagnose\probe_box_api.txt"
zeilen = []

mesh = unreal.EditorAssetLibrary.load_asset("/Game/Waffen/Cutpieces/SM_Cutpiece_Unten")
zeilen.append("asset: %r" % (mesh,))
zeilen.append("typ: %s" % type(mesh).__name__)

box = mesh.get_bounding_box()
zeilen.append("box-typ: %s" % type(box).__name__)
zeilen.append("box-attribute: %s" % sorted(a for a in dir(box) if not a.startswith("_")))

for name in ("get_size", "size", "get_extent", "extent", "min", "max"):
    hat = hasattr(box, name)
    zeilen.append("hat %-10s %s" % (name, hat))
    if not hat:
        continue
    try:
        wert = getattr(box, name)
        wert = wert() if callable(wert) else wert
        zeilen.append("   -> %r  (typ %s)" % (wert, type(wert).__name__))
        zeilen.append("   -> x=%s y=%s z=%s"
                      % (getattr(wert, "x", "?"), getattr(wert, "y", "?"),
                         getattr(wert, "z", "?")))
    except Exception as fehler:
        zeilen.append("   -> Fehler: %s" % fehler)

with open(ZIEL, "w", encoding="utf-8") as f:
    f.write("\n".join(zeilen) + "\n")

unreal.SystemLibrary.quit_editor()
