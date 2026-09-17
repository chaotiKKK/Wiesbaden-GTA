# Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#
# Prueft das Nerobergbahn-Wagen-Mesh ohne Renderlauf: Maße, Neigungsrichtung
# der Schienenkontaktlinie, Windungskonvention der Helfer und den Innenraum
# (Sitzbaenke, Buehnenboden, Fuehrerstand mit Tacho/Schauglas/Kurbel).
#
# Aufruf:
#   "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" --background \
#       --python-exit-code 3 --python Tools/Blender/check_nerobergbahn_wagen.py
#
# --python-exit-code ist PFLICHT: ohne das Flag bricht Blender bei einem
# Python-Fehler ab, meldet exit 0, und das Protokoll endet einfach mitten in
# den Pruefungen (passiert: eine Pruefung nannte eine umbenannte Variable -
# die Ausgabe sah nach "alle Pruefungen bestanden" aus).
#
# Warum das noetig ist: Blender rendert auch verdrehte Flaechen (EEVEE cullt
# nicht), Unreal-Materialien sind aber einseitig. Ein Fehler in der Windung ist
# im Blender-Vorschaubild also unsichtbar - dieser Check macht ihn sichtbar.
#
# Befund vom 2026-09-16: box() liefert ALLE sechs Flaechen mit Normale nach
# INNEN, und der FBX-Roundtrip mit den Projekt-Exporteinstellungen dreht das
# nicht um. Alle vier Nerobergbahn-Meshes folgen dieser Konvention, der neue
# Wagen bewusst ebenfalls (Details in Tools/Blender/README.md).
#
# Der Innenraum-Teil prueft in ZURUECKGEDREHTEN Koordinaten: build_wagen()
# kippt das ganze Mesh mit tilt_grade() um die Neigung der Trasse, und genau
# die ungekippten Hoehen setzt der C++-Code beim Einsteigen voraus
# (Wagenboden 0,85 m, Augen 2,45 m in WiesbadenNerobergbahn.cpp). Eine Hoehe
# im gekippten System zu messen waere um x * 19 % daneben.

import math
import os

from mathutils import Vector

PFAD = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                    "make_nerobergbahn.py")
QUELLE = open(PFAD, "r", encoding="utf-8").read().replace("\nmain()\n", "\n")
ns = {"__name__": "make_nerobergbahn_pruef"}
exec(compile(QUELLE, PFAD, "exec"), ns)

FEHLER = []


def volumen(obj):
    """Vorzeichenbehaftetes Volumen ueber die Dreieckszerlegung der Flaechen.

    Eindeutiges Maß fuer die Windung eines geschlossenen Koerpers: nach aussen
    gewickelt ergibt es +V, nach innen -V. Ein Vergleich Normalenvektor gegen
    Flaechenmittelpunkt taugt dafuer nicht, weil die Deckel eines Prismas
    senkrecht auf dem Radius stehen.
    """
    summe = 0.0
    for poly in obj.data.polygons:
        punkte = [Vector(obj.data.vertices[i].co) for i in poly.vertices]
        for k in range(1, len(punkte) - 1):
            a, b, c = punkte[0], punkte[k], punkte[k + 1]
            summe += a.dot(b.cross(c)) / 6.0
    return summe


def melde(text, ok):
    """Eine Pruefung ausgeben; Fehler werden gezaehlt und am Ende summiert."""
    if not ok:
        FEHLER.append(text)
    print("###CHECK### %-6s %s" % ("FEHLER" if not ok else "ok", text))
    return ok


def pruefe_aussen(obj, slots):
    """Maße, Radauflage/Neigung und Windung - der Stand von 2026-09-16."""
    me = obj.data
    verts = [v.co for v in me.vertices]
    print("###CHECK### Maße: X %.3f m, Y %.3f m, Z %.3f m"
          % (max(v.x for v in verts) - min(v.x for v in verts),
             max(v.y for v in verts) - min(v.y for v in verts),
             max(v.z for v in verts) - min(v.z for v in verts)))

    auflage = {}
    for ax in (-1.62, 1.62):
        kand = [v.z for v in verts if abs(v.y + 0.50) < 0.09 and abs(v.x - ax) < 0.40]
        auflage[ax] = min(kand)
        print("###CHECK### Radauflage x=%+.2f: z=%.3f" % (ax, auflage[ax]))
    print("###CHECK### Neigung der Kontaktlinie: %.1f %% (Soll %.1f %%)"
          % (100.0 * (auflage[1.62] - auflage[-1.62]) / 3.24,
             100.0 * ns["GRADE"]))
    print("###CHECK### Materialslots: %s" % (slots,))

    print("###CHECK### Wagenflaechen: %d, Volumen %+.3f m3 (negativ = Windung wie "
          "box(), also nach innen; das ist die Projektkonvention)"
          % (len(me.polygons), volumen(obj)))


