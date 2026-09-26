# Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#
# Prueft die drei Dachaufbauten des SebboTower ohne Renderlauf: Masse,
# Windung (Aussen-Normalen), Logo-UV-Reihenfolge und die Ursprungs-Invarianten,
# auf die sich der Platzer im Actor verlaesst (z = 0 am Standfuss).
#
# Aufruf:
#   "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" --background \
#       --python WiesbadenReal/Tools/Blender/check_sebbo_dach.py
#
# Warum das noetig ist: Blender zeigt nach innen gewickelte Flaechen nicht an
# (EEVEE cullt nicht), Unreal schon - ein falsch gewickeltes Bauteil faellt
# dort still aus der Reihe. Und die Logo-Flaeche traegt ihre UVs in EINER
# festen Reihenfolge (unten links, unten rechts, oben rechts, oben links);
# vertauscht man sie, steht die Wortmarke im Spiel auf dem Kopf oder
# gespiegelt - im Blender-Render ist das erst auf den Nahaufnahmen zu sehen.

import os

from mathutils import Vector

PFAD = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                    "make_sebbo_dach.py")
QUELLE = open(PFAD, "r", encoding="utf-8").read().replace("\nmain()\n", "\n")
ns = {"__name__": "make_sebbo_dach_pruef"}
exec(compile(QUELLE, PFAD, "exec"), ns)

FEHLER = []


def pruefe(bedingung, text):
    print("###WBSDCHK### %s %s" % ("ok  " if bedingung else "FEHL", text))
    if not bedingung:
        FEHLER.append(text)


def volumen(obj):
    """Vorzeichenbehaftetes Volumen: nach aussen gewickelt +V, nach innen -V."""
    summe = 0.0
    for poly in obj.data.polygons:
        punkte = [Vector(obj.data.vertices[i].co) for i in poly.vertices]
        for k in range(1, len(punkte) - 1):
            a, b, c = punkte[0], punkte[k], punkte[k + 1]
            summe += a.dot(b.cross(c)) / 6.0
    return summe


def masse(obj):
    v = [x.co for x in obj.data.vertices]
    return (min(p.x for p in v), max(p.x for p in v),
            min(p.y for p in v), max(p.y for p in v),
            min(p.z for p in v), max(p.z for p in v))


def pruefe_werkzeug():
    """Die Builder-Helfer gegen bekannte Koerper pruefen - erst dann sind die
    Aussagen zu den Bauteilen selber etwas wert."""
    b = ns["MeshBuilder"]()
    b.box(-1.0, 1.0, -1.0, 1.0, -1.0, 1.0, "SbMetall")
    obj = b.to_object("PruefWuerfel")
    pruefe(abs(volumen(obj) - 8.0) < 1e-6,
           "box() wickelt nach AUSSEN (Wuerfel 2x2x2 = +8, nicht -8)")
    bpy.data.objects.remove(obj, do_unlink=True)

    b = ns["MeshBuilder"]()
    b.quad_towards((0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0), "SbMetall",
                   (0, 0, -1))
    obj = b.to_object("PruefFlaeche")
    poly = obj.data.polygons[0]
    n = poly.normal
    pruefe(n.z < -0.99, "quad_towards() dreht die Flaeche zum Hinweis")
    bpy.data.objects.remove(obj, do_unlink=True)


def pruefe_schuessel():
    obj, _ = ns["build_schuessel"]()
    size = ns["boden_auf_null"](obj)
    x0, x1, y0, y1, z0, z1 = masse(obj)
    pruefe(abs(z0) < 1e-6, "Schuessel: Standfuss auf z = 0")
    pruefe(x0 < -0.80, "Schuessel: Schale oeffnet zu -X (x_min %.2f)" % x0)
    pruefe(1.70 <= z1 <= 2.00, "Schuessel: Hoehe %.2f m (Soll ~1,85)" % z1)
    pruefe(y1 - y0 <= 1.40, "Schuessel: Breite %.2f m begrenzt" % (y1 - y0))
    bpy.data.objects.remove(obj, do_unlink=True)


