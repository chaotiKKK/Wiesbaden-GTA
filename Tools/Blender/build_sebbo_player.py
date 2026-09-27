"""Neue Sebbo-Spielfigur aus dem geriggten Tripo-GLB (Skelett + 10 Bewegungen).

Quelle: Data/Raw/Sebbo/Spieler/sebbo_source.glb (gitignored, aus Downloads
"Sebbo_3d.glb"). Anders als Iris/Denno bringt diese Figur ihr Skelett schon
mit - 61 Knochen in UE5-Mannequin-Benennung (root, pelvis, spine_01 ...) -
und zehn fertige Bewegungen. Hier wird nichts geriggt, nur umgesetzt:

  1. Masstab: Tripo normiert auf ~0,98 m -> HEIGHT_M. Knochen-Verschiebungen
     in den Aktionen werden mitskaliert (sie stehen in Armatur-Einheiten).
  2. Blick +X, Boden Z = 0 (wie tripo_rig.py; der FBX-Export mit axis_forward
     -Z / up Y bringt Blick +X unverdreht nach Unreal). Die Blickrichtung kommt
     aus der Geometrie (Fussgelenk -> Ballen), nicht aus einer Annahme.
  3. Aktionen heissen Sebbo_<Bewegung> - der Import (import_tripo_figure.py,
     WB_FIGUR=Sebbo) macht daraus A_Sebbo_<Bewegung>. Welche Bewegung aus
     welchem Tripo-Clip kommt, steht NUR in C++ (EWbSebboMove, gelesen ueber
     Tools/sebbo_bewegungen.py).
  4. Ducken (BlenderFrom-Eintraege): das Modell hat keinen Duck-Clip. Er
     entsteht aus Idle bzw. Walk - Becken tiefer, Oberkoerper vor; die Beine
     per Zwei-Knochen-IK (gerechnet, Knie nach vorn), sodass jedes Fussgelenk
     EXAKT auf der Bahn der Quelle bleibt - Schrittlaenge und Bodenkontakt
     stimmen damit.
  5. Farbtextur als T_Sebbo_Part0.png, Materialschlitz M_Sebbo_Part0.
     Normal- und Rauheitskarte entfallen: M_WbFigur kennt nur die Grundfarbe.

Aufruf:
  blender -b -P Tools/Blender/build_sebbo_player.py
Ausgabe: Data/Raw/Sebbo/Spieler/SK_Sebbo.fbx + tex/, Kontrollbilder unter
.planning/sebbo-spieler/.
"""
import math
import os
import sys
from pathlib import Path

import bpy
from mathutils import Matrix, Vector

ROOT = Path(__file__).resolve().parents[2]
SRC_DIR = ROOT / 'Data' / 'Raw' / 'Sebbo' / 'Spieler'
SRC = SRC_DIR / 'sebbo_source.glb'
PREVIEW = ROOT.parent / '.planning' / 'sebbo-spieler'
HEIGHT_M = 1.80
TEXTURE_MAX = 2048
TAG = '###SEBBO_SPIELER'

sys.path.insert(0, str(ROOT / 'Tools'))
from sebbo_bewegungen import bewegungen  # noqa: E402

# (Tripo-Stamm, Bewegung). Exakter Vergleich des Stamms: "run" steckt in "turn".
CLIPS = [(src, name) for name, art, src in bewegungen() if art == 'tripo']
# In Blender gebaute Bewegungen: (Bewegung, Quelle).
AUTHORED = [(name, src) for name, art, src in bewegungen() if art == 'blender']

# Wie tief und wie weit vor: Becken senken (m), Rumpf vorneigen, Kopf wieder
# heben (Grad).
CROUCH = {
    'CrouchIdle': dict(drop=0.46, lean=18.0, neck=-12.0),
    'CrouchWalk': dict(drop=0.38, lean=26.0, neck=-16.0),
}


def log(msg):
    print('%s %s' % (TAG, msg))


def fcurves_of(action):
    """Alle F-Kurven einer Aktion - geschichtete Aktionen (Blender 4.4+) und alte."""
    layers = getattr(action, 'layers', None)
    if layers:
        for layer in layers:
            for strip in layer.strips:
                for bag in strip.channelbags:
                    yield from bag.fcurves
    elif hasattr(action, 'fcurves'):
        yield from action.fcurves


