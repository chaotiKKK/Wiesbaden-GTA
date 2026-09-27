"""ESWE-Haltestelle als echte Meshes: Wartehalle, Haltestellenmast mit H-Schild,
DFI-Stele - statt Engine-Zylinder und -Wuerfel an jeder Halte.

    "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" --background \
        --python Tools/Blender/make_eswe_haltestelle.py -- --out Data/Raw/EsweHalte

Ergebnis: je Teil eine FBX plus `eswe_haltestelle.json` (Slots, Materialien,
Masse). Den Import nach Unreal macht `Tools/import_eswe_haltestelle.py`, das
Aufstellen je Halte und Fahrtrichtung `AWiesbadenBusStopMonitor`.

VORBILD (Wiesbaden, ESWE Verkehr):
* Mast: verzinktes Rundrohr, oben das runde H-Schild (Zeichen 224: gruenes H
  auf gelbem Grund, gruener Rand), darunter das blaue ESWE-Band mit orangem
  Strich, das weisse Namensschild, die Linienschilder und der Fahrplankasten.
* Wartehalle: anthrazitfarbener Rahmen, Glasrueckwand in drei Feldern,
  Glas-Seitenwand, an der anderen Seite die beleuchtete Werbevitrine
  (City-Light), flaches Dach mit blauer Blende, Sitzbank an der Rueckwand.
* DFI: schlanke Stele mit zweiseitiger Anzeige (Texte setzt das Spiel live).

ACHSEN (im Unreal-Mesh): +X entlang der Strasse in Fahrtrichtung des Busses,
+Y vom Bordstein WEG (auf den Gehweg), +Z nach oben - gebaut wird mit +Y vom
Bordstein weg, exportiert gespiegelt (siehe main: FBX spiegelt Y zurueck), Ursprung im Fusspunkt an der
bordsteinseitigen Bezugslinie. Die Texte (Haltename, Linien, Abfahrten)
kommen NICHT ins Mesh - sie sind je Halte verschieden und entstehen im Spiel
als Textflaechen auf den weissen Schildern bzw. dem schwarzen Schirm.

Die Bauteile, Windungspruefung und den FBX-Export liefert
`make_street_furniture.py` (dieselben Fallen: Materialien in Unreal
einseitig, Blender exportiert Zentimeter, Ursprung ueber die Vertexlage).
"""
import json
import math
import os
import sys

import bpy

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_street_furniture as msf  # noqa: E402

log = msf.log

# Lineare Basisfarben, Metallic, Rauheit. `shader` waehlt im Import den Master:
# lack (deckend), glas (durchsichtig), leucht (selbstleuchtend).
MATERIALS = {
    "WbEsweRahmen": ((0.050, 0.054, 0.060), 0.8, 0.35, "lack"),     # DB 703 anthrazit
    "WbEsweSilber": ((0.420, 0.430, 0.450), 0.9, 0.30, "lack"),     # verzinkter Mast
    "WbEsweGlas":   ((0.550, 0.650, 0.680), 0.0, 0.05, "glas"),
    "WbEsweHGelb":  ((0.900, 0.640, 0.010), 0.0, 0.40, "lack"),     # RAL 1003
    "WbEsweHGruen": ((0.000, 0.190, 0.060), 0.0, 0.40, "lack"),     # RAL 6024
    "WbEsweWeiss":  ((0.820, 0.820, 0.800), 0.0, 0.45, "lack"),
    "WbEsweBlau":   ((0.000, 0.085, 0.300), 0.0, 0.35, "lack"),     # ESWE-Blau
    "WbEsweOrange": ((0.850, 0.200, 0.000), 0.0, 0.35, "lack"),     # ESWE-Orange
    "WbEsweLeucht": ((0.950, 0.900, 0.780), 0.0, 0.50, "leucht"),   # City-Light-Vitrine
    "WbEsweSchirm": ((0.008, 0.008, 0.010), 0.0, 0.10, "lack"),     # DFI-Schirm
}
for _name, (_rgb, _met, _rough, _shader) in MATERIALS.items():
    msf.MATERIALS[_name] = (_rgb, _met, _rough)


def scheibe_x(b, cy, cz, r, x0, x1, mat, seiten=32):
    """Runde Scheibe mit Achse entlang X (Schild quer zur Strasse)."""
    b.soll_volumen += math.pi * r * r * abs(x1 - x0)
    ring = [(cy + r * math.cos(2 * math.pi * i / seiten), cz + r * math.sin(2 * math.pi * i / seiten))
            for i in range(seiten)]
    for i in range(seiten):
        j = (i + 1) % seiten
        (ya, za), (yb, zb) = ring[i], ring[j]
        b.quad((x0, ya, za), (x0, yb, zb), (x1, yb, zb), (x1, ya, za), mat)
        b.quad((x1, cy, cz), (x1, ya, za), (x1, yb, zb), (x1, cy, cz), mat)
        b.quad((x0, cy, cz), (x0, yb, zb), (x0, ya, za), (x0, cy, cz), mat)


