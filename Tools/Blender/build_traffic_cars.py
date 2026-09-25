"""Verkehrsfahrzeuge aus Tripo-Modellen: Karosserie + vier drehbare Raeder.

Je Fahrzeug aus Tools/verkehr_fahrzeuge.json:
  1. Tripo-glTF laden (30 Teile mit je eigener Textur, ~1,9 Mio Dreiecke).
  2. Raeder finden: die Tripo-Teile, die den Boden beruehren und rund sind
     (Laenge ~ Hoehe), dazu jedes Teil, das ganz in ihrem Umriss liegt
     (Radkappe, Felge). Genau vier - sonst Abbruch.
  3. Rahmen wie das Spiel: Front +X, links +Y (Unreal: -Y), Ursprung in der
     Radstandmitte zwischen den Spuren, Boden z = 0; Massstab = echte Laenge.
     Tripo-Modelle geraten oft zu breit (Golf: Spur 1,62 statt 1,43 m) - die
     Breite wird auf die echte Spurweite gestaucht, nur Y: die Raeder bleiben
     rund. Jedes Rad sitzt auf dem Boden (Tripo laesst einzelne schweben).
  4. Texturen umbenennen (T_<Name>_PartN) und verkleinern, Karosserie und
     Raeder getrennt dezimieren (das Spiel zeichnet sie mit Nanite).
  5. FBX je Teil (SM_<Name>_Body, SM_<Name>_Wheel_FL/FR/RL/RR) - der Rad-
     Ursprung bleibt der Fahrzeugursprung: die Radmitte ist die Mitte seiner
     Bounds, daraus rechnet das Spiel Drehung und Einschlag.
  6. masse.json (Radstand, Spur, Radradius, Laenge ...) fuer C++
     (WiesbadenTrafficCarTypes) und Kontrollbilder nach .planning/verkehr-modelle/.

Blender 5.2:
  blender -b -P Tools/Blender/build_traffic_cars.py -- Golf
  blender -b -P Tools/Blender/build_traffic_cars.py -- alle
"""
import json
import math
import os
import sys
from pathlib import Path

import bpy
from mathutils import Matrix, Vector

ROOT = Path(__file__).resolve().parents[2]
REGISTRY = json.loads((ROOT / 'Tools/verkehr_fahrzeuge.json').read_text(encoding='utf-8'))['fahrzeuge']
WHEELS = ('FL', 'FR', 'RL', 'RR')
# Tripo-Rahmen: Laenge ~1 entlang Y, Front -Y, links +X, Boden z = 0.
GROUND_TOL = 0.02
CONTAIN_TOL = 0.006


def log(name, msg):
    print('###%s %s' % (name.upper(), msg), flush=True)


def world_box(obj):
    pts = [obj.matrix_world @ v.co for v in obj.data.vertices]
    return (Vector((min(p.x for p in pts), min(p.y for p in pts), min(p.z for p in pts))),
            Vector((max(p.x for p in pts), max(p.y for p in pts), max(p.z for p in pts))))


def tris(obj):
    return sum(len(p.vertices) - 2 for p in obj.data.polygons)


def join(objs, name):
    bpy.ops.object.select_all(action='DESELECT')
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    if len(objs) > 1:
        bpy.ops.object.join()
    obj = bpy.context.view_layer.objects.active
    obj.name = name
    obj.data.name = name
    return obj


def load(src):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=str(src))
    parts = [o for o in bpy.context.scene.objects if o.type == 'MESH']
    bpy.ops.object.select_all(action='DESELECT')
    for o in parts:
        o.select_set(True)
    bpy.context.view_layer.objects.active = parts[0]
    bpy.ops.object.parent_clear(type='CLEAR_KEEP_TRANSFORM')
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    for o in list(bpy.context.scene.objects):
        if o.type != 'MESH':
            bpy.data.objects.remove(o, do_unlink=True)
    # Tripo haengt Ein-Dreieck-Reste an (tripo_part_34 & Co.) - weg damit.
    keep = []
    for o in parts:
        if len(o.data.polygons) <= 2:
            bpy.data.objects.remove(o, do_unlink=True)
        else:
            keep.append(o)
    return keep