def load():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=str(SRC))
    arms = [o for o in bpy.context.scene.objects if o.type == 'ARMATURE']
    # Der glTF-Import legt neben der Figur ein Hilfs-Mesh fuer die Knochenform
    # an - die Figur ist das Mesh mit Armature-Modifikator.
    meshes = [o for o in bpy.context.scene.objects
              if o.type == 'MESH' and any(m.type == 'ARMATURE' for m in o.modifiers)]
    if len(arms) != 1 or len(meshes) != 1:
        raise RuntimeError('Erwartet 1 Armatur + 1 Mesh, gefunden %d + %d' % (len(arms), len(meshes)))
    arm, body = arms[0], meshes[0]
    for o in list(bpy.context.scene.objects):
        if o.type == 'MESH' and o != body:
            log('Hilfsobjekt entfernt: %s (%d Ecken)' % (o.name, len(o.data.vertices)))
            bpy.data.objects.remove(o, do_unlink=True)
    # "Armature" heisst beim FBX-Import in Unreal KEIN zusaetzlicher Wurzelknochen.
    arm.name = 'Armature'
    body.name = 'SK_Sebbo'
    body.data.name = 'SK_Sebbo'
    return arm, body


def rename_actions():
    found = {}
    for action in list(bpy.data.actions):
        stem = action.name.lower().split('.')[0]
        for key, kind in CLIPS:
            if stem == key and kind not in found:
                action.name = 'Sebbo_%s' % kind
                action.use_fake_user = True
                found[kind] = action
                break
        else:
            log('Aktion ohne Zuordnung: %s' % action.name)
    missing = [k for _, k in CLIPS if k not in found]
    if missing:
        raise RuntimeError('Bewegungen fehlen: %s (Aktionen: %s)' % (missing, [a.name for a in bpy.data.actions]))
    for kind, action in found.items():
        start, end = action.frame_range
        log('Aktion Sebbo_%s: Bilder %d-%d' % (kind, start, end))
    freeze_root_rotation(found['Turn'])
    return found


def freeze_root_rotation(action):
    """Turn dreht die WURZEL um ~176 Grad (Umdrehen auf der Stelle). Im Spiel
    dreht der Actor selbst - als Schleife verdoppelte der Clip die Drehung und
    sprang je Durchlauf zurueck. Die Wurzel bleibt in Ruhelage, uebrig bleibt
    das Treten auf der Stelle."""
    frozen = 0
    for fc in fcurves_of(action):
        if fc.data_path == 'pose.bones["root"].rotation_quaternion':
            rest = 1.0 if fc.array_index == 0 else 0.0
            for kp in fc.keyframe_points:
                kp.co.y = rest
                kp.handle_left.y = rest
                kp.handle_right.y = rest
            fc.update()
            frozen += 1
    if frozen != 4:
        raise RuntimeError('Turn: %d statt 4 Wurzel-Quaternionkurven gefunden' % frozen)
    log('Turn: Wurzeldrehung eingefroren (%d Kurven)' % frozen)


def world_bbox(body):
    pts = [body.matrix_world @ v.co for v in body.data.vertices]
    lo = Vector((min(p.x for p in pts), min(p.y for p in pts), min(p.z for p in pts)))
    hi = Vector((max(p.x for p in pts), max(p.y for p in pts), max(p.z for p in pts)))
    return lo, hi


def facing_angle(arm):
    """Winkel der Blickrichtung (Fussgelenk -> Ballen, beide Fuesse) gegen +X."""
    fwd = Vector((0.0, 0.0, 0.0))
    for side in ('l', 'r'):
        foot = arm.matrix_world @ arm.data.bones['foot_%s' % side].head_local
        ball = arm.matrix_world @ arm.data.bones['ball_%s' % side].head_local
        fwd += ball - foot
    fwd.z = 0.0
    return math.atan2(fwd.y, fwd.x), fwd.length