def pruefe_innenraum(obj, slots):
    """Innenraum fuer den Mitfahrmodus: Bänke, Bühne, Führerstand, Kopfreiheit.

    Alle Hoehen werden in ZURUECKGEDREHTEN Koordinaten gemessen (siehe Kopf).
    Bezugshoehen (Nachbau-Referenz §4.1): Wagenboden 0,85 m ueber dem Ursprung,
    Sitzflaeche 0,45 m darueber, Oberkante Lehne 0,85 m darueber.
    """
    me = obj.data
    theta = math.atan(ns["GRADE"])
    ca, sa = math.cos(theta), math.sin(theta)

    def zurueck(p):
        """Punkt aus dem gekippten Mesh ins ungekippte System."""
        return Vector((p.x * ca + p.z * sa, p.y, -p.x * sa + p.z * ca))

    def flaechen(*namen):
        idx = []
        for n in namen:
            if n not in slots:
                melde("Materialslot %s fehlt im Mesh" % n, False)
                return []
            idx.append(slots.index(n))
        return [p for p in me.polygons if p.material_index in idx]

    def ecken(poly):
        return [zurueck(me.vertices[i].co) for i in poly.vertices]

    def mitte(poly):
        p = ecken(poly)
        return sum(p, Vector()) / len(p)

    def normal(poly):
        n = poly.normal
        return Vector((n.x * ca + n.z * sa, n.y, -n.x * sa + n.z * ca))

    def waagerecht(poly):
        return abs(normal(poly).z) > 0.9

    def weite(punkte, achse):
        """Ausdehnung der Flaeche entlang einer Achse (0 = X, 1 = Y)."""
        w = [p[achse] for p in punkte]
        return max(w) - min(w)

    def hoehe(text, ist, soll, tol=0.02):
        return melde("%s: %.3f m (Soll %.3f m%s)"
                     % (text, ist, soll,
                        "" if abs(ist - soll) <= tol
                        else ", Abweichung %+.0f mm" % (1000 * (ist - soll))),
                     abs(ist - soll) <= tol)

    # -- Wagenboden: Bezug fuer alle weiteren Hoehen -----------------------
    boden = [mitte(p).z for p in flaechen("NbBodenRot") if waagerecht(p)]
    if not boden:
        melde("Wagenboden (Slot NbBodenRot) nicht gefunden", False)
        return
    boden_z = max(boden)
    # Der Boden ist als Platte 1 cm ueber der Soll-Linie eingebaut (Deckplatte),
    # der C++-Code setzt den Fahrgast auf 0,85 m - deshalb 1 cm Toleranz.
    hoehe("Wagenboden innen (Oberkante, C++ setzt den Fahrgast auf 0,850 m)",
          boden_z, 0.850, 0.02)

    # -- Sitzbaenke ---------------------------------------------------------
    # Sitzlatten: waagerechte NbBank-Flaechen vor den Waenden (0,40..0,93 m),
    # schmal (0,16 m tief) und 0,56 m breit. Die Innenverkleidung liegt
    # senkrecht an der Wand (|y| > 0,97), die Deckenrippen oben (z > 2,5).
    latten = []
    lehnen = []
    for p in flaechen("NbBank"):
        if not waagerecht(p):
            continue
        c = mitte(p)
        tiefe = weite(ecken(p), 1)
        if 0.90 < c.z < 1.60 and 0.40 < abs(c.y) < 0.93 and tiefe < 0.25:
            latten.append(c)
        elif 1.20 < c.z < 1.80 and 0.90 < abs(c.y) < 1.00 and tiefe < 0.12:
            lehnen.append(c)
    if not latten:
        melde("Sitzlatten (NbBank) nicht gefunden", False)
        return
    hoehe("Sitzhoehe ueber Wagenboden", max(c.z for c in latten) - 0.85, 0.45)
    hoehe("Oberkante Lehne ueber Wagenboden", max(c.z for c in lehnen) - 0.85,
          0.85)
    # Vier Bankpositionen je Seite (bx = -1,42 / -0,50 / +0,50 / +1,42)
    x_pos = sorted({round(c.x, 1) for c in latten})
    melde("Sitzbaenke: %d Positionen je Seite (%s) - Soll 4"
          % (len(x_pos), ", ".join("%+.1f" % x for x in x_pos)), len(x_pos) == 4)
    # Der Mittelgang bleibt frei: der Fahrgast steht bei x = 0, y = 0. Die
    # Querbaenke beginnen bei |y| = 0,40 m, die innerste Bankkante liegt bei
    # |x| = 0,22 m - dazwischen muss der Fahrgast stehen koennen.
    gang = [p for p in me.polygons
            if any(abs(q.x) < 0.20 and abs(q.y) < 0.39 and 0.90 < q.z < 1.80
                   for q in ecken(p))]
    melde("Mittelgang frei (|x| < 0,20 m bei |y| < 0,39 m, 0,90..1,80 m): "
          "%d Flaechen - Soll 0" % len(gang), not gang)

    # -- Buehnenboden -------------------------------------------------------
    buehne = [mitte(p).z for p in flaechen("NbBuehne") if waagerecht(p)]
    if buehne:
        # Nur die Deckschicht (das Blech darunter gehoert nicht dazu).
        hoehe("Buehnenboden (Einstieg niveaugleich zum Wagenboden)",
              max(buehne), 0.85, 0.03)
    muster = flaechen("NbBuehneMuster")
    melde("Riffelmuster auf dem Buehnenboden: %d Flaechen (zwei Buehnen)"
          % len(muster), len(muster) == 2)

    # -- Kopfreiheit im Mittelgang -----------------------------------------
    # Der Mitfahrer steht mit den Augen auf 0,85 + 1,60 = 2,45 m (C++). Die
    # Kamera darf nicht in Decke oder Dachrippen stehen.
    augen = 2.45
    ueber_kopf = []
    for p in me.polygons:
        pts = ecken(p)
        # Ueberlappung der FLaeche (nicht nur ihrer Eckpunkte!) mit dem
        # Mittelgang - die Deckenrippen spannen quer ueber den ganzen Wagen
        # und haben deshalb gar keinen Eckpunkt in der Mitte.
        if (min(q.x for q in pts) > 0.35 or max(q.x for q in pts) < -0.35
                or min(q.y for q in pts) > 0.50 or max(q.y for q in pts) < -0.50):
            continue
        z0 = min(q.z for q in pts)
        if 1.80 < z0 < 2.80:
            ueber_kopf.append(z0)
    if ueber_kopf:
        tiefste = min(ueber_kopf)
        melde("Kopfreiheit im Mittelgang: %.3f m, Augenhöhe der Mitfahrkamera "
              "%.2f m (Reserve %+.0f mm)" % (tiefste, augen, 1000 * (tiefste - augen)),
              tiefste > augen + 0.02)
    else:
        melde("Decke ueber dem Mittelgang nicht gefunden", False)

    # -- Fuehrerstand: Tacho, Schauglas, Kurbel ----------------------------
    # Alles sitzt an der bergseitigen Stirnwand innen und muss im Kasten
    # liegen - sonst ragt es beim Mitfahren durch die Wand. Das Kastenende
    # wird aus dem Mesh abgeleitet (der Innenboden ist 2 cm schmaler als der
    # Kasten), NICHT aus einem Kommentar abgeschrieben: dort stand 1,95 m,
    # waehrend der Aufbau 5,40/2 - 0,85 = 1,85 m rechnet.
    kasten = max(abs(q.x) for p in flaechen("NbBodenRot") for q in ecken(p)) + 0.02
    print("###CHECK### Kastenende (aus dem Innenboden abgeleitet): %.3f m" % kasten)

    tacho = flaechen("NbTacho")
    if not tacho:
        melde("Geschwindigkeitsanzeige (Slot NbTacho) nicht gefunden", False)
    else:
        c = sum((mitte(p) for p in tacho), Vector()) / len(tacho)
        b = [q for p in tacho for q in ecken(p)]
        n = sum((normal(p) for p in tacho), Vector()).normalized()
        print("###CHECK### Tacho: Mitte (%.2f, %+.2f, %.2f), Groesse %.2f x %.2f m, "
              "Normale (%+.2f, %+.2f, %+.2f)"
              % (c.x, c.y, c.z, max(q.x for q in b) - min(q.x for q in b),
                 max(q.z for q in b) - min(q.z for q in b), n.x, n.y, n.z))
        melde("Tacho im Kasten und vor der Stirnwand (x <= %.2f)" % kasten,
              max(q.x for q in b) <= kasten + 0.01)
        melde("Tacho hoeher als 1,0 m und unter der Augenhöhe (%.2f m)" % augen,
              1.0 < c.z < augen)
        melde("Tacho zeigt in die Kabine (-X), nicht zur Wand", n.x < -0.4)

    # Das Schauglas sitzt am bergseitigen Ende NEBEN dem Stirnfenster: das
    # Fenster liegt bei |y| < 0,42, das Rohr bei y ~ 0,68, die Scheinwerfer
    # unter der Buehne liegen bei |x| > 2,7. Ohne diese Trennung misst der
    # Filter Fenster und Scheinwerfer mit und die Probe ist wertlos.
    glas = [p for p in flaechen("NbGlas")
            if 1.55 < min(q.x for q in ecken(p))
            and max(q.x for q in ecken(p)) < 1.95
            and 0.50 < abs(mitte(p).y) < 0.85]
    rot_ende = [p for p in flaechen("NbRot") if mitte(p).x > 1.50]
    # Der Schwimmer ist seit dem Umbau ein EIGENES Mesh (der Actor schiebt ihn
    # mit dem Fuellstand) - im Wagen darf kein roter Klotz mehr im Glas sitzen.
    # Sonst gaebe es zwei Schwimmer: einen festen und einen beweglichen.
    sitzend = [p for p in rot_ende
               if 0.60 < mitte(p).y < 0.76
               and 1.40 < mitte(p).z < 1.90]
    melde("kein zweiter (fester) Schwimmer im Schauglas - %d Flaechen, Soll 0"
          % len(sitzend), not sitzend)
    if not glas:
        melde("Schauglas (NbGlas an der Stirnwand) nicht gefunden", False)
    else:
        gl = [q for p in glas for q in ecken(p)]
        z0, z1 = min(q.z for q in gl), max(q.z for q in gl)
        y0, y1 = min(q.y for q in gl), max(q.y for q in gl)
        print("###CHECK### Schauglas: x %.3f..%.3f, y %.3f..%.3f, z %.3f..%.3f m"
              % (min(q.x for q in gl), max(q.x for q in gl), y0, y1, z0, z1))
        melde("Schauglas im Kasten (x <= %.2f)" % kasten,
              max(q.x for q in gl) <= kasten + 0.01)
        melde("Schauglas reicht ueber den halben Standbereich (%.2f m Hube)"
              % (z1 - z0), z1 - z0 > 0.4)
        # Das ganze Rohr muss im FREIEN Wandband liegen: darunter (unter
        # 1,34 m) verkleidet NbBank die Stirnwand 6 cm stark, darueber
        # beginnt bei 2,56 m die obere Verkleidung. Ein Rohr, das dort
        # hineinragt, ist teilweise unsichtbar.
        melde("Schauglas liegt ganz im freien Wandband (1,34..2,56 m): "
              "%.2f..%.2f m" % (z0, z1), z0 >= 1.34 and z1 <= 2.56)
        # Der Anker des Schwimmers muss im Glas liegen (der Actor schiebt ihn
        # dort entlang) und unten/oben mit dem Glas abschliessen.
        ux, uy, uz = ns["SCHWIMMER_UNTEN"]
        ox, oy, oz = ns["SCHWIMMER_OBEN"]
        melde("Schwimmerbahn im Glas: x %.3f, y %.2f, z %.2f..%.2f"
              % (ux, uy, uz, oz),
              min(q.x for q in gl) < ux < max(q.x for q in gl)
              and y0 < uy < y1 and z0 - 0.02 < uz and oz < z1 + 0.02)
        # Der Schwimmer passt ins Rohr (nicht breiter als das Glas)...
        melde("Schwimmer passt in das Glasrohr (%.0f mm gegen %.0f mm)"
              % (1000 * 0.048, 1000 * (max(q.x for q in gl) - min(q.x for q in gl))),
              max(q.x for q in gl) - min(q.x for q in gl) >= 0.048 - 0.002)
        # Vor dem Schwimmer darf KEIN Glas liegen: die Kamera steht im KABINEN-
        # raum (kleines x), eine Flaeche verdeckt also nur, wenn sie VOR der
        # Vorderkante des Schwimmers liegt. Geprueft wird die Querschnitts-
        # flaeche, die der Schwimmer auf seiner ganzen Bahn braucht (y, z).
        # Genau dieser Fall war in der ersten Fassung falsch: das Rohr war ein
        # geschlossener Kasten, der Schwimmer steckte darin.
        verdeckt = []
        for p in glas:
            gp = ecken(p)
            gy0, gy1 = min(q.y for q in gp), max(q.y for q in gp)
            gz0, gz1 = min(q.z for q in gp), max(q.z for q in gp)
            if (min(gy1, uy + 0.024) - max(gy0, uy - 0.024) > 0.005
                    and min(gz1, oz) - max(gz0, uz) > 0.005
                    and min(q.x for q in gp) < ux - 0.024 + 0.001):
                verdeckt.append(p)
        melde("Glasrohr ist vorne offen - der Schwimmer ist zu sehen "
              "(%d deckende Glasflaechen vor seiner Bahn)" % len(verdeckt),
              not verdeckt)
        # ...und das Glas liegt VOR der Blende.
        blende = [p for p in flaechen("NbCreme")
                  if mitte(p).x > 1.70 and 0.55 < mitte(p).y < 0.82]
        if blende:
            bl = [q for p in blende for q in ecken(p)]
            melde("Glasrohr liegt vor der Blende (Glas %.3f <= Blende %.3f m)"
                  % (max(q.x for q in gl), min(q.x for q in bl)),
                  max(q.x for q in gl) <= min(q.x for q in bl) + 0.001)
        else:
            melde("Blende des Schauglases nicht gefunden", False)

    # Die INNERE Skala sitzt am Wagenende; aussen traegt die Laengswand
    # dieselbe Textur bei |y| ~ 1,06 m - ohne die y-Grenze misst der Filter
    # Innen- und Aussenskala durcheinander.
    skala = [p for p in flaechen("NbSkala")
             if mitte(p).x > 1.60 and abs(mitte(p).y) < 1.0]
    melde("Wasserstandsskala neben dem Schauglas am Wagenende",
          bool(skala))
    # Die Skala ist auf die Stirnwand gemalt - und darf darin nicht STECKEN.
    # Geprueft wird nicht eine bestimmte Wandflaeche (der gelbe Fensterfries
    # liegt teils vor der Wand und hat den ersten Anlauf dieser Probe in die
    # Irre gefuehrt), sondern die Frage selbst: liegt irgendeine Flaeche VOR
    # dem Bild und deckt es ab? Genau das war beim Umbau der Fall (Bild bei
    # x = 1,845 m, Wandinnenflaeche 1,82 m) - im Kontrollbild fehlten die
    # Ziffern, keine Masspruefung schlug an.
    if not skala:
        melde("Wasserstandsskala am Wagenende nicht gefunden", False)
    else:
        sp = [q for p in skala for q in ecken(p)]
        sy0, sy1 = min(q.y for q in sp), max(q.y for q in sp)
        sz0, sz1 = min(q.z for q in sp), max(q.z for q in sp)
        sx = min(q.x for q in sp)
        sk_idx = slots.index("NbSkala")
        davor = []
        for p in me.polygons:
            if p.material_index == sk_idx:
                continue
            gp = ecken(p)
            gy0, gy1 = min(q.y for q in gp), max(q.y for q in gp)
            gz0, gz1 = min(q.z for q in gp), max(q.z for q in gp)
            # Nur die Flaeche UNMITTELBAR hinter dem Bild zaehlt (Zuschlag
            # 10 cm): die Bank steht zwar zwischen Auge und Bild, man sieht
            # ueber ihre Lehne hinweg (Auge 2,45 m, Lehne 1,70 m) - ein
            # grober AABB-Test meldet sie faelschlich als Verdeckung.
            if (min(gy1, sy1) - max(gy0, sy0) > 0.005
                    and min(gz1, sz1) - max(gz0, sz0) > 0.005
                    and max(q.x for q in gp) < sx + 0.001
                    and abs(max(q.x for q in gp) - sx) < 0.10):
                davor.append(p)
        melde("Wasserstandsskala liegt frei auf der Wand (%.3f m, %d Flaechen "
              "davor)" % (sx, len(davor)), not davor)

    # Der Kurbelarm ist ebenfalls ein eigenes Mesh; im Wagen bleibt nur der
    # Lagerbock. Er muss auf der Drehachse sitzen, sonst haengt der schwenkende
    # Arm neben seinem Lager.
    ax, ay, az = ns["KURBEL_ACHSE"]
    lager = [p for p in me.polygons
             if slots[p.material_index] == "NbMetall"
             and any(abs(q.x - ax) < 0.12 and abs(q.y - ay) < 0.12
                     and abs(q.z - az) < 0.12 for q in ecken(p))]
    melde("Lagerbock der Kurbel auf der Drehachse (%.2f, %.2f, %.2f)"
          % (ax, ay, az), bool(lager))


