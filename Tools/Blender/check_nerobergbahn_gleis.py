# Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#
# Prueft die fuenf Gleisbauteile der Nerobergbahn ohne Renderlauf: Masse,
# Querschnittsstapel, Windung (Aussen-Normalen) und die Mittelpunkt-Invarianten,
# auf die sich der Platzer im Actor verlaesst.
#
# Aufruf:
#   "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" --background \
#       --python WiesbadenReal/Tools/Blender/check_nerobergbahn_gleis.py
#
# Warum das noetig ist: der Actor setzt jedes Teil an seinen Quer-Offset
# (Schienen +-1,00 m, Zahnstangen +-0,50 m, Seilkanal 0) - waere ein Bauteil
# nicht um Y = 0 zentriert, waere die ganze Spur verschoben und man saehe es
# erst im Spiel. Und Blender zeigt nach innen gewickelte Flaechen nicht an
# (EEVEE cullt nicht), Unreal schon.

import os

from mathutils import Vector

PFAD = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                    "make_nerobergbahn.py")
QUELLE = open(PFAD, "r", encoding="utf-8").read().replace("\nmain()\n", "\n")
ns = {"__name__": "make_nerobergbahn_pruef"}
exec(compile(QUELLE, PFAD, "exec"), ns)

FEHLER = []


def pruefe(bedingung, text):
    print("###GCD### %s %s" % ("ok  " if bedingung else "FEHL", text))
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


def achsen(obj):
    v = [x.co for x in obj.data.vertices]
    return (min(p.x for p in v), max(p.x for p in v),
            min(p.y for p in v), max(p.y for p in v),
            min(p.z for p in v), max(p.z for p in v))


def flaechenmitten(obj, z, tol=1e-5):
    """X-Lagen der Flaechen, deren Mittelpunkt auf der Hoehe z liegt.

    Ueber Flaechen, nicht ueber Vertices: die Sprossen einer Zahnstange und die
    Seitenbleche teilen sich dieselben Y-Werte, sind also ueber die Vertices
    nicht zu trennen - ueber die Oberkante (Sprosse -0,085, Blech -0,055) schon.
    """
    xs = []
    for poly in obj.data.polygons:
        pts = [obj.data.vertices[i].co for i in poly.vertices]
        mitte = sum(pts, Vector((0, 0, 0))) / len(pts)
        if abs(mitte.z - z) < tol:
            xs.append(mitte.x)
    return xs


def gruppiere(werte, tol=0.004):
    """Zusammenhaengende Werte zu Clustern - fuer Sprossen-/Stabteilungen."""
    zentren = []
    for w in sorted(werte):
        if not zentren or w - zentren[-1][-1] > tol:
            zentren.append([w])
        else:
            zentren[-1].append(w)
    return [sum(c) / len(c) for c in zentren]


