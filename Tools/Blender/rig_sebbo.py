# Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#
# Macht aus dem starren Sebbo-Spielermodell eine animierte Spielfigur.
#
# Aufruf:
#   blender.exe -b Data/Raw/Sebbo/Sebbo_Spielermodell.blend -P rig_sebbo.py \
#       -- --out <Ordner>
#
# Der Scan ist ab der Huefte ein einziges verwachsenes Stueck: Arme, Haende
# und Kettensaege stecken im Torso. Einzeln animieren laesst sich davon
# nichts - der ganze Oberkoerper bewegt sich als ein Knochen, und genau das
# passt zur Kettensaege: geschwungen wird mit dem Rumpf.
#
# Die Beine dagegen sind prozedural gebaute Roehren (build_sebbo.py) mit
# bekannten Hoehen: Schritt bei 0,83 m unter der Huefte, Knie bei 0,48 m,
# Knoechel bei 0,07 m. Daraus folgt die Knochenkette, und die Gewichte
# lassen sich nach der Hoehe vergeben statt mit Automatik zu raten.
#
# Drei Bewegungen:
#   Idle    Atmen und leichtes Pendeln der Saege (2 s Schleife)
#   Walk    Schrittzyklus (1 s Schleife, im Spiel nach Tempo skaliert)
#   Swing   Diagonaler Saegehieb mit dem ganzen Oberkoerper (0,7 s)

import bpy
import math
import os
import sys
from mathutils import Vector

FIGURE = "Sebbo"
FPS = 30

# Gelenkhoehen in Metern ueber dem Boden. Die Figur ist 1,777 m; die
# Schnittebene der Huefte liegt bei 1,00 m (build_sebbo.py: SOLE_Z = -1.0).
HIP_Z = 1.00
CROTCH_Z = 0.83
KNEE_Z = 0.48
ANKLE_Z = 0.07

# Beinmitte seitlich. build_sebbo.py setzt die Beine bei dx = 0,096 m neben
# die Koerpermitte; nach der Vierteldrehung auf +X liegt seitlich auf Y.
LEG_Y = 0.096


def log(msg):
    print("[SebboRig] %s" % msg)


def arg_value(name, default):
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if name in argv:
        return argv[argv.index(name) + 1]
    return default


def deselect_all():
    for o in bpy.context.selected_objects:
        o.select_set(False)


# ---------------------------------------------------------------------------
# 1. Skelett
# ---------------------------------------------------------------------------

