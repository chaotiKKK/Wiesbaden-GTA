"""Die acht Waffen des Auftrags in Blender bauen, texturieren und exportieren.

Auftrag 26.09.2026: "alle assets selbst in blender erstellen und texturieren".
Dieses Skript ist die Quelle der Waffen-Meshes - die uassets sind git-ignoriert,
das Skript ist getrackt (Muster der uebrigen Tools/Blender/build_*.py).

  Pistole, Gewehr, MG, Laserpistole, Lichtschwert, Raketenwerfer,
  Granatwerfer, Plasmacutter

KONVENTIONEN (wie build_ka52_cockpit.py, dort gemessen):
  * Alle Masszahlen sind ZENTIMETER; der Export schreibt scale_length 0.01,
    damit UE wieder bei Zentimetern landet (ohne das kam das Cockpit
    100-fach gross an, 26.09. gemessen).
  * Lauf/Blick zeigt nach +X, "oben" ist +Z, Ursprung am Griff.
  * Die FBX wird ueber spiegeln_y exportiert (Y-Invertierung fuer UE) und
    danach zurueckgespiegelt - die Vorschau rendern wir aus der SZENE.

TEXTUR: jede Materialfamilie bekommt eine selbst gerechnete PNG-Textur
(Verlauf + Rauschen + Kratzer, numpy) mit Smart-UV. Keine uebernommenen
Bilder - der Auftrag verlangt eigene Texturierung.

Aufruf (Blender 5.2):
  blender -b -P Tools/Blender/build_weapons.py -- alle
  blender -b -P Tools/Blender/build_weapons.py -- pistole lichtschwert
"""
import math
import os
import sys

import bpy
import mathutils

sys.path.append(str(os.path.dirname(os.path.abspath(__file__))))

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
ZIEL = os.path.join(ROOT, "Content", "Data", "Raw", "Waffen")
TEXTUREN = os.path.join(ZIEL, "tex")
os.makedirs(TEXTUREN, exist_ok=True)

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else ["alle"]


# --- Materialien und Texturen ----------------------------------------------

def textur(name, grundfarbe, rauheit=0.035, kratzer=24):
    """Selbst gerechnete Textur: Vertikalverlauf + Rauschen + Kratzer.

    numpy ist im Blender-Python enthalten; gerechnet statt uebernommen -
    der Auftrag verlangt eigene Texturen.
    """
    import numpy as np

    n = 256
    rng = np.random.default_rng(7)
    basis = np.array(grundfarbe, dtype=np.float32)
    verlauf = np.linspace(1.12, 0.88, n, dtype=np.float32)[:, None]
    rauschen = rng.normal(0.0, rauheit, (n, n)).astype(np.float32)
    farbe = basis[None, None, :] * (verlauf[:, :, None] + rauschen[:, :, None])

    for _ in range(kratzer):
        x0, y0 = int(rng.integers(0, n)), int(rng.integers(0, n))
        laenge = int(rng.integers(8, 60))
        winkel = rng.uniform(0.0, math.pi)
        for t in range(laenge):
            x = int(x0 + t * math.cos(winkel)) % n
            y = int(y0 + t * math.sin(winkel)) % n
            farbe[y, x] *= 1.22

    bild = bpy.data.images.new(name, n, n)
    pixel = np.ones((n, n, 4), dtype=np.float32)
    pixel[:, :, :3] = np.clip(farbe, 0.0, 1.0)
    bild.pixels.foreach_set(pixel.reshape(-1).tolist())
    bild.filepath_raw = os.path.join(TEXTUREN, name + ".png")
    bild.file_format = "PNG"
    bild.save()
    return bild


def bsdf_finden(nt):
    """Principled-BSDF ueber den TYP suchen: dieser Blender ist DEUTSCH,
    die Knotennamen sind lokalisiert (Welt: "Hintergrund", nicht
    "Background" - am 26.09.2026 genau daran gescheitert)."""
    for n in nt.nodes:
        if n.type == "BSDF_PRINCIPLED":
            return n
    return None


def material_texturiert(name, bild, metall=1.0, rauheit=0.45):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    bsdf = bsdf_finden(nt)
    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.image = bild
    tex.location = (-420, 220)
    if bsdf is not None:
        nt.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    bsdf.inputs["Metallic"].default_value = metall
    bsdf.inputs["Roughness"].default_value = rauheit
    return m


