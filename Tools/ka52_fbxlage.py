"""Blender-Lauf: das IMPORTIERTE FBX auf seine echte Achsenlage vermessen.

Anlass und Weg: Im Roh-GLB liegt die Nase bei Y = -5,8 m und der Heckfinner
bei Y = +8,3 m; der Pawn dreht das Mesh mit Yaw -90, also Modell-+Y auf
Welt-+X. Faellt die FBX-Achse anders aus als das GLB, zeigt der Hubschrauber
nach Welt-X = rueckwaerts - und daran haengen Cockpit, Kamera, Lichter, MG
und der Anflug auf den Helipad.

Deshalb wird nicht das GLB, sondern die FBX gemessen: die FBX ist das, was
Unreal anzeigt. Eine Bounding Box allein reicht dafuer nicht (sie deckt beide
Enden zugleich ab) - deshalb werden die Enden getrennt ausgewertet:

  Nase:  breit und flach, die Rumpfoberkante laeuft flach an
  Heck:  schmal, und der Finner reicht hoch (Z bis rund 295 cm)

Zusaetzlich: Rotorachse (Scheibenmitte, Ebenenabstand, Blattwinkel) - das ist
zugleich der Beweis fuer "die Rotoren sitzen fest auf der Rotorstangenachse
und laufen synchron gegenlaeufig".

Aufruf:

  "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" ^
    -b --factory-startup --python Tools/ka52_fbxlage.py -- ^
    "<Content/Data/Raw/Ka52/ka52_ue.fbx>" "<Zielordner>"
"""

import math
import os
import sys

import bpy
import mathutils

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
FBX = argv[0] if argv else r"C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Content\Data\Raw\Ka52\ka52_ue.fbx"
ZIEL = argv[1] if len(argv) > 1 else r"C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Saved\Diagnose\ka52"
os.makedirs(ZIEL, exist_ok=True)

z = []


def sag(t=""):
    z.append(str(t))


def importieren():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=os.path.abspath(FBX),
                            use_manual_orientation=False,
                            global_scale=1.0,
                            automatic_bone_orientation=True)
    return {o.name: o for o in bpy.context.scene.objects if o.type == "MESH"}


def punkte(ob):
    mw = ob.matrix_world
    return [mw @ v.co for v in ob.data.vertices]


