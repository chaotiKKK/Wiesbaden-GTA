"""Die vorbereiteten Trenn-Stuecke des Plasmacutters in Blender bauen.

Meilenstein Plasmacutter (Auftrag 26.09.2026): Schnittebene, vorbereitete
Stuecke, Glutkante - alles selbst in Blender designed, modelliert und
texturiert. Die Engine-Cubes, die AWiesbadenCuttable bisher nutzt, sind nur
der Platzhalter; dieses Skript ist die Quelle der echten Meshes.

  SM_Cutpiece_Unten  - Maschinenblock-Unterteil (0..50 cm)
  SM_Cutpiece_Oben   - Maschinenblock-Obererteil (50..100 cm)
  SM_Cutpiece_Glut   - die Glutkante (52 x 52 x 2 cm, emissiv)

KONVENTIONEN (wie build_weapons.py, dort gemessen):
  * Alle Masse in ZENTIMETER; Export schreibt scale_length 0.01,
    sonst kommt das Mesh 100-fach gross in UE an.
  * Jedes Stueck hat seinen Ursprung in der eigenen MITTE (die C++-Seite
    setzt es auf Z 25 bzw. Z 75 und skaliert nicht).
  * Y-Spiegelung beim Export (UE-Konvention), danach zurueckgespiegelt.
  * Texturen selbst gerechnet (numpy + Rauschen + Kratzer), keine
    uebernommenen Bilder.

Aufruf (Blender 5.2, dieser Blender ist DEUTSCH - Knoten per Typ suchen):
  blender -b -P Tools/Blender/build_cutpieces.py
"""
import math
import os
import sys

import bpy
import mathutils

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
ZIEL = os.path.join(ROOT, "Content", "Data", "Raw", "Cutpieces")
TEXTUREN = os.path.join(ZIEL, "tex")
os.makedirs(TEXTUREN, exist_ok=True)


# --- Materialien und Texturen ----------------------------------------------

def textur(name, grundfarbe, rauheit=0.035, kratzer=24):
    """Selbst gerechnete Textur: Vertikalverlauf + Rauschen + Kratzer."""
    import numpy as np

    n = 256
    rng = np.random.default_rng(11)
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


def textur_glut():
    """Glutkante: heisses Orange mit Korn und abkuehlenden Randzonen."""
    import numpy as np

    n = 256
    rng = np.random.default_rng(23)
    yy, xx = np.mgrid[0:n, 0:n].astype(np.float32) / n
    # Heiss in der Mitte (Schnittfuge), nach aussen abkuehlend.
    heiss = np.exp(-((xx - 0.5) ** 2) / 0.045) * np.exp(-((yy - 0.5) ** 2) / 0.35)
    korn = rng.normal(0.0, 0.16, (n, n)).astype(np.float32)
    farbe = np.zeros((n, n, 3), dtype=np.float32)
    farbe[:, :, 0] = np.clip(1.0 * heiss + 0.22 + korn, 0.0, 1.0)
    farbe[:, :, 1] = np.clip(0.38 * heiss + 0.05 + korn * 0.5, 0.0, 1.0)
    farbe[:, :, 2] = np.clip(0.06 * heiss + 0.01, 0.0, 1.0)

    bild = bpy.data.images.new("T_Cut_Glut", n, n)
    pixel = np.ones((n, n, 4), dtype=np.float32)
    pixel[:, :, :3] = farbe
    bild.pixels.foreach_set(pixel.reshape(-1).tolist())
    bild.filepath_raw = os.path.join(TEXTUREN, "T_Cut_Glut.png")
    bild.file_format = "PNG"
    bild.save()
    return bild


def bsdf_finden(nt):
    """Principled-BSDF ueber den TYP suchen (dieser Blender ist DEUTSCH)."""
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


def material_glut(bild):
    """Emissives Material fuer die Glutkante (selbst gebaut, kein Standard)."""
    m = bpy.data.materials.new("M_Cut_Glut")
    m.use_nodes = True
    bsdf = bsdf_finden(m.node_tree)
    tex = m.node_tree.nodes.new("ShaderNodeTexImage")
    tex.image = bild
    tex.location = (-420, 220)
    if bsdf is not None:
        m.node_tree.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
        if "Emission Color" in bsdf.inputs:
            m.node_tree.links.new(tex.outputs["Color"], bsdf.inputs["Emission Color"])
            bsdf.inputs["Emission Strength"].default_value = 9.0
        bsdf.inputs["Roughness"].default_value = 0.5
    return m


