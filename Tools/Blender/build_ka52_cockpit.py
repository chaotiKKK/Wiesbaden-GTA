"""Baut Cockpit-Innenraum und 30-mm-Kanone fuer die Ka-52 und exportiert FBX.

WARUM DAS SKRIPT EXISTIERT: Das gelieferte Modell ist eine AUSSENansicht -
ein geschlossenes Rumpfmesh mit aufgemalter Kabine. Im Spiel sieht man
daraus im Cockpit nichts: der Pilot schaut in eine leere Kiste, und die
erste Person, die es dort gibt, ist der Rumpf selbst (die Kamera-Komponente
blendet ihn aus, dann schwebt der Pilot im Nichts). Ein sichtbares Cockpit
braucht darum gebaute Geometrie: Sitze, Pulte, Steuerknueppel, Pedale.

KOORDINATEN: Modellraum in Zentimetern, wie das importierte FBX
(Tools/ka52_fbxlage.py, Saved/Diagnose/ka52/fbxlage.txt):
  Rumpf        Y -580 (Nase) bis +826 (Heck), X -438..+433, Z 0..295
  Kabine       Y -500..-250, Oberkante um 220
  Sitze        Tandem: vorn rechts (Schuetze), hinten links (Kapitaen)
  Nase/Ziel    -Y

Die Kanone sitzt auf dem Steuerbord-Pylon bei (360, -230, 95) und zeigt
in Flugrichtung - sie wird in der Komponente (Vehicles/WiesbadenHeliGun-
Component) um diesen Modellpunkt gedreht.

Aufruf:

  "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" ^
    -b --factory-startup --python Tools/Blender/build_ka52_cockpit.py -- ^
    "<Content/Data/Raw/Ka52>"

Schreibt dort ZWEI FBX mit je einem Objekt - nicht eine Datei mit zwei
Objekten, aus zwei guten Gruenden (beide gemessen am 26.09.2026):
  ka52_cockpit.fbx     Cockpit_Interior  Sitze, Pulte, Knueppel, Pedale
  ka52_gun_turret.fbx  Gun_Turret        Pylon, Trommel, Rohr, Bremse

  1. bpy.ops.object.select_all(action="SELECT") beim Join nimmt die GANZE
     Szene. Die Kanone wurde darum in den Pylon gejoint und das FBX
     enthielt nur ein Mesh namens Gun_Turret mit der Kabine darin. Der
     UE-Import brachte danach ein einziges Mesh: die Kabine fehlte im
     Spiel und die Kanone war mit Sitzzeug gefuellt. Deshalb join()_t
     jetzt nur noch die eigene Collection (nur_join).
  2. Der UE-Namensgeber nimmt bei mehreren Meshes in einer FBX den
     Mesh-Namen und fuegt sie fuer den StaticMesh-Import zusammen; ueber
     destination_name laesst sich der Name dann nicht mehr erzwingen. Zwei
     Dateien mit je einem Mesh ergeben dagegen exakt die Namen, die die
     C++-KompONENTEN per ConstructorPath laden (SM_Ka52Cockpit,
     SM_Ka52GunTurret).
"""

import math
import os
import sys

import bpy
import bmesh
import mathutils

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
ZIEL = argv[0] if argv else r"C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Content\Data\Raw\Ka52"
os.makedirs(ZIEL, exist_ok=True)

# --- Masse in Zentimetern (Modellraum) -------------------------------------
KABINE_VORN = -500.0     # Bugkante des Innenraums
KABINE_HINTEN = -235.0   # Schott
BODEN_Z = 74.0           # Kabinenboden
SITZ_X_VORN = 34.0       # Schuetze sitzt rechts (+X)
SITZ_X_HINTEN = -34.0    # Kapitaen sitzt links (-X)
SITZ_Y_VORN = -395.0
SITZ_Y_HINTEN = -282.0

mat_schicht = None
mat_metall = None
mat_dunkel = None
mat_schirm = None


