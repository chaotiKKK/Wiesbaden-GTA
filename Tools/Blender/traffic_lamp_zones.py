"""Lampenzonen der Verkehrsautos: UV-Dreiecke der Leuchten, die NIE umlackiert werden.

Beim roten Golf haben Rueckleuchten und Lack denselben Farbwinkel (gemessen:
beide -15..-5 Grad) - die Farbe allein trennt sie nicht. Die Geometrie tut es:
Leuchten sitzen an Heck und Front in Lampenhoehe, aussen, und schauen nach
hinten bzw. vorn. Diese Flaechen werden hier am fertigen Karosserie-Mesh
(Data/Raw/Verkehr/<Name>/SM_<Name>_Body.fbx) bestimmt und als UV-Dreiecke je
Tripo-Teil nach lampenzonen.json geschrieben; Tools/traffic_paint_masks.py
stanzt sie aus der Lackmaske.

Zonen (Meter, Fahrzeugrahmen: +X vorn, Boden 0) aus Tools/verkehr_fahrzeuge.json
"lampen": {"heck_z": [z0, z1], "front_z": [z0, z1], "tiefe_m": 0.2, "innen_m": 0.22}.

    blender -b -P Tools/Blender/traffic_lamp_zones.py -- Golf Peugeot
"""
import json
import sys
from pathlib import Path

import bpy

ROOT = Path(__file__).resolve().parents[2]
REGISTRY = json.loads((ROOT / 'Tools/verkehr_fahrzeuge.json').read_text(encoding='utf-8'))['fahrzeuge']
VORGABE = {'heck_z': [0.6, 1.1], 'front_z': [0.5, 0.9], 'tiefe_m': 0.2, 'innen_m': 0.22}


def zonen(name, cfg):
    lampen = dict(VORGABE, **cfg.get('lampen', {}))
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=str(ROOT / 'Data/Raw/Verkehr' / name / ('SM_%s_Body.fbx' % name)))
    obj = next(o for o in bpy.context.scene.objects if o.type == 'MESH')
    me = obj.data
    welt = obj.matrix_world
    punkte = [welt @ v.co for v in me.vertices]
    xmin, xmax = min(p.x for p in punkte), max(p.x for p in punkte)
    rot = welt.to_3x3()
    uv = me.uv_layers.active.data
    me.calc_loop_triangles()
    ergebnis = {}
    zahl = {'heck': 0, 'front': 0}
    for tri in me.loop_triangles:
        c = sum((punkte[i] for i in tri.vertices), punkte[tri.vertices[0]] * 0) / 3.0
        n = (rot @ tri.normal).normalized()
        if abs(c.y) < lampen['innen_m']:
            continue
        heck = c.x < xmin + lampen['tiefe_m'] and n.x < -0.35 and lampen['heck_z'][0] < c.z < lampen['heck_z'][1]
        front = c.x > xmax - lampen['tiefe_m'] and n.x > 0.35 and lampen['front_z'][0] < c.z < lampen['front_z'][1]
        if not (heck or front):
            continue
        zahl['heck' if heck else 'front'] += 1
        mat = me.materials[tri.material_index].name
        teil = mat.split('_Part')[-1].split('.')[0]
        ergebnis.setdefault(teil, []).append([round(c, 5) for l in tri.loops for c in uv[l].uv])
    ziel = ROOT / 'Data/Raw/Verkehr' / name / 'lampenzonen.json'
    ziel.write_text(json.dumps(ergebnis), encoding='utf-8')
    print('###LAMPEN %s: %d Heck-, %d Front-Dreiecke in %d Teilen (Laenge %.2f m)' % (
        name, zahl['heck'], zahl['front'], len(ergebnis), (xmax - xmin) / (1.0 if xmax - xmin < 20 else 100.0)))


namen = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
for fahrzeug in (namen or list(REGISTRY)):
    zonen(fahrzeug, REGISTRY[fahrzeug])
