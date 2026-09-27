# Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#
# Prueft die fuenf Dachaufbauten des SebboTower ohne Renderlauf: Masse,
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
# gespiegelt - im Blender-Render ist das erst auf den Nahaufnahmen zu sehen,
# und die Messung in Tools/check_sebbo_dach_bilder.py geht direkt vom
# Kontrollbild gegen die Quelltextur.

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
        soll = [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)]
        gefunden = 0
        for poly in obj.data.polygons:
            if poly.material_index != slot:
                continue
            gefunden += 1
            n = poly.normal
            pruefe(n.x > 0.99,
                   "Logo: Wortmarke zeigt zu +X (Strassenseite)")
            uvs = [tuple(uvl.data[li].uv) for li in poly.loop_indices]
            pruefe(all(abs(u[0] - s[0]) < 1e-4 and abs(u[1] - s[1]) < 1e-4
                       for u, s in zip(uvs, soll)),
                   "Logo: UV-Reihenfolge BL/BR/TR/TL (kein 180-Grad-Fehler)")
            xs = [obj.data.vertices[i].co.x for i in poly.vertices]
            pruefe(abs(min(xs) - 0.13) < 1e-4,
                   "Logo: Wortmarke 1 cm vor der Platte (x = 0,13)")
        pruefe(gefunden == 1, "Logo: genau eine Wortmarken-Flaeche (gefunden %d)"
               % gefunden)
    pruefe_felder(obj, uvl)


def pruefe_felder(obj, uvl):
    """AG-Logo und blaues Feld: Lage, Vorsprung und Feldverhaeltnis."""
    soll = [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)]
    for slotname, y_links, y_rechts, x_vor in (
            ("SbLogoAG", ns["LOGO_FELD_LINKS"], ns["LOGO_FELD_TRENNER"], 0.17),):
        slot = obj.data.materials.find(slotname)
        pruefe(slot >= 0, "Logo: Slot %s vorhanden" % slotname)
        if slot < 0:
            continue
        gefunden = 0
        for poly in obj.data.polygons:
            if poly.material_index != slot:
                continue
            gefunden += 1
            pruefe(poly.normal.x > 0.99,
                   "Logo/%s: Flaeche zeigt zu +X (Strassenseite)" % slotname)
            uvs = [tuple(uvl.data[li].uv) for li in poly.loop_indices]
            pruefe(all(abs(u[0] - s[0]) < 1e-4 and abs(u[1] - s[1]) < 1e-4
                       for u, s in zip(uvs, soll)),
                   "Logo/%s: UV-Reihenfolge BL/BR/TR/TL (kein 180-Grad-Fehler)"
                   % slotname)
            punkte = [obj.data.vertices[i].co for i in poly.vertices]
            pruefe(abs(min(p.x for p in punkte) - x_vor) < 1e-4,
                   "Logo/%s: Flaeche 1 cm vor dem blauen Feld (x = %.2f)"
                   % (slotname, x_vor))
            pruefe(abs(min(p.y for p in punkte) - y_links) < 1e-3
                   and abs(max(p.y for p in punkte) - y_rechts) < 1e-3,
                   "Logo/%s: Feld liegt bei y %.2f .. %.2f m"
                   % (slotname, y_links, y_rechts))
            breite = max(p.y for p in punkte) - min(p.y for p in punkte)
            hoehe = max(p.z for p in punkte) - min(p.z for p in punkte)
            pruefe(abs(breite / hoehe - (y_rechts - y_links) / 1.35) < 0.005,
                   "Logo/%s: Feldverhaeltnis %.4f passt zur Textur (1,1037)"
                   % (slotname, breite / hoehe))
        pruefe(gefunden == 1, "Logo/%s: genau eine Bildflaeche (gefunden %d)"
               % (slotname, gefunden))

    # Das marineblaue Feld muss 4 cm vor der Anthrazitplatte stehen, sonst
    # liegt es IN der Platte und die freigestellte Wortmarke haette ihren
    # Grund nicht.
    navy = obj.data.materials.find("SbNavy")
    pruefe(navy >= 0, "Logo: Slot SbNavy vorhanden")
    if navy >= 0:
        vorderkante = max(obj.data.vertices[i].co.x
                          for poly in obj.data.polygons
                          if poly.material_index == navy
                          for i in poly.vertices)
        pruefe(abs(vorderkante - 0.16) < 1e-4,
               "Logo: das blaue Feld steht %.2f m vor der Platte (Soll 0,16)"
               % vorderkante)
    bpy.data.objects.remove(obj, do_unlink=True)