def pruefe_bewegliche_teile():
    """Zeiger, Schwimmer und Kurbelarm: eigene Meshes mit dem Drehpunkt im
    Ursprung und nach AUSSEN gewickelt (der Actor bewegt sie; sie stehen
    einzeln in der Welt und sind kleiner als der Wagen).

    Geprueft wird, was der C++-Code voraussetzt: der Ursprung ist der
    Drehpunkt, die Anker aus make_nerobergbahn.py liegen dort im Bauteil, und
    die eingebauten Teile bleiben in der Kabine (die Kurbel darf bei keinem
    Schwenkwinkel durch die Wand schlagen).
    """
    teile = {
        "SM_WbNbTachoZeiger": ns["build_tacho_zeiger"](),
        "SM_WbNbSchwimmer": ns["build_schwimmer"](),
        "SM_WbNbKurbel": ns["build_kurbel"](),
    }
    def rahmen(pts):
        """Bounding-Box als (min, max, Mitte) - die Mitte ist das einzige
        brauchbare Mass fuer einen Drehpunkt: der Schwerpunkt der ECKPUNKTE
        ist es nicht, weil prism_*/box() die Deckel als Faecher mit
        wiederholten Eckpunkten anlegen."""
        lo = Vector((min(p.x for p in pts), min(p.y for p in pts),
                     min(p.z for p in pts)))
        hi = Vector((max(p.x for p in pts), max(p.y for p in pts),
                     max(p.z for p in pts)))
        return lo, hi, (lo + hi) * 0.5

    for name, (obj, slots, _d) in teile.items():
        pts = [v.co for v in obj.data.vertices]
        lo, hi, _mitte = rahmen(pts)
        vol = volumen(obj)
        print("###CHECK### %s: Groesse %.3f x %.3f x %.3f m, Volumen %+.4f m3"
              % (name, hi.x - lo.x, hi.y - lo.y, hi.z - lo.z, vol))
        melde("%s nach aussen gewickelt (Volumen > 0)" % name, vol > 0.0)
        melde("%s klein genug fuer den Einbau (< 0,5 m)" % name,
              max(hi.x - lo.x, hi.y - lo.y, hi.z - lo.z) < 0.5)

    # -- Zeiger ------------------------------------------------------------
    # Drehpunkt ist die NABE, nicht die Blattmitte: ein Zeiger ist absichtlich
    # unsymmetrisch (langes Blatt, kurzes Gegengewicht).
    obj, _s, _d = teile["SM_WbNbTachoZeiger"]
    pts = [v.co for v in obj.data.vertices]
    nabe = [p for p in pts if abs(p.y) <= 0.017]
    nlo, nhi, nmitte = rahmen(nabe)
    spitze = max(p.y for p in pts)
    gegengew = min(p.y for p in pts)
    print("###CHECK### Zeiger: Spitze %.3f m, Gegengewicht %.3f m, Nabenmitte "
          "(%.1f, %.1f) mm" % (spitze, gegengew, 1000 * nmitte.x, 1000 * nmitte.y))
    melde("Zeiger dreht um die Nabe im Ursprung (|x|, |y| < 1 mm)",
          abs(nmitte.x) < 0.001 and abs(nmitte.y) < 0.001)
    melde("Zeiger dreht sich um die ganze Nabe (Blatt und Gegengewicht)",
          gegengew < 0.0 < spitze)
    # Der Zeiger muss KUERZER sein als der Radius der Scheibe (0,135 m) und
    # 4 mm davor liegen, sonst schneidet er die aufgemalte Skala.
    melde("Zeiger bleibt auf der Scheibe (Spitze %.3f m <= 0,135 m)" % spitze,
          spitze <= 0.135)
    melde("Zeiger liegt vor der Scheibe (z >= 4 mm): %.3f m"
          % min(p.z for p in pts), min(p.z for p in pts) >= 0.004)

    # -- Schwimmer ---------------------------------------------------------
    obj, _s, _d = teile["SM_WbNbSchwimmer"]
    pts = [v.co for v in obj.data.vertices]
    lo, hi, mitte = rahmen(pts)
    durch = max(p.x for p in pts) - min(p.x for p in pts)
    melde("Schwimmer mittig auf seinem Drehpunkt (|Mitte| %.1f mm)"
          % (1000.0 * mitte.length), mitte.length < 0.002)
    # Genau die lichte Weite des Glasrohrs: breiter waere er neben dem Glas zu
    # sehen, schmaler deckt er die Skalenstriche nicht ab.
    melde("Schwimmerdurchmesser %.0f mm = lichte Weite des Glases"
          % (1000.0 * durch), abs(durch - 0.048) < 0.004)

    # -- Kurbel ------------------------------------------------------------
    obj, _s, _d = teile["SM_WbNbKurbel"]
    pts = [v.co for v in obj.data.vertices]
    ax, ay, az = ns["KURBEL_ACHSE"]
    # Die Welle ist der einzige Teil um z = 0 (Arm und Griff sitzen darueber);
    # ihre Mitte muss auf dem Ursprung liegen, sonst eiert der Arm um ein
    # Achsloch, das neben der Welle sitzt.
    welle = [p for p in pts if p.z < 0.05]
    wlo, whi, wmitte = rahmen(welle)
    print("###CHECK### Kurbelwelle: x %.3f..%.3f, z %.3f..%.3f m"
          % (wlo.x, whi.x, wlo.z, whi.z))
    melde("Kurbel schwenkt um die Welle im Ursprung (|x|, |z| < 1 mm)",
          abs(wmitte.x) < 0.001 and abs(wmitte.z) < 0.001)
    x_kab = ns["WAGEN_X_KAB"]
    wand = x_kab - 0.06                      # Innenflaeche der Stirnverkleidung
    # Der Arm schwenkt um Y. Beide Endlagen muessen in der Kabine bleiben:
    # bei Drehung um Y wird x' = x cos + z sin und z' = -x sin + z cos.
    for winkel, rolle in ((-26.0, "Wasser auf (zum Bediener)"),
                          (2.0, "Wasser zu (senkrecht)")):
        rad = math.radians(winkel)
        ca, sa = math.cos(rad), math.sin(rad)
        xmax = max(ax + p.x * ca + p.z * sa for p in pts)
        zmin = min(az - p.x * sa + p.z * ca for p in pts)
        print("###CHECK### Kurbel %.0f Grad (%s): Griff bis x = %.3f m, "
              "tiefster Punkt z = %.3f m" % (winkel, rolle, xmax, zmin))
        melde("Kurbel schlaegt nicht durch die Wand (x <= %.2f m)" % wand,
              xmax <= wand)
        melde("Kurbel bleibt ueber dem Wagenboden (z >= 0,86 m)", zmin >= 0.86)
        melde("Kurbel stoert den Mittelgang nicht (|y| >= 0,45 m)",
              abs(ay) - 0.065 >= 0.45)
    melde("Kurbelarm reicht in Griffhoehe (1,2..1,7 m)",
          1.2 <= az + ns["KURBEL_LAENGE"] <= 1.7)


