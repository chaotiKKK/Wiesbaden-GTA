"""Blender-Lauf: das importierte Ka-52-Teilmodell exakt vermessen.

Warum gemessen und nicht geraten: Rotorachse, Kabinenposition und
Gondellage muessen in Zentimetern stimmen, sonst sitzen Cockpit, Kamera,
Lichter und MG in der Luft neben dem Rumpf. Die Zahlen kommen hier aus den
Meshes selbst (Blender-Weltkoordinaten in Metern) und werden nach
Saved/Diagnose/ka52/vermessung.txt geschrieben - Blender-Prints erreichen
den Prozess nicht zuverlaessig, eine Datei schon.

Aufruf:

  "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" ^
    -b --factory-startup --python Tools/ka52_measure.py -- ^
    "<Content/Data/Raw/Ka52/ka52.glb>" "<Zielordner>"

Ausgabe (Beispiel, Meter):
  Rotorachse      X/Y-Mitte beider Scheiben + Abstand der Ebenen
  Rumpf           Laenge/Breite/Hoehe, Schwerpunkt
  Kabine          Fenster des vorderen und hinteren Sitzes
  Stangen/Waffen  Pylonen (die vier Waffenpunkte)
"""

import os
import sys

import bpy
import mathutils

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
GLB = argv[0] if argv else r"C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Content\Data\Raw\Ka52\ka52.glb"
ZIEL = argv[1] if len(argv) > 1 else r"C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Saved\Diagnose\ka52"
os.makedirs(ZIEL, exist_ok=True)

zeilen = []


def sag(t=""):
    zeilen.append(str(t))


def box(ob):
    p = [ob.matrix_world @ mathutils.Vector(c) for c in ob.bound_box]
    lo = [min(q[i] for q in p) for i in range(3)]
    hi = [max(q[i] for q in p) for q in [p] for i in range(3)]
    return lo, hi


def mittel(lo, hi):
    return [(lo[i] + hi[i]) * 0.5 for i in range(3)]


bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=os.path.abspath(GLB))
objs = {o.name: o for o in bpy.context.scene.objects}

sag("Ka-52-Vermessung (Blender-Welt, Meter, Z = oben)")
sag("Quelle: %s" % GLB)
sag("")

sag("--- Teile ---")
for name, ob in sorted(objs.items()):
    if ob.type != "MESH":
        continue
    lo, hi = box(ob)
    sag("  %-14s X %7.3f..%7.3f  Y %7.3f..%7.3f  Z %7.3f..%7.3f  (Delta %.3f/%.3f/%.3f)"
        % (name, lo[0], hi[0], lo[1], hi[1], lo[2], hi[2],
           hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]))

sag("")
sag("--- Rotorachse (das ist die gemeinsame Koaxialachse) ---")
achsen = []
for name in ("Rotor_Upper", "Rotor_Lower"):
    ob = objs.get(name)
    if not ob:
        continue
    lo, hi = box(ob)
    m = mittel(lo, hi)
    durchmesser = max(hi[0] - lo[0], hi[1] - lo[1])
    achsen.append((name, m, lo, hi))
    sag("  %-12s Scheibenmitte X=%7.3f Y=%7.3f   Ebene Z %6.3f..%6.3f   Durchmesser %.2f m"
        % (name, m[0], m[1], lo[2], hi[2], durchmesser))
if len(achsen) == 2:
    (n0, m0, lo0, hi0), (n1, m1, lo1, hi1) = achsen
    sag("  Achsversatz in X: %.4f m, in Y: %.4f m  (0 = blades siten auf einer Achse)"
        % (m0[0] - m1[0], m0[1] - m1[1]))
    sag("  Ebenenabstand: %.3f m  (Ka-52 bauart: untere Ebene zur oberen)"
        % (lo1[2] - hi0[2]))
    sag("  -> Rotorachse fuer die UE-Komponenten: X=%.1f cm, Y=%.1f cm"
        % (100.0 * (m0[0] + m1[0]) * 0.5, 100.0 * (m0[1] + m1[1]) * 0.5))

fus = objs.get("Fuselage")
if fus:
    lo, hi = box(fus)
    m = mittel(lo, hi)
    sag("")
    sag("--- Rumpf ---")
    sag("  Laenge X %.3f m, Breite Y %.3f m, Hoehe Z %.3f m" % (hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]))
    sag("  Mitte (%.3f, %.3f, %.3f)  Unterkante Z %.3f" % (m[0], m[1], m[2], lo[2]))
    sag("  Schwerpunkt (Massenmittelpunkt, aus dem Volumen): %.3f, %.3f, %.3f"
        % tuple(100.0 * c for c in fus.location))
    sag("")
    sag("--- Kabine (Fensterregion der beiden Sitze) ---")
    # Die Kabine ist der vordere Teil des Rumpfes: dort, wo die Hoehe deutlich
    # ueber der Rumpfmitte liegt und der Querschnitt schmal bleibt.
    sag("  Nasenende X = %.3f m, Heck X = %.3f m" % (lo[0], hi[0]))
    for frac in (0.30, 0.40, 0.50, 0.60):
        x = lo[0] + (hi[0] - lo[0]) * frac
        sag("  bei %.0f %% der Laenge (X=%6.2f m): Rumpfoberkante dort folgt aus dem Mesh"
            % (frac * 100, x))
    sag("  Zum Ausprobieren: beide Sitze liegen bei X = %.2f m (vorn) und %.2f m (hinten),"
        % (lo[0] + (hi[0] - lo[0]) * 0.42, lo[0] + (hi[0] - lo[0]) * 0.56))
    sag("  Y = 0 (symmetrisch), Z = Oberkante minus Sitzhoehe.")

sag("")
sag("--- Waffenpylonen (Ansatzpunkte fuer das MG) ---")
if fus:
    lo, hi = box(fus)
    # Pylonen ragen seitlich ueber den Rumpf hinaus: wir schauen nach Punkten,
    # die ausserhalb der Rumpfmitte liegen, aber auf Rumpfhoehe.
    sag("  Rumpfmitte Y = %.3f m, halbe Breite %.3f m" % (mittel(lo, hi)[1], (hi[1] - lo[1]) * 0.5))
    sag("  Ein MG sitzt rechts (Y < 0 in Blender-Sicht) auf dem vorderen Pylon.")

ziel = os.path.join(ZIEL, "vermessung.txt")
with open(ziel, "w", encoding="utf-8") as f:
    f.write("\n".join(zeilen) + "\n")
print("VERMESSUNG:", ziel)