def material(name, farbe, rauheit=0.6, metall=0.0, emission=0.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    bsdf = m.node_tree.nodes.get("Principled BSDF")
    if bsdf:
        bsdf.inputs["Base Color"].default_value = (farbe[0], farbe[1], farbe[2], 1.0)
        bsdf.inputs["Roughness"].default_value = rauheit
        bsdf.inputs["Metallic"].default_value = metall
        if emission > 0.0 and "Emission Color" in bsdf.inputs:
            bsdf.inputs["Emission Color"].default_value = (farbe[0], farbe[1], farbe[2], 1.0)
            bsdf.inputs["Emission Strength"].default_value = emission
    return m


def kasten(halb_x, halb_y, halb_z, mitte, name, mat, sammlung):
    """Quader mit Ursprung in der Mitte."""
    bpy.ops.mesh.primitive_cube_add(size=2.0, location=mitte)
    ob = bpy.context.object
    ob.name = name
    ob.scale = (halb_x, halb_y, halb_z)
    ob.data.materials.append(mat)
    for c in ob.users_collection:
        c.objects.unlink(ob)
    sammlung.objects.link(ob)
    return ob


def zylinder(laenge, radius, mitte, name, mat, sammlung, achse="X", segmente=16):
    bpy.ops.mesh.primitive_cylinder_add(radius=radius, depth=laenge,
                                         vertices=segmente, location=mitte)
    ob = bpy.context.object
    ob.name = name
    if achse == "X":
        ob.rotation_euler = (0.0, math.radians(90.0), 0.0)
    elif achse == "Y":
        ob.rotation_euler = (math.radians(90.0), 0.0, 0.0)
    ob.data.materials.append(mat)
    for c in ob.users_collection:
        c.objects.unlink(ob)
    sammlung.objects.link(ob)
    return ob


def sitz(x, y, name, sammlung):
    """Ein Sitz: Kissen, Lehne, Kopfstuetze, Seitenwangen."""
    teile = []
    teile.append(kasten(23.0, 24.0, 4.0, (x, y, BODEN_Z + 30.0), name + "_Kissen", mat_schicht, sammlung))
    # Lehne: 12 Grad zurueck geneigt, wie ein echter Pilotensitz.
    lehne = kasten(23.0, 6.0, 42.0, (x, y + 22.0, BODEN_Z + 78.0), name + "_Lehne", mat_schicht, sammlung)
    lehne.rotation_euler = (math.radians(-12.0), 0.0, 0.0)
    teile.append(lehne)
    teile.append(kasten(12.0, 7.0, 14.0, (x, y + 30.0, BODEN_Z + 132.0), name + "_Kopfstuetze", mat_schicht, sammlung))
    for s in (-1, 1):
        teile.append(kasten(3.0, 20.0, 20.0, (x + s * 22.0, y + 2.0, BODEN_Z + 52.0),
                            name + "_Wange" + ("_L" if s < 0 else "_R"), mat_schicht, sammlung))
    return teile


TAFEL_NEIGUNG = -24.0     # Grad um X. Negativ, damit die Tafel zum PILOTEN
                           # (groesseres Y) und nach oben zeigt; mit +24
                           # schaute die Flaeche in die Nase und die
                           # Bildschirme steckten IN der Tafel (gemessen
                           # 26.09. - die Vorschau war ein schwarzes Rechteck).


def tafel_punkt(mitte, dx, dy, dz):
    """Weltpunkt aus einem Offset im gedrehten Tafelsystem.

    Ohne das Hilfsding konstruiert man jedes Bedienteil in Weltkoordinaten
    und verliert beim Drehen den Anschluss an die Tafel - genau das war der
    Fehler, den diese Funktion hier beseitigt.
    """
    a = math.radians(TAFEL_NEIGUNG)
    return (mitte[0] + dx,
            mitte[1] + dy * math.cos(a) - dz * math.sin(a),
            mitte[2] + dy * math.sin(a) + dz * math.cos(a))


def pulpit(x, y, name, sammlung):
    """Instrumententafel mit drei Bildschirmen, zum Piloten geneigt."""
    teile = []
    mitte = (x, y, BODEN_Z + 76.0)
    tafel = kasten(46.0, 6.0, 26.0, mitte, name + "_Tafel", mat_dunkel, sammlung)
    tafel.rotation_euler = (math.radians(TAFEL_NEIGUNG), 0.0, 0.0)
    teile.append(tafel)

    # Schirme AUF der Tafelflaeche (dz = +27.5 ueber der halben Hoehe 26,
    # dy = +7 nach hinten), jeder mit derselben Neigung wie die Tafel.
    for i, dx in enumerate((-28.0, 0.0, 28.0)):
        b = kasten(11.0, 1.6, 9.0, tafel_punkt(mitte, dx, 5.5, 26.5),
                   name + "_Schirm" + str(i), mat_schirm, sammlung)
        b.rotation_euler = (math.radians(TAFEL_NEIGUNG), 0.0, 0.0)
        teile.append(b)

    # Bedienknaefte unter den Schirmen und ein Handlauf an der Unterkante:
    # ohne sie steht da eine leere Tafel, an der man nichts bedienen kann.
    for i, dx in enumerate((-30.0, -10.0, 10.0, 30.0)):
        k = kasten(3.5, 3.5, 4.0, tafel_punkt(mitte, dx, 9.0, 14.0),
                   name + "_Knopf" + str(i), mat_metall, sammlung)
        k.rotation_euler = (math.radians(TAFEL_NEIGUNG), 0.0, 0.0)
        teile.append(k)
    leiste = kasten(46.0, 3.0, 3.0, tafel_punkt(mitte, 0.0, 6.0, -27.0),
                    name + "_Leiste", mat_metall, sammlung)
    leiste.rotation_euler = (math.radians(TAFEL_NEIGUNG), 0.0, 0.0)
    teile.append(leiste)
    return teile


def steuerbuegel(x, y, name, sammlung):
    """Zyklik-Stange zwischen den Knien."""
    teile = []
    teile.append(kasten(4.0, 22.0, 4.0, (x, y, BODEN_Z + 18.0), name + "_Buegel", mat_metall, sammlung))
    teile.append(zylinder(26.0, 2.6, (x, y - 20.0, BODEN_Z + 20.0), name + "_Stange", mat_metall, sammlung, achse="X"))
    teile.append(kasten(5.0, 4.0, 4.0, (x, y - 32.0, BODEN_Z + 21.0), name + "_Griff", mat_dunkel, sammlung))
    return teile


def kollektiv(x, y, name, sammlung):
    """Kollektivhebel links vom Sitz."""
    teile = []
    teile.append(kasten(5.0, 9.0, 26.0, (x, y, BODEN_Z + 30.0), name + "_Fuss", mat_dunkel, sammlung))
    teile.append(zylinder(34.0, 2.4, (x, y, BODEN_Z + 58.0), name + "_Hebel", mat_metall, sammlung, achse="Z"))
    teile.append(kasten(5.0, 5.0, 9.0, (x, y, BODEN_Z + 78.0), name + "_Griff", mat_dunkel, sammlung))
    return teile


def pedal(y, name, sammlung):
    teile = []
    for s in (-1, 1):
        p = kasten(9.0, 4.0, 13.0, (s * 16.0, y, BODEN_Z + 16.0), name + "_Pedal", mat_metall, sammlung)
        p.rotation_euler = (math.radians(22.0), 0.0, 0.0)
        teile.append(p)
    teile.append(kasten(34.0, 3.0, 3.0, (0.0, y - 4.0, BODEN_Z + 30.0), name + "_Querholm", mat_metall, sammlung))
    return teile


def nur_join(sammlung, name):
    """Fasst NUR die Objekte einer Collection in ein Mesh zusammen.

    bpy.ops.object.select_all() nimmt dagegen die ganze Szene - so wurde
    am 26.09. die bereits fertige Kabine mit in den Kanonenpylon gejoint
    und das FBX enthielt nur noch Gun_Turret.
    """
    bpy.ops.object.select_all(action="DESELECT")
    eigene = list(sammlung.objects)
    for ob in eigene:
        ob.select_set(True)
    bpy.context.view_layer.objects.active = eigene[0]
    bpy.ops.object.join()
    joined = bpy.context.object
    joined.name = name
    return joined


def cockpit():
    # KEIN read_factory_settings hier: das loescht auch die Materialien, und
    # danach zeigen die gespeicherten Zeiger ins Leere
    # ("StructRNA of type Material has been removed"). Die Szene wird
    # deshalb einmal in main() zurueckgesetzt, VOR den Materialien.
    sammel = bpy.data.collections.new("Cockpit_Interior")
    bpy.context.scene.collection.children.link(sammel)

    # Boden und Schott: ohne sie steht man im Nichts, wenn der Rumpf
    # ausgeblendet ist (die Kamera-Komponente blendet ihn aus).
    kasten(78.0, (KABINE_HINTEN - KABINE_VORN) * 0.5, 3.0,
           (0.0, (KABINE_VORN + KABINE_HINTEN) * 0.5, BODEN_Z),
           "Boden", mat_dunkel, sammel)
    kasten(78.0, 4.0, 70.0, (0.0, KABINE_HINTEN, BODEN_Z + 70.0), "Schott", mat_schicht, sammel)
    # Seitenschotten und Kanzelholm: sie geben der Kabine Halt und nehmen
    # der Kamera den Blick auf die Rumpfnaht.
    for s in (-1, 1):
        kasten(3.0, (KABINE_HINTEN - KABINE_VORN) * 0.5, 40.0,
               (s * 78.0, (KABINE_VORN + KABINE_HINTEN) * 0.5, BODEN_Z + 40.0),
               "Seitenwand" + ("_L" if s < 0 else "_R"), mat_schicht, sammel)
    kasten(78.0, 4.0, 5.0, (0.0, KABINE_VORN + 6.0, BODEN_Z + 128.0), "Kanzelholm", mat_metall, sammel)

    sitz(SITZ_X_VORN, SITZ_Y_VORN, "SitzSchuetze", sammel)
    sitz(SITZ_X_HINTEN, SITZ_Y_HINTEN, "SitzKapitaen", sammel)
    pulpit(SITZ_X_VORN, SITZ_Y_VORN - 62.0, "PultSchuetze", sammel)
    pulpit(SITZ_X_HINTEN, SITZ_Y_HINTEN - 58.0, "PultKapitaen", sammel)
    steuerbuegel(SITZ_X_VORN - 4.0, SITZ_Y_VORN - 44.0, "ZyklikVorn", sammel)
    steuerbuegel(SITZ_X_HINTEN - 4.0, SITZ_Y_HINTEN - 40.0, "ZyklikHinten", sammel)
    kollektiv(SITZ_X_VORN - 40.0, SITZ_Y_VORN - 6.0, "KollektivVorn", sammel)
    kollektiv(SITZ_X_HINTEN - 40.0, SITZ_Y_HINTEN - 2.0, "KollektivHinten", sammel)
    pedal(KABINE_VORN + 42.0, "Pedale", sammel)

    # Mittkonsole zwischen den Sitzen - beim Ka-52 der gemeinsame
    # Bedienblock, an dem auch die Waffenbedienung haengt.
    kasten(16.0, 90.0, 34.0, (0.0, (SITZ_Y_VORN + SITZ_Y_HINTEN) * 0.5, BODEN_Z + 34.0),
           "Mittkonsole", mat_dunkel, sammel)

    # Alles in EIN Objekt zusammenfassen: der Import soll daraus ein Mesh
    # machen, das man der Cockpit-Kamera zuweisen kann.
    return nur_join(sammel, "Cockpit_Interior")


def kanone():
    sammel = bpy.data.collections.new("Gun_Turret")
    # .children (nicht .objects): eine Collection wird als Kind eingehaengt,
    # .objects.link erwartet ein Objekt und meldet sonst "expected a Object
    # type, not Collection" (gemessen).
    bpy.context.scene.collection.children.link(sammel)

    # Pylon und Lagerbock sitzen im Ursprung (das ist der Drehpunkt des
    # Geschuetzes in der Komponente: Modellpunkt 360, -230, 95).
    kasten(26.0, 22.0, 16.0, (0.0, 0.0, -14.0), "Pylon", mat_dunkel, sammel)
    kasten(20.0, 18.0, 14.0, (0.0, 0.0, 4.0), "Lagerbock", mat_metall, sammel)
    for s in (-1, 1):
        kasten(4.0, 16.0, 20.0, (s * 12.0, 0.0, 16.0), "Wiege" + ("_L" if s < 0 else "_R"),
               mat_metall, sammel)
    # Trommel (Munition) links am Rohr.
    kasten(12.0, 30.0, 14.0, (-20.0, 6.0, 8.0), "Munitionstrommel", mat_dunkel, sammel)
    # Rohr: 210 cm lang, danach kommt die Mündung (MuzzlePoint bei x = 210).
    # Radius 5.0 cm entspricht einer 30-mm-Schnellfeuerkanone der 2A42;
    # mit 4.2 las sich das Rohr in der Vorschau als Stock.
    zylinder(196.0, 5.0, (100.0, 0.0, 18.0), "Rohr", mat_metall, sammel, achse="X", segmente=20)
    # Mündungsbremse: kurzer,dickerer Abschnitt am Rohrende mit zwei
    # Kammern, wie an einer 2A42.
    zylinder(22.0, 7.0, (187.0, 0.0, 18.0), "Bremsmantel", mat_dunkel, sammel, achse="X", segmente=16)
    kasten(7.0, 8.0, 8.0, (187.0, 0.0, 18.0), "Muendungsbremse", mat_dunkel, sammel)
    zylinder(14.0, 4.4, (205.0, 0.0, 18.0), "Bremsrohr", mat_metall, sammel, achse="X", segmente=16)

    return nur_join(sammel, "Gun_Turret")


def spiegeln_y(objekte):
    """Spiegelt die Objekte an der Y-Achse - und macht es wieder rueckgaengig.

    WARUM: Der FBX-Export legt die Y-Achse um, egal ob axis_forward="-Y"
    oder "Y" heisst (beide am 26.09. gemessen). Die Kabine stand danach
    bei Y +231..+500, also ueber dem Heck statt ueber der Nase - der
    Rumpf hat die Nase aber bei -Y (Tools/ka52_fbxlage.py). Gespiegelt wird
    deshalb einmal hier, an der einzigen Stelle, an der es hingehört, und
    danach wieder rueckgaengig gemacht, damit die Vorschau die Szenengeometrie
    zeigt, die man gebaut hat.

    Eine Spiegelung hat negative Determinante und dreht damit die
    Umlaufrichtung der Dreiecke: ohne recalc_face_normals kaemen die
   Flaechen im Spiel verkehrt herum und schwarz.
    """
    spiegel = mathutils.Matrix.Diagonal((1.0, -1.0, 1.0, 1.0))
    for ob in objekte:
        # M^-1 · S · M statt nur S: gespiegelt wird an der WELT-Y-Achse.
        # Die Kabine hat ihren Objektursprung auf Y -367 (der Boden war das
        # erste gejointe Objekt), ein reines S auf den Mesh-Daten spiegelte
        # deshalb an -367 und ergab Y -504..-235 statt -231..500
        # (gemessen 26.09. an der Rueck-Lesung der FBX).
        m = ob.matrix_world.inverted() @ spiegel @ ob.matrix_world
        ob.data.transform(m)
        bm = bmesh.new()
        bm.from_mesh(ob.data)
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
        bm.to_mesh(ob.data)
        bm.free()


def exportieren(pfad, objekte):
    # Genau die genannten Meshes, keine Kamera, kein Licht, keine Armatur.
    bpy.ops.object.select_all(action="DESELECT")
    for o in objekte:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objekte[0]

    # MASS: Alle Koordinaten in diesem Skript sind Zentimeter-Zahlen
    # (Blender rechnet aber intern in Metern). Ohne scale_length exportiert
    # Blender die rohen Zahlen mit UnitScaleFactor 1, und UE las daraus
    # 100-fache Werte: die Kabine kam als 162 m breites Rumpf-Asset an
    # (gemessen 26.09.). Mit scale_length 0.01 schreibt der Export
    # UnitScaleFactor 0.01, und UE teilt wieder durch - die Kabine landet
    # mit 162 cm Breite im Asset.
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
            path_mode="STRIP",
            axis_forward="-Y",
            axis_up="Z")
    finally:
        spiegeln_y(objekte)


