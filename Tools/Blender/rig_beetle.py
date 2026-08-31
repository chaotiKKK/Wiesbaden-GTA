"""Baut aus Karosserie und Rad ein SKELETT-Mesh fuer Chaos Vehicles.

Unreals Fahrzeugphysik dreht und federt die Raeder ueber Knochen. Ein
statisches Mesh reicht dafuer nicht - es braucht ein Skelett mit je einem
Knochen pro Rad.

Aufbau des Skeletts (Unreal-Konvention fuer Fahrzeuge):

    VehicleRoot            Wurzel, traegt die Karosserie
      Wheel_FL             vorne links
      Wheel_FR             vorne rechts
      Wheel_BL             hinten links
      Wheel_BR             hinten rechts

Die Radknochen sind KINDER der Wurzel und NICHT ineinander verschachtelt:
Chaos setzt jeden Radknochen unabhaengig, eine Kette wuerde die Bewegung des
einen auf den anderen uebertragen.

Radpositionen aus WiesbadenCar.cpp, dort am Modell vermessen (cm):

    vorne links   (129.9, -65.6, 34.3)
    vorne rechts  (130.5,  65.3, 34.3)
    hinten links  (-112.2, -65.6, 34.3)
    hinten rechts (-111.6,  65.3, 34.3)

Radstand 2,42 m, Spurweite 1,31 m - beides entspricht dem realen Kaefer
(2,40 / 1,30). Z ist der Radhalbmesser; darauf steht das Fahrzeug.

Blender rechnet in METERN, Unreal in Zentimetern. Die Positionen werden
deshalb durch 100 geteilt; der FBX-Export mit apply_unit_scale macht daraus
wieder Zentimeter. Beim Import in Unreal MUSS import_uniform_scale auf 1,0
stehen - mit der Vorgabe waere der Kaefer 175 m lang (schon passiert).

Aufruf:
  blender.exe --background --python Tools/Blender/rig_beetle.py -- <Quellordner> <Zieldatei>
"""

import math
import os
import sys

import bpy
from mathutils import Vector

args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
SRC = args[0] if args else "."
OUT = args[1] if len(args) > 1 else "SK_VWBeetle.fbx"

# Name -> Position in cm, wie in WiesbadenCar.cpp.
WHEELS = [
    ("Wheel_FL", Vector((129.9, -65.6, 34.3))),
    ("Wheel_FR", Vector((130.5, 65.3, 34.3))),
    ("Wheel_BL", Vector((-112.2, -65.6, 34.3))),
    ("Wheel_BR", Vector((-111.6, 65.3, 34.3))),
]

ROOT_BONE = "VehicleRoot"
CM = 0.01


def clear_scene():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    for block in (bpy.data.meshes, bpy.data.armatures, bpy.data.objects):
        for item in list(block):
            if item.users == 0:
                block.remove(item)


def import_fbx(path):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=path)
    return [o for o in bpy.data.objects if o not in before and o.type == "MESH"]


def join(objects, name):
    bpy.ops.object.select_all(action="DESELECT")
    for o in objects:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    if len(objects) > 1:
        bpy.ops.object.join()
    result = bpy.context.view_layer.objects.active
    result.name = name
    result.data.name = name
    return result


clear_scene()

body_parts = import_fbx(os.path.join(SRC, "SM_VWBeetle1969_Body.fbx"))
if not body_parts:
    raise SystemExit("Karosserie nicht importiert")
body = join(body_parts, "Beetle_Body")

wheel_parts = import_fbx(os.path.join(SRC, "SM_VWBeetle_Wheel.fbx"))
if not wheel_parts:
    raise SystemExit("Rad nicht importiert")
wheel_master = join(wheel_parts, "Beetle_WheelMaster")

# Der Unreal-Export liefert Zentimeter; Blender arbeitet in Metern und der
# Importer rechnet um. Massstab pruefen statt annehmen: Der Kaefer ist
# 4,15 m lang.
bpy.context.view_layer.update()
body_dim = body.dimensions
print("###RIG### Karosserie %.2f x %.2f x %.2f m" % (body_dim.x, body_dim.y, body_dim.z))
print("###RIG### Rad %.3f x %.3f x %.3f m"
      % (wheel_master.dimensions.x, wheel_master.dimensions.y, wheel_master.dimensions.z))