def find_wheels(name, parts):
    """Vier Reifen (Boden, rund, schmal) + alles in ihrem Umriss."""
    boxes = {o.name: world_box(o) for o in parts}
    tires = []
    for o in parts:
        lo, hi = boxes[o.name]
        ext = hi - lo
        round_ = 0.7 < ext.y / max(ext.z, 1e-6) < 1.4
        if lo.z < GROUND_TOL and round_ and ext.x < 0.12 and ext.z < 0.25:
            tires.append(o)
    # Ein Kandidat ganz im Umriss eines anderen ist dessen Radkappe/Felge, kein
    # eigener Reifen (BMW E46: die Nabe reicht bis 1,7 cm an den Boden).
    def innen(a, b):
        alo, ahi = boxes[a.name]
        blo, bhi = boxes[b.name]
        return all(alo[i] >= blo[i] - CONTAIN_TOL and ahi[i] <= bhi[i] + CONTAIN_TOL for i in range(3))
    tires = [t for t in tires if not any(o is not t and innen(t, o) for o in tires)]
    if len(tires) != 4:
        raise RuntimeError('%s: %d Reifen statt 4: %s' % (name, len(tires), [o.name for o in tires]))
    groups = {}
    for tire in tires:
        lo, hi = boxes[tire.name]
        c = (lo + hi) * 0.5
        key = ('F' if c.y < 0 else 'R') + ('L' if c.x > 0 else 'R')   # Front -Y, links +X
        groups[key] = [tire]
    if sorted(groups) != sorted(WHEELS):
        raise RuntimeError('%s: Radlagen %s' % (name, sorted(groups)))
    for o in parts:
        if any(o is g[0] for g in groups.values()):
            continue
        lo, hi = boxes[o.name]
        for key, g in groups.items():
            tlo, thi = boxes[g[0].name]
            inside = all(lo[i] >= tlo[i] - CONTAIN_TOL and hi[i] <= thi[i] + CONTAIN_TOL for i in range(3))
            if inside:
                g.append(o)
                break
    for key in WHEELS:
        log(name, 'Rad %s: %s' % (key, [o.name for o in groups[key]]))
    return groups


def textures(name, objs, tex_dir, main_edge, other_edge):
    os.makedirs(tex_dir, exist_ok=True)
    images = {}
    for obj in objs:
        for mat in obj.data.materials:
            part = mat.name.split('tripo_part_')[-1].split('_')[0]
            mat.name = 'M_%s_Part%s' % (name, part)
            for node in mat.node_tree.nodes:
                if node.type == 'TEX_IMAGE' and node.image:
                    images[part] = node.image
    biggest = max(images.values(), key=lambda im: im.size[0] * im.size[1])
    for part, img in sorted(images.items(), key=lambda kv: int(kv[0])):
        w, h = img.size
        edge = main_edge if img is biggest else other_edge
        if max(w, h) > edge:
            f = edge / max(w, h)
            img.scale(max(16, round(w * f)), max(16, round(h * f)))
        img.name = 'T_%s_Part%s' % (name, part)
        img.filepath_raw = os.path.join(str(tex_dir), 'T_%s_Part%s.png' % (name, part))
        img.file_format = 'PNG'
        img.save()
        log(name, 'Textur Part%s %s -> %s' % (part, (w, h), tuple(img.size)))


def decimate(obj, target):
    before = tris(obj)
    if before > target:
        mod = obj.modifiers.new('decimate', 'DECIMATE')
        mod.ratio = target / before
        bpy.ops.object.select_all(action='DESELECT')
        obj.select_set(True)
        bpy.context.view_layer.objects.active = obj
        bpy.ops.object.modifier_apply(modifier=mod.name)
    return before, tris(obj)


def export_fbx(obj, path):
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.export_scene.fbx(
        filepath=str(path), use_selection=True, apply_unit_scale=True, global_scale=1.0,
        apply_scale_options='FBX_SCALE_NONE', object_types={'MESH'}, mesh_smooth_type='FACE',
        use_mesh_modifiers=False, add_leaf_bones=False, bake_anim=False,
        axis_forward='-Z', axis_up='Y', path_mode='STRIP', embed_textures=False)