def vorschau():
    """Drei Ansichten der gebauten Teile als PNG.

    Bewusst aus der Szene gerendert und NICHT aus der zurueckgelesenen FBX:
    die FBX ist gespiegelt und skaliert exportiert, die Kameras stehen aber
    in den Modellkoordinaten. Die Datei prueft der UE-Import (Mass- und
    Lagebericht in Saved/Diagnose/ka52/import_ka52_cockpit_ergebnis.txt),
    das Bild dient dem Entwurf.
    """
    welt = bpy.data.worlds.new("Welt")
    bpy.context.scene.world = welt
    welt.use_nodes = True
    bg = welt.node_tree.nodes["Background"]
    bg.inputs[0].default_value = (0.10, 0.12, 0.16, 1.0)
    bg.inputs[1].default_value = 1.6

    for name, pos, energie in (("K", (-160.0, -520.0, 260.0), 900.0),
                               ("F", (180.0, -180.0, 180.0), 350.0),
                               ("G", (-320.0, -320.0, 300.0), 600.0)):
        d = bpy.data.lights.new("L" + name, type="AREA")
        d.energy = energie
        d.size = 260.0
        o = bpy.data.objects.new("L" + name, d)
        bpy.context.scene.collection.objects.link(o)
        o.location = pos
        ziel = (0.0, -350.0, 120.0) if name != "G" else (60.0, 0.0, 20.0)
        o.rotation_euler = (mathutils.Vector(ziel)
                            - mathutils.Vector(pos)).to_track_quat("-Z", "Y").to_euler()

    s = bpy.context.scene
    s.render.resolution_x = 900
    s.render.resolution_y = 640
    s.render.image_settings.file_format = "PNG"
    try:
        s.view_settings.view_transform = "Standard"
    except Exception:
        pass

    # Die Kanone wird als eigenes Asset importiert und von der Komponente an
    # den Modellpunkt (360, -230, 95) gesetzt - ihr FBX hat den Ursprung
    # also im Drehpunkt, nicht im Modellraum. Ihre Kamera steht darum bei 0
    # und nicht beim Modellpunkt; mit dem Modellpunkt war das Bild leer
    # (gemessen 26.09.).
    aufnahmen = (
        # Pilotensicht: Augenhoehe ueber dem vorderen Sitz, Blick nach vorn
        # auf die Tafel. Frueher stand die Kamera 75 cm VOR dem Sitz und
        # sah nach hinten in die Lehne des Hinterplatzes.
        ("cockpit_pilot", (34.0, -392.0, 206.0), (34.0, -455.0, 165.0), 28.0),
        ("cockpit_oben", (0.0, -360.0, 450.0), (0.0, -360.0, 100.0), 45.0),
        # Kanone im Modellraum des Assets: Rohr zeigt nach +X, Laenge 222.
        ("kanone", (-170.0, -280.0, 165.0), (110.0, 0.0, 25.0), 50.0),
    )
    for name, pos, ziel, winkel in aufnahmen:
        dat = bpy.data.cameras.new("K_" + name)
        dat.lens = winkel
        ob = bpy.data.objects.new("K_" + name, dat)
        bpy.context.scene.collection.objects.link(ob)
        ob.location = pos
        ob.rotation_euler = (mathutils.Vector(ziel) - mathutils.Vector(pos))             .to_track_quat("-Z", "Y").to_euler()
        s.camera = ob
        s.render.filepath = os.path.join(ZIEL, "vorschau_" + name + ".png")
        bpy.ops.render.render(write_still=True)