# -- Skelett ---------------------------------------------------------------
# Das ARMATURE-OBJEKT heisst wie der Wurzelknochen.
#
# Blenders FBX-Export macht das Armature-Objekt selbst zum Wurzelknochen des
# Skeletts. Hiess es "SK_VWBeetle", entstand die Kette
#
#     SK_VWBeetle -> VehicleRoot -> vier Raeder
#
# und der Fahrgestell-Koerper sass damit eine Ebene UNTER der Wurzel. Chaos
# rechnet die Radpositionen relativ zur Transformation des simulierten
# Koerpers; stimmt die nicht mit der Skelettwurzel ueberein, tastet es an
# anderer Stelle, als die Knochen liegen. Im Protokoll sah das so aus: meine
# eigenen Strahlen fanden die Fahrbahn 25 bis 62 cm unter den Radknochen, die
# Radabtastung von Chaos fand bei 74,3 cm Reichweite nichts.
#
# Mit "VehicleRoot" als Objektnamen ist die Wurzel selbst der Fahrzeugknoten:
#
#     VehicleRoot -> vier Raeder
armature_data = bpy.data.armatures.new("VehicleRoot")
armature = bpy.data.objects.new("VehicleRoot", armature_data)
bpy.context.collection.objects.link(armature)
bpy.context.view_layer.objects.active = armature
bpy.ops.object.mode_set(mode="EDIT")

# KEIN eigener Wurzelknochen mehr - das Armature-Objekt ist die Wurzel.
# Ein zusaetzlicher Knochen desselben Namens ergaebe zwei Ebenen und genau
# den Versatz, der behoben werden soll.
root = None

for name, pos_cm in WHEELS:
    pos = pos_cm * CM
    bone = armature_data.edit_bones.new(name)
    bone.head = pos
    # Der Knochen zeigt nach aussen (in Fahrzeug-Y), damit seine Achse
    # erkennbar ist. Chaos benutzt die Laenge nicht - nur den Ursprung.
    bone.tail = pos + Vector((0.0, math.copysign(0.15, pos.y), 0.0))
    # Kein Elternknochen: Der Exporter haengt sie unter das Armature-Objekt,
    # und das ist der Wurzelknochen VehicleRoot.
    # KEINE Verbindung: ein verbundener Knochen zieht seinen Kopf an das
    # Ende des Elternknochens und landet damit in der Fahrzeugmitte.
    bone.use_connect = False

bpy.ops.object.mode_set(mode="OBJECT")

# -- Karosserie an die Wurzel ---------------------------------------------
body.parent = armature
mod = body.modifiers.new(name="Armature", type="ARMATURE")
mod.object = armature
group = body.vertex_groups.new(name=ROOT_BONE)
group.add([v.index for v in body.data.vertices], 1.0, "REPLACE")

# -- Vier Raeder, je eines an seinen Knochen ------------------------------
for name, pos_cm in WHEELS:
    pos = pos_cm * CM
    wheel = wheel_master.copy()
    wheel.data = wheel_master.data.copy()
    wheel.name = "Beetle_%s" % name
    wheel.data.name = wheel.name
    bpy.context.collection.objects.link(wheel)

    # Das exportierte Rad ist auf seinen eigenen Mittelpunkt zentriert; es
    # muss nur noch an die Radposition verschoben werden. Die rechte Seite
    # wird gespiegelt, damit die Felgenschrift nicht seitenverkehrt steht.
    wheel.location = pos

    if pos.y > 0.0:
        # MULTIPLIZIEREN, nicht zuweisen.
        #
        # Der FBX-Importer setzt den Objekten eine Skalierung von 0,01, weil
        # die Datei in Zentimetern vorliegt und Blender in Metern rechnet. Ein
        # `wheel.scale.y = -1.0` ueberschreibt diese 0,01 auf der Y-Achse und
        # macht das Rad dort hundertfach breit. Im Import nach Unreal sah man
        # es als Fahrzeug von 414,7 x 1717,0 x 154,2 cm - Laenge und Hoehe
        # richtig, Breite 17 Meter.
        wheel.scale.y *= -1.0

    bpy.context.view_layer.objects.active = wheel
    bpy.ops.object.select_all(action="DESELECT")
    wheel.select_set(True)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)

    wheel.parent = armature
    mod = wheel.modifiers.new(name="Armature", type="ARMATURE")
    mod.object = armature
    grp = wheel.vertex_groups.new(name=name)
    grp.add([v.index for v in wheel.data.vertices], 1.0, "REPLACE")

