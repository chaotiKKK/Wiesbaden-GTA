"""
Sucht Rad-Geometrie im konvertierten Modell.

Das Modell ist nach Materialien gruppiert, nicht nach Bauteilen - es gibt also
keine Objekte namens "Wheel". Die Raeder werden daher ueber ihre Form gefunden:
in Loose Parts zerlegen und Cluster suchen, die wie ein Rad aussehen.

Radsignatur (Fahrzeug-Laengsachse liegt auf Y):
  - schmal entlang X (Laufflaechenbreite), deutlich groesser in Y und Z
  - Y- und Z-Ausdehnung annaehernd gleich (Kreisscheibe)
  - Unterkante nahe Z = 0 (steht auf der Strasse)
  - seitlich versetzt (|Mitte X| deutlich groesser als 0)
"""
import bpy
import json
import sys
from mathutils import Vector


def world_bounds(ob):
    xs, ys, zs = [], [], []
    for c in ob.bound_box:
        w = ob.matrix_world @ Vector(c)
        xs.append(w.x)
        ys.append(w.y)
        zs.append(w.z)
    return (min(xs), min(ys), min(zs)), (max(xs), max(ys), max(zs))


def analyse(blend_path, target_names, report_out):
    bpy.ops.wm.open_mainfile(filepath=blend_path)

    parts = []

    for name in target_names:
        ob = bpy.data.objects.get(name)
        if ob is None:
            continue

        # Zerlegen in zusammenhaengende Teile.
        bpy.ops.object.select_all(action="DESELECT")
        ob.select_set(True)
        bpy.context.view_layer.objects.active = ob
        bpy.ops.mesh.separate(type="LOOSE")

        for o in list(bpy.context.selected_objects):
            if o.type != "MESH" or len(o.data.polygons) == 0:
                continue
            mn, mx = world_bounds(o)
            size = [mx[i] - mn[i] for i in range(3)]
            ctr = [(mx[i] + mn[i]) / 2 for i in range(3)]

            # Radkriterien
            round_ratio = (min(size[1], size[2]) / max(size[1], size[2])) if max(size[1], size[2]) > 0 else 0.0
            is_wheel = (
                size[0] < 0.35                       # schmal quer
                and 0.35 < size[1] < 0.95            # Durchmesser Laengsrichtung
                and 0.35 < size[2] < 0.95            # Durchmesser Hochrichtung
                and round_ratio > 0.75               # rund, nicht laenglich
                and mn[2] < 0.20                     # steht auf der Strasse
                and abs(ctr[0]) > 0.40               # seitlich versetzt
            )

            parts.append({
                "name": o.name,
                "source": name,
                "tris": len(o.data.polygons),
                "size": [round(s, 3) for s in size],
                "center": [round(c, 3) for c in ctr],
                "minz": round(mn[2], 3),
                "round_ratio": round(round_ratio, 3),
                "is_wheel": is_wheel,
            })

    parts.sort(key=lambda p: -p["tris"])
    wheels = [p for p in parts if p["is_wheel"]]

    report = {
        "total_parts": len(parts),
        "wheel_candidates": len(wheels),
        "wheels": wheels,
        "largest_parts": parts[:15],
    }

    with open(report_out, "w") as f:
        json.dump(report, f, indent=1)

    print("###PARTS### %d Teile, %d Radkandidaten" % (len(parts), len(wheels)))
    for w in wheels:
        print("  RAD %-28s tris=%-6d size=%s center=%s" % (w["name"], w["tris"], w["size"], w["center"]))


if __name__ == "__main__":
    argv = sys.argv[sys.argv.index("--") + 1:]
    analyse(argv[0], argv[1].split(","), argv[2])