def pruefe_fbx(pfad):
    """Liest die geschriebene FBX zurueck und misst sie im FBX-Sprachraum.

    Nötig, weil sich aus dem Blender-Szenenmass nicht schliessen laesst, ob
    der Export oder der UE-Import die Achse dreht - beides tat am 26.09.,
    und die Zahlen der beiden Seiten sahen identisch aus. Der Weg ueber den
    FBX-Sprachraum macht sichtbar, was tatsaechlich in der Datei steht.
    """
    alt_objekte = list(bpy.context.scene.objects)
    for ob in alt_objekte:
        bpy.data.objects.remove(ob, do_unlink=True)
    # scale_length fest auf 1: der FBX-Import teilt durch die
    # Szeneneinheit, und der Export hat 0.01 geschrieben. Ohne diese Zeile
    # war die erste Messung 100-fach und die zweite 1-fach, je nachdem, ob
    # schon zurueckgesetzt war - dieselbe Datei, zwei Ergebnisse.
    bpy.context.scene.unit_settings.scale_length = 1.0
    bpy.ops.import_scene.fbx(filepath=os.path.abspath(pfad), global_scale=100.0)
    importiert = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    text = "%-22s" % os.path.basename(pfad)
    if not importiert:
        return text + " KEIN MESH IN DER FBX"
    for ob in importiert:
        ko = [ob.matrix_world @ mathutils.Vector(c) for c in ob.bound_box]
        xs = [p.x for p in ko]
        ys = [p.y for p in ko]
        zs = [p.z for p in ko]
        text += ("%s: X %.1f..%.1f Y %.1f..%.1f Z %.1f..%.1f"
                 % (ob.name, min(xs), max(xs), min(ys), max(ys),
                    min(zs), max(zs)))
    # Diese Funktion laeuft zuletzt: die gebaute Szene wird nicht wieder
    # gebraucht, das Zuruecksetzen raeumt nur den Import auf.
    bpy.ops.wm.read_factory_settings(use_empty=True)
    return text


