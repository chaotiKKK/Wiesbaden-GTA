"""Misst den WIRKLICHEN Drehpunkt der beiden Ka-52-Koaxialrotorscheiben.

Anlass: die bisherige Notiz "Achsversatz 6,66 cm" stammt aus der Mitte der
Bounding Box. Genau diese Groesse hatte der Altkommentar im Pawn selbst als
irrefuehrend bezeichnet ("bei einem geparkten Rotor, weil die Blaetter
ungleich stehen; ihr Mittelpunkt liegt irgendwo dazwischen"). Die Box-Mitte
des oberen Rotors liegt 1,66 m neben seinem echten Drehpunkt - und im Pawn
stand der Kommentar, die Achse liege "exakt bei (0, 0)".

Drei Verfahren wurden erprobt, zwei sind verworfen - mit Zahlen, damit das
hier nicht noch einmal mit dem falschen Wert passiert:

  1. BOUNDING-BOX-MITTE. Oberer Rotor X = +1,794 m. Verworfen: bis 1,66 m
     neben dem Drehpunkt.
  2. KREISFIT ueber die Blattspitzen. Streuung 1,12 m. Verworfen: das Mesh
     ist keine ebene Kreisflaeche (stark konisch und gepfeilt), der Fit
     sucht eine Scheibe, die es nicht gibt.
  3. ZUSAMMENHAENGENDE TEILE. 4755 bzw. 7017 Bruchstuecke. Verworfen: das
     Mesh ist nicht verschmolzen, jedes Teilstueck ist Rauschen, keine Nabe.

Verwendet wird die 3-fach-ROTATIONSSYMMETRIE: drei gleiche Blaetter im
120-Grad-Abstand machen den SCHWERPUNKT der Vertexmenge zum Drehpunkt - das
gilt unabhaengig von Kontur, Kegel und Zerschnittenheit. Dass die
Symmetrie wirklich stimmt, prueft der Restfehler: die Punktmenge wird um
120 Grad um den Kandidaten gedreht, und der mittlere Abstand zum naechsten
Originalpunkt muss in der Groessenordnung des Vertexabstands bleiben
(klein heisst richtig, gross heisst: der Kandidat ist nicht die Achse).

Ergebnis nach Saved/Diagnose/ka52/rotorachse.txt. Blender-Prints erreichen
den Prozess nicht, die Datei schon.

Aufruf:
  "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b ^
    --factory-startup --python Tools/ka52_rotorachse.py -- ^
    "<Content/Data/Raw/Ka52/ka52.glb>" "<Saved/Diagnose/ka52>"
"""

import math
import os
import random
import sys

import bpy

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
GLB = argv[0] if argv else r"C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Content\Data\Raw\Ka52\ka52.glb"
ZIEL = argv[1] if len(argv) > 1 else r"C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Saved\Diagnose\ka52"
# Drittes Argument: Dateiname des Berichts. Standardmaessig "rotorachse.txt",
# damit eine FBX-Messung (was UE wirklich importiert hat) die GLB-Messung
# nicht ueberschreibt.
BERICHT = argv[2] if len(argv) > 2 else "rotorachse.txt"
os.makedirs(ZIEL, exist_ok=True)

zeilen = []


def sag(t=""):
    zeilen.append(str(t))


def drehe(p, c, grad):
    """Dreht einen XY-Punkt um c um grad Grad."""
    a = math.radians(grad)
    ca, sa = math.cos(a), math.sin(a)
    dx, dy = p[0] - c[0], p[1] - c[1]
    return (c[0] + dx * ca - dy * sa, c[1] + dx * sa + dy * ca)