def normalize(arm, body, actions):
    bpy.context.scene.frame_set(0)
    lo, hi = world_bbox(body)
    scale = HEIGHT_M / (hi.z - lo.z)
    angle, strength = facing_angle(arm)
    log('Quelle: Hoehe %.3f m, Boden %.3f, Blick %.1f Grad (Fussvektor %.3f) -> Faktor %.3f' % (
        hi.z - lo.z, lo.z, math.degrees(angle), strength, scale))

    transform = (Matrix.Translation((0.0, 0.0, -lo.z * scale))
                 @ Matrix.Rotation(-angle, 4, 'Z') @ Matrix.Scale(scale, 4))
    arm.matrix_world = transform @ arm.matrix_world
    bpy.ops.object.select_all(action='DESELECT')
    arm.select_set(True)
    body.select_set(True)
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)

    # Knochen-Verschiebungen stehen in Armatur-Einheiten - sonst schrumpfte
    # jede Hueftbewegung auf die Tripo-Groesse zurueck.
    scaled = 0
    for action in actions.values():
        for fc in fcurves_of(action):
            if fc.data_path.endswith('.location'):
                for kp in fc.keyframe_points:
                    kp.co.y *= scale
                    kp.handle_left.y *= scale
                    kp.handle_right.y *= scale
                fc.update()
                scaled += 1
    lo, hi = world_bbox(body)
    angle, _ = facing_angle(arm)
    log('Ziel: Hoehe %.3f m, Boden %.3f, Blick %.1f Grad, %d Verschiebungskurven skaliert' % (
        hi.z - lo.z, lo.z, math.degrees(angle), scaled))


def _key_pose(pb, frame):
    pb.keyframe_insert('location', frame=frame)
    if pb.rotation_mode == 'QUATERNION':
        pb.keyframe_insert('rotation_quaternion', frame=frame)
    else:
        pb.keyframe_insert('rotation_euler', frame=frame)


def _about(pivot, rot):
    """Drehung um einen Punkt (Armaturraum)."""
    return Matrix.Translation(pivot) @ rot.to_matrix().to_4x4() @ Matrix.Translation(-pivot)


def _knee(hip, ankle, thigh_len, shin_len, pole):
    """Zwei-Knochen-IK: Kniepunkt fuer Huefte -> Fussgelenk, Knie Richtung pole."""
    reach = ankle - hip
    d = min(max(reach.length, abs(thigh_len - shin_len) + 1e-4), thigh_len + shin_len - 1e-4)
    u = reach.normalized()
    x = (thigh_len ** 2 - shin_len ** 2 + d ** 2) / (2.0 * d)
    h = math.sqrt(max(thigh_len ** 2 - x ** 2, 0.0))
    v = (pole - u * pole.dot(u)).normalized()
    return hip + u * x + v * h


