"""Blender-Lauf: das angehaengte Ka-52-GLB oeffnen, vermessen, Pictures rendern.

Warum ueberhaupt Blender: das Modell ist ein "Tripo"-KI-Export mit EINEM
fusen Mesh (1 007 915 Vertices, 1.9 M Dreiecke, 1 Material, 2 Texturen) und
OHNE Rotor-Knoten. Man kann im Terminal nicht entscheiden, ob die Rotoren
als Geometrie im Rumpf stecken oder gar nicht vorhanden sind - da hilft nur
ein Bild. Blender ist auf dieser Maschine installiert (5.2), also wird es
benutzt statt geraten.

Aufruf (Blender laeuft im Hintergrund, hat keinen eigenen Bildschirm):

  "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" ^
    -b --factory-startup --python Tools/ka52_blender_inspect.py -- ^
    "<pfad.glb>" "<zielordner>"

Schreibt nach <zielordner>:
  katalog.txt      - Meswerte als Text (Blender-Prints erreichen den
                     Blender-Prozess nicht zuverlaessig, darum eine Datei)
  v_*.png          - Ansichten von vorn/seitlich/oben/Schräg

Die Bilder sind mit PIL zu einem Kontaktbogen zusammengesetzt, den man sich
im Preview ansehen kann.
"""

import os
import sys

import bpy
import mathutils

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
GLB = argv[0] if argv else r"C:\Users\HP\Downloads\military helicopter 3d model (1).glb"
ZIEL = argv[1] if len(argv) > 1 else os.path.abspath("Saved/Diagnose/ka52")

os.makedirs(ZIEL, exist_ok=True)
bericht = []


def sag(text=""):
    bericht.append(str(text))


def importieren(pfad):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=os.path.abspath(pfad))
    return list(bpy.context.scene.objects)


def vermessen(objs):
    sag("=== Katalog ===")
    sag("Objekte: %d" % len(objs))
    gesamt_min = [1e30] * 3
    gesamt_max = [-1e30] * 3
    tris = 0
    for o in objs:
        if o.type != "MESH":
            sag("  %-30s Typ=%s  (kein Mesh)" % (o.name[:30], o.type))
            continue
        me = o.data
        n_tris = sum(max(0, len(p.vertices) - 2) for p in me.polygons)
        tris += n_tris
        bb = [o.matrix_world @ mathutils.Vector(c) for c in o.bound_box]
        lo = [min(p[i] for p in bb) for i in range(3)]
        hi = [max(p[i] for p in bb) for i in range(3)]
        for i in range(3):
            gesamt_min[i] = min(gesamt_min[i], lo[i])
            gesamt_max[i] = max(gesamt_max[i], hi[i])
        mats = ", ".join((m.name if m else "-")[:24] for m in o.data.materials)
        sag("  %-30s Vertices=%7d Dreiecke=%7d Materialien=%s"
            % (o.name[:30], len(me.vertices), n_tris, mats))
        sag("      bbox X %.3f..%.3f  Y %.3f..%.3f  Z %.3f..%.3f"
            % (lo[0], hi[0], lo[1], hi[1], lo[2], hi[2]))
    sag("")
    sag("Dreiecke gesamt: %d" % tris)
    sag("Gesamtbox  Laenge X %.3f m, Hoehe Y %.3f m, Breite Z %.3f m"
        % (gesamt_max[0] - gesamt_min[0], gesamt_max[1] - gesamt_min[1],
           gesamt_max[2] - gesamt_min[2]))
    return gesamt_min, gesamt_max


def beleuchtung(lo, hi):
    mitte = [(lo[i] + hi[i]) * 0.5 for i in range(3)]
    ausdehnung = max(hi[i] - lo[i] for i in range(3)) * 2.0

    welt = bpy.data.worlds.new("Welt")
    bpy.context.scene.world = welt
    welt.use_nodes = True
    bg = welt.node_tree.nodes["Background"]
    bg.inputs[0].default_value = (0.35, 0.42, 0.55, 1.0)
    bg.inputs[1].default_value = 1.0

    for name, pos, energie, groesse in (
            ("Key", (mitte[0] + ausdehnung, mitte[1] + ausdehnung, mitte[2] + ausdehnung), 900.0, ausdehnung * 0.6),
            ("Fill", (mitte[0] - ausdehnung, mitte[1] + ausdehnung * 0.3, mitte[2] - ausdehnung), 350.0, ausdehnung),
            ("Gegen", (mitte[0], mitte[1] + ausdehnung, mitte[2] - ausdehnung * 1.4), 250.0, ausdehnung * 0.5)):
        dat = bpy.data.lights.new("L_" + name, type="AREA")
        dat.energy = energie
        dat.size = groesse
        ob = bpy.data.objects.new("L_" + name, dat)
        bpy.context.scene.collection.objects.link(ob)
        ob.location = pos
        richtung = mathutils.Vector(mitte) - mathutils.Vector(pos)
        ob.rotation_euler = richtung.to_track_quat("-Z", "Y").to_euler()
    return mitte, ausdehnung


def kamera_und_rendern(objs, lo, hi, mitte, ausdehnung):
    bpy.ops.object.select_all(action="SELECT")
    bpy.context.view_layer.objects.active = objs[0]

    kameras = {}
    blickrichtungen = {
        "vorn":   (0.0, -1.0, 0.0),
        "seitlich": (1.0, 0.0, 0.0),
        "oben":   (0.0, 0.0, -1.0),
        "schraeg": (0.9, -0.75, 0.9),
    }
    for name, richtung in blickrichtungen.items():
        dat = bpy.data.cameras.new("K_" + name)
        dat.lens = 55
        ob = bpy.data.objects.new("K_" + name, dat)
        bpy.context.scene.collection.objects.link(ob)
        d = mathutils.Vector(richtung).normalized() * ausdehnung
        ob.location = mathutils.Vector(mitte) + d
        ob.rotation_euler = (-d).to_track_quat("-Z", "Y").to_euler()
        kameras[name] = ob

    szene = bpy.context.scene
    szene.render.resolution_x = 900
    szene.render.resolution_y = 640
    szene.render.film_transparent = False
    szene.render.image_settings.file_format = "PNG"
    try:
        szene.view_settings.view_transform = "AgX"
    except Exception:
        pass
    pfade = {}
    for name, kam in kameras.items():
        szene.camera = kam
        pfad = os.path.join(ZIEL, "v_%s.png" % name)
        szene.render.filepath = pfad
        bpy.ops.render.render(write_still=True)
        pfade[name] = pfad
    return pfade


def main():
    sag("GLB: %s (%.1f MB)" % (GLB, os.path.getsize(GLB) / 1048576.0))
    objs = importieren(GLB)
    lo, hi = vermessen(objs)
    # Die Messwerte zuerst festschreiben: ein Fehler im Rendern soll die
    # Vermessung nicht wieder mitnehmen (genau so ist schon eine Messung
    # verloren gegangen - siehe AGENTS.md, Ergebnisdatei ist das Mass).
    katalog = os.path.join(ZIEL, "katalog.txt")
    with open(katalog, "w", encoding="utf-8") as f:
        f.write("\n".join(bericht) + "\n")
    mitte, ausdehnung = beleuchtung(lo, hi)
    pfade = kamera_und_rendern(objs, lo, hi, mitte, ausdehnung)
    sag("")
    sag("Bilder: %s" % ", ".join(pfade.values()))
    with open(katalog, "w", encoding="utf-8") as f:
        f.write("\n".join(bericht) + "\n")
    print("KATALOG GESCHRIEBEN:", katalog)


main()