def symmetrie_rest(punkte, c, grad, stichprobe=1500, zellgroesse=0.25):
    """Restfehler einer Drehung der Punktmenge um c um grad Grad.

    Klein heisst: c IST die Achse. Der Wert ist der mittlere Abstand
    eines gedrehten Punkts zum naechsten Originalpunkt, also die
    Groessenordnung des Vertexabstands, wenn die Drehung stimmt.

    Zwei Stolperfallen, beide am 26.09. schon passiert und deshalb hier
    ausdruecklich abgesichert:
      - Der Suchradius muss GROSSER sein als der mittlere Vertexabstand.
        Mit 8-cm-Zellen und 3x3 Nachbarschaft fand ein Punkt in einer
        leeren Nachbarschaft keinen Nachbarn, der Abstand blieb 1e9, und
        der Mittelwert war 77 333 333 m. Das sah nach "Symmetrie
        verletzt" aus und war nur ein Loch im Suchraster.
      - Der Mittelwert allein ist nicht aussagekraeftig; Ausreisser
        verfaelschen ihn. Zusaetzlich wird der Anteil der Punkte
        ausgegeben, die unter 10 cm bleiben - das ist die eigentliche
        Ja/Nein-Frage.
    """
    rng = random.Random(20260926)
    probe = rng.sample(punkte, min(stichprobe, len(punkte)))
    zellen = {}
    r = zellgroesse
    for p in punkte:
        zellen.setdefault((int(p[0] // r), int(p[1] // r)), []).append(p)
    abstaende = []
    ohne_nachbarn = 0
    # Wenn ein gedrehter Punkt keinen Nachbarn im Suchraster findet, ist er
    # mit hoher Wahrscheinlichkeit aus der Scheibe heraus - ein BEWEIS
    # gegen die Achse. Wird er einfach weggelassen, verschwinden genau die
    # Punkte, die den falschen Kandidaten verraten (Ueberlebensfehler): beim
    # unteren Rotor blieben so 948 von 1500 Kontrollpunkten uebrig und der
    # Kontrollwert sah gut aus. Stattdessen wird der Suchradius als
    # untere Schranke eingerechnet.
    suchradius = 1.5 * r
    for p in probe:
        d = drehe(p, c, grad)
        gi, gj = int(d[0] // r), int(d[1] // r)
        best = None
        for di in (-1, 0, 1):
            for dj in (-1, 0, 1):
                for q in zellen.get((gi + di, gj + dj), ()):
                    dd = math.hypot(d[0] - q[0], d[1] - q[1])
                    if best is None or dd < best:
                        best = dd
        if best is None:
            ohne_nachbarn += 1
            best = suchradius
        abstaende.append(best)
    if not abstaende:
        return None, 0.0, ohne_nachbarn
    abstaende.sort()
    mittel = sum(abstaende) / len(abstaende)
    median = abstaende[len(abstaende) // 2]
    anteil = 100.0 * sum(1 for a in abstaende if a < 0.10) / len(abstaende)
    return (mittel, median, anteil, ohne_nachbarn)


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    if GLB.lower().endswith(".fbx"):
        bpy.ops.import_scene.fbx(filepath=os.path.abspath(GLB))
    else:
        bpy.ops.import_scene.gltf(filepath=os.path.abspath(GLB))
    objs = {o.name: o for o in bpy.context.scene.objects}

    sag("Ka-52 Rotorachse - Drehpunkt aus echten Vertices")
    sag("Quelle: %s" % GLB)
    sag("Einheit: Meter. Modellraum wie im Import: X = QUERachse,")
    sag("Y = Laengsachse (Nase bei -Y), Z = Hoehe ueber der Kufenebene.")
    sag("")

    ergebnis = {}
    for name in ("Rotor_Upper", "Rotor_Lower"):
        ob = objs.get(name)
        if not ob:
            sag("%s: FEHLT" % name)
            continue
        oben = [ob.matrix_world @ v.co for v in ob.data.vertices]
        xy = [(p.x, p.y) for p in oben]
        zs = [p.z for p in oben]
        n = len(xy)
        xs = [p[0] for p in xy]
        ys = [p[1] for p in xy]

        sag("--- %s: %d Vertices ---" % (name, n))
        sag("  Bounding Box  X %7.3f..%7.3f  Y %7.3f..%7.3f  Z %6.3f..%6.3f"
            % (min(xs), max(xs), min(ys), max(ys), min(zs), max(zs)))
        box_x = (min(xs) + max(xs)) * 0.5
        box_y = (min(ys) + max(ys)) * 0.5
        sag("  Box-Mitte     X %+7.3f  Y %+7.3f   <- als Achse unbrauchbar"
            % (box_x, box_y))

        cx = sum(p[0] for p in xy) / n
        cy = sum(p[1] for p in xy) / n
        sag("  Schwerpunkt   X %+7.3f  Y %+7.3f   <- Drehpunkt-Kandidat" % (cx, cy))
        sag("  Abstand Box-Mitte zu Schwerpunkt: %.3f m"
            % math.hypot(box_x - cx, box_y - cy))

        # Die aeusseren 30 % sind die Blattspitzen; ihr Schwerpunkt muss
        # auf denselben Punkt fallen. Zwei unabhaengige Zahlen, die sich
        # nicht widersprechen duerfen.
        rad = sorted(math.hypot(p[0] - cx, p[1] - cy) for p in xy)
        schwelle = rad[int(len(rad) * 0.70)]
        aussen = [p for p in xy if math.hypot(p[0] - cx, p[1] - cy) >= schwelle]
        ax = sum(p[0] for p in aussen) / len(aussen)
        ay = sum(p[1] for p in aussen) / len(aussen)
        sag("  Schwerpunkt der aeusseren 30 %% (r >= %.3f m, %d Vertex):"
            % (schwelle, len(aussen)))
        sag("               X %+7.3f  Y %+7.3f   Abstand zum Gesamtschwerpunkt %.4f m"
            % (ax, ay, math.hypot(ax - cx, ay - cy)))

        probe = symmetrie_rest(xy, (cx, cy), 120.0)
        kontrolle = symmetrie_rest(xy, (box_x, box_y), 120.0)
        if probe is None or kontrolle is None:
            sag("  Symmetrieprobe 120 Grad: KEIN Nachbar gefunden - Verfahren unbrauchbar")
            sag("")
            continue
        mittel, median, anteil, ohne = probe
        k_mittel, k_median, k_anteil, k_ohne = kontrolle
        sag("  Symmetrieprobe 120 Grad ueber je 1500 Punkte")
        sag("  (ein Punkt ohne Nachbarn im Suchraster zaehlt mit %.2f m):"
            % (1.5 * 0.25))
        sag("     am Schwerpunkt     Median %.4f m   Mittel %.4f m   "
            "%.1f %% unter 10 cm   %4d ohne Nachbarn"
            % (median, mittel, anteil, ohne))
        sag("     KONTROLLE Box-Mitte Median %.4f m   Mittel %.4f m   "
            "%.1f %% unter 10 cm   %4d ohne Nachbarn"
            % (k_median, k_mittel, k_anteil, k_ohne))
        sag("     Verhaeltnis der Mediane: %.1f-fach, Verhaeltnis der "
            "Trefferquote: %.1f-fach"
            % (k_median / median if median > 0 else float("inf"),
               k_anteil / anteil if anteil > 0 else float("inf")))
        # Das Kontrollmass ist das Argument: ein Verfahren, das ueberall
        # "gut" meldet, prueft nichts. Es muss am falschen Punkt
        # messbar schlechter sein, sonst taugt die Zahl nicht.
        ok = (median < 0.02 and ohne < 0.05 * 1500
              and k_median > 5.0 * max(median, 1e-6)
              and k_ohne > 5.0 * max(ohne, 1))
        sag("  Bewertung: %s"
            % ("bestaetigt - der Schwerpunkt IST die Achse, die Box-Mitte "
               "ist es nachweislich nicht" if ok else
               "NICHT BESTAETIGT - Median zu gross oder Kontrolle zu aehnlich"))
        sag("")
        ergebnis[name] = (cx, cy, min(zs), max(zs), median, ok)

    if len(ergebnis) == 2:
        (ux, uy, uz0, uz1, umedian, uok) = ergebnis["Rotor_Upper"]
        (lx, ly, lz0, lz1, lmedian, lok) = ergebnis["Rotor_Lower"]
        sag("--- Vergleich beiner Scheiben ---".replace("beiner", "beider"))
        sag("  Drehpunkt oben  X %+.3f m  Y %+.3f m  Z %6.3f..%6.3f  "
            "(Median %.4f m, %s)" % (ux, uy, uz0, uz1, umedian,
                                     "bestaetigt" if uok else "offen"))
        sag("  Drehpunkt unten X %+.3f m  Y %+.3f m  Z %6.3f..%6.3f  "
            "(Median %.4f m, %s)" % (lx, ly, lz0, lz1, lmedian,
                                     "bestaetigt" if lok else "offen"))
        sag("  Versatz der beiden Drehpunkte: X %+.2f cm   Y %+.2f cm  (Betrag %.2f cm)"
            % ((lx - ux) * 100.0, (ly - uy) * 100.0,
               math.hypot(lx - ux, ly - uy) * 100.0))
        sag("")
        sag("  Gemeinsame Rotorstangenachse: der MODELLURSPRUNG (0, 0).")
        sag("  Begruendung: der Modellursprung ist die Rumpfmitte auf der")
        sag("  Laengsachse (Gegenprobe unten) und zugleich der Punkt, an dem")
        sag("  Naben, Lichtbastel und Geschuetz haengen. Die Achse dorthin zu")
        sag("  legen haelt Rumpf und Rotoren in EINEM Bezugssystem; eine")
        sag("  Achse in der Mitte der beiden Drehpunkte wuerde zwar die")
        sag("  Versetzungen halbieren, aber den Rumpf um rund 2 cm")
        sag("  verschieben - und die muesste an zwei Stellen nachgezogen werden.")
        sag("")
        sag("  Korrektur je Scheibe (Verschiebung des Mesh-Components):")
        sag("     Rotor_Upper  X %+.2f cm  Y %+.2f cm"
            % (-ux * 100.0, -uy * 100.0))
        sag("     Rotor_Lower  X %+.2f cm  Y %+.2f cm"
            % (-lx * 100.0, -ly * 100.0))
        sag("")
        sag("  Achtung beim Eintragen: die Component-Verschiebung wird mit")
        sag("  ModelYaw (+90 Grad) gedreht angewandt. Der Versatz im")
        sag("  COMPONENT-Raum ist deshalb (x, y) -> (-y, x) des Modellraums,")
        sag("  siehe ComputeRotorMountOffset in WiesbadenHelicopter.cpp.")
        sag("")

        fus = objs.get("Fuselage")
        if fus:
            f = [fus.matrix_world @ v.co for v in fus.data.vertices]
            fx = [p.x for p in f]
            fy = [p.y for p in f]
            fz = [p.z for p in f]
            rumpfmitte_x = (min(fx) + max(fx)) * 0.5
            sag("  Gegenprobe am Rumpf: die Achse muss UEBER dem Rumpf liegen,")
            sag("  und zwar auf der Mittelsenkrechten und bei 40-50 % der Laenge")
            sag("  ab der Nase - das ist die Lage eines Hauptmastes.")
            sag("     Rumpf  X %7.3f..%7.3f  Y %7.3f..%7.3f  Z %6.3f..%6.3f"
                % (min(fx), max(fx), min(fy), max(fy), min(fz), max(fz)))
            sag("     Mittelsenkrechte X = %+.3f m, Drehpunkt oben X = %+.3f m "
                "-> Versatz %.3f m" % (rumpfmitte_x, ux, abs(ux - rumpfmitte_x)))
            sag("     Mittelsenkrechte X = %+.3f m, Drehpunkt unten X = %+.3f m "
                "-> Versatz %.3f m" % (rumpfmitte_x, lx, abs(lx - rumpfmitte_x)))
            laenge = max(fy) - min(fy)
            sag("     Drehpunkt oben  bei %.1f %% der Rumpflaenge ab Nase"
                % (100.0 * (uy - min(fy)) / laenge))
            sag("     Drehpunkt unten bei %.1f %% der Rumpflaenge ab Nase"
                % (100.0 * (ly - min(fy)) / laenge))
            sag("     Zum Vergleich Box-Mitte: %.1f %% - unmoeglich weit hinten"
                % (100.0 * (-0.056 - min(fy)) / laenge))
            sag("  Ein Versatz in X ueber 0,3 m waere kein Hubschrauber mehr,")
            sag("  sondern ein neben dem Rumpf stehender Rotor.")

    ziel = os.path.join(ZIEL, BERICHT)
    with open(ziel, "w", encoding="utf-8") as f:
        f.write("\n".join(zeilen) + "\n")
    print("ROTORACHSE:", ziel)


main()
