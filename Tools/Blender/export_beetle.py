"""
Bereitet das konvertierte Kaefer-Modell fuer Unreal auf und exportiert FBX.

Schritte:
  1. Alle Teile zu einem Mesh vereinen (Materialslots bleiben erhalten).
  2. Um +90 Grad um Z drehen: das Modell zeigt nach -Y, Unreal erwartet +X.
  3. Ursprung auf Bodenmitte legen (X/Y-Mitte, Z = tiefster Punkt). Ein Pivot
     auf Radaufstandshoehe ist fuer ein Fahrzeug die brauchbare Referenz -
     sonst schwebt oder versinkt es beim Platzieren.
  4. Zwei Ausgaben:
       - voll: Spielerfahrzeug (Nahsicht)
       - reduziert: Verkehr. Der Verkehr wird als InstancedStaticMesh mit
         hunderten Instanzen gezeichnet; die volle Aufloesung waere dort
         Verschwendung.
"""
import bpy
import sys
import math
from mathutils import Vector


def scene_bounds():
    """
    Bounding-Box aus den tatsaechlichen Vertex-Positionen.

    Bewusst NICHT ueber ob.bound_box: dieses Feld ist gecacht und wird nach
    einer direkten Manipulation der Vertex-Koordinaten nicht sofort neu
    berechnet. Eine Messung darueber meldet die alten Werte und verschleiert,
    ob eine Verschiebung ueberhaupt angekommen ist.
    """
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
    return mn, mx


def export(blend_path, out_full, out_traffic, traffic_target_tris):
    bpy.ops.wm.open_mainfile(filepath=blend_path)

    meshes = [o for o in bpy.data.objects if o.type == "MESH"]
    if not meshes:
        print("###ERR### keine Meshes")
        return

    # -- 1. Vereinen -------------------------------------------------------
    bpy.ops.object.select_all(action="DESELECT")
    for o in meshes:
        o.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    bpy.ops.object.join()

    car = bpy.context.view_layer.objects.active
    car.name = "SM_VWBeetle1969"

    # -- 2. Ausrichten -----------------------------------------------------
    # Modell zeigt nach -Y (per Render verifiziert), Unreal-Fahrzeuge nach +X.
    car.rotation_euler = (0.0, 0.0, math.radians(90.0))
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)

    # -- 3. Ursprung auf Bodenmitte ----------------------------------------
    mn, mx = scene_bounds()
    pivot = Vector(((mn.x + mx.x) / 2.0, (mn.y + mx.y) / 2.0, mn.z))

    # Die Vertices direkt verschieben statt bpy.ops.object.origin_set zu
    # verwenden: der Operator braucht einen vollstaendigen UI-Kontext und wird
    # im --background-Modus ohne Fehlermeldung als CANCELLED verworfen. Das
    # faellt nur auf, wenn man die Bounding-Box danach nachrechnet - der Export
    # haette sonst einen um 26 cm versetzten Pivot gehabt.
    for v in car.data.vertices:
        v.co.x -= pivot.x
        v.co.y -= pivot.y
        v.co.z -= pivot.z
    car.data.update()
    car.location = (0.0, 0.0, 0.0)

    mn, mx = scene_bounds()
    print("###ABMESSUNG### Laenge(X)=%.3f Breite(Y)=%.3f Hoehe(Z)=%.3f  min=%s max=%s" % (
        mx.x - mn.x, mx.y - mn.y, mx.z - mn.z,
        [round(v, 3) for v in mn], [round(v, 3) for v in mx]))

    tris_full = len(car.data.polygons)

    # -- 4a. Vollversion ----------------------------------------------------
    bpy.ops.object.select_all(action="DESELECT")
    car.select_set(True)
    bpy.context.view_layer.objects.active = car

    bpy.ops.export_scene.fbx(
        filepath=out_full,
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
    print("###FBX_VOLL### %s (%d Dreiecke)" % (out_full, tris_full))

    # -- 4b. Verkehrsversion ------------------------------------------------
    ratio = min(1.0, float(traffic_target_tris) / float(max(1, tris_full)))
    dec = car.modifiers.new(name="Decimate", type="DECIMATE")
    dec.decimate_type = "COLLAPSE"
    dec.ratio = ratio

    bpy.ops.export_scene.fbx(
        filepath=out_traffic,
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

    # Tatsaechliche Dreieckszahl nach Decimate ermitteln.
    depsgraph = bpy.context.evaluated_depsgraph_get()
    evaluated = car.evaluated_get(depsgraph)
    tris_dec = len(evaluated.data.polygons)
    print("###FBX_VERKEHR### %s (%d Dreiecke, Ratio %.4f)" % (out_traffic, tris_dec, ratio))


if __name__ == "__main__":
    argv = sys.argv[sys.argv.index("--") + 1:]
    export(argv[0], argv[1], argv[2], int(argv[3]))