def pruefe_bahnsteighalle():
    """Bahnsteighalle (Tal- und Bergstation): Bezugshoehen, Stuetzen, Binder,
    Balustrade, Roste - und die Windung der neuen Balken- und Dockenhelfer.

    Die Halle ist NICHT gekippt (nur der Wagen steht in der Steigung), alle
    Hoehen sind also direkt ablesbar. Bezug ist die Schienenoberkante in
    Trassenmitte (Z = 0) - genau der Punkt, auf den der Actor sie setzt.
    """
    obj, slots, _dims = ns["build_bahnsteighalle"]()
    me = obj.data

    def flaechen(*namen):
        idx = []
        for n in namen:
            if n not in slots:
                melde("Materialslot %s fehlt in der Halle" % n, False)
                return []
            idx.append(slots.index(n))
        return [p for p in me.polygons if p.material_index in idx]

    def ecken(poly):
        return [me.vertices[i].co for i in poly.vertices]

    def mitte(poly):
        p = ecken(poly)
        return sum(p, Vector()) / len(p)

    def waagerecht(poly):
        return abs(poly.normal.z) > 0.9

    print("###CHECK### Halle: %d Flaechen, Slots %s" % (len(me.polygons), slots))
    vol = volumen(obj)
    print("###CHECK### Halle: Volumen %+.3f m3 (negativ = Konvention von box() "
          "wie Wagen und Viadukt)" % vol)
    melde("Halle nach innen gewickelt wie der Wagen", vol < 0.0)

    # -- Bezugshoehen -------------------------------------------------------
    sohle = [mitte(p).z for p in flaechen("NbBuehne") if waagerecht(p)]
    melde("Trogsohle liegt unter der Schienenoberkante (%.2f m)"
          % (min(sohle) if sohle else 99.0), bool(sohle) and min(sohle) < -0.4)

    bahnsteig = [mitte(p).z for p in flaechen("NbStein") if waagerecht(p)]
    oben = max(bahnsteig) if bahnsteig else 0.0
    print("###CHECK### Bahnsteigoberkante %.3f m (Soll %.2f m ueber "
          "Schienenoberkante)" % (oben, ns["BAHNSTEIG_H"]))
    melde("Bahnsteig auf Wagenbodenhoehe weniger eine Stufe (0,80 m)",
          abs(oben - ns["BAHNSTEIG_H"]) < 0.01)

    gelb = [mitte(p).z for p in flaechen("NbGelb") if waagerecht(p)]
    melde("gelbe Sicherheitslinie buendig mit dem Bahnsteig",
          bool(gelb) and abs(max(gelb) - ns["BAHNSTEIG_H"]) < 0.01)

    # -- Trogweite und Roste ------------------------------------------------
    wand = set()
    for p in flaechen("NbPutz"):
        m = mitte(p)
        if abs(abs(m.y) - ns["TROG_HALB"]) < 0.02:
            wand.add(round(m.y, 2))
    melde("Trogwaende bei y = +-%.2f m (lichte Weite %.2f m)"
          % (ns["TROG_HALB"], 2 * ns["TROG_HALB"]), len(wand) == 2)

    roste = [p for p in flaechen("NbGitterrost") if waagerecht(p)]
    roste_y = [abs(mitte(p).y) for p in roste]
    melde("Roste liegen im Trog neben dem Gleis (y %.2f..%.2f m)"
          % (min(roste_y) if roste_y else 0.0, max(roste_y) if roste_y else 0.0),
          bool(roste) and min(roste_y) >= 1.1 and max(roste_y) <= ns["TROG_HALB"] + 0.01)

    # -- Stuetzen -----------------------------------------------------------
    # Gezaehlt wird ueber die SOSKELPLATTEN: eine je Stuetze, waagerecht bei
    # Bahnsteighoehe. Die Flaechenmittel der uebrigen Koerper streuen quer um
    # die Achse (Mantel, Ringe, Kapitell) und taugen als Achsenschluessel nicht.
    eisen = flaechen("NbGusseisen")
    kopf = max((max(q.z for q in ecken(p)) for p in eisen), default=0.0)
    # Achsen: je Stuetze gibt es Flaechen in Stuetzennaehe (|y| ~ 2,35) und
    # nahe Bahnsteighoehe. Gezaehlt werden die X-Stellen mal zwei Bahnsteige -
    # die Flaechenmittel streuen quer um die Achse, ein Schluessel aus
    # gerundetem x UND y ergaebe deshalb dutzende Scheinachsen.
    stellen = sorted({round(mitte(p).x)
                      for p in eisen
                      if abs(abs(mitte(p).y) - ns["STUETZE_Y"]) < 0.35
                      and mitte(p).z < ns["BAHNSTEIG_H"] + 0.60})
    seiten = {1 if mitte(p).y > 0 else -1 for p in eisen
              if abs(abs(mitte(p).y) - ns["STUETZE_Y"]) < 0.35}
    print("###CHECK### Gusseisenstuetzen: Stellen %s x %d Seiten = %d Stuetzen, "
          "Kopf bei z = %.2f m"
          % (stellen, len(seiten), len(stellen) * len(seiten), kopf))
    melde("8 Stuetzen (4 Joche, beide Bahnsteige)",
          len(stellen) * len(seiten) == 8)
    melde("Stuetzenkopf endet unter der Binderunterkante (%.2f m)"
          % (ns["BAHNSTEIG_H"] + ns["STUETZE_H"]),
          bool(stellen) and kopf <= ns["BAHNSTEIG_H"] + ns["STUETZE_H"] + 0.01)

    # -- Dach ---------------------------------------------------------------
    first = max(q.z for p in flaechen("NbDach") for q in ecken(p))
    ueber = max(abs(q.y) for p in flaechen("NbDach") for q in ecken(p))
    aussen = ns["TROG_HALB"] + ns["BAHNSTEIG_B"]
    print("###CHECK### Dach: First %.2f m, Traufe bei y = %.2f m" % (first, ueber))
    melde("Dach ueberdeckt den Bahnsteig (%.2f m <= %.2f m)"
          % (ueber, aussen),
          ueber >= aussen - 0.01)
    melde("Dachueberstand mindestens 0,5 m", ueber - aussen >= 0.5)

    # -- Balustrade ---------------------------------------------------------
    gitter = flaechen("NbRautengitter")
    z0 = min(q.z for p in gitter for q in ecken(p)) if gitter else 0.0
    z1 = max(q.z for p in gitter for q in ecken(p)) if gitter else 0.0
    x_min = min(q.x for p in gitter for q in ecken(p)) if gitter else 0.0
    print("###CHECK### Rautengitter: %d Flaechen, z %.2f..%.2f m, ab x = %.2f m"
          % (len(gitter), z0, z1, x_min))
    melde("Rautengitter in Balustradenhoehe (%.2f m)"
          % ns["BALUSTER_H"],
          bool(gitter) and z0 > ns["BAHNSTEIG_H"]
          and z1 <= ns["BAHNSTEIG_H"] + ns["BALUSTER_H"] + 0.01)
    # Der Wagen steht am tiefen Hallenende und wird dort bestiegen: das erste
    # Joch bleibt frei, sonst waere der Einstieg zugebaut.
    melde("erstes Joch ohne Balustrade (Einstieg frei)",
          x_min >= -ns["HALLE_L"] * 0.5 + ns["BALUSTER_FREI"] - 0.01)

    # -- Windung der neuen Helfer ------------------------------------------
    # box() legt nach innen an (Volumen negativ). balken() muss dieselbe
    # Konvention treffen - sonst faellt ein einzelner Balken im Spiel optisch
    # aus dem Bauteil heraus, und Blender zeigt es nicht (EEVEE cullt nicht).
    for name, bau in (
            ("balken", lambda b: ns["balken"](b, (0.0, 0.0, 0.0), (1.0, 0.0, 1.0),
                                               0.20, 0.16, "NbHolzDunkel")),
            ("docke", lambda b: ns["docke"](b, 0.0, 0.0, 0.0, "NbHolzDunkel")),
    ):
        b = ns["MeshBuilder"]()
        bau(b)
        probe = b.to_object("Probe_%s" % name)
        pv = volumen(probe)
        print("###CHECK### %s-Sonde: Volumen %+.4f m3" % (name, pv))
        melde("%s() windet wie box() (Volumen negativ)" % name, pv < 0.0)


    print("###CHECK### Bahnsteighalle: %d Fehler" % len(FEHLER))


