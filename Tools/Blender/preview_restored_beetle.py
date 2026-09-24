"""Render three local review images of the generated Beetle shell."""
import bpy
from pathlib import Path
from mathutils import Vector

root = Path(__file__).resolve().parents[2]
source = root / 'Data/Raw/Beetle/Herbie/restored_beetle_body.glb'
dest = root / 'Saved/Diagnose'
dest.mkdir(parents=True, exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=str(source))
scene = bpy.context.scene
scene.render.engine = 'BLENDER_WORKBENCH'
scene.display.shading.color_type = 'MATERIAL'
scene.display.shading.light = 'STUDIO'
scene.display.shading.show_shadows = True
scene.display.shading.show_cavity = True
scene.render.resolution_x = 1600
scene.render.resolution_y = 900
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = 'PNG'
scene.render.film_transparent = False
scene.camera = bpy.data.objects.new('Review camera', bpy.data.cameras.new('Review camera'))
scene.collection.objects.link(scene.camera)
scene.camera.data.type = 'ORTHO'
scene.camera.data.ortho_scale = 5.5

for name, location in [('vorn', (4.2,-6.0,3.1)),
                       ('seite', (0,-6.5,2.2)),
                       ('hinten', (-4.3,5.5,2.8))]:
    scene.camera.location = location
    direction = Vector((0,0,.78)) - scene.camera.location
    scene.camera.rotation_euler = direction.to_track_quat('-Z','Y').to_euler()
    scene.render.filepath = str(dest / ('beetle_restored_' + name + '.png'))
    bpy.ops.render.render(write_still=True)
    print('###BEETLE_PREVIEW###', scene.render.filepath, flush=True)