def pruebe_magazin():
    obj, _ = ns["build_magazin"]()
    ns["boden_auf_null"](obj)
    x0, x1, y0, y1, z0, z1 = masse(obj)
    pruefe(abs(z0) < 1e-6, "Magazin: Standfuss auf z = 0")
    pruefe(1.85 <= z1 <= 2.05,
           "Magazin: Hoehe %.2f m (Soll 1,95)" % z1)
    pruefe(1.40 <= (y1 - y0) <= 1.60,
           "Magazin: Fuss breite %.2f m (Soll 1,50)" % (y1 - y0))
    pruefe(x0 < -0.30,
           "Magazin: die Strebe steht nach -X (x_min %.2f)" % x0)

    slot = obj.data.materials.find("MgCover")
    pruefe(slot >= 0, "Magazin: Slot MgCover vorhanden")
    if slot >= 0:
        uvl = obj.data.uv_layers.active
        soll = [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)]
        for poly in obj.data.polygons:
            if poly.material_index != slot:
                continue
            pruefe(poly.normal.x > 0.99, "Magazin: Cover zeigt zu +X")
            uvs = [tuple(uvl.data[li].uv) for li in poly.loop_indices]
            pruefe(all(abs(u[0] - s[0]) < 1e-4 and abs(u[1] - s[1]) < 1e-4
                       for u, s in zip(uvs, soll)),
                   "Magazin: UV-Reihenfolge BL/BR/TR/TL")
            punkte = [obj.data.vertices[i].co for i in poly.vertices]
            breite = max(p.y for p in punkte) - min(p.y for p in punkte)
            hoehe = max(p.z for p in punkte) - min(p.z for p in punkte)
            pruefe(abs(min(p.x for p in punkte) - 0.11) < 1e-4,
                   "Magazin: Cover 1 cm vor der Tafel (x = 0,11)")
            pruefe(abs(breite / hoehe - 1.30 / 1.60) < 0.005,
                   "Magazin: Tafelverhaeltnis %.4f passt zum Cover (0,8125)"
                   % (breite / hoehe))
            break
    bpy.data.objects.remove(obj, do_unlink=True)


def pruefe_pflanze():
    obj, namen = ns["build_pflanze"]()
    ns["boden_auf_null"](obj)
    x0, x1, y0, y1, z0, z1 = masse(obj)
    pruefe(abs(z0) < 1e-6, "Pflanze: Standfuss auf z = 0")
    pruefe(1.05 <= z1 <= 1.35,
           "Pflanze: Hoehe %.2f m mit Kuebel (Soll ~1,20)" % z1)
    pruefe(0.45 <= (x1 - x0) <= 0.80 and 0.40 <= (y1 - y0) <= 0.80,
           "Pflanze: Grundriss %.2f x %.2f m (Soll ~0,58 x 0,52)"
           % (x1 - x0, y1 - y0))
    # Der Kuebel muss geschlossen und nach aussen gewickelt sein (das Spiel
    # macht daraus eine Konvexhuelle), das Blattwerk besteht dagegen aus
    # einseitigen Streifen - die brauchen in Unreal ein ZWEISEITIGES
    # Material, sonst ist die Pflanze von der einen Seite aus leer.
    v = volumen(obj)
    pruefe(v > 0.0, "Pflanze: nach aussen gewickelt (V %.4f m3)" % v)
    blaetter = 0
    for name in namen:
        if name.startswith("PbBlatt") or name == "PbReif":
            blaetter += 1
        pruefe(name in ns["MATERIALS"],
               "Pflanze: Slot %s steht in der Materialtabelle" % name)
    pruefe(blaetter >= 3, "Pflanze: %d Blatt-Slots" % blaetter)
    for name, (rgb, art) in ns["MATERIALS"].items():
        if art == "foliage":
            pruefe(name.startswith("Pb"),
                   "Blattmaterial %s ist als foliage gebucht" % name)
    bpy.data.objects.remove(obj, do_unlink=True)


def pruefe_manifest():
    for name, (rgb, kind) in ns["MATERIALS"].items():
        pruefe(kind in ("paint", "metal", "dark", "decal", "foliage"),
               "Material %s hat bekannte ART (%s)" % (name, kind))
        if kind == "decal":
            pruefe(name in ns["TEXTURES"],
                   "Decal-Slot %s hat Textur-Eintrag" % name)
    for name, datei in ns["TEXTURES"].items():
        pfad = os.path.join(ns["TEX_DIR_DEFAULT"], datei)
        pruefe(os.path.exists(pfad),
               "Textur %s liegt vor (%s)" % (datei, pfad))


import bpy  # noqa: E402  (nach exec: Reihenfolge der Pruefer ist egal)

pruefe_werkzeug()
pruefe_schuessel()
pruefe_mast()
pruefe_logo()
pruebe_magazin()
pruefe_pflanze()
pruefe_manifest()

print("###WBSDCHK### Dachaufbauten: %d Fehler" % len(FEHLER))
for f in FEHLER:
    print("###WBSDCHK###   - %s" % f)