def build_wartehalle():
    b = msf.MeshBuilder()
    L, T, H = 2.10, 1.50, 2.45        # halbe Laenge, Tiefe, Traufhoehe
    R = "WbEsweRahmen"
    # Pfosten: vier Ecken + zwei in der Rueckwand
    for x in (-L + 0.03, -0.70, 0.70, L - 0.03):
        b.box(x - 0.03, x + 0.03, T - 0.06, T, 0.0, H, R)
    for x in (-L + 0.03, L - 0.03):
        b.box(x - 0.03, x + 0.03, 0.02, 0.08, 0.0, H, R)
    # Rueckwand: Riegel unten/oben + drei Glasfelder
    b.box(-L, L, T - 0.06, T, 0.06, 0.12, R)
    b.box(-L, L, T - 0.06, T, H - 0.08, H, R)
    for x0, x1 in ((-L + 0.06, -0.73), (-0.67, 0.67), (0.73, L - 0.06)):
        b.box(x0, x1, T - 0.04, T - 0.02, 0.12, H - 0.08, "WbEsweGlas")
    # Glas-Seitenwand (Fahrtrichtung vorn), vorne offen fuer den Einstieg
    b.box(L - 0.06, L, 0.35, T - 0.06, 0.06, 0.12, R)
    b.box(L - 0.06, L, 0.35, T - 0.06, H - 0.08, H, R)
    b.box(L - 0.04, L - 0.02, 0.35, T - 0.06, 0.12, H - 0.08, "WbEsweGlas")
    b.box(L - 0.06, L, 0.30, 0.36, 0.0, H, R)
    # Werbevitrine (City-Light) als hintere Seitenwand, beidseitig beleuchtet
    b.box(-L - 0.02, -L + 0.12, 0.22, T - 0.06, 0.12, 2.30, R)
    b.box(-L - 0.03, -L - 0.02, 0.30, T - 0.14, 0.24, 2.18, "WbEsweLeucht")
    b.box(-L + 0.12, -L + 0.13, 0.30, T - 0.14, 0.24, 2.18, "WbEsweLeucht")
    # Dach: flache Platte, zur Strasse ueberstehend, mit blauer Blende + orangem Strich
    b.box(-L - 0.15, L + 0.15, -0.25, T + 0.10, H, H + 0.08, R)
    b.box(-L - 0.15, L + 0.15, -0.28, -0.25, H - 0.04, H + 0.10, "WbEsweBlau")
    b.box(-L - 0.15, L + 0.15, -0.29, -0.28, H - 0.04, H - 0.01, "WbEsweOrange")
    # Sitzbank an der Rueckwand
    b.box(-1.20, 1.20, T - 0.50, T - 0.08, 0.44, 0.48, R)
    for x in (-1.00, 1.00):
        b.box(x - 0.03, x + 0.03, T - 0.30, T - 0.08, 0.0, 0.44, R)
    return b


def build_haltemast():
    b = msf.MeshBuilder()
    # Mast mit Fussmanschette
    b.zylinder(0.0, 0.0, 0.0, 2.95, 0.038, "WbEsweSilber", seiten=16)
    b.zylinder(0.0, 0.0, 0.0, 0.06, 0.060, "WbEsweRahmen", seiten=16)
    # Schilder VOR dem Mast (zum ankommenden Bus, -X): mittig auf der Mastachse
    # lief das Rohr sichtbar vorn durch H-Schild und Namensschild.
    ox = -0.055
    # H-Schild (Zeichen 224), beidseitig: gruene Grundscheibe, gelbe Flaeche, gruenes H
    hz = 2.66
    scheibe_x(b, 0.0, hz, 0.225, ox - 0.012, ox + 0.012, "WbEsweHGruen")
    for s in (1.0, -1.0):
        x0, x1 = sorted((ox + 0.012 * s, ox + 0.016 * s))
        scheibe_x(b, 0.0, hz, 0.195, x0, x1, "WbEsweHGelb")
        x0, x1 = sorted((ox + 0.016 * s, ox + 0.020 * s))
        for y in (-0.075, 0.075):
            b.box(x0, x1, y - 0.022, y + 0.022, hz - 0.115, hz + 0.115, "WbEsweHGruen")
        b.box(x0, x1, -0.053, 0.053, hz - 0.020, hz + 0.020, "WbEsweHGruen")
    # ESWE-Band (blau, oranger Strich), Namensschild, Linienschild - quer zur Strasse
    b.box(ox - 0.010, ox + 0.010, -0.24, 0.24, 2.35, 2.43, "WbEsweBlau")
    b.box(ox - 0.010, ox + 0.010, -0.24, 0.24, 2.335, 2.35, "WbEsweOrange")
    b.box(ox - 0.010, ox + 0.010, -0.24, 0.24, 2.15, 2.32, "WbEsweWeiss")
    b.box(ox - 0.010, ox + 0.010, -0.24, 0.24, 1.97, 2.13, "WbEsweWeiss")
    # Schellen, die die Schilder am Mast halten
    for z in (2.20, 2.40, 2.60):
        b.box(ox + 0.010, -0.030, -0.02, 0.02, z - 0.02, z + 0.02, "WbEsweSilber")
    # Fahrplankasten zum Gehweg
    b.box(-0.20, 0.20, 0.04, 0.10, 1.10, 1.60, "WbEsweRahmen")
    b.box(-0.18, 0.18, 0.10, 0.105, 1.12, 1.58, "WbEsweWeiss")
    return b


