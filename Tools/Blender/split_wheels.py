"""
Trennt die vier Raeder vom Kaefer-Modell und exportiert Karosserie und Rad
getrennt.

Das Quellmodell ist nach Materialien gruppiert, nicht nach Bauteilen - es gibt
kein Objekt "Rad". Die Raeder werden daher ueber ihre Lage gefunden: in Loose
Parts zerlegen, dann alle Teile einsammeln, die nahe genug an einem der vier
Radmittelpunkte liegen. Reifen, Felge und Nabenkappe sind eigene Teile und
gehoeren zusammen.

Ausgabe:
  - SM_VWBeetle1969_Body.fbx   Karosserie ohne Raeder
  - SM_VWBeetle_Wheel.fbx      ein Rad, Ursprung in der Radmitte
  - wheel_positions.json       die vier Radmitten im Koordinatensystem der
                               exportierten Karosserie (Meter)
"""
import bpy
import json
import math
import sys
from mathutils import Vector

# Radmittelpunkte im Quellmodell (aus der Loose-Part-Analyse, Meter).
WHEEL_ANCHORS = [
    (0.662, -1.567, 0.353),   # rechts vorn  (Front liegt bei -Y)
    (0.662, 0.854, 0.353),    # rechts hinten
    (-0.648, 0.860, 0.353),   # links hinten
    (-0.648, -1.561, 0.353),  # links vorn
]

# Ein Teil gehoert zu einem Rad, wenn seine Mitte innerhalb dieses Radius um
# einen Ankerpunkt liegt. 0.42 m umfasst Reifen (0.686 Durchmesser) samt
# Felge, ohne den Kotfluegel zu erfassen.
WHEEL_RADIUS = 0.42
WHEEL_MAX_Z = 0.85


def part_center(ob):
    total = Vector((0.0, 0.0, 0.0))
    for v in ob.data.vertices:
        total += ob.matrix_world @ v.co
    return total / max(1, len(ob.data.vertices))


def vertex_bounds(objects):
    mn = Vector((1e9, 1e9, 1e9))
    mx = Vector((-1e9, -1e9, -1e9))
    for ob in objects:
        for v in ob.data.vertices:
            w = ob.matrix_world @ v.co
            for i in range(3):
                mn[i] = min(mn[i], w[i])
                mx[i] = max(mx[i], w[i])
    return mn, mx


def join_objects(objects, name):
    if not objects:
        return None
    bpy.ops.object.select_all(action="DESELECT")
    for o in objects:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    if len(objects) > 1:
        bpy.ops.object.join()
    result = bpy.context.view_layer.objects.active
    result.name = name
    return result


def export_fbx(ob, filepath):
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.export_scene.fbx(
        filepath=filepath,
        use_selection=True,
        apply_unit_scale=True,
        global_scale=1.0,
        apply_scale_options="FBX_SCALE_NONE",
        axis_forward="X",
        axis_up="Z",
        object_types={"MESH"},
        use_mesh_modifiers=True,
        mesh_smooth_type="FACE",
        use_tspace=True,
        path_mode="STRIP",
    )