def material(name, farbe, rauheit=0.6, metall=0.0, emission=0.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    bsdf = bsdf_finden(m.node_tree)
    if bsdf:
        bsdf.inputs["Base Color"].default_value = (farbe[0], farbe[1], farbe[2], 1.0)
        bsdf.inputs["Roughness"].default_value = rauheit
        bsdf.inputs["Metallic"].default_value = metall
        if emission > 0.0 and "Emission Color" in bsdf.inputs:
            bsdf.inputs["Emission Color"].default_value = (farbe[0], farbe[1], farbe[2], 1.0)
            bsdf.inputs["Emission Strength"].default_value = emission
    return m


MAT = {}


def materialien_bauen():
    bild_metall = textur("T_Waffe_Metall", (0.46, 0.47, 0.50), rauheit=0.030)
    bild_dunkel = textur("T_Waffe_Dunkel", (0.20, 0.20, 0.22), rauheit=0.025)
    bild_kunst = textur("T_Waffe_Kunststoff", (0.26, 0.27, 0.26), rauheit=0.050, kratzer=8)
    MAT["metall"] = material_texturiert("M_Waffe_Metall", bild_metall, 1.0, 0.38)
    MAT["dunkel"] = material_texturiert("M_Waffe_Dunkel", bild_dunkel, 0.9, 0.55)
    MAT["kunst"] = material_texturiert("M_Waffe_Kunststoff", bild_kunst, 0.0, 0.80)
    MAT["holz"] = material("M_Waffe_Holz", (0.24, 0.14, 0.07), 0.65, 0.0)
    MAT["glut"] = material("M_Waffe_Glut", (1.0, 0.35, 0.05), 0.4, 0.0, emission=8.0)
    MAT["laser"] = material("M_Waffe_Laser", (0.2, 0.7, 1.0), 0.3, 0.0, emission=14.0)
    MAT["klinge"] = material("M_Waffe_Klinge", (0.35, 0.85, 1.0), 0.2, 0.0, emission=24.0)


# --- Grundkoerper ----------------------------------------------------------

def kasten(halb_x, halb_y, halb_z, mitte, name, mat, teile, winkel=None):
    bpy.ops.mesh.primitive_cube_add(size=2.0, location=mitte)
    ob = bpy.context.object
    ob.name = name
    ob.scale = (halb_x, halb_y, halb_z)
    if winkel:
        ob.rotation_euler = (math.radians(winkel[0]), math.radians(winkel[1]),
                             math.radians(winkel[2]))
    ob.data.materials.append(mat)
    teile.append(ob)
    return ob


def zylinder(laenge, radius, mitte, name, mat, teile, achse="X", segmente=16):
    bpy.ops.mesh.primitive_cylinder_add(radius=radius, depth=laenge,
                                        vertices=segmente, location=mitte)
    ob = bpy.context.object
    ob.name = name
    if achse == "X":
        ob.rotation_euler = (0.0, math.radians(90.0), 0.0)
    elif achse == "Y":
        ob.rotation_euler = (math.radians(90.0), 0.0, 0.0)
    ob.data.materials.append(mat)
    teile.append(ob)
    return ob


def nur_join(teile, name):
    bpy.ops.object.select_all(action="DESELECT")
    for o in teile:
        o.select_set(True)
    bpy.context.view_layer.objects.active = teile[0]
    if len(teile) > 1:
        bpy.ops.object.join()
    ob = bpy.context.object
    ob.name = name
    return ob


def uv_aufziehen(ob):
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    try:
        bpy.ops.uv.smart_project(angle_limit=math.radians(66.0))
    except Exception:
        pass
    bpy.ops.object.mode_set(mode="OBJECT")


# --- Die acht Waffen -------------------------------------------------------

def pistole():
    """Dienstpistole: Schlitten, Lauf, Griff, Magazin, Abzugbuegel."""
    t = []
    kasten(9.0, 1.6, 2.2, (2.0, 0.0, 6.0), "P_Schlitten", MAT["metall"], t)
    zylinder(14.0, 1.1, (10.0, 0.0, 6.4), "P_Lauf", MAT["dunkel"], t)
    kasten(3.2, 1.5, 1.0, (17.2, 0.0, 6.4), "P_Muendung", MAT["dunkel"], t)
    griff = kasten(2.6, 1.8, 5.6, (-3.6, 0.0, 0.6), "P_Griff", MAT["kunst"], t,
                   winkel=(0.0, -14.0, 0.0))
    kasten(1.5, 1.2, 2.2, (-2.0, 0.0, 2.6), "P_Magazin", MAT["dunkel"], t,
           winkel=(0.0, -14.0, 0.0))
    # Abzugbuegel: zwei Schenkel + Steg.
    kasten(2.4, 0.35, 0.35, (0.8, 0.0, 1.8), "P_Buegel_v", MAT["metall"], t)
    kasten(0.35, 0.35, 1.6, (3.0, 0.0, 2.8), "P_Buegel_h", MAT["metall"], t)
    kasten(0.35, 0.35, 1.6, (-1.4, 0.0, 2.8), "P_Buegel_r", MAT["metall"], t)
    kasten(0.8, 0.35, 0.9, (0.9, 0.0, 3.4), "P_Abzug", MAT["dunkel"], t)
    kasten(0.5, 0.7, 0.6, (10.0, 0.0, 8.6), "P_Kimme", MAT["dunkel"], t)
    kasten(0.5, 0.7, 0.6, (-1.0, 0.0, 8.6), "P_Korn", MAT["dunkel"], t)
    return nur_join(t, "SM_Waffe_Pistole")


def gewehr():
    """Sturmgewehr: Handschutz, Gehaeuse, Magazin, Schaft, Visier."""
    t = []
    zylinder(34.0, 1.2, (26.0, 0.0, 5.6), "G_Lauf", MAT["metall"], t)
    zylinder(4.0, 1.9, (42.0, 0.0, 5.6), "G_Muendung", MAT["dunkel"], t)
    kasten(10.0, 2.4, 2.6, (20.0, 0.0, 5.4), "G_Handschutz", MAT["kunst"], t)
    kasten(11.0, 2.2, 3.4, (1.0, 0.0, 6.2), "G_Gehaeuse", MAT["metall"], t)
    kasten(3.4, 1.6, 4.6, (1.6, 0.0, 0.2), "G_Magazin", MAT["kunst"], t,
           winkel=(0.0, 12.0, 0.0))
    griff = kasten(2.0, 1.6, 4.4, (-5.2, 0.0, 1.4), "G_Griff", MAT["kunst"], t,
                   winkel=(0.0, -18.0, 0.0))
    kasten(9.0, 1.9, 2.6, (-13.0, 0.0, 5.6), "G_Schaft", MAT["kunst"], t)
    kasten(1.6, 1.9, 3.2, (-21.0, 0.0, 5.2), "G_Schaftkappe", MAT["dunkel"], t)
    kasten(6.0, 1.2, 1.4, (3.0, 0.0, 10.4), "G_Schiene", MAT["dunkel"], t)
    kasten(2.2, 1.6, 2.0, (5.0, 0.0, 12.2), "G_ZF", MAT["dunkel"], t)
    return nur_join(t, "SM_Waffe_Gewehr")


def mg():
    """Maschinengewehr: langer Lauf, Zweibein, Patronenkasten, Schulterstuetze."""
    t = []
    zylinder(52.0, 1.4, (34.0, 0.0, 6.0), "MG_Lauf", MAT["metall"], t)
    zylinder(7.0, 2.4, (55.0, 0.0, 6.0), "MG_Zug", MAT["dunkel"], t)
    kasten(12.0, 2.8, 3.6, (8.0, 0.0, 6.0), "MG_Gehaeuse", MAT["metall"], t)
    kasten(4.6, 3.4, 4.2, (-2.0, -3.2, 3.6), "MG_Kasten", MAT["kunst"], t)
    kasten(2.2, 1.8, 4.6, (-6.4, 0.0, 1.8), "MG_Griff", MAT["kunst"], t,
           winkel=(0.0, -18.0, 0.0))
    kasten(2.2, 1.8, 4.6, (-10.4, 0.0, 2.2), "MG_Griff2", MAT["kunst"], t,
           winkel=(0.0, -24.0, 0.0))
    kasten(10.0, 2.2, 3.0, (-16.0, 0.0, 6.4), "MG_Stuetze", MAT["metall"], t)
    kasten(1.4, 2.6, 3.4, (-25.0, 0.0, 6.0), "MG_Schulter", MAT["kunst"], t)
    # Zweibein: zwei schraege Beine unter dem Lauf.
    for seite in (-1.0, 1.0):
        kasten(0.5, 0.5, 7.0, (34.0, seite * 3.2, -0.6), "MG_Bein" + str(seite),
               MAT["metall"], t, winkel=(seite * 16.0, 0.0, 0.0))
    kasten(4.0, 1.4, 1.2, (12.0, 0.0, 10.6), "MG_Kimme", MAT["dunkel"], t)
    return nur_join(t, "SM_Waffe_MG")


def laserpistole():
    """Energiepistole: Korpus, Spule, Gluehraehre, Gruiff."""
    t = []
    kasten(8.0, 1.9, 2.6, (2.0, 0.0, 6.2), "L_Korpus", MAT["metall"], t)
    zylinder(6.0, 1.6, (11.0, 0.0, 6.2), "L_Spule", MAT["dunkel"], t)
    zylinder(3.4, 1.05, (15.6, 0.0, 6.2), "L_Roehre", MAT["laser"], t)
    zylinder(2.2, 2.1, (4.0, 0.0, 6.2), "L_Ring", MAT["laser"], t, achse="X")
    kasten(2.6, 1.8, 5.4, (-3.4, 0.0, 1.4), "L_Griff", MAT["kunst"], t,
           winkel=(0.0, -14.0, 0.0))
    kasten(1.8, 1.3, 1.2, (0.6, 0.0, 3.2), "L_Abzug", MAT["dunkel"], t)
    kasten(3.0, 1.1, 1.2, (1.0, 0.0, 9.2), "L_Schiene", MAT["dunkel"], t)
    zylinder(1.2, 0.7, (9.6, 0.0, 9.2), "L_Ziel", MAT["laser"], t)
    return nur_join(t, "SM_Waffe_Laserpistole")


def lichtschwert():
    """Energieklinge: Gruiff mit Knauf, Knopf, leuchtende Klinge."""
    t = []
    zylinder(26.0, 1.9, (0.0, 0.0, 0.0), "S_Griff", MAT["dunkel"], t, achse="Z")
    zylinder(3.0, 2.4, (0.0, 0.0, -14.0), "S_Knauf", MAT["metall"], t, achse="Z")
    zylinder(2.4, 2.2, (0.0, 0.0, 14.0), "S_Kopf", MAT["metall"], t, achse="Z")
    kasten(1.2, 0.8, 1.6, (0.0, 2.2, 4.0), "S_Knopf", MAT["glut"], t)
    zylinder(4.0, 1.2, (0.0, 0.0, 17.0), "S_Emitter", MAT["metall"], t, achse="Z")
    zylinder(110.0, 1.75, (0.0, 0.0, 73.0), "S_Klinge", MAT["klinge"], t, achse="Z",
             segmente=20)
    return nur_join(t, "SM_Waffe_Lichtschwert")


def raketenwerfer():
    """Schulterrohr mit Visier, Gruiff und Rakete im Ansatz."""
    t = []
    zylinder(110.0, 4.6, (14.0, 0.0, 8.0), "R_Rohr", MAT["metall"], t, segmente=20)
    zylinder(6.0, 5.6, (64.0, 0.0, 8.0), "R_Muendung", MAT["dunkel"], t, segmente=20)
    zylinder(6.0, 5.6, (-38.0, 0.0, 8.0), "R_Endstueck", MAT["dunkel"], t, segmente=20)
    zylinder(4.4, 3.2, (-43.0, 0.0, 8.0), "R_Rakete", MAT["glut"], t)
    kasten(3.2, 2.0, 3.2, (2.0, 0.0, 0.6), "R_Griff", MAT["kunst"], t,
           winkel=(0.0, -16.0, 0.0))
    kasten(2.2, 1.6, 2.4, (10.0, 0.0, 1.6), "R_Griff2", MAT["kunst"], t,
           winkel=(0.0, -12.0, 0.0))
    kasten(1.4, 1.2, 4.4, (14.0, 0.0, 13.6), "R_Visier", MAT["dunkel"], t)
    kasten(6.0, 1.6, 1.4, (22.0, 0.0, 13.2), "R_Schiene", MAT["dunkel"], t)
    kasten(10.0, 3.4, 1.8, (-6.0, 0.0, 2.2), "R_Stuetze", MAT["kunst"], t)
    return nur_join(t, "SM_Waffe_Raketenwerfer")


def granatwerfer():
    """Unterlaengrohr mit Trommel, Gruiff und Visier."""
    t = []
    zylinder(46.0, 3.4, (16.0, 0.0, 8.0), "GL_Rohr", MAT["metall"], t, segmente=20)
    zylinder(5.0, 4.2, (38.0, 0.0, 8.0), "GL_Muendung", MAT["dunkel"], t, segmente=20)
    zylinder(9.0, 5.2, (2.0, 0.0, 6.4), "GL_Trommel", MAT["dunkel"], t, achse="Y",
             segmente=20)
    kasten(3.0, 1.9, 3.4, (-4.0, 0.0, 0.8), "GL_Griff", MAT["kunst"], t,
           winkel=(0.0, -16.0, 0.0))
    kasten(2.0, 1.5, 2.6, (8.0, 0.0, 2.2), "GL_Griff2", MAT["kunst"], t,
           winkel=(0.0, -12.0, 0.0))
    kasten(1.4, 1.2, 3.6, (6.0, 0.0, 12.2), "GL_Visier", MAT["dunkel"], t)
    kasten(8.0, 1.5, 1.2, (12.0, 0.0, 11.8), "GL_Schiene", MAT["dunkel"], t)
    kasten(2.6, 1.2, 1.6, (1.0, 0.0, 10.4), "GL_Kimme", MAT["dunkel"], t)
    return nur_join(t, "SM_Waffe_Granatwerfer")


def plasmacutter():
    """Werkzeug: Korpus, zwei Elektroden, Schneidduese, Gruiff mit Knauf."""
    t = []
    kasten(7.0, 3.2, 4.2, (0.0, 0.0, 8.0), "PC_Korpus", MAT["metall"], t)
    zylinder(14.0, 1.7, (12.0, 0.0, 8.0), "PC_Hals", MAT["dunkel"], t)
    zylinder(3.0, 2.6, (20.0, 0.0, 8.0), "PC_Duese", MAT["metall"], t)
    zylinder(1.6, 1.0, (22.6, 0.0, 8.0), "PC_Elektrode", MAT["glut"], t)
    for seite in (-1.0, 1.0):
        zylinder(9.0, 1.1, (14.0, seite * 2.6, 8.0), "PC_Schiene" + str(seite),
                 MAT["dunkel"], t)
    kasten(3.2, 2.6, 5.2, (-3.6, 0.0, 1.8), "PC_Griff", MAT["kunst"], t,
           winkel=(0.0, -12.0, 0.0))
    zylinder(1.4, 1.2, (-8.2, 0.0, -1.4), "PC_Knauf", MAT["glut"], t, achse="Z")
    kasten(1.6, 1.2, 1.4, (0.6, 0.0, 3.6), "PC_Abzug", MAT["dunkel"], t)
    kasten(2.6, 1.4, 1.2, (1.0, 0.0, 12.8), "PC_Buegel", MAT["dunkel"], t)
    zylinder(2.2, 0.8, (6.0, 0.0, 12.8), "PC_Zielpunkt", MAT["laser"], t)
    return nur_join(t, "SM_Waffe_Plasmacutter")


BAUE = {
    "pistole": pistole,
    "gewehr": gewehr,
    "mg": mg,
    "laserpistole": laserpistole,
    "lichtschwert": lichtschwert,
    "raketenwerfer": raketenwerfer,
    "granatwerfer": granatwerfer,
    "plasmacutter": plasmacutter,
}


# --- Export, Kontrolle, Vorschau -------------------------------------------

def spiegeln_y(objekte):
    for o in objekte:
        o.scale = (o.scale[0], -o.scale[1], o.scale[2])


def exportieren(pfad, objekte):
    # scale_length 0.01 + Spiegelung wie im Ka52-Builder (dort gemessen,
    # dass ohne beide der Massstab 100-fach und die Achse gedreht ankommt).
    bpy.ops.object.select_all(action="DESELECT")
    for o in objekte:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objekte[0]
    bpy.context.scene.unit_settings.scale_length = 0.01
    spiegeln_y(objekte)
    try:
        bpy.ops.export_scene.fbx(
            filepath=pfad,
            use_selection=True,
            apply_unit_scale=True,
            global_scale=1.0,
            apply_scale_options="FBX_SCALE_NONE",
            object_types={"MESH"},
            use_mesh_modifiers=False,
            mesh_smooth_type="FACE",
            path_mode="COPY",
            axis_forward="-Y",
            axis_up="Z")
    finally:
        spiegeln_y(objekte)


def masse(ob):
    ko = [ob.matrix_world @ mathutils.Vector(c) for c in ob.bound_box]
    xs = [v.x for v in ko]
    ys = [v.y for v in ko]
    zs = [v.z for v in ko]
    return (max(xs) - min(xs), max(ys) - min(ys), max(zs) - min(zs))


def vorschau(objekte):
    """Ein Kontrollbild: alle Waffen nebeneinander in einer Reihe."""
    welt = bpy.data.worlds.new("Welt")
    bpy.context.scene.world = welt
    welt.use_nodes = True
    bg = next((n for n in welt.node_tree.nodes if n.type == "BACKGROUND"), None)
    if bg is not None:
        bg.inputs[0].default_value = (0.12, 0.13, 0.17, 1.0)
        bg.inputs[1].default_value = 2.2

    for name, pos, energie in (("K", (0.0, -150.0, 120.0), 260000.0),
                               ("F", (90.0, -70.0, 60.0), 90000.0),
                               ("S", (-90.0, -40.0, 40.0), 45000.0)):
        d = bpy.data.lights.new("L" + name, type="AREA")
        d.energy = energie
        d.size = 300.0
        o = bpy.data.objects.new("L" + name, d)
        bpy.context.scene.collection.objects.link(o)
        o.location = pos
        o.rotation_euler = (mathutils.Vector((0.0, 0.0, 8.0)) - mathutils.Vector(pos)) \
            .to_track_quat("-Z", "Y").to_euler()

    s = bpy.context.scene
    s.render.resolution_x = 1400
    s.render.resolution_y = 1000
    s.render.image_settings.file_format = "PNG"
    try:
        s.view_settings.view_transform = "Standard"
    except Exception:
        pass

    # Blick von oben wie ein Planblatt: die acht Waffen liegen parallel,
    # jede in ihrer eigenen Reihe - so ist jede einzeln erkennbar.
    dat = bpy.data.cameras.new("K_Reihe")
    dat.lens = 30.0
    ob = bpy.data.objects.new("K_Reihe", dat)
    bpy.context.scene.collection.objects.link(ob)
    ob.location = (0.0, -12.0, 205.0)
    ob.rotation_euler = (mathutils.Vector((0.0, 0.0, 8.0))
                         - mathutils.Vector(ob.location)) \
        .to_track_quat("-Z", "Y").to_euler()
    s.camera = ob
    s.render.filepath = os.path.join(ZIEL, "vorschau_waffen_reihe.png")
    bpy.ops.render.render(write_still=True)


def main():
    materialien_bauen()
    namen = list(BAUE) if "alle" in argv else [n for n in argv if n in BAUE]
    gebaut = []
    bericht = []
    for name in namen:
        # Szene leeren: je Waffe ein eigener Export ohne Reste.
        for ob in list(bpy.context.scene.objects):
            bpy.data.objects.remove(ob, do_unlink=True)
        ob = BAUE[name]()
        uv_aufziehen(ob)
        mx, my, mz = masse(ob)
        bericht.append("%-16s %6.1f x %5.1f x %5.1f cm  Dreiecke %d"
                       % (name, mx, my, mz, len(ob.data.polygons)))
        pfad = os.path.join(ZIEL, ob.name + ".fbx")
        exportieren(pfad, [ob])
        bericht.append("    -> " + os.path.basename(pfad))
        gebaut.append((name, ob))

    # Kontrollbild aus einer frisch gebauten Reihe (die Exporte haben die
    # Szene jeweils geleert; die Bilder dienen dem Entwurf, der Massstab
    # steht im Bericht).
    for ob in list(bpy.context.scene.objects):
        bpy.data.objects.remove(ob, do_unlink=True)
    # Planblatt: jede Waffe laengs im Bild (Lauf zeigt nach unten), die
    # acht Reihen nebeneinander. Das Lichtschwert liegt quer dazu - es ist
    # 143 cm lang und wuerde sonst aus dem Blatt wachsen.
    reihe = []
    x = -66.5
    for name in namen:
        ob = BAUE[name]()
        bpy.ops.object.select_all(action="DESELECT")
        ob.select_set(True)
        bpy.context.view_layer.objects.active = ob
        bpy.ops.object.origin_set(type="ORIGIN_GEOMETRY", center="MEDIAN")
        if name == "lichtschwert":
            ob.rotation_euler = (math.radians(90.0), 0.0, 0.0)
        else:
            ob.rotation_euler = (0.0, 0.0, math.radians(90.0))
        ob.location = (x, 0.0, 8.0)
        x += 19.0
        reihe.append(ob)
    vorschau(reihe)

    bericht.append("Textur-Ordner: " + TEXTUREN)
    text = "\n".join(bericht) + "\n"
    with open(os.path.join(ZIEL, "bau_waffen_bericht.txt"), "w", encoding="utf-8") as f:
        f.write(text)
    print(text)


main()
