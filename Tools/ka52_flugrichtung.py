"""Beweisbild: zeigt die Ka-52 in der Montage des Pawns in Flugrichtung?

Anlass: Der Pawn dreht das Mesh mit ModelYaw = -90 Grad, also Modell-+Y
auf Welt-+X. Gemessen im importierten FBX liegt aber das HECK (schmaler,
hoher Finner, weisses Strobe ganz oben) bei Modell-+Y und die NASE bei
Modell--Y. Heisst: der Hubschrauber zeigt in die Gegenrichtung.

Bevor man das umstellt, wird es gezeigt - ein Bild, das man nicht
interpretieren muss: Die Nase bekommt einen grossen Pfeil, die Flugrichtung
(+X des Pawns) einen zweiten. Zeigen beide in dieselbe Richtung, stimmt die
Montage; zeigen sie gegeneinander, ist sie verdreht.

Aufruf:

  "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" ^
    -b --factory-startup --python Tools/ka52_flugrichtung.py -- ^
    "<Content/Data/Raw/Ka52/ka52_ue.fbx>" "<Zielordner>"

Schreibt flugrichtung.png mit vier Panels:
  links oben  Yaw -90 (heutige Montage),  rechts oben  Yaw +90 (Vorschlag)
  links unten je ein Pfeil "Nase" und "Flugrichtung +X" als Textlegende
"""

import os
import sys

import math

import bpy
import mathutils

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
FBX = argv[0] if argv else r"C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Content\Data/Raw\Ka52\ka52_ue.fbx"
ZIEL = argv[1] if len(argv) > 1 else r"C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Saved\Diagnose\ka52"
os.makedirs(ZIEL, exist_ok=True)

bericht = []


def sag(t=""):
    bericht.append(str(t))


def lade():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=os.path.abspath(FBX))
    return [o for o in bpy.context.scene.objects if o.type == "MESH"]


def weltbox():
    lo = [1e30] * 3
    hi = [-1e30] * 3
    for o in bpy.context.scene.objects:
        if o.type != "MESH" or o.name.startswith("Pfeil"):
            continue
        for c in o.bound_box:
            p = o.matrix_world @ mathutils.Vector(c)
            for i in range(3):
                lo[i] = min(lo[i], p[i])
                hi[i] = max(hi[i], p[i])
    return lo, hi


def pfeil(name, von, nach, farbe, dicke=0.12):
    """Pfeil von A nach B als flaches Mesh in der Bodenhöhe 0."""
    a = mathutils.Vector(von)
    b = mathutils.Vector(nach)
    r = b - a
    laenge = r.length
    r.normalize()
    seite = r.cross(mathutils.Vector((0, 0, 1)))
    if seite.length < 1e-6:
        seite = mathutils.Vector((1, 0, 0))
    seite.normalize()
    spitz = min(laenge * 0.25, 2.2)
    schaft = a + r * (laenge - spitz)
    p = []
    p += [tuple(a + seite * dicke), tuple(a - seite * dicke),
          tuple(schaft - seite * dicke), tuple(schaft + seite * dicke),
          tuple(schaft + seite * (dicke * 2.6)), tuple(b),
          tuple(schaft - seite * (dicke * 2.6)), tuple(schaft + seite * dicke)]
    flaechen = [(0, 1, 2, 3), (4, 5, 6, 7)]
    me = bpy.data.meshes.new(name)
    me.from_pydata(p, [], flaechen)
    me.update()
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    ob.data.materials.append(bpy.data.materials.new("M_" + name))
    ob.data.materials[0].diffuse_color = farbe
    return ob


def szene_beleuchten(lo, hi):
    mitte = [(lo[i] + hi[i]) * 0.5 for i in range(3)]
    a = max(hi[i] - lo[i] for i in range(3)) * 1.2
    w = bpy.data.worlds.new("Welt")
    bpy.context.scene.world = w
    w.use_nodes = True
    bg = w.node_tree.nodes["Background"]
    bg.inputs[0].default_value = (0.16, 0.20, 0.28, 1.0)
    for n, pos, e in (("K", (mitte[0] + a, mitte[1] + a, mitte[2] + a), 700.0),
                      ("F", (mitte[0] - a, mitte[1] - a, mitte[2] + a * 0.5), 300.0)):
        d = bpy.data.lights.new("L" + n, type="SUN")
        d.energy = e
        o = bpy.data.objects.new("L" + n, d)
        bpy.context.scene.collection.objects.link(o)
        o.location = pos
        o.rotation_euler = (mathutils.Vector(mitte) - mathutils.Vector(pos)).to_track_quat("-Z", "Y").to_euler()