def main(blend_path, out_dir):
    bpy.ops.wm.open_mainfile(filepath=blend_path)

    # meshId3 traegt die Raeder; die uebrigen Objekte bleiben unangetastet.
    target = bpy.data.objects.get("Object049_meshId3")
    if target is None:
        print("###ERR### Object049_meshId3 fehlt")
        return

    others = [o for o in bpy.data.objects if o.type == "MESH" and o is not target]

    bpy.ops.object.select_all(action="DESELECT")
    target.select_set(True)
    bpy.context.view_layer.objects.active = target
    bpy.ops.mesh.separate(type="LOOSE")

    parts = [o for o in bpy.context.selected_objects if o.type == "MESH" and len(o.data.vertices) > 0]

    # Teile den vier Raedern zuordnen.
    wheel_groups = [[] for _ in WHEEL_ANCHORS]
    rest = []

    for ob in parts:
        c = part_center(ob)
        assigned = False

        if c.z < WHEEL_MAX_Z:
            for i, anchor in enumerate(WHEEL_ANCHORS):
                a = Vector(anchor)
                if (c - a).length < WHEEL_RADIUS:
                    wheel_groups[i].append(ob)
                    assigned = True
                    break

        if not assigned:
            rest.append(ob)

    counts = [len(g) for g in wheel_groups]
    print("###ZUORDNUNG### Radteile je Rad: %s, Rest: %d" % (counts, len(rest)))

    if min(counts) == 0:
        print("###ERR### mindestens ein Rad ohne Teile")
        return

    # Radmitte NUR aus dem Reifen bestimmen, nicht aus der ganzen Gruppe.
    #
    # Die Gruppe enthaelt neben Reifen, Felge und Nabenkappe auch angrenzende
    # Kleinteile (Bremse, Aufhaengung). Deren Bounding-Box zieht den
    # Mittelpunkt nach oben und nach innen - bei den Hinterraedern um 10 cm in
    # der Hoehe und 13 cm in der Spur. Ein Rad ist rotationssymmetrisch, sein
    # Mittelpunkt ist der des Reifens; der Reifen wiederum ist zuverlaessig das
    # Teil mit den meisten Dreiecken.
    wheel_centers = []
    for group in wheel_groups:
        tire = max(group, key=lambda o: len(o.data.polygons))
        mn, mx = vertex_bounds([tire])
        wheel_centers.append((mn + mx) / 2.0)

    # -- Ein Rad als eigenes Mesh -------------------------------------------
    # Genommen wird das Rad rechts vorn; die uebrigen drei sind seine
    # Spiegelungen und werden in Unreal ueber die Komponenten-Transformation
    # gesetzt.
    donor_index = 0
    donor = join_objects(wheel_groups[donor_index], "SM_VWBeetle_Wheel")

    # Die anderen drei Raeder verwerfen - sie stecken sonst doppelt im Modell.
    for i, group in enumerate(wheel_groups):
        if i == donor_index:
            continue
        for ob in group:
            bpy.data.objects.remove(ob, do_unlink=True)

    # -- Karosserie ----------------------------------------------------------
    body = join_objects(rest + others, "SM_VWBeetle1969_Body")

    # -- Gemeinsame Ausrichtung ---------------------------------------------
    # Dieselbe Transformation wie beim urspruenglichen Export: +90 Grad um Z
    # (Modell zeigt nach -Y, Unreal erwartet +X), danach Ursprung auf die
    # Bodenmitte der KAROSSERIE. Beide Objekte muessen exakt gleich behandelt
    # werden, sonst sitzen die Raeder versetzt.
    rot = math.radians(90.0)

    def rotate_z(v, angle):
        ca, sa = math.cos(angle), math.sin(angle)
        return Vector((v.x * ca - v.y * sa, v.x * sa + v.y * ca, v.z))

    for ob in (body, donor):
        if ob:
            ob.rotation_euler = (0.0, 0.0, rot)

    bpy.ops.object.select_all(action="DESELECT")
    for ob in (body, donor):
        if ob:
            ob.select_set(True)
    bpy.context.view_layer.objects.active = body
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)

    # Pivot: XY-Mitte der Karosserie, Z auf der RADAUFSTANDSFLAECHE.
    #
    # Nicht auf der Karosserieunterkante: die Raeder ragen rund 3 cm tiefer,
    # das Fahrzeug wuerde sonst mit den Reifen im Boden stehen. Bezugspunkt ist
    # der tiefste Punkt von Karosserie UND Rad zusammen - dort beruehrt das
    # Auto die Strasse.
    bmn, bmx = vertex_bounds([body])
    dmn, _dmx = vertex_bounds([donor])
    ground_z = min(bmn.z, dmn.z)
    pivot = Vector(((bmn.x + bmx.x) / 2.0, (bmn.y + bmx.y) / 2.0, ground_z))

    # Vertices direkt verschieben - bpy.ops.object.origin_set wird im
    # --background-Modus ohne Meldung verworfen.
    for v in body.data.vertices:
        v.co -= pivot
    body.data.update()

    # Radmitten in dasselbe System bringen.
    final_centers = []
    for c in wheel_centers:
        r = rotate_z(c, rot)
        final_centers.append((r.x - pivot.x, r.y - pivot.y, r.z - pivot.z))

    # Das Spender-Rad auf seinen eigenen Mittelpunkt zentrieren, damit es in
    # Unreal um die eigene Achse rotiert werden kann.
    donor_center = Vector(final_centers[donor_index])
    for v in donor.data.vertices:
        w = donor.matrix_world @ v.co
        v.co = Vector((w.x - pivot.x - donor_center.x,
                       w.y - pivot.y - donor_center.y,
                       w.z - pivot.z - donor_center.z))
    donor.data.update()
    donor.matrix_world.identity()

    bmn, bmx = vertex_bounds([body])
    wmn, wmx = vertex_bounds([donor])

    print("###KAROSSERIE### %.3f x %.3f x %.3f m, %d Dreiecke"
          % (bmx.x - bmn.x, bmx.y - bmn.y, bmx.z - bmn.z, len(body.data.polygons)))
    print("###RAD### %.3f x %.3f x %.3f m, %d Dreiecke"
          % (wmx.x - wmn.x, wmx.y - wmn.y, wmx.z - wmn.z, len(donor.data.polygons)))

    for i, c in enumerate(final_centers):
        print("###RADMITTE%d### %.4f %.4f %.4f" % (i, c[0], c[1], c[2]))

    export_fbx(body, "%s/SM_VWBeetle1969_Body.fbx" % out_dir)
    export_fbx(donor, "%s/SM_VWBeetle_Wheel.fbx" % out_dir)

    with open("%s/wheel_positions.json" % out_dir, "w") as f:
        json.dump({
            "wheel_centers_m": final_centers,
            "wheel_radius_m": (wmx.z - wmn.z) / 2.0,
            "body_size_m": [bmx.x - bmn.x, bmx.y - bmn.y, bmx.z - bmn.z],
        }, f, indent=1)

    print("###FERTIG###")


if __name__ == "__main__":
    argv = sys.argv[sys.argv.index("--") + 1:]
    main(argv[0], argv[1])