MAT = {}


def materialien_bauen():
    bild_stahl = textur("T_Cut_Stahl", (0.42, 0.43, 0.46), rauheit=0.032)
    bild_rost = textur("T_Cut_Rost", (0.33, 0.24, 0.18), rauheit=0.070, kratzer=10)
    MAT["stahl"] = material_texturiert("M_Cut_Stahl", bild_stahl, 1.0, 0.42)
    MAT["rost"] = material_texturiert("M_Cut_Rost", bild_rost, 0.6, 0.72)
    MAT["glut"] = material_glut(textur_glut())


# --- Grundkoerper ----------------------------------------------------------

def kasten(halb_x, halb_y, halb_z, mitte, name, mat, teile):
    bpy.ops.mesh.primitive_cube_add(size=2.0, location=mitte)
    ob = bpy.context.object
    ob.name = name
    ob.scale = (halb_x, halb_y, halb_z)
    ob.data.materials.append(mat)
    teile.append(ob)
    return ob


def zylinder(laenge, radius, mitte, name, mat, teile, achse="Z", segmente=16):
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


# --- Die vorbereiteten Stuecke ---------------------------------------------
# Ein Maschinenblock (Kessel-Abschnitt) aus zwei Haelften: jede 50 cm hoch,
# gemeinsam ergeben sie den 1-m-Block, den der Cutter bei Z 50 trennt.
# Ursprung je Stueck in der Stueck-MITTE.

def stueck_unten():
    teile = []
    # Kernblock 50 x 50 x 50, Mitte Z 0 (im Stueck-Rahmen).
    kasten(25.0, 25.0, 25.0, (0.0, 0.0, 0.0), "Kern", MAT["stahl"], teile)
    # Bodenblech mit Ueberstand.
    kasten(28.0, 28.0, 3.0, (0.0, 0.0, -26.5), "Boden", MAT["rost"], teile)
    # Verstrebungen an den Längsseiten.
    for y in (-26.5, 26.5):
        kasten(20.0, 1.5, 20.0, (0.0, y, -2.0), "Rippe", MAT["rost"], teile)
    # Rohrstutzen (Trennseite zeigt nach oben).
    zylinder(10.0, 7.0, (12.0, -12.0, 22.0), "Stutzen", MAT["stahl"], teile)
    # Schraubenkränze an der Trennfuge.
    for winkel in range(0, 360, 45):
        wx = 20.0 * math.cos(math.radians(winkel))
        wy = 20.0 * math.sin(math.radians(winkel))
        zylinder(4.0, 2.2, (wx, wy, 26.0), "Schraube", MAT["rost"], teile)
    return nur_join(teile, "SM_Cutpiece_Unten")


def stueck_oben():
    teile = []
    # Kernblock, Mitte Z 0 (liegt spaeter auf Z 75).
    kasten(25.0, 25.0, 25.0, (0.0, 0.0, 0.0), "Kern", MAT["stahl"], teile)
    # Deckel mit Ueberstand.
    kasten(28.0, 28.0, 3.0, (0.0, 0.0, 26.5), "Deckel", MAT["rost"], teile)
    # Kuehlrippen quer.
    for x in (-16.0, -8.0, 0.0, 8.0, 16.0):
        kasten(1.5, 26.5, 14.0, (x, 0.0, 6.0), "Kuehlrippe", MAT["rost"], teile)
    # Ventil oben.
    zylinder(12.0, 5.0, (-12.0, 12.0, 34.0), "Ventil", MAT["stahl"], teile)
    kasten(6.0, 6.0, 3.0, (-12.0, 12.0, 41.0), "Ventilkopf", MAT["rost"], teile)
    # Schraubenkränze an der Trennfuge (unten).
    for winkel in range(0, 360, 45):
        wx = 20.0 * math.cos(math.radians(winkel))
        wy = 20.0 * math.sin(math.radians(winkel))
        zylinder(4.0, 2.2, (wx, wy, -26.0), "Schraube", MAT["rost"], teile)
    return nur_join(teile, "SM_Cutpiece_Oben")