def render_views(name, body, wheels, out_dir, dims):
    """Kontrollbilder: Seite, schraeg vorn, und die Raeder gedreht + eingeschlagen
    (Drehpunkt = Mitte der Rad-Bounds, wie im Spiel)."""
    scene = bpy.context.scene
    scene.render.engine = 'BLENDER_WORKBENCH'
    scene.display.shading.light = 'STUDIO'
    scene.display.shading.color_type = 'TEXTURE'
    scene.render.resolution_x, scene.render.resolution_y = 900, 520
    scene.render.film_transparent = True
    cam = bpy.data.objects.new('cam', bpy.data.cameras.new('cam'))
    scene.collection.objects.link(cam)
    scene.camera = cam
    os.makedirs(str(out_dir), exist_ok=True)
    look = Vector((0, 0, dims['hoehe_m'] * 0.4))

    def shot(tag, direction, distance):
        cam.location = look + direction.normalized() * distance
        cam.rotation_euler = (look - cam.location).to_track_quat('-Z', 'Y').to_euler()
        scene.render.filepath = os.path.join(str(out_dir), '%s_%s.png' % (name, tag))
        bpy.ops.render.render(write_still=True)

    L = dims['laenge_m']
    shot('seite', Vector((0, -1, 0.05)), L * 1.6)
    shot('schraeg', Vector((1, -0.9, 0.35)), L * 1.5)
    # Raeder um ihre Bounds-Mitte: 30 Grad gedreht, vorn 25 Grad links eingeschlagen.
    for key, w in wheels.items():
        lo, hi = world_box(w)
        c = (lo + hi) * 0.5
        rot = Matrix.Rotation(math.radians(30), 4, 'Y')
        if key.startswith('F'):
            rot = Matrix.Rotation(math.radians(25), 4, 'Z') @ rot
        w.matrix_world = Matrix.Translation(c) @ rot @ Matrix.Translation(-c) @ w.matrix_world
    shot('lenkung', Vector((0.05, 0.0, 1.0)), L * 2.2)
    shot('lenkung_seite', Vector((0.25, -1, 0.1)), L * 1.4)