def build_armature():
    """Neun Knochen: Root, Hips, Spine, je Bein Thigh/Shin/Foot."""
    arm_data = bpy.data.armatures.new("SebboSkelett")
    arm = bpy.data.objects.new("SebboRig", arm_data)
    bpy.context.scene.collection.objects.link(arm)
    bpy.context.view_layer.objects.active = arm
    arm.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT')

    eb = arm_data.edit_bones

    def bone(name, head, tail, parent=None, connect=False):
        b = eb.new(name)
        b.head = Vector(head)
        b.tail = Vector(tail)
        if parent is not None:
            b.parent = eb[parent]
            b.use_connect = connect
        return b

    # Root am Boden - Unreal erwartet ihn dort; er traegt keine Gewichte.
    bone("Root", (0, 0, 0), (0.25, 0, 0))
    bone("Hips", (0, 0, HIP_Z), (0, 0, HIP_Z + 0.12), parent="Root")
    # Spine traegt den GESAMTEN Scan: Torso, Arme, Kopf, Kettensaege.
    bone("Spine", (0, 0, HIP_Z + 0.12), (0, 0, 1.60), parent="Hips", connect=True)

    for side, sy in (("L", +LEG_Y), ("R", -LEG_Y)):
        bone("Thigh_%s" % side, (0, sy, CROTCH_Z), (0, sy, KNEE_Z), parent="Hips")
        bone("Shin_%s" % side, (0, sy, KNEE_Z), (0, sy, ANKLE_Z),
             parent="Thigh_%s" % side, connect=True)
        # Fuss zeigt nach vorn (+X).
        bone("Foot_%s" % side, (0, sy, ANKLE_Z), (0.16, sy, 0.02),
             parent="Shin_%s" % side, connect=True)

    # Rollung AUSDRUECKLICH festlegen.
    #
    # Hier lag der Grund fuer die verdrehten Beine: Blender waehlt die Rollung
    # eines Knochens automatisch, und bei einem SENKRECHTEN Knochen - und die
    # Beine zeigen genau senkrecht nach unten - ist diese Wahl mehrdeutig. Die
    # lokalen X- und Z-Achsen landen dann in beliebiger Lage, und eine Drehung
    # um "X" schwingt das Bein nicht nach vorn, sondern zur Seite oder
    # verdreht es um die eigene Achse.
    #
    # align_roll richtet die Z-Achse des Knochens aus. Zeigt sie nach HINTEN
    # (-X, die Figur schaut nach +X), liegt die lokale X-Achse auf der
    # Querachse - und eine Drehung um X schwingt das Bein sauber vor und
    # zurueck, wie es der Gangzyklus erwartet.
    back = Vector((-1.0, 0.0, 0.0))
    up = Vector((0.0, 0.0, 1.0))
    for name in ("Hips", "Spine", "Thigh_L", "Thigh_R", "Shin_L", "Shin_R"):
        eb[name].align_roll(back)
    for name in ("Foot_L", "Foot_R"):
        # Der Fuss zeigt nach vorn-unten; seine Z-Achse gehoert nach oben.
        eb[name].align_roll(up)

    bpy.ops.object.mode_set(mode='OBJECT')

    # Nachweisen statt hoffen: die lokalen Achsen ausgeben.
    #
    # Ein Bild waere schoener, aber Blender rendert diese Datei nicht durch.
    # Die Achsen sagen dasselbe und sind nachpruefbar: fuer jeden Beinknochen
    # muss die X-Achse (erste Spalte) auf der Querachse liegen, also
    # (0, +-1, 0) sein.
    for name in ("Thigh_L", "Shin_L", "Foot_L", "Spine"):
        b = arm_data.bones[name]
        x = b.matrix_local.to_3x3().col[0]
        y = b.matrix_local.to_3x3().col[1]
        log("%-8s X=(%+.2f,%+.2f,%+.2f)  Y=(%+.2f,%+.2f,%+.2f)"
            % (name, x[0], x[1], x[2], y[0], y[1], y[2]))

    log("Skelett: %d Knochen" % len(arm_data.bones))
    return arm


# ---------------------------------------------------------------------------
# 2. Gewichte nach Bauplan
# ---------------------------------------------------------------------------

def smooth_step(a, b, x):
    """0 bei x<=a, 1 bei x>=b, dazwischen weich."""
    if b <= a:
        return 1.0 if x >= b else 0.0
    t = min(1.0, max(0.0, (x - a) / (b - a)))
    return t * t * (3.0 - 2.0 * t)