def pruefe_mast():
    obj, _ = ns["build_mast"]()
    size = ns["boden_auf_null"](obj)
    x0, x1, y0, y1, z0, z1 = masse(obj)
    pruefe(abs(z0) < 1e-6, "Mast: Standfuss auf z = 0")
    pruefe(abs(x0 + x1) < 0.06 and abs(y0 + y1) < 0.06,
           "Mast: in X/Y zentriert (Platzer setzt PosCm auf die Achse)")
    pruefe(3.80 <= z1 <= 4.10, "Mast: Hoehe %.2f m (Soll ~3,97)" % z1)
    # Gemessen 0,091 m3 (Sockel + Rohr + Querstangen + Spitze). Die Grenzen
    # fangen BEIDE Fehler ab: nach innen gewickelt waere das Volumen negativ,
    # eine drastisch falsche Geometrie deutlich zu gross.
    v = volumen(obj)
    pruefe(0.05 < v < 0.25,
           "Mast: geschlossen und nach aussen gewickelt (V %.3f m3)" % v)
    bpy.data.objects.remove(obj, do_unlink=True)


def pruefe_logo():
    obj, _ = ns["build_logo"]()
    size = ns["boden_auf_null"](obj)
    x0, x1, y0, y1, z0, z1 = masse(obj)
    pruefe(abs(z0) < 1e-6, "Logo: Standfuss auf z = 0")
    pruefe(abs((y1 - y0) - 4.80) < 0.02,
           "Logo: Plattenbreite %.2f m (Soll 4,80)" % (y1 - y0))
    pruefe(abs(z1 - 1.90) < 0.02, "Logo: Hoehe %.2f m (Soll 1,90)" % z1)

    # Wortmarke: Flaeche mit Material SbLogo, Normale +X, UVs in der
    # Reihenfolge unten links -> unten rechts -> oben rechts -> oben links.
    slot = obj.data.materials.find("SbLogo")
    pruefe(slot >= 0, "Logo: Slot SbLogo vorhanden")
    if slot >= 0:
        uvl = obj.data.uv_layers.active
        for poly in obj.data.polygons:
            if poly.material_index != slot:
                continue
            n = poly.normal
            pruefe(n.x > 0.99,
                   "Logo: Wortmarke zeigt zu +X (Strassenseite)")
            uvs = [tuple(uvl.data[li].uv) for li in poly.loop_indices]
            soll = [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)]
            pruefe(all(abs(u[0] - s[0]) < 1e-4 and abs(u[1] - s[1]) < 1e-4
                       for u, s in zip(uvs, soll)),
                   "Logo: UV-Reihenfolge BL/BR/TR/TL (kein 180-Grad-Fehler)")
            xs = [obj.data.vertices[i].co.x for i in poly.vertices]
            pruefe(abs(min(xs) - 0.13) < 1e-4,
                   "Logo: Wortmarke 1 cm vor der Platte (x = 0,13)")
            break
    bpy.data.objects.remove(obj, do_unlink=True)


def pruefe_manifest():
    for name, (rgb, kind) in ns["MATERIALS"].items():
        pruefe(kind in ("paint", "metal", "dark", "decal"),
               "Material %s hat bekannte ART (%s)" % (name, kind))
        if kind == "decal":
            pruefe(name in ns["TEXTURES"],
                   "Decal-Slot %s hat Textur-Eintrag" % name)


import bpy  # noqa: E402  (nach exec: Reihenfolge der Pruefer ist egal)

pruefe_werkzeug()
pruefe_schuessel()
pruefe_mast()
pruefe_logo()
pruefe_manifest()

print("###WBSDCHK### Dachaufbauten: %d Fehler" % len(FEHLER))
for f in FEHLER:
    print("###WBSDCHK###   - %s" % f)
