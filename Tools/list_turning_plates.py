"""Listet die Wendeplatten einer gebackenen Karte - NUR LESEND.

Fuer Kontrollaufnahmen an Sackgassen: Anzahl, Radien und die Platten
naechst dem Garagenhof-Start sowie an benannten Strassen, mit Weltlage
(cm), Radius und Fahrtrichtung zur Platte (Yaw fuer eine Pose).

Aufruf (Skriptpfad VOLLSTAENDIG, Karte per WB_MAP nur aus cmd/PowerShell -
Git Bash schreibt /Game/... um, siehe verify_anchor_state.py):

  UnrealEditor-Cmd.exe <uproject> -run=pythonscript
    -script="C:\\...\\Tools\\list_turning_plates.py" -unattended -nop4 -nosplash -nullrhi

Ergebnis: Saved/Diagnose/wendeplatten_<Karte>.txt (fehlt sie, ist der Lauf
fehlgeschlagen).
"""

import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from karte import standard_karte_pfad

import unreal

MAP = os.environ.get("WB_MAP", standard_karte_pfad())
START = (-115711.0, -121867.0)   # Garagenhof Platter Str. 144

RESULT_FILE = os.path.join(
    unreal.Paths.project_saved_dir(), "Diagnose",
    "wendeplatten_%s.txt" % MAP.rsplit("_", 1)[-1])


def main():
    os.makedirs(os.path.dirname(RESULT_FILE), exist_ok=True)
    if os.path.exists(RESULT_FILE):
        os.remove(RESULT_FILE)
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    les.load_level(MAP)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    if MAP.lower() not in world.get_path_name().lower():
        raise RuntimeError("Karte nicht geladen: %s, offen %s" % (MAP, world.get_path_name()))

    builders = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WiesbadenWorldBuilder)
    if not builders:
        raise RuntimeError("kein WiesbadenWorldBuilder in %s" % MAP)
    net = builders[0].get_editor_property("road_network")
    plates = list(net.get_editor_property("turning_plates"))
    segments = net.get_editor_property("segments")
    lanes = net.get_editor_property("lanes")

    lines = ["Karte %s: %d Wendeplatten" % (MAP, len(plates))]
    if not plates:
        _write(lines)
        return
    radii = sorted(p.get_editor_property("radius_cm") for p in plates)
    lines.append("Radius cm: min %.0f / median %.0f / max %.0f"
                 % (radii[0], radii[len(radii) // 2], radii[-1]))

    def row(p):
        c = p.get_editor_property("center")
        seg = segments[p.get_editor_property("segment_index")]
        lane = lanes[p.get_editor_property("lane_id")]
        pts = lane.get_editor_property("centerline")
        yaw = 0.0
        if len(pts) >= 2:
            a, b = pts[-2], pts[-1]
            yaw = math.degrees(math.atan2(b.y - a.y, b.x - a.x))
        name = seg.get_editor_property("street_name") or "-"
        return ("X %.0f Y %.0f Z %.0f  R %.0f  Anfahrt-Yaw %.0f  Way %d  %s"
                % (c.x, c.y, c.z, p.get_editor_property("radius_cm"), yaw,
                   seg.get_editor_property("source_way_id"), name))

    def dist(p):
        c = p.get_editor_property("center")
        return math.hypot(c.x - START[0], c.y - START[1])

    lines.append("")
    lines.append("Naechst dem Garagenhof-Start:")
    for p in sorted(plates, key=dist)[:12]:
        lines.append("  %6.0f m  %s" % (dist(p) / 100.0, row(p)))

    lines.append("")
    lines.append("An benannten Strassen (erste 25 im Umkreis 3 km):")
    n = 0
    for p in sorted(plates, key=dist):
        if dist(p) > 300000:
            break
        seg = segments[p.get_editor_property("segment_index")]
        if seg.get_editor_property("street_name"):
            lines.append("  %6.0f m  %s" % (dist(p) / 100.0, row(p)))
            n += 1
            if n >= 25:
                break
    _write(lines)


def _write(lines):
    with open(RESULT_FILE, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")


main()