def author_crouch(arm, actions):
    """Baut jede BlenderFrom-Bewegung aus ihrer Quelle (siehe CROUCH).

    Becken tiefer, Rumpf vor, Kopf zurueck; die Beine per analytischer
    Zwei-Knochen-IK so, dass jedes Fussgelenk EXAKT auf der Bahn der Quelle
    bleibt (Knie nach vorn). Blender-IK-Constraints wirkten auf dieses Skelett
    im Hintergrundlauf nicht (Fehler blieb 20 cm, jede Kette, jeder Loeser) -
    darum wird hier gerechnet und absolut gesetzt, ohne Backen.
    """
    sc = bpy.context.scene
    pb = arm.pose.bones
    built = {}

    def use(action):
        arm.animation_data.action = action
        if hasattr(arm.animation_data, 'action_slot') and action.slots:
            arm.animation_data.action_slot = action.slots[0]

    for name, src in AUTHORED:
        if name not in CROUCH:
            raise RuntimeError('%s: keine Bauvorschrift in CROUCH' % name)
        cfg = CROUCH[name]
        source = actions[src]
        start, end = (int(v) for v in source.frame_range)
        frames = range(start, end + 1)

        # 1. Aus der UNVERAENDERTEN Quelle rechnen, erst dann setzen: konstante
        #    Tripo-Kanaele haben nur 2 Schluessel - ein Schluessel in Bild f
        #    verschob Bild f+1, das dann NOCH einmal abgesenkt wurde.
        use(source)
        down = Matrix.Translation((0.0, 0.0, -cfg['drop']))
        forward = Vector((1.0, 0.0, 0.0))
        poses, feet = {}, {}
        for f in frames:
            sc.frame_set(f)
            lean = _about(pb['spine_01'].head, Matrix.Rotation(math.radians(cfg['lean']), 3, 'Y').to_quaternion())
            neck_head = down @ lean @ pb['neck_01'].head
            neck = _about(neck_head, Matrix.Rotation(math.radians(cfg['neck']), 3, 'Y').to_quaternion())
            pose = {'pelvis': down @ pb['pelvis'].matrix,
                    'spine_01': down @ lean @ pb['spine_01'].matrix,
                    'neck_01': neck @ down @ lean @ pb['neck_01'].matrix}
            feet[f] = {}
            for s in 'lr':
                hip_src, knee_src, ankle = pb['thigh_%s' % s].head, pb['calf_%s' % s].head, pb['foot_%s' % s].head
                feet[f][s] = ankle.copy()
                hip = down @ hip_src
                knee_old = down @ knee_src
                pole = (knee_src - 0.5 * (hip_src + ankle)) + forward * 0.3
                knee = _knee(hip, ankle, (knee_src - hip_src).length, (ankle - knee_src).length, pole)
                thigh = _about(hip, (knee_old - hip).rotation_difference(knee - hip))
                ankle_now = thigh @ down @ ankle
                calf = _about(knee, (ankle_now - knee).rotation_difference(ankle - knee))
                pose['thigh_%s' % s] = thigh @ down @ pb['thigh_%s' % s].matrix
                pose['calf_%s' % s] = calf @ thigh @ down @ pb['calf_%s' % s].matrix
                pose['foot_%s' % s] = pb['foot_%s' % s].matrix.copy()   # Fuss liegt wie in der Quelle
            poses[f] = pose

        # 2. Kopie: Zielposen ABSOLUT setzen, Eltern zuerst.
        action = source.copy()
        action.name = 'Sebbo_%s' % name
        action.use_fake_user = True
        use(action)
        order = ('pelvis', 'spine_01', 'neck_01', 'thigh_l', 'calf_l', 'foot_l', 'thigh_r', 'calf_r', 'foot_r')
        for f in frames:
            sc.frame_set(f)
            for bone in order:
                pb[bone].matrix = poses[f][bone]
                bpy.context.view_layer.update()
                _key_pose(pb[bone], f)

        # 3. Pruefen: Fussgelenke auf der Quellbahn, Becken tiefer, Knie VOR der
        #    Linie Huefte-Fuss (Blick +X).
        foot_err = knee_ok = low = high = 0.0
        for f in frames:
            sc.frame_set(f)
            low += pb['pelvis'].head.z
            for s in 'lr':
                foot_err = max(foot_err, (pb['foot_%s' % s].head - feet[f][s]).length)
                hip, knee, ankle = pb['thigh_%s' % s].head, pb['calf_%s' % s].head, pb['foot_%s' % s].head
                knee_ok += 1.0 if knee.x > 0.5 * (hip.x + ankle.x) else 0.0
        use(source)
        for f in frames:
            sc.frame_set(f)
            high += pb['pelvis'].head.z
        n = len(frames)
        log('%s aus %s: Bilder %d-%d, Becken %.2f -> %.2f m, Fussabweichung max %.1f cm, Knie vorn %.0f %%' % (
            name, src, start, end, high / n, low / n, foot_err * 100.0, 100.0 * knee_ok / (2 * n)))
        if foot_err > 0.02 or knee_ok < 2 * n:
            raise RuntimeError('%s: Fuesse oder Knie falsch (siehe Log)' % name)
        built[name] = action
    return built