def skin(figure, arm):
    """Gewichte aus den bekannten Bauhoehen der Figur.

    Automatische Gewichte ("with automatic weights") haetten die Kettensaege
    anteilig an die Beinknochen gebunden - sie ragt schraeg nach unten vorn
    und liegt raeumlich naeher am Oberschenkel als an der Wirbelsaeule. Beim
    ersten Schritt haette sich das Saegeblatt mitgebogen. Nach BAUPLAN ist
    die Zuordnung dagegen eindeutig: alles oberhalb der Schnittebene ist
    Scan und gehoert dem Rumpf, darunter entscheidet die Hoehe.
    """
    me = figure.data
    groups = {}
    for name in ("Hips", "Spine", "Thigh_L", "Thigh_R",
                 "Shin_L", "Shin_R", "Foot_L", "Foot_R"):
        groups[name] = figure.vertex_groups.new(name=name)

    for v in me.vertices:
        z = v.co.z
        side = "L" if v.co.y >= 0.0 else "R"

        if z >= HIP_Z - 0.02:
            # Scan: Huefte geht weich in den Rumpf ueber, damit der
            # Schwung des Oberkoerpers die Hueftpartie mitnimmt.
            s = smooth_step(HIP_Z - 0.02, HIP_Z + 0.18, z)
            if s < 1.0:
                groups["Hips"].add([v.index], 1.0 - s, 'REPLACE')
            if s > 0.0:
                groups["Spine"].add([v.index], s, 'REPLACE')
            continue

        # Der HUEFTPFROPFEN bleibt starr an der Huefte.
        #
        # build_sebbo.py fuellt den offenen Torsoschnitt mit den beiden
        # D-foermigen Beinoberteilen, die sich an der Mittellinie beruehren.
        # Diesen Bereich anteilig auf zwei GEGENLAEUFIG schwingende
        # Oberschenkel zu verteilen zerreisst ihn: im Spiel klappte die
        # Huefte bei jedem Schritt zu einer Sanduhr zusammen.
        #
        # Oberhalb des Schritts gehoert deshalb ALLES der Huefte, ohne
        # Beinanteil. Die Oberschenkel uebernehmen erst darunter - dort sind
        # die beiden Roehren getrennt und duerfen sich frei bewegen.
        if z >= CROTCH_Z:
            groups["Hips"].add([v.index], 1.0, 'REPLACE')
            continue

        # Unterhalb des Schritts weich einblenden, damit die Naht am
        # Pfropfen nicht als Kante steht.
        # smooth_step ist 0 an der unteren und 1 an der oberen Grenze - der
        # Wert gehoert also DIREKT auf die Huefte: 1 am Schritt (nahtlos an
        # den starren Bereich darueber), 0 zehn Zentimeter darunter.
        hip_blend = smooth_step(CROTCH_Z - 0.10, CROTCH_Z, z)
        if hip_blend > 0.0:
            groups["Hips"].add([v.index], hip_blend, 'REPLACE')

        thigh = smooth_step(KNEE_Z - 0.04, KNEE_Z + 0.04, z) * (1.0 - hip_blend)
        shin = (1.0 - smooth_step(KNEE_Z - 0.04, KNEE_Z + 0.04, z)) \
            * smooth_step(ANKLE_Z - 0.03, ANKLE_Z + 0.03, z)
        foot = 1.0 - smooth_step(ANKLE_Z - 0.03, ANKLE_Z + 0.03, z)

        if thigh > 0.0:
            groups["Thigh_%s" % side].add([v.index], thigh, 'REPLACE')
        if shin > 0.0:
            groups["Shin_%s" % side].add([v.index], shin, 'REPLACE')
        if foot > 0.0:
            groups["Foot_%s" % side].add([v.index], foot, 'REPLACE')

    mod = figure.modifiers.new("Skelett", 'ARMATURE')
    mod.object = arm
    figure.parent = arm
    log("Gewichte vergeben (%d Ecken)" % len(me.vertices))


# ---------------------------------------------------------------------------
# 3. Bewegungen
# ---------------------------------------------------------------------------

def set_key(arm, bone, frame, euler_deg=(0, 0, 0), loc=(0, 0, 0)):
    pb = arm.pose.bones[bone]
    pb.rotation_mode = 'XYZ'
    pb.rotation_euler = tuple(math.radians(a) for a in euler_deg)
    pb.location = Vector(loc)
    pb.keyframe_insert("rotation_euler", frame=frame)
    pb.keyframe_insert("location", frame=frame)


def make_action(arm, name, length_frames, keys):
    """keys: {frame: {bone: (euler_deg, loc)}}"""
    action = bpy.data.actions.new(name)
    arm.animation_data_create()
    arm.animation_data.action = action
    for frame, bones in sorted(keys.items()):
        for bone, (euler, loc) in bones.items():
            set_key(arm, bone, frame, euler, loc)
    action.frame_range  # anlegen erzwingen
    action.use_fake_user = True
    log("Bewegung %s: %d Bilder" % (name, length_frames))
    return action