bpy.data.objects.remove(wheel_master, do_unlink=True)

# Massprobe vor dem Export.
#
# Ein falscher Massstab faellt sonst erst nach Export, Import und
# Editor-Start auf - drei Schritte spaeter, mit entsprechend langer
# Rueckverfolgung. Der Kaefer von 1969 misst 4,08 x 1,55 x 1,50 m.
bpy.context.view_layer.update()
lo = Vector((1e9, 1e9, 1e9))
hi = Vector((-1e9, -1e9, -1e9))
for o in bpy.data.objects:
    if o.type != "MESH":
        continue
    for corner in o.bound_box:
        world = o.matrix_world @ Vector(corner)
        for axis in range(3):
            lo[axis] = min(lo[axis], world[axis])
            hi[axis] = max(hi[axis], world[axis])

size = hi - lo
print("###RIG### Gesamtmass %.2f x %.2f x %.2f m" % (size.x, size.y, size.z))
if not (3.5 < size.x < 4.8 and 1.2 < size.y < 2.0 and 1.2 < size.z < 1.9):
    raise SystemExit(
        "ABBRUCH: Gesamtmass %.2f x %.2f x %.2f m passt nicht zu einem Kaefer "
        "(erwartet rund 4,15 x 1,54 x 1,51)." % (size.x, size.y, size.z))

# -- Export ---------------------------------------------------------------
#
# ZWEI Dateien: das vollstaendige Fahrzeug und ein zweites NUR mit der
# Karosserie.
#
# Der Grund ist der Kollisionskoerper. Unreal erzeugt ihn automatisch aus dem
# gesamten Mesh - also einschliesslich der Raeder - und der Kasten reicht damit
# bis unter die Reifenaufstandsflaeche. Gemessen: Koerper von -36,5 cm bis
# +186,8 cm, Radaufstand bei -34,3 cm. Der Wagen steht dann auf seinem
# Kollisionskasten wie auf einem Schlitten, die Raeder haengen frei, und der
# Motor dreht ohne Last gegen den Begrenzer.
#
# Aus der Karosserie allein entsteht ein Koerper, der ueber den Raedern endet.
# Zuweisen laesst er sich anschliessend dem vollstaendigen Mesh, weil
# Physik-Assets ueber KNOCHENNAMEN zugeordnet werden und beide dieselbe Wurzel
# haben.
#
# Der Weg ueber Blender ist noetig, weil die Koerper eines Physik-Assets von
# Unreals Python-Schnittstelle aus nicht erreichbar sind (geprueft: weder
# skeletal_body_setups noch bodies noch body_setup).
def export(path, objects):
    bpy.ops.object.select_all(action="DESELECT")
    armature.select_set(True)
    for o in objects:
        o.select_set(True)
    bpy.context.view_layer.objects.active = armature

    bpy.ops.export_scene.fbx(
        filepath=path,
        use_selection=True,
        apply_unit_scale=True,
        global_scale=1.0,
        apply_scale_options="FBX_SCALE_NONE",
        object_types={"ARMATURE", "MESH"},
        use_armature_deform_only=True,
        add_leaf_bones=False,
        bake_anim=False,
        mesh_smooth_type="FACE",
        axis_forward="-Z",
        axis_up="Y",
    )
    print("###RIG### geschrieben: %s" % path)


