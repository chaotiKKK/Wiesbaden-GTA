"""
Kontrollmontage: setzt Karosserie und vier Raeder mit genau den Werten
zusammen, die im C++-Konstruktor stehen, und rendert das Ergebnis.

Zweck ist nicht, das Modell zu pruefen - das ist in Ordnung -, sondern die
hartcodierten Radpositionen. Sitzen sie falsch, sieht man es hier sofort:
Raeder neben dem Kotfluegel, in der Luft oder im Boden.
"""
import bpy
import math
import sys
from mathutils import Vector

# Exakt die Werte aus AWiesbadenCar::AWiesbadenCar (cm -> m).
WHEEL_POSITIONS_CM = [
    (129.9, -65.6, 34.3),   # vorne links
    (130.5, 65.3, 34.3),    # vorne rechts
    (-112.2, -65.6, 34.3),  # hinten links
    (-111.6, 65.3, 34.3),   # hinten rechts
]


def import_fbx(path):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=path)
    return [o for o in bpy.data.objects if o not in before and o.type == "MESH"]


def main(fbx_dir, out_prefix):
    bpy.ops.wm.read_factory_settings(use_empty=True)

    body_objs = import_fbx("%s/SM_VWBeetle1969_Body.fbx" % fbx_dir)
    wheel_objs = import_fbx("%s/SM_VWBeetle_Wheel.fbx" % fbx_dir)

    if not body_objs or not wheel_objs:
        print("###ERR### Import fehlgeschlagen")
        return

    wheel_src = wheel_objs[0]

    # Vier Kopien an den hartcodierten Positionen.
    for i, (x, y, z) in enumerate(WHEEL_POSITIONS_CM):
        copy = wheel_src.copy()
        copy.data = wheel_src.data.copy()
        copy.name = "Wheel_%d" % i
        bpy.context.scene.collection.objects.link(copy)
        copy.location = Vector((x / 100.0, y / 100.0, z / 100.0))

    # Das Original entfernen, sonst steht ein fuenftes Rad im Ursprung.
    bpy.data.objects.remove(wheel_src, do_unlink=True)

    # Depsgraph aktualisieren, BEVOR gemessen wird: matrix_world ist gecacht
    # und traegt nach dem Setzen von .location noch den alten Wert. Ohne diesen
    # Aufruf meldet die Messung die Raeder im Ursprung - also einen Radradius
    # zu tief -, waehrend das Rendering (das den Depsgraph selbst auswertet)
    # das richtige Bild zeigt. Messung und Bild wuerden sich widersprechen.
    bpy.context.view_layer.update()

    # Gesamtausdehnung ueber die Vertices messen.
    mn = Vector((1e9, 1e9, 1e9))
    mx = Vector((-1e9, -1e9, -1e9))
    for ob in bpy.data.objects:
        if ob.type != "MESH":
            continue
        for v in ob.data.vertices:
            w = ob.matrix_world @ v.co
            for i in range(3):
                mn[i] = min(mn[i], w[i])
                mx[i] = max(mx[i], w[i])

    print("###MONTAGE### %.3f x %.3f x %.3f m, tiefster Punkt Z=%.4f"
          % (mx.x - mn.x, mx.y - mn.y, mx.z - mn.z, mn.z))

    ctr = (mn + mx) / 2.0
    size = mx - mn
    radius = max(size) * 1.2

    scene = bpy.context.scene
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.render.resolution_x = 1000
    scene.render.resolution_y = 620

    shading = scene.display.shading
    shading.light = "STUDIO"
    shading.color_type = "SINGLE"
    shading.single_color = (0.55, 0.55, 0.58)
    shading.show_cavity = True

    cam_data = bpy.data.cameras.new("Cam")
    cam_data.type = "ORTHO"
    cam_data.ortho_scale = max(size) * 1.12
    cam = bpy.data.objects.new("Cam", cam_data)
    scene.collection.objects.link(cam)
    scene.camera = cam

    views = [
        ("seite", Vector((0, -radius, 0)), (math.radians(90), 0, 0)),
        ("front", Vector((radius, 0, 0)), (math.radians(90), 0, math.radians(90))),
    ]

    for name, offset, rot in views:
        cam.location = ctr + offset
        cam.rotation_euler = rot
        scene.render.filepath = "%s_%s.png" % (out_prefix, name)
        bpy.ops.render.render(write_still=True)
        print("###VIEW### %s" % scene.render.filepath)


if __name__ == "__main__":
    argv = sys.argv[sys.argv.index("--") + 1:]
    main(argv[0], argv[1])