def build_dfi():
    b = msf.MeshBuilder()
    R = "WbEsweRahmen"
    b.box(-0.06, 0.06, -0.06, 0.06, 0.0, 1.97, R)
    b.box(-0.60, 0.60, -0.08, 0.08, 1.95, 2.60, R)
    for y0, y1 in ((-0.085, -0.080), (0.080, 0.085)):
        b.box(-0.55, 0.55, y0, y1, 2.00, 2.50, "WbEsweSchirm")
        b.box(-0.55, 0.55, y0, y1, 2.52, 2.57, "WbEsweBlau")
    b.box(-0.62, 0.62, -0.10, 0.10, 2.60, 2.63, "WbEsweBlau")
    return b


BAUPLAN = [
    ("SM_WbEsweWartehalle", build_wartehalle),
    ("SM_WbEsweHaltemast", build_haltemast),
    ("SM_WbEsweDfi", build_dfi),
]


def main():
    out_dir = msf.arg_value("--out", "Data/Raw/EsweHalte")
    if not os.path.isabs(out_dir):
        wurzel = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
        out_dir = os.path.join(wurzel, out_dir)
    os.makedirs(out_dir, exist_ok=True)
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o, do_unlink=True)

    manifest = {"meshes": [], "materials": {}}
    fehler = 0
    for name, bauer in BAUPLAN:
        b = bauer()
        # FBX spiegelt Y (Blender +Y -> Unreal -Y). Eine Spiegelung laesst sich
        # im Spiel nicht wegdrehen - MakeFromXY(Fahrt, -Seite) stellte die Halle
        # KOPFUEBER unter die Strasse. Darum hier vorab spiegeln: im Unreal-Mesh
        # liegt "vom Bordstein weg" dann auf +Y, und das Spiel dreht nur noch
        # um die Hochachse. Die Windung richtet orient_outward() wieder.
        b.verts = [(x, -y, z) for (x, y, z) in b.verts]
        if not b.windung_pruefen(name):
            fehler += 1
        b.orient_outward()
        obj = b.to_object(name)
        bpy.context.view_layer.update()
        m = msf.masse(obj)
        if m["unterkante_m"] < -0.001:
            log("FEHLER %s steht %.3f m im Boden" % (name, -m["unterkante_m"]))
            fehler += 1
        # Seitenlage fuer den Import-Check: Blender -Y (hier vom Bordstein weg)
        # muss im Unreal-Mesh auf +Y liegen, sonst steht die Halle verkehrt.
        ys = [v.co.y for v in obj.data.vertices]
        m["y_min_m"], m["y_max_m"] = round(min(ys), 3), round(max(ys), 3)
        msf.export_fbx(obj, os.path.join(out_dir, name + ".fbx"))
        manifest["meshes"].append({"name": name, "fbx": name + ".fbx", "slots": list(b.mat_names), **m})
        log("%-22s %5d Flaechen, %d Slots, %.2f m hoch, Y %.2f..%.2f"
            % (name, len(b.faces), len(b.mat_names), m["hoehe_m"], m["y_min_m"], m["y_max_m"]))
    for name, (rgb, metallic, rough, shader) in MATERIALS.items():
        manifest["materials"][name] = {"base_color": [round(c, 4) for c in rgb],
                                       "metallic": metallic, "roughness": rough, "shader": shader}
    with open(os.path.join(out_dir, "eswe_haltestelle.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)
    log("Manifest: %d Meshes, %d Materialien, %d Fehler" % (len(manifest["meshes"]), len(MATERIALS), fehler))
    return fehler


if __name__ == "__main__":
    raise SystemExit(main())