# -- Kollisionskoerper als eigener Quader ----------------------------------
#
# NICHT aus der Karosserie ableiten. Der Versuch ist gemacht und gescheitert:
# Blender meldet Karosserie 1,51 m hoch, Gesamtfahrzeug 1,54 m - die Raeder
# ragen also nur 3 cm ueber die Karosserie hinaus, das Bodenblech des Kaefers
# sitzt fast auf Reifenhoehe. Ein Kasten um die Karosserie allein reichte
# ebenso tief wie einer um das ganze Fahrzeug (gemessen -39,5 statt -36,5 cm
# relativ zum Ursprung, bei Radaufstand -34,3).
#
# Der Koerper eines Fahrzeugs gehoert UEBER die Radaufstandsflaechen. Sonst
# ruht der Wagen darauf wie auf einem Schlitten, die Raeder haengen frei, und
# der Motor dreht ohne Last gegen den Begrenzer - genau das Bild aus den
# Fahrproben.
#
# Deshalb ein schlichter Quader, bewusst bemasst:
#
#   Laenge  4,15 m   wie die Karosserie
#   Breite  1,54 m   wie die Karosserie
#   Hoehe   von +0,25 m bis +1,45 m ueber dem Ursprung
#
# Die Unterkante bei +25 cm liegt 59 cm ueber dem Radaufstand (-34,3 cm) und
# entspricht ungefaehr der Bodenfreiheit eines Kaefers plus Sicherheitsabstand.
CHASSIS_MIN_Z = 0.25
CHASSIS_MAX_Z = 1.45

bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0.0, 0.0, 0.0))
proxy = bpy.context.view_layer.objects.active
proxy.name = "Beetle_ChassisProxy"
proxy.data.name = proxy.name

body_dim = body.dimensions
proxy.scale = (body_dim.x, body_dim.y, CHASSIS_MAX_Z - CHASSIS_MIN_Z)
proxy.location = (0.0, 0.0, (CHASSIS_MIN_Z + CHASSIS_MAX_Z) * 0.5)

bpy.ops.object.select_all(action="DESELECT")
proxy.select_set(True)
bpy.context.view_layer.objects.active = proxy
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)

proxy.parent = armature
pmod = proxy.modifiers.new(name="Armature", type="ARMATURE")
pmod.object = armature
pgrp = proxy.vertex_groups.new(name=ROOT_BONE)
pgrp.add([v.index for v in proxy.data.vertices], 1.0, "REPLACE")

print("###RIG### Kollisions-Quader %.2f x %.2f m, Z von %.2f bis %.2f m"
      % (body_dim.x, body_dim.y, CHASSIS_MIN_Z, CHASSIS_MAX_Z))

chassis_path = os.path.splitext(OUT)[0] + "_Chassis.fbx"
export(chassis_path, [proxy])

# Der Quader ist NUR fuer die Kollision. Im sichtbaren Fahrzeug hat er nichts
# zu suchen - sonst steckt ein grauer Kasten im Auto.
bpy.data.objects.remove(proxy, do_unlink=True)

bpy.ops.object.select_all(action="DESELECT")
armature.select_set(True)
for o in bpy.data.objects:
    if o.parent == armature:
        o.select_set(True)
bpy.context.view_layer.objects.active = armature

os.makedirs(os.path.dirname(os.path.abspath(OUT)), exist_ok=True)
bpy.ops.export_scene.fbx(
    filepath=OUT,
    use_selection=True,
    apply_unit_scale=True,
    global_scale=1.0,
    apply_scale_options="FBX_SCALE_NONE",
    object_types={"ARMATURE", "MESH"},
    use_armature_deform_only=True,
    add_leaf_bones=False,
    bake_anim=False,
    mesh_smooth_type="FACE",
    axis_forward="-Z",
    axis_up="Y",
)

print("###RIG### Skelett-Mesh geschrieben: %s" % OUT)
print("###RIG### Knochen: %s" % ", ".join(b.name for b in armature_data.bones))