def build(name, cfg):
    out_dir = ROOT / 'Data/Raw/Verkehr' / name
    preview_dir = ROOT.parent / '.planning/verkehr-modelle' / name
    parts = load(ROOT / cfg['quelle'])
    log(name, '%d Teile, %d Dreiecke' % (len(parts), sum(tris(o) for o in parts)))
    groups = find_wheels(name, parts)
    wheel_parts = {id(o) for g in groups.values() for o in g}
    body = join([o for o in parts if id(o) not in wheel_parts], 'SM_%s_Body' % name)
    wheels = {key: join(groups[key], 'SM_%s_Wheel_%s' % (name, key)) for key in WHEELS}

    # Rahmen: Front -Y -> +X (90 Grad um Z), echte Laenge, Boden 0.
    lo, hi = world_box(body)
    for w in wheels.values():
        wlo, whi = world_box(w)
        lo = Vector(map(min, lo, wlo))
        hi = Vector(map(max, hi, whi))
    scale = cfg['laenge_m'] / (hi.y - lo.y)
    frame = Matrix.Scale(scale, 4) @ Matrix.Rotation(math.radians(90), 4, 'Z')
    for obj in [body] + list(wheels.values()):
        obj.matrix_world = frame @ obj.matrix_world
    # Breite auf die echte Spurweite (Mitte der Reifen vorn).
    if cfg.get('spur_m'):
        track = world_box(wheels['FL'])[0].y + world_box(wheels['FL'])[1].y \
            - world_box(wheels['FR'])[0].y - world_box(wheels['FR'])[1].y
        squeeze = cfg['spur_m'] / (0.5 * track)
        log(name, 'Breite x %.3f (Spur %.3f -> %.3f m)' % (squeeze, 0.5 * track, cfg['spur_m']))
        for obj in [body] + list(wheels.values()):
            obj.matrix_world = Matrix.Diagonal((1.0, squeeze, 1.0, 1.0)) @ obj.matrix_world
    # Jedes Rad auf den Boden; danach Ursprung in die Radstandmitte.
    ground = min(world_box(w)[0].z for w in wheels.values())
    for obj in [body] + list(wheels.values()):
        obj.matrix_world = Matrix.Translation((0, 0, -ground)) @ obj.matrix_world
    for key, w in wheels.items():
        drop = world_box(w)[0].z
        if abs(drop) > 1e-4:
            log(name, 'Rad %s um %.1f cm auf den Boden gesetzt' % (key, drop * 100))
            w.matrix_world = Matrix.Translation((0, 0, -drop)) @ w.matrix_world
    centers = {}
    for key, w in wheels.items():
        wlo, whi = world_box(w)
        centers[key] = ((wlo + whi) * 0.5, (whi.z - wlo.z) * 0.5, (whi.x - wlo.x) * 0.5)
    x0 = 0.25 * sum(c[0].x for c in centers.values())
    y0 = 0.25 * sum(c[0].y for c in centers.values())
    for obj in [body] + list(wheels.values()):
        obj.matrix_world = Matrix.Translation((-x0, -y0, 0)) @ obj.matrix_world
    bpy.ops.object.select_all(action='DESELECT')
    for obj in [body] + list(wheels.values()):
        obj.select_set(True)
    bpy.context.view_layer.objects.active = body
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)

    textures(name, [body] + list(wheels.values()), out_dir / 'tex',
             cfg.get('hauptteil_textur', 2048), cfg.get('nebenteil_textur', 1024))
    b0, b1 = decimate(body, cfg['dreiecke'])
    log(name, 'Karosserie %d -> %d Dreiecke, %d Materialien' % (b0, b1, len(body.data.materials)))
    for key, w in wheels.items():
        w0, w1 = decimate(w, cfg['rad_dreiecke'])
        log(name, 'Rad %s %d -> %d Dreiecke' % (key, w0, w1))

    # Masse im Spielrahmen (m).
    lo, hi = world_box(body)
    wc = {}
    for key, w in wheels.items():
        wlo, whi = world_box(w)
        lo = Vector(map(min, lo, wlo))
        hi = Vector(map(max, hi, whi))
        c = (wlo + whi) * 0.5
        wc[key] = {'x': round(c.x, 4), 'y': round(c.y, 4), 'z': round(c.z, 4),
                   'radius': round((whi.z - wlo.z) * 0.5, 4), 'breite': round(whi.x - wlo.x, 4)}
    wheelbase = 0.5 * (wc['FL']['x'] + wc['FR']['x']) - 0.5 * (wc['RL']['x'] + wc['RR']['x'])
    dims = {
        'vorbild': cfg.get('vorbild', ''),
        'laenge_m': round(hi.x - lo.x, 4), 'breite_m': round(hi.y - lo.y, 4), 'hoehe_m': round(hi.z - lo.z, 4),
        'vorn_m': round(hi.x, 4), 'hinten_m': round(lo.x, 4),
        'radstand_m': round(wheelbase, 4),
        'spur_vorn_m': round(wc['FL']['y'] - wc['FR']['y'], 4),
        'spur_hinten_m': round(wc['RL']['y'] - wc['RR']['y'], 4),
        'radradius_m': round(0.25 * sum(w['radius'] for w in wc.values()), 4),
        'raeder': wc,
    }
    (out_dir / 'masse.json').write_text(json.dumps(dims, indent=2, ensure_ascii=False), encoding='utf-8')
    log(name, 'Masse %s' % json.dumps({k: v for k, v in dims.items() if k != 'raeder'}, ensure_ascii=False))

    export_fbx(body, out_dir / ('SM_%s_Body.fbx' % name))
    for key, w in wheels.items():
        export_fbx(w, out_dir / ('SM_%s_Wheel_%s.fbx' % (name, key)))
    render_views(name, body, wheels, preview_dir, dims)
    log(name, 'FERTIG')


wanted = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else ['alle']
names = list(REGISTRY) if wanted in (['alle'], []) else wanted
for vehicle in names:
    if vehicle not in REGISTRY:
        raise SystemExit('Fahrzeug %s fehlt in Tools/verkehr_fahrzeuge.json' % vehicle)
    build(vehicle, REGISTRY[vehicle])
