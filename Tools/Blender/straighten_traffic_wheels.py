"""Vorhandene Rad-FBX der Verkehrsautos gerade stellen - ohne Neubau des ganzen Autos.

Ein Neubau (build_traffic_cars.py) schriebe Texturen, Lackmasken und alle
Assets neu. Die Raeder allein lassen sich nachbessern: FBX laden, echte Achse
auf Y drehen (wheel_align.straighten), mit denselben Export-Einstellungen
zurueckschreiben. Danach in Unreal nur die Raeder neu einlesen:
  set WB_FAHRZEUG=alle & set WB_NUR_RAEDER=1 & UnrealEditor-Cmd ... -script=Tools/import_traffic_cars.py

  blender -b -P Tools/Blender/straighten_traffic_wheels.py -- Transporter Kaefer
  blender -b -P Tools/Blender/straighten_traffic_wheels.py            (alle)
"""
import json
import sys
from pathlib import Path

import bpy

sys.path.insert(0, str(Path(__file__).resolve().parent))
import wheel_align  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
REGISTRY = json.loads((ROOT / 'Tools/verkehr_fahrzeuge.json').read_text(encoding='utf-8'))['fahrzeuge']
WHEELS = ('FL', 'FR', 'RL', 'RR')


def export_fbx(obj, path):
    # Dieselben Einstellungen wie build_traffic_cars.export_fbx.
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.export_scene.fbx(
        filepath=str(path), use_selection=True, apply_unit_scale=True, global_scale=1.0,
        apply_scale_options='FBX_SCALE_NONE', object_types={'MESH'}, mesh_smooth_type='FACE',
        use_mesh_modifiers=False, add_leaf_bones=False, bake_anim=False,
        axis_forward='-Z', axis_up='Y', path_mode='STRIP', embed_textures=False)


def straighten_file(path):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=str(path))
    meshes = [o for o in bpy.context.scene.objects if o.type == 'MESH']
    if len(meshes) != 1:
        raise RuntimeError('%s: %d Meshes statt 1' % (path, len(meshes)))
    obj = meshes[0]
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    result = wheel_align.straighten(obj)
    if result[2] > result[3]:
        bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
        export_fbx(obj, path)
    return result


names = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
for name in (names or list(REGISTRY)):
    for key in WHEELS:
        fbx = ROOT / 'Data/Raw/Verkehr' / name / ('SM_%s_Wheel_%s.fbx' % (name, key))
        yaw, camber, before, after = straighten_file(fbx)
        print('###RAD %s %s Einschlag %.2f Sturz %.2f Grad, Breite %.1f -> %.1f cm%s' % (
            name, key, yaw, camber, before * 100, after * 100, '' if before > after else ' (unveraendert)'), flush=True)