def main():
    # -- Bauteile bauen -----------------------------------------------------
    teile = {}
    for name, fn in [("schiene", ns["build_schiene"]),
                     ("zahnstange", ns["build_zahnstange"]),
                     ("seilkanal", ns["build_seilkanal"]),
                     ("schwelle", ns["build_schwelle"]),
                     ("schotterbett", ns["build_schotterbett"])]:
        obj, slots, _dims = fn()
        teile[name] = obj
        x0, x1, y0, y1, z0, z1 = achsen(obj)
        print("###GCD### %-13s X %6.3f..%+6.3f  Y %6.3f..%+6.3f  Z %6.3f..%+6.3f"
              "  %3d Flaechen  %s"
              % (name, x0, x1, y0, y1, z0, z1, len(obj.data.polygons), slots))
        # Windung: nach aussen (positiv). box() liefert nach innen, die
        # Trassenteile dreht orient_outward() deshalb um.
        pruefe(volumen(obj) > 0.0,
               "%s ist nach aussen gewickelt (Volumen %+.4f m3)"
               % (name, volumen(obj)))
        # Der Platzer setzt jedes Teil auf die Trassenmitte bzw. auf einen
        # Quer-Offset - beides geht nur mit Mittelpunkt in X und Y.
        pruefe(abs(x0 + x1) < 0.02, "%s mittig in X (%.3f/%.3f)" % (name, x0, x1))
        pruefe(abs(y0 + y1) < 0.02, "%s mittig in Y (%.3f/%.3f)" % (name, y0, y1))
        # Symmetrie quer: sonst kippt das Gleis in der Ausweiche schief.
        ywerte = sorted(round(v.co.y, 6) for v in obj.data.vertices)
        symmetrisch = all(abs(a + b) < 1e-6 for a, b in zip(ywerte, reversed(ywerte)))
        pruefe(symmetrisch, "%s ist in Y spiegelsymmetrisch" % name)

    # -- Querschnittsstapel: die Bezugshoehen muessen exakt aufeinandersitzen --
    sch, za, se, sw, be = (achsen(teile[k]) for k in
                           ("schiene", "zahnstange", "seilkanal",
                            "schwelle", "schotterbett"))
    pruefe(abs(sch[5]) < 1e-6, "Schienenoberkante liegt auf Z=0 (%.6f)" % sch[5])
    pruefe(abs(sch[4] + 0.140) < 1e-6,
           "Schienenfuss liegt auf Z=-0,140 (%.6f)" % sch[4])
    pruefe(abs(sw[5] - sch[4]) < 1e-6,
           "Schwellenoberkante = Schienenfuss (%.4f)" % sw[5])
    pruefe(abs(se[5] - sch[4]) < 1e-6,
           "Seilkanalkrone = Schienenfuss, der Rost schliesst buendig ab (%.4f)" % se[5])
    pruefe(abs(za[4] + 0.140) < 1e-6,
           "Zahnstange steht auf der Schwellenoberkante (%.4f)" % za[4])
    pruefe(abs(be[5] - sw[4]) < 1e-6,
           "Schotterbettkrone traegt die Schwellenunterseite (%.4f)" % be[5])

    # -- Profilmasse ---------------------------------------------------------
    rail = teile["schiene"].data.vertices
    kopf = [v.co for v in rail if v.co.z > -0.008]
    fuss = [v.co for v in rail if v.co.z < -0.130]
    pruefe(abs(max(p.y for p in kopf) - 0.030) < 1e-6,
           "Schienenkopf halb 30 mm breit (%.4f)" % max(p.y for p in kopf))
    pruefe(abs(max(p.y for p in fuss) - 0.070) < 1e-6,
           "Schienenfuss halb 70 mm breit - traegt das Spurkranzrad (%.4f)"
           % max(p.y for p in fuss))

    # -- Zahnstange: 100 mm Teilung, Zaehne zwischen den Seitenblechen --------
    sprossen = gruppiere(flaechenmitten(teile["zahnstange"], -0.085))
    teilung = [round(b - a, 4) for a, b in zip(sprossen, sprossen[1:])]
    pruefe(len(sprossen) == 21 and teilung
           and all(abs(t - 0.10) < 1e-3 for t in teilung),
           "%d Sprossen auf 2 m, Teilung %s m (Soll 0,10)"
           % (len(sprossen), sorted(set(teilung))))

    # -- Seilkanal: Rostabdeckung links und rechts der Mittelschienenauflage --
    rost = gruppiere(flaechenmitten(teile["seilkanal"], -0.142))
    rostteilung = [round(b - a, 4) for a, b in zip(rost, rost[1:])]
    print("###GCD### Seilkanal: %d Roststaebe, Stabteilung %s m"
          % (len(rost), sorted(set(rostteilung))))
    pruefe(len(rost) == 41 and rostteilung
           and all(abs(t - 0.05) < 1e-3 for t in rostteilung),
           "Roststaebe 41 je Feld mit 0,05 m Teilung")

    # -- Spurweite gegen die Wagenraeder -------------------------------------
    # Das Gleis traegt die Raeder nur, wenn deren Laufflaeche auf den
    # Schienenlagen sitzt: Wagenmitte +-0,50 m + halbe Spurweite 0,50 m =
    # Schiene bei 0,00 und +-1,00 m. Gemessen wird die Mitte des Radkranzes
    # (tiefste Vertices am Radsatz, ohne Rahmen und Bremsgestaenge).
    wagen, _slots, _d = ns["build_wagen"]()
    satz = [v.co for v in wagen.data.vertices
            if 1.45 < abs(v.co.x) < 1.80 and v.co.y > 0.30]
    zmin = min(p.z for p in satz) if satz else 0.0
    kranz = sorted(abs(p.y) for p in satz if p.z < zmin + 0.06)
    mitte = 0.5 * (kranz[0] + kranz[-1]) if kranz else 0.0
    pruefe(len(kranz) > 0 and abs(mitte - 0.50) < 0.03,
           "Radkranzmitte bei Y=+0,500 m (%.3f, Kranz %.3f..%.3f) - damit "
           "liegen die Raeder auf den Schienenlagen 0,00 / +-1,00 m"
           % (mitte, kranz[0] if kranz else -1, kranz[-1] if kranz else -1))

    print("###GCD### ERGEBNIS: %d Fehler" % len(FEHLER))
    for f in FEHLER:
        print("###GCD###   - %s" % f)


main()