def stueck_glut():
    """Die Glutkante: 52 x 52 x 2 cm, Ursprung in der Mitte."""
    teile = []
    kasten(26.0, 26.0, 1.0, (0.0, 0.0, 0.0), "Glutplatte", MAT["glut"], teile)
    return nur_join(teile, "SM_Cutpiece_Glut")


# --- Export, Kontrolle, Vorschau -------------------------------------------

def spiegeln_y(objekte):
    for o in objekte:
        o.scale = (o.scale[0], -o.scale[1], o.scale[2])


def exportieren(pfad, objekte):
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
    """Kontrollbild: der komplette Block mit sichtbarer Trennfuge."""
    welt = bpy.data.worlds.new("Welt")
    bpy.context.scene.world = welt
    welt.use_nodes = True
    bg = next((n for n in welt.node_tree.nodes if n.type == "BACKGROUND"), None)
    if bg is not None:
        bg.inputs[0].default_value = (0.10, 0.11, 0.14, 1.0)
        bg.inputs[1].default_value = 2.0

    for name, pos, energie in (("K", (120.0, -140.0, 160.0), 260000.0),
                               ("F", (-80.0, -90.0, 60.0), 90000.0)):
        d = bpy.data.lights.new("L" + name, type="AREA")
        d.energy = energie
        d.size = 200.0
        o = bpy.data.objects.new("L" + name, d)
        bpy.context.scene.collection.objects.link(o)
        o.location = pos
        o.rotation_euler = (mathutils.Vector((0.0, 0.0, 50.0)) - mathutils.Vector(pos)) \
            .to_track_quat("-Z", "Y").to_euler()

    s = bpy.context.scene
    s.render.resolution_x = 1000
    s.render.resolution_y = 1000
    s.render.image_settings.file_format = "PNG"
    try:
        s.view_settings.view_transform = "Standard"
    except Exception:
        pass

    dat = bpy.data.cameras.new("K_Block")
    dat.lens = 42.0
    ob = bpy.data.objects.new("K_Block", dat)
    bpy.context.scene.collection.objects.link(ob)
    ob.location = (150.0, -180.0, 120.0)
    ob.rotation_euler = (mathutils.Vector((0.0, 0.0, 45.0))
                         - mathutils.Vector(ob.location)) \
        .to_track_quat("-Z", "Y").to_euler()
    s.camera = ob
    s.render.filepath = os.path.join(ZIEL, "vorschau_cutpieces.png")
    bpy.ops.render.render(write_still=True)


BAUE = {
    "unten": stueck_unten,
    "oben": stueck_oben,
    "glut": stueck_glut,
}


def main():
    materialien_bauen()
    bericht = []
    gebaut = {}
    for name in ("unten", "oben", "glut"):
        for ob in list(bpy.context.scene.objects):
            bpy.data.objects.remove(ob, do_unlink=True)
        ob = BAUE[name]()
        uv_aufziehen(ob)
        mx, my, mz = masse(ob)
        bericht.append("%-20s %6.1f x %5.1f x %5.1f cm  Dreiecke %d"
                       % (ob.name, mx, my, mz, len(ob.data.polygons)))
        pfad = os.path.join(ZIEL, ob.name + ".fbx")
        exportieren(pfad, [ob])
        bericht.append("    -> " + os.path.basename(pfad))
        gebaut[name] = BAUE[name]()

    # Kontrollbild aus frisch gebauten Stuecken: Block mit Trennfuge.
    for ob in list(bpy.context.scene.objects):
        bpy.data.objects.remove(ob, do_unlink=True)
    unten = BAUE["unten"]()
    oben = BAUE["oben"]()
    glut = BAUE["glut"]()
    unten.location = (0.0, 0.0, 25.0)
    oben.location = (0.0, 0.0, 75.0)
    glut.location = (0.0, 0.0, 50.0)
    vorschau([unten, oben, glut])

    bericht.append("Textur-Ordner: " + TEXTUREN)
    text = "\n".join(bericht) + "\n"
    with open(os.path.join(ZIEL, "bau_cutpieces_bericht.txt"), "w",
              encoding="utf-8") as f:
        f.write(text)
    print(text)


main()
