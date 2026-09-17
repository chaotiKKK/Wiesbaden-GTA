"""verify_ka52_actor.py — Prueft, was das C++ WIRKLICH bindet.

Der Asset-Import (verify_ka52.py) sagt nur, dass die Meshes in /Game liegen.
Hier wird der CDO von AWiesbadenHelicopter gelesen: welche Meshes haengen an
welchen Komponenten, wo sitzen die Rotor-Naben und wie weit liegen die
Rotor-Meshes in XY von ihrer Nabe entfernt (= muessen ~0 sein, sonst kreist
der Rotor um etwas anderes als die Mastachse).

Ergebnis geht ZUSAETZLICH in eine Datei (Saved/Diagnose/ka52_actor.txt),
weil Python-prints im -stdout-Kanal eines Commandlets verschwinden koennen.
"""
import os
import unreal

OUT = os.path.join(unreal.Paths.project_saved_dir(), "Diagnose", "ka52_actor.txt")
os.makedirs(os.path.dirname(OUT), exist_ok=True)
LINES = []


def log(m):
    unreal.log_warning("###WBKA52A### %s" % m)
    LINES.append(m)


cls = unreal.load_class(None, "/Script/WiesbadenReal.WiesbadenHelicopter")
if not cls:
    log("FEHLER: Klasse /Script/WiesbadenReal.WiesbadenHelicopter nicht ladbar")
else:
    cdo = unreal.get_default_object(cls)
    log("Klasse: %s" % cls.get_name())

    # Erst alle Komponenten des CDO auflisten - Namen und Klassen.
    comps = {}
    for prop in ("SceneRoot", "CollisionSphere", "FuselageMesh", "TailBoomMesh", "TailFinMesh",
                 "MainRotorHub", "MainRotorBlade", "LowerRotorHub", "LowerRotorBlade",
                 "TailRotorHub", "TailRotorBlade", "UpperRotorBlur", "LowerRotorBlur"):
        try:
            c = cdo.get_editor_property(prop)
        except Exception as exc:  # noqa: BLE001 - Commandlet soll weiterlaufen
            log("%-16s FEHLT (%s)" % (prop, exc))
            continue
        comps[prop] = c
        if c is None:
            log("%-16s None" % prop)
            continue

    for name, c in comps.items():
        loc = c.get_editor_property("relative_location")
        info = "%-16s %-28s rel=(%.1f, %.1f, %.1f)" % (
            name, c.get_class().get_name(), loc.x, loc.y, loc.z)
        if isinstance(c, unreal.StaticMeshComponent):
            sm = c.get_editor_property("static_mesh")
            sm_name = sm.get_name() if sm else "KEIN MESH"
            if sm:
                b = sm.get_bounds()
                o = b.origin
                e = b.box_extent
                info += " mesh=%s bounds_origin=(%.1f, %.1f, %.1f) extent=(%.1f, %.1f, %.1f)" % (
                    sm_name, o.x, o.y, o.z, e.x, e.y, e.z)
            else:
                info += " mesh=%s" % sm_name
            info += " sichtbar=%s" % c.get_editor_property("visible")
        log(info)

    # Der eigentliche Test: Mesh-Ursprung gegen die Nabe. Der Mesh-Ursprung
    # liegt per Definition auf der Mastachse (Import), also darf der XY-Abstand
    # Blatt->Nabe hoechstens wenige cm betragen. Die z-Differenz ist die
    # Hub-Hoehe im Modell (Blatt-Mesh liegt auf Bodenhoehe, der Hub hebt es).
    for hub_name, blade_name in (("MainRotorHub", "MainRotorBlade"),
                                 ("LowerRotorHub", "LowerRotorBlade")):
        hub = comps.get(hub_name)
        blade = comps.get(blade_name)
        if not hub or not blade:
            continue
        hl = hub.get_editor_property("relative_location")
        bl = blade.get_editor_property("relative_location")
        dx, dy, dz = bl.x - hl.x, bl.y - hl.y, bl.z - hl.z
        log("ACHSENTEST %s: Hub z=%.1f  Mesh-Offset (dx=%.1f, dy=%.1f, dz=%.1f)"
            % (hub_name, hl.z, dx, dy, dz))
        log("  -> XY-Abstand Mastachse = %.2f cm %s"
            % ((dx * dx + dy * dy) ** 0.5,
               "OK" if (dx * dx + dy * dy) ** 0.5 < 5.0 else "ZU GROSS"))

    log("Fuselage-Rotation: %s" % cdo.get_editor_property("FuselageMesh").get_editor_property("relative_rotation"))

log("ENDE")

with open(OUT, "w", encoding="utf-8") as fh:
    fh.write("\n".join(LINES) + "\n")
unreal.log_warning("###WBKA52A### geschrieben: %s" % OUT)