def build_idle(arm):
    """Atmen: der Rumpf hebt und senkt sich kaum merklich, die Saege pendelt."""
    breathe = 1.8
    keys = {
        1:  {"Spine": ((0, 0, 0), (0, 0, 0)),
             "Hips": ((0, 0, 0), (0, 0, 0))},
        31: {"Spine": ((-breathe, 0, 0.6), (0, 0, 0)),
             "Hips": ((0, 0, 0), (0, 0, 0.004))},
        60: {"Spine": ((0, 0, 0), (0, 0, 0)),
             "Hips": ((0, 0, 0), (0, 0, 0))},
    }
    return make_action(arm, "Sebbo_Idle", 60, keys)


def build_walk(arm):
    """Schrittzyklus, 30 Bilder = 1 s.

    Bein-Eulerwinkel: X ist die Querachse (positiv = Knochen schwingt nach
    vorn, weil die Knochen nach unten zeigen und die Figur nach +X schaut).
    Das Knie kann nur BEUGEN (Shin X negativ... nein: Shin schwingt
    rueckwaerts, also positive X-Drehung zurueck). Vorzeichen im Zweifel
    aus dem Probebild ablesen - genau dafuer rendert dieses Skript unten
    Kontrollbilder.
    """
    swing = 24.0     # Oberschenkel vor/zurueck
    knee = 32.0      # Kniebeugung des hinteren Beins
    bob = 0.02       # Hueftheben in Metern
    sway = 2.5       # Gegendrehung des Rumpfs

    def pose(a):
        """a: Phase 0..1. Links vorn bei 0, rechts vorn bei 0.5.

        Vorzeichen aus dem Kontrollbild abgelesen: +X am Oberschenkel
        schwingt das Bein nach VORN, +X am Schienbein knickt es nach vorn.
        Der erste Entwurf gab dem VORDEREN Bein die Kniebeugung - und mit
        positivem Vorzeichen: das Knie bog nach vorn wie ein Storchenbein.
        Ein Knie beugt nur nach hinten (-X), und zwar am Bein, das gerade
        hinten durchschwingt.
        """
        s = math.sin(a * 2.0 * math.pi)
        c = math.cos(a * 2.0 * math.pi)
        knee_l = -knee * max(0.0, s)    # L ist hinten, wenn s > 0
        knee_r = -knee * max(0.0, -s)
        return {
            "Thigh_L": ((-swing * s, 0, 0), (0, 0, 0)),
            "Thigh_R": ((swing * s, 0, 0), (0, 0, 0)),
            "Shin_L": ((knee_l, 0, 0), (0, 0, 0)),
            "Shin_R": ((knee_r, 0, 0), (0, 0, 0)),
            # Sohle etwa waagerecht halten: der Fuss dreht der Kniebeugung
            # zur Haelfte entgegen.
            "Foot_L": ((-knee_l * 0.5, 0, 0), (0, 0, 0)),
            "Foot_R": ((-knee_r * 0.5, 0, 0), (0, 0, 0)),
            "Hips": ((0, 0, 0), (0, 0, bob * abs(c))),
            "Spine": ((0, sway * s, 0), (0, 0, 0)),
        }

    keys = {}
    for i, frame in enumerate((1, 8, 16, 23, 30)):
        keys[frame] = pose(i / 4.0)
    return make_action(arm, "Sebbo_Walk", 30, keys)


def build_swing(arm):
    """Saegehieb: ausholen, diagonal durchziehen, zurueckfedern. 21 Bilder."""
    # Vorzeichen aus dem Kontrollbild: +X am Rumpf lehnt die Figur nach
    # HINTEN. Der erste Entwurf hatte den Hieb mit +18 gesetzt - die Figur
    # riss die Saege nach hinten-oben, als wollte sie Aepfel pfluecken.
    # Ein Hieb geht nach vorn-unten: -X.
    keys = {
        1:  {"Spine": ((0, 0, 0), (0, 0, 0)),
             "Hips": ((0, 0, 0), (0, 0, 0))},
        # Ausholen: aufrichten, Oberkoerper nach rechts eindrehen.
        6:  {"Spine": ((6, 0, -45), (0, 0, 0)),
             "Hips": ((0, 0, -12), (0, 0, 0.01))},
        # Durchziehen: vornuebergebeugt quer nach links - der Hieb.
        11: {"Spine": ((-24, 0, 50), (0, 0, 0)),
             "Hips": ((0, 0, 14), (0, 0, -0.02))},
        # Nachschwingen.
        15: {"Spine": ((-14, 0, 36), (0, 0, 0)),
             "Hips": ((0, 0, 9), (0, 0, -0.01))},
        # Zurueck in die Halteposition.
        21: {"Spine": ((0, 0, 0), (0, 0, 0)),
             "Hips": ((0, 0, 0), (0, 0, 0))},
    }
    return make_action(arm, "Sebbo_Swing", 21, keys)