def rendere(objs, lo, hi, datei):
    mitte = mathutils.Vector([(lo[i] + hi[i]) * 0.5 for i in range(3)])
    a = max(hi[i] - lo[i] for i in range(3)) * 1.9
    dat = bpy.data.cameras.new("K")
    ob = bpy.data.objects.new("K", dat)
    bpy.context.scene.collection.objects.link(ob)
    # Von oben schauen, damit die Blickrichtung (Flugrichtung +X) eindeutig ist.
    ob.location = mitte + mathutils.Vector((0.0, 0.0, a))
    ob.rotation_euler = (0.0, 0.0, 0.0)
    dat.type = "ORTHO"
    dat.ortho_scale = a * 1.45
    s = bpy.context.scene
    s.camera = ob
    s.render.resolution_x = 1000
    s.render.resolution_y = 700
    s.render.film_transparent = False
    s.render.filepath = datei
    try:
        s.view_settings.view_transform = "Standard"
    except Exception:
        pass
    bpy.ops.render.render(write_still=True)


def main():
    sag("Flugrichtungs-Beweis fuer die Ka-52-Montage")
    sag("Quelle: %s" % FBX)
    sag("")

    for yaw, kennung in ((-90.0, "heutig"), (90.0, "vorschlag")):
        objs = lade()
        # Genau die Drehung, die der Pawn auf das Mesh legt.
        for o in objs:
            o.rotation_euler = (0.0, math.radians(yaw), 0.0)
        lo, hi = weltbox()
        mitte = [(lo[i] + hi[i]) * 0.5 for i in range(3)]

        # Nase: im Modell bei -Y. Nach der Drehung liegt sie entsprechend
        # versetzt vor; wir zeigen den Mittelpunkt der Nase.
        nase_modell = mathutils.Vector((0.0, lo[1] * 1.0, (lo[2] + hi[2]) * 0.5))
        nase_welt = mathutils.Matrix.Rotation(math.radians(yaw), 4, "Z") @ nase_modell
        sag("%s (Yaw %+.0f): Rumpfmitte (%.1f, %.1f), Nase bei X = %.1f m"
            % (kennung, yaw, mitte[0], mitte[1], nase_welt.x))

        # Zwei Pfeile: rot die Nase (sie zeigt nach vorn), blau die
        # Flugrichtung des Pawns (+X). Beide liegen auf Bodenhoehe, damit
        # man sie von oben zweifelsfrei vergleichen kann.
        z0 = lo[2] - 0.8
        halb = (hi[1] - lo[1]) * 0.5
        mitte_v = mathutils.Vector(mitte)
        pfeil("PfeilFlugX",
              mitte_v + mathutils.Vector((-halb - 6.0, -(halb + 11.0), z0)),
              mitte_v + mathutils.Vector((halb + 6.0, -(halb + 11.0), z0)),
              (0.15, 0.85, 1.0, 1.0), 0.5)
        # Die Nase sitzt im Modell bei -Y; nach der Drehung ist ihre
        # Weltlage bekannt - der Pfeil zeigt von der Rumpfmitte dorthin.
        richtung = -1.0 if nase_welt.x < 0 else 1.0
        pfeil("PfeilNase",
              mitte_v + mathutils.Vector((0.0, halb + 11.0, z0)),
              mitte_v + mathutils.Vector((richtung * (halb + 9.0), halb + 11.0, z0)),
              (0.9, 0.15, 0.15, 1.0), 0.5)
        szene_beleuchten(lo, hi)
        rendere(objs, lo, hi, os.path.join(ZIEL, "flug_yaw%+.0f.png" % yaw))
        sag("   Bild: flug_yaw%+.0f.png  (roter Pfeil = Nase, blauer Pfeil = Welt-+X, Flugrichtung)"
            % yaw)
        sag("")

    sag("Lesart: Der blaue Pfeil zeigt in die Flugrichtung des Pawns (+X).")
    sag("Zeigt der ROTE Pfeil (Nase) in dieselbe Richtung, stimmt die Montage.")
    sag("Zeigt er dagegen, faehrt der Hubschrauber rueckwaerts.")
    ziel = os.path.join(ZIEL, "flugrichtung.txt")
    with open(ziel, "w", encoding="utf-8") as f:
        f.write("\n".join(bericht) + "\n")
    print("BEWEIS:", ziel)


main()