def textures(body):
    tex_dir = SRC_DIR / 'tex'
    tex_dir.mkdir(parents=True, exist_ok=True)
    for old in tex_dir.glob('*.png'):
        old.unlink()
    if len(body.data.materials) != 1:
        raise RuntimeError('Erwartet 1 Material, gefunden %d' % len(body.data.materials))
    mat = body.data.materials[0]
    mat.name = 'M_Sebbo_Part0'
    bsdf = next(n for n in mat.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    link = bsdf.inputs['Base Color'].links
    if not link or link[0].from_node.type != 'TEX_IMAGE':
        raise RuntimeError('Grundfarbe ohne Bildtextur')
    img = link[0].from_node.image
    w, h = img.size
    if max(w, h) > TEXTURE_MAX:
        f = TEXTURE_MAX / max(w, h)
        img.scale(round(w * f), round(h * f))
    img.name = 'T_Sebbo_Part0'
    img.filepath_raw = str(tex_dir / 'T_Sebbo_Part0.png')
    img.file_format = 'PNG'
    img.save()
    log('Textur %s -> %s' % ((w, h), tuple(img.size)))


def render_checks(arm, actions):
    """Kontrollbilder: Blick, Groesse und Bewegung SEHEN statt herleiten."""
    PREVIEW.mkdir(parents=True, exist_ok=True)
    sc = bpy.context.scene
    sc.render.engine = 'BLENDER_WORKBENCH'
    sc.display.shading.color_type = 'TEXTURE'
    sc.render.resolution_x, sc.render.resolution_y = 420, 560
    cam = bpy.data.objects.new('PruefKamera', bpy.data.cameras.new('PruefKamera'))
    cam.data.type = 'ORTHO'
    cam.data.ortho_scale = 2.3
    sc.collection.objects.link(cam)
    sc.camera = cam
    views = {'vorn': Vector((4.0, 0.0, 0.95)), 'seite': Vector((0.0, -4.0, 0.95))}
    for kind in ('Idle', 'Walk', 'Run', 'Jump', 'Kick', 'Turn', 'CrouchIdle', 'CrouchWalk'):
        action = actions[kind]
        arm.animation_data.action = action
        if hasattr(arm.animation_data, 'action_slot') and action.slots:
            arm.animation_data.action_slot = action.slots[0]
        start, end = action.frame_range
        for view, pos in views.items():
            if kind not in ('Idle', 'Walk', 'CrouchIdle', 'CrouchWalk') and view == 'vorn':
                continue
            cam.location = pos
            cam.rotation_euler = (Vector((0.0, 0.0, 0.95)) - pos).to_track_quat('-Z', 'Y').to_euler()
            for i, frame in enumerate((start, (start + end) * 0.5)):
                sc.frame_set(int(frame))
                if view == 'seite':
                    pb = arm.pose.bones
                    m = arm.matrix_world
                    feet = sum(((m @ pb['ball_%s' % s].head) - (m @ pb['foot_%s' % s].head) for s in 'lr'), Vector())
                    log('%s Bild %d: Fuesse %.0f Grad' % (kind, frame, math.degrees(math.atan2(feet.y, feet.x))))
                sc.render.filepath = str(PREVIEW / ('%s_%s_%d.png' % (kind, view, i)))
                bpy.ops.render.render(write_still=True)
    log('Kontrollbilder: %s' % PREVIEW)


def export(arm, body, actions):
    arm.animation_data.action = actions['Idle']
    if hasattr(arm.animation_data, 'action_slot') and actions['Idle'].slots:
        arm.animation_data.action_slot = actions['Idle'].slots[0]
    bpy.context.scene.frame_set(0)
    bpy.ops.object.select_all(action='DESELECT')
    arm.select_set(True)
    body.select_set(True)
    bpy.context.view_layer.objects.active = arm
    fbx = SRC_DIR / 'SK_Sebbo.fbx'
    bpy.ops.export_scene.fbx(
        filepath=str(fbx), use_selection=True, apply_unit_scale=True, global_scale=1.0,
        apply_scale_options='FBX_SCALE_NONE', object_types={'ARMATURE', 'MESH'},
        mesh_smooth_type='FACE', use_mesh_modifiers=False, add_leaf_bones=False,
        bake_anim=True, bake_anim_use_all_actions=True, bake_anim_use_nla_strips=False,
        bake_anim_step=1.0, bake_anim_simplify_factor=0.0,
        axis_forward='-Z', axis_up='Y', path_mode='STRIP', embed_textures=False)
    log('FBX %s (%.1f MB), %d Dreiecke, %d Knochen' % (
        fbx, os.path.getsize(str(fbx)) / 1e6,
        sum(len(p.vertices) - 2 for p in body.data.polygons), len(arm.data.bones)))


def main():
    arm, body = load()
    actions = rename_actions()
    normalize(arm, body, actions)
    actions.update(author_crouch(arm, actions))
    textures(body)
    if os.environ.get('WB_OHNE_BILDER') != '1':
        render_checks(arm, actions)
    export(arm, body, actions)
    log('FERTIG')


main()