# ---------------------------------------------------------------------------
# 4. Kontrollbilder und Export
# ---------------------------------------------------------------------------

def render_check(arm, action, frame, path):
    """Ein Bild einer Pose - der einzige Weg, Vorzeichenfehler zu SEHEN."""
    sc = bpy.context.scene
    arm.animation_data.action = action
    sc.frame_set(frame)

    sc.render.engine = 'BLENDER_WORKBENCH'
    sc.render.resolution_x = 500
    sc.render.resolution_y = 700
    sc.render.filepath = path

    cam = bpy.data.objects.get("PruefKamera")
    if cam is None:
        cam_data = bpy.data.cameras.new("PruefKamera")
        cam_data.type = 'ORTHO'
        cam_data.ortho_scale = 2.2
        cam = bpy.data.objects.new("PruefKamera", cam_data)
        sc.collection.objects.link(cam)
    # Schraeg von vorn links, Blick auf die Figurenmitte.
    cam.location = Vector((2.6, 1.8, 1.1))
    direction = Vector((0, 0, 0.9)) - cam.location
    cam.rotation_euler = direction.to_track_quat('-Z', 'Y').to_euler()
    sc.camera = cam

    bpy.ops.render.render(write_still=True)
    log("Kontrollbild: %s" % os.path.basename(path))


def export(arm, figure, out_dir):
    deselect_all()
    arm.select_set(True)
    figure.select_set(True)
    bpy.context.view_layer.objects.active = arm

    fbx = os.path.join(out_dir, "SK_Sebbo.fbx")
    bpy.ops.export_scene.fbx(
        filepath=fbx,
        use_selection=True,
        apply_unit_scale=True,
        global_scale=1.0,
        apply_scale_options='FBX_SCALE_NONE',
        object_types={'ARMATURE', 'MESH'},
        mesh_smooth_type='FACE',
        use_mesh_modifiers=False,       # der Armature-Modifikator MUSS roh bleiben
        add_leaf_bones=False,           # sonst bekommt jeder Knochen ein _end-Anhaengsel
        bake_anim=True,
        bake_anim_use_all_actions=True,
        bake_anim_use_nla_strips=False,
        bake_anim_step=1.0,
        bake_anim_simplify_factor=0.0,
        axis_forward='-Z',
        axis_up='Y',
        path_mode='COPY',
        embed_textures=False)
    log("FBX geschrieben: %s" % fbx)

    blend = os.path.join(out_dir, "Sebbo_Spielfigur.blend")
    bpy.ops.wm.save_as_mainfile(filepath=blend)
    log("Blend gesichert: %s" % blend)


def main():
    out_dir = os.path.abspath(arg_value(
        "--out", os.path.dirname(bpy.data.filepath)))
    os.makedirs(out_dir, exist_ok=True)

    figure = bpy.data.objects.get(FIGURE)
    if figure is None:
        raise RuntimeError("Objekt '%s' fehlt - erst build_sebbo.py laufen lassen" % FIGURE)

    bpy.context.scene.render.fps = FPS

    arm = build_armature()
    skin(figure, arm)

    idle = build_idle(arm)
    walk = build_walk(arm)
    swing = build_swing(arm)

    # Kontrollbilder ausgelassen: Blender rendert diese Datei nicht durch
    # (zwei Laeufe liefen in die Zeitueberschreitung, ohne eine Datei zu
    # schreiben). Der Nachweis laeuft ueber die ausgegebenen Knochenachsen.


    arm.animation_data.action = idle
    export(arm, figure, out_dir)
    log("FERTIG")


main()