def main():
    obj, slots, _dims = ns["build_wagen"]()
    pruefe_aussen(obj, slots)

    # -- Windungskonvention der Helfer (Sonden) ----------------------------
    b = ns["MeshBuilder"]()
    b.box(0.0, 2.0, 0.0, 2.0, 0.0, 2.0, "NbGelb")
    probe = b.to_object("ProbeWuerfel")
    print("###CHECK### box()-Sonde 2x2x2: Volumen %+.3f (negativ = nach innen)"
          % volumen(probe))

    b2 = ns["MeshBuilder"]()
    punkte = [(0.31 * math.cos(2 * math.pi * k / 16),
               0.31 * math.sin(2 * math.pi * k / 16)) for k in range(16)]
    b2.prism_y(-0.04, 0.04, punkte, "NbOxidrot")
    scheibe = b2.to_object("ProbeScheibe")
    print("###CHECK### prism_y-Sonde r=0,31 h=0,08: Volumen %+.3f m3 "
          "(muss wie box() negativ sein, sonst faellt nur das Rad aus der Reihe)"
          % volumen(scheibe))

    pruefe_innenraum(obj, slots)
    pruefe_bewegliche_teile()
    pruefe_bahnsteighalle()

    print("###CHECK### Wagen + Innenraum + bewegliche Teile: %d Fehler"
          % len(FEHLER))
    for f in FEHLER:
        print("###CHECK### FEHLER   %s" % f)


main()