def main():
    sag("Ka-52 IMPORTIERTES FBX - Achsenlage")
    sag("Quelle: %s" % FBX)
    sag("")
    objs = importieren()
    sag("Meshes: %s" % sorted(objs))
    sag("")

    alle = {}
    for name, ob in sorted(objs.items()):
        p = punkte(ob)
        alle[name] = p
        lo = [min(q[i] for q in p) for i in range(3)]
        hi = [max(q[i] for q in p) for i in range(3)]
        sag("  %-14s X %8.1f..%8.1f  Y %8.1f..%8.1f  Z %8.1f..%8.1f cm   (%.0f/%.0f/%.0f)"
            % (name, 100 * lo[0], 100 * hi[0], 100 * lo[1], 100 * hi[1],
               100 * lo[2], 100 * hi[2],
               100 * (hi[0] - lo[0]), 100 * (hi[1] - lo[1]), 100 * (hi[2] - lo[2])))

    fus = alle.get("Fuselage")
    if not fus:
        sag("kein Fuselage-Mesh - nichts weiter auswertbar")
        return
    lo = [min(q[i] for q in fus) for i in range(3)]
    hi = [max(q[i] for q in fus) for i in range(3)]
    spanne = [(i, hi[i] - lo[i]) for i in range(3)]
    laengs = max(spanne, key=lambda t: t[1])[0]
    quer = max((t for t in spanne if t[0] != laengs), key=lambda t: t[1])[0]
    sag("")
    sag("Laengsachse: %s (%.0f cm), Querachse: %s (%.0f cm), Hoehe: %.0f cm"
        % ("XYZ"[laengs], 100 * (hi[laengs] - lo[laengs]),
           "XYZ"[quer], 100 * (hi[quer] - lo[quer]),
           100 * (hi[2] - lo[2])))
    sag("")

    # -- Enden getrennt: Nase (flach, breit) gegen Heck (schmal, Finner hoch) --
    sage = sag
    laenge = hi[laengs] - lo[laengs]
    anteil = 0.12
    sage("--- Enden der Laengsachse (erste/letzte %.0f %%) ---" % (anteil * 100))
    for ende, richtung in (("niedriges Ende", 1), ("hohes Ende", -1)):
        if richtung == 1:
            sch = [q for q in fus if q[laengs] < lo[laengs] + laenge * anteil]
            lage = "%.0f cm" % (100 * lo[laengs])
        else:
            sch = [q for q in fus if q[laengs] > hi[laengs] - laenge * anteil]
            lage = "%.0f cm" % (100 * hi[laengs])
        if not sch:
            continue
        breite = (max(q[quer] for q in sch) - min(q[quer] for q in sch))
        oben = max(q[2] for q in sch)
        sag("  %-13s bei %-8s  Querbreite %6.1f cm   Oberkante %6.1f cm   Vertices %d"
            % (ende, lage, 100 * breite, 100 * oben, len(sch)))
    sag("")
    sag("  Deutung: das ENDE mit der GROSSEN Oberkante (Finner) ist das Heck,")
    sag("  das flache Ende ist die Nase.")

    # -- Rotorachse ----------------------------------------------------------
    sag("")
    sag("--- Rotoren (Achse, Ebenen, Blaetter) ---")
    scheiben = {}
    for name in ("Rotor_Upper", "Rotor_Lower"):
        p = alle.get(name)
        if not p:
            continue
        l = [min(q[i] for q in p) for i in range(3)]
        h = [max(q[i] for q in p) for i in range(3)]
        mitte = [(l[i] + h[i]) * 0.5 for i in range(3)]
        durchmesser = max((h[i] - l[i]) for i in (0, 1)) * 100
        scheiben[name] = (mitte, l, h)
        sag("  %-12s Scheibenmitte (%.1f, %.1f) cm  Ebene z %.1f..%.1f cm  "
            "Durchmesser %.0f cm"
            % (name, 100 * mitte[0], 100 * mitte[1],
               100 * l[2], 100 * h[2], durchmesser))
    if len(scheiben) == 2:
        (mu, lu, hu), (ml, ll, hl) = scheiben["Rotor_Upper"], scheiben["Rotor_Lower"]
        sag("  Achsversatz: %.2f cm in X, %.2f cm in Y   (0 = dieselbe Stange)"
            % (100 * (mu[0] - ml[0]), 100 * (mu[1] - ml[1])))
        sag("  Ebenenabstand (Oberkante unten -> Unterkante oben): %.1f cm"
            % (100 * (ll[2] - hu[2])))

    # -- Blattwinkel: liegen die Blaetter symmetrisch um die Achse? ---------
    sag("")
    sag("--- Blattwinkel (Sicht von oben, Winkel ab Modell-+X) ---")
    for name in ("Rotor_Upper", "Rotor_Lower"):
        p = alle.get(name)
        if not p or name not in scheiben:
            continue
        mitte, l, h = scheiben[name]
        # Nur Punkte weit draussen (Blattspitzen) zaehlen als Blatt.
        axt = 0 if (h[0] - l[0]) >= (h[1] - l[1]) else 1
        ayt = 1 - axt
        rad = [q for q in p if math.hypot(q[axt] - mitte[axt], q[ayt] - mitte[ayt]) > 0.8 * max(h[axt] - l[axt], h[ayt] - l[ayt]) * 0.5]
        if not rad:
            continue
        winkel = sorted(math.degrees(math.atan2(q[ayt] - mitte[ayt], q[axt] - mitte[axt])) % 360.0 for q in rad)
        # Cluster: Winkel mit Abstand < 15 Grad zusammenfassen
        cluster = []
        for w in winkel:
            if cluster and w - cluster[-1][-1] < 15.0:
                cluster[-1].append(w)
            else:
                cluster.append([w])
        mittel_winkel = [sum(c) / len(c) for c in cluster if len(c) > 0.005 * len(winkel)]
        sag("  %-12s %d Winkelgruppen: %s"
            % (name, len(mittel_winkel),
               ", ".join("%.1f Grad" % w for w in mittel_winkel)))
    sag("")
    sag("  Drei Gruppen mit ~120 Grad Abstand = drei Blaetter (Ka-52).")
    sag("  Zusaetzlich wichtig: Rotor_Upper und Rotor_Lower muessen dieselbe")
    sag("  Nabe teilen (Achsversatz 0) und synchron gegenlaeufig laufen -")
    sag("  das laeuft im Pawn ueber zwei Naben-Komponenten mit umgekehrtem")
    sag("  Vorzeichen, siehe AWiesbadenHelicopter::UpdateRotors.")

    ziel = os.path.join(ZIEL, "fbxlage.txt")
    with open(ziel, "w", encoding="utf-8") as f:
        f.write("\n".join(z) + "\n")
    print("FBX-LAGE:", ziel)


main()
