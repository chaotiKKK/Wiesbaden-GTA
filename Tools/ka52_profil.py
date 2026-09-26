"""Blender-Lauf: Laengsachse und Rumpfprofil des Ka-52 bestimmen.

Anlass: die Bounding-Box allein laesst zwei Lesarten zu - Laenge X (8,7 m)
oder Laenge Y (14,1 m). Fuer Cockpit, Kamera, Lichter und MG muss das
eindeutig sein, sonst sitzt alles an der falschen Stelle.

Methode: der Rumpf wird entlang BEIDER horizontaler Achsen in Scheiben
geschnitten und je Scheibe die Querbreite und die Hoehe ausgegeben. Die
Laengsachse ist diejenige, an der das Profil eine Nase (schmal, spitz)
zeigt und ueber die meiste Laenge breit bleibt; die Querachse zeigt die
Fluegel als kurzen, sehr breiten Ausschlag.

Aufruf:

  "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" ^
    -b --factory-startup --python Tools/ka52_profil.py -- ^
    "<Content/Data/Raw/Ka52/ka52.glb>" "<Zielordner>"
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


bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=os.path.abspath(GLB))
fus = bpy.data.objects.get("Fuselage")
if fus is None:
    raise SystemExit("kein Fuselage-Objekt")

# Weltpositionen aller Vertices des Rumpfes
mw = fus.matrix_world
pts = [mw @ v.co for v in fus.data.vertices]
N = len(pts)
sag("Fuselage: %d Vertices" % N)
sag("")
sag("A) Scheiben entlang X  ->  Spalte zeigt Y-Breite und Z-Hoehe der Scheibe")
sag("      X(m)   n      Y min..max   Breite   Z min..max   Hoehe")
xlo = min(p.x for p in pts); xhi = max(p.x for p in pts)
schritt = 16
kanten_x = []
for i in range(schritt):
    a = xlo + (xhi - xlo) * i / schritt
    b = xlo + (xhi - xlo) * (i + 1) / schritt
    sch = [p for p in pts if a <= p.x < b]
    if not sch:
        continue
    ylo = min(p.y for p in sch); yhi = max(p.y for p in sch)
    zlo = min(p.z for p in sch); zhi = max(p.z for p in sch)
    kanten_x.append((a, b, len(sch), yhi - ylo, zhi - zlo, ylo, yhi, zlo, zhi))
    sag("%7.2f %6d   %7.2f..%7.2f  %6.2f   %6.2f..%6.2f  %6.2f"
        % (a, len(sch), ylo, yhi, yhi - ylo, zlo, zhi, zhi - zlo))

sag("")
sag("B) Scheiben entlang Y  ->  Spalte zeigt X-Breite und Z-Hoehe der Scheibe")
sag("      Y(m)   n      X min..max   Breite   Z min..max   Hoehe")
ylo_ = min(p.y for p in pts); yhi_ = max(p.y for p in pts)
kanten_y = []
for i in range(schritt):
    a = ylo_ + (yhi_ - ylo_) * i / schritt
    b = ylo_ + (yhi_ - ylo_) * (i + 1) / schritt
    sch = [p for p in pts if a <= p.y < b]
    if not sch:
        continue
    xlo_ = min(p.x for p in sch); xhi_ = max(p.x for p in sch)
    zlo = min(p.z for p in sch); zhi = max(p.z for p in sch)
    kanten_y.append((a, b, len(sch), xhi_ - xlo_, zhi - zlo, xlo_, xhi_, zlo, zhi))
    sag("%7.2f %6d   %7.2f..%7.2f  %6.2f   %6.2f..%6.2f  %6.2f"
        % (a, len(sch), xlo_, xhi_, xhi_ - xlo_, zlo, zhi, zhi - zlo))

sag("")
sag("--- Auswertung ---")


def bewerte(name, daten, laengs_idx, quer_idx):
    breiten = [d[3] for d in daten]
    mitte = sum(breiten) / len(breiten)
    schmal = [d for d in daten if d[3] < 0.35 * mitte]
    breit = [d for d in daten if d[3] > 0.75 * mitte]
    sag("  Achse %s: mittlere Querbreite %.2f m, schmale Enden (<35%%): %d, breite Abschnitte (>75%%): %d"
        % (name, mitte, len(schmal), len(breit)))
    if schmal:
        sag("    schmal bei %s von %.2f bis %.2f m"
            % (laengs_idx, min(d[0] for d in schmal), max(d[1] for d in schmal)))
    if breit:
        breitest = max(daten, key=lambda d: d[3])
        sag("    breitester Abschnitt %s von %.2f bis %.2f m (%.2f m), Hoehe dort %.2f m"
            % (laengs_idx, breitest[0], breitest[1], breitest[3], breitest[4]))
    return mitte


bewerte("X", kanten_x, "X", "Y")
bewerte("Y", kanten_y, "Y", "X")
sag("")
sag("Die Laengsachse ist die mit den meisten schmalen Enden UND dem")
sag("breitesten Hauptkoerper: das ist die Rumpfrichtung (Nase schmal,")
sag("Rumpf breit, Heck schmal). Die andere Achse zeigt den typischen")
sag("Fluegelschlag: ein einzelner sehr breiter Abschnitt, sonst schmal.")

ziel = os.path.join(ZIEL, "rumpfprofil.txt")
with open(ziel, "w", encoding="utf-8") as f:
    f.write("\n".join(zeilen) + "\n")
print("PROFIL:", ziel)