def main():
    global mat_schicht, mat_metall, mat_dunkel, mat_schirm
    bpy.ops.wm.read_factory_settings(use_empty=True)
    mat_schicht = material("Ka52_Sitzbezug", (0.13, 0.14, 0.17), rauheit=0.85)
    mat_metall = material("Ka52_Metall", (0.34, 0.36, 0.40), rauheit=0.35, metall=0.9)
    # Die Instrumente sind nicht tiefschwarz: 0.05 gibt im Cockpit eine
    # geschlossene dunkle Flaeche, in der man nichts erkennt (Vorschau
    # 26.09.). Etneues Grau plus kräftige Schirme machen die Kabine lesbar.
    mat_dunkel = material("Ka52_Instrument", (0.11, 0.115, 0.13), rauheit=0.7)
    mat_schirm = material("Ka52_Schirm", (0.05, 0.26, 0.33), rauheit=0.25, emission=2.4)

    kabine = cockpit()
    pfad_cockpit = os.path.join(ZIEL, "ka52_cockpit.fbx")
    exportieren(pfad_cockpit, [kabine])

    kanone_ = kanone()
    pfad_kanone = os.path.join(ZIEL, "ka52_gun_turret.fbx")
    exportieren(pfad_kanone, [kanone_])

    # Masse der Szene mitschreiben: sie muessen exakt den Modellkoordinaten
    # entsprechen, die das Skript oben dokumentiert. Die Masse der FBX
    # kommen weiter unten - pruefe_fbx setzt die Szene zurueck und
    # vernichtet damit die gebaute Geometrie.
    bericht = ["Blender %s" % bpy.app.version_string,
               "Cockpit_Interior: %d Objekt(e) nach dem Join"
               % len([o for o in bpy.context.scene.objects if o.name == "Cockpit_Interior"]),
               "Gun_Turret:       %d Objekt(e) nach dem Join"
               % len([o for o in bpy.context.scene.objects if o.name == "Gun_Turret"])]
    for ob in (kabine, kanone_):
        ko = [ob.matrix_world @ mathutils.Vector(c) for c in ob.bound_box]
        xs = [p.x for p in ko]
        ys = [p.y for p in ko]
        zs = [p.z for p in ko]
        bericht.append("SZENE %-16s X %.1f..%.1f  Y %.1f..%.1f  Z %.1f..%.1f cm"
                       % (ob.name, min(xs), max(xs), min(ys), max(ys),
                          min(zs), max(zs)))
    for pfad in (pfad_cockpit, pfad_kanone):
        bericht.append("EXPORT %s  %d Byte"
                       % (os.path.basename(pfad), os.path.getsize(pfad)))

    # Vorschau: aus der Sicht des Piloten (Augenhoehe) und von oben. Ohne
    # Bild traegt man Kabine und Kanone ins Spiel, ohne je gesehen zu
    # haben, ob da etwas steht. Muss vor pruefe_fbx laufen - das loescht
    # die Szene.
    vorschau()

    # Was wirklich in den Dateien steht, gemessen an einer Rueck-Lesung:
    # so ist der Unterschied zwischen "der Export dreht die Achse" und
    # "der UE-Import dreht sie" vom Blatt zu nehmen.
    for pfad in (pfad_cockpit, pfad_kanone):
        bericht.append("FBX  " + pruefe_fbx(pfad))
    with open(os.path.join(ZIEL, "ka52_export_bericht.txt"), "w", encoding="utf-8") as fh:
        fh.write("\n".join(bericht) + "\n")
    print("\n".join(bericht))


main()
