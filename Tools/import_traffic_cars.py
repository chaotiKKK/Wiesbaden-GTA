"""Verkehrsfahrzeuge (Tools/Blender/build_traffic_cars.py) nach Unreal.

Je Fahrzeug aus Tools/verkehr_fahrzeuge.json (Umgebungsvariable WB_FAHRZEUG =
<Name> oder alle, Vorgabe alle) nach /Game/Vehicles/Traffic/<Name>:

  Meshes/     SM_<Name>_Body, SM_<Name>_Wheel_FL/FR/RL/RR  (Nanite)
  Materials/  M_<Name>_PartN  (Instanzen von /Game/Vehicles/Traffic/Mats/M_WbTrafficCar)
  Textures/   T_<Name>_PartN

Der Rad-Ursprung ist der Fahrzeugursprung (Radstandmitte am Boden): die
Radmitte ist die Mitte der Rad-Bounds - daraus rechnet der Verkehr Drehung und
Einschlag (UTrafficVehicleSpawnerComponent).

M_WbTrafficCar traegt used_with_instanced_static_meshes + used_with_nanite -
der Verkehr zeichnet als InstancedStaticMesh; ohne das Flag rendert Unreal im
-game kommentarlos das Standardmaterial.

Wie die anderen Importe: der Fahrzeugordner wird GANZ geloescht und ueber einen
Zwischenordner neu aufgebaut. Bericht: Saved/Diagnose/traffic_cars_import.txt.

  set WB_FAHRZEUG=alle
  UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=Tools/import_traffic_cars.py
"""
import glob
import json
import os
import unreal

eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
MP = unreal.MaterialProperty
PROJECT = unreal.Paths.project_dir()
REGISTRY = json.load(open(os.path.join(PROJECT, 'Tools', 'verkehr_fahrzeuge.json'), encoding='utf-8'))['fahrzeuge']
MATS = '/Game/Vehicles/Traffic/Mats'
MASTER = MATS + '/M_WbTrafficCar'
WHEELS = ('FL', 'FR', 'RL', 'RR')
# Tripo liefert nur Farbe; Lack glaenzt, Reifen nicht.
BODY_ROUGHNESS = 0.38
WHEEL_ROUGHNESS = 0.75


def assets_in(path):
    return [p.split('.')[0] for p in eal.list_assets(path, recursive=True, include_folder=False)]


def class_of(path):
    return str(eal.find_asset_data(path).asset_class_path.asset_name)


def build_master():
    # Nur neu bauen, wenn es fehlt (oder WB_MASTER_NEU=1): Loeschen wuerde die
    # Materialinstanzen der NICHT neu importierten Fahrzeuge verwaisen lassen.
    if eal.does_asset_exist(MASTER):
        if os.environ.get('WB_MASTER_NEU') != '1':
            return eal.load_asset(MASTER)
        eal.delete_asset(MASTER)
    eal.make_directory(MATS)
    mat = tools.create_asset('M_WbTrafficCar', MATS, unreal.Material, unreal.MaterialFactoryNew())
    color = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, -700, -200)
    color.set_editor_property('parameter_name', 'BaseColor')
    color.set_editor_property('texture', eal.load_asset('/Engine/EngineResources/WhiteSquareTexture'))
    mel.connect_material_property(color, '', MP.MP_BASE_COLOR)
    rough = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -700, 150)
    rough.set_editor_property('parameter_name', 'Rauheit')
    rough.set_editor_property('default_value', BODY_ROUGHNESS)
    mel.connect_material_property(rough, '', MP.MP_ROUGHNESS)
    spec = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -700, 300)
    spec.set_editor_property('parameter_name', 'Glanz')
    spec.set_editor_property('default_value', 0.5)
    mel.connect_material_property(spec, '', MP.MP_SPECULAR)
    mat.set_editor_property('used_with_instanced_static_meshes', True)
    mat.set_editor_property('used_with_nanite', True)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    return mat


def import_vehicle(name, master):
    src = os.path.join(PROJECT, 'Data', 'Raw', 'Verkehr', name)
    root = '/Game/Vehicles/Traffic/%s' % name
    staging = root + '/_Import'
    if eal.does_directory_exist(root):
        eal.delete_directory(root)
    eal.make_directory(root)

    # Texturen
    tex_tasks = []
    for png in sorted(glob.glob(os.path.join(src, 'tex', 'T_%s_Part*.png' % name))):
        t = unreal.AssetImportTask()
        t.filename = png
        t.destination_path = root + '/Textures'
        t.destination_name = os.path.splitext(os.path.basename(png))[0]
        t.automated = True
        t.replace_existing = True
        t.save = True
        tex_tasks.append(t)
    if not tex_tasks:
        raise RuntimeError('Keine Texturen unter %s/tex' % src)
    tools.import_asset_tasks(tex_tasks)

    # Meshes (FBX, statisch, ohne Materialien)
    mesh_names = ['SM_%s_Body' % name] + ['SM_%s_Wheel_%s' % (name, w) for w in WHEELS]
    tasks = []
    for mesh in mesh_names:
        fbx = os.path.join(src, mesh + '.fbx')
        if not os.path.isfile(fbx):
            raise FileNotFoundError(fbx)
        t = unreal.AssetImportTask()
        t.filename = fbx
        t.destination_path = staging
        t.destination_name = mesh
        t.automated = True
        t.replace_existing = True
        t.save = True
        ui = unreal.FbxImportUI()
        ui.set_editor_property('import_mesh', True)
        ui.set_editor_property('import_as_skeletal', False)
        ui.set_editor_property('import_materials', False)
        ui.set_editor_property('import_textures', False)
        ui.set_editor_property('import_animations', False)
        ui.set_editor_property('mesh_type_to_import', unreal.FBXImportType.FBXIT_STATIC_MESH)
        smd = ui.static_mesh_import_data
        smd.set_editor_property('combine_meshes', True)
        smd.set_editor_property('auto_generate_collision', False)
        smd.set_editor_property('generate_lightmap_u_vs', False)
        smd.set_editor_property('import_uniform_scale', 1.0)
        try:
            smd.set_editor_property('build_nanite', True)
        except Exception:
            pass
        t.options = ui
        tasks.append(t)
    tools.import_asset_tasks(tasks)

    meshes = {}
    for path in assets_in(staging):
        if class_of(path) != 'StaticMesh':
            continue
        leaf = path.rsplit('/', 1)[1]
        if leaf not in mesh_names:
            raise RuntimeError('Unerwartetes Mesh %s' % path)
        dst = '%s/Meshes/%s' % (root, leaf)
        if not eal.rename_asset(path, dst):
            raise RuntimeError('Verschieben fehlgeschlagen: %s' % path)
        meshes[leaf] = dst
    missing = [m for m in mesh_names if m not in meshes]
    if missing:
        raise RuntimeError('%s: Meshes fehlen %s' % (name, missing))

    # Materialien je Schlitz (Schlitzname = Blender-Material M_<Name>_PartN).
    instances = {}
    lines = []
    for leaf in mesh_names:
        mesh = eal.load_asset(meshes[leaf])
        wheel = '_Wheel_' in leaf
        slots = list(mesh.get_editor_property('static_materials'))
        new_slots = []
        for slot in slots:
            slot_name = str(slot.get_editor_property('material_slot_name'))
            part = slot_name.split('_Part')[-1] if '_Part' in slot_name else None
            if part is None or not part.isdigit():
                raise RuntimeError('%s: Schlitz %s ohne Teil-Nummer' % (leaf, slot_name))
            inst_name = 'M_%s_Part%s' % (name, part)
            if inst_name not in instances:
                texture = eal.load_asset('%s/Textures/T_%s_Part%s' % (root, name, part))
                if texture is None:
                    raise RuntimeError('Textur fehlt fuer %s' % inst_name)
                inst = tools.create_asset(inst_name, root + '/Materials', unreal.MaterialInstanceConstant,
                                          unreal.MaterialInstanceConstantFactoryNew())
                mel.set_material_instance_parent(inst, master)
                mel.set_material_instance_texture_parameter_value(inst, 'BaseColor', texture)
                mel.set_material_instance_scalar_parameter_value(
                    inst, 'Rauheit', WHEEL_ROUGHNESS if wheel else BODY_ROUGHNESS)
                eal.save_loaded_asset(inst)
                instances[inst_name] = inst
            slot.set_editor_property('material_interface', instances[inst_name])
            new_slots.append(slot)
        mesh.set_editor_property('static_materials', new_slots)
        nanite = mesh.get_editor_property('nanite_settings')
        nanite.set_editor_property('enabled', True)
        mesh.set_editor_property('nanite_settings', nanite)
        eal.save_loaded_asset(mesh)
        b = mesh.get_bounds()
        lines.append('%s: origin (%.1f, %.1f, %.1f) extent (%.1f, %.1f, %.1f), %d Schlitze, %d Dreiecke' % (
            leaf, b.origin.x, b.origin.y, b.origin.z, b.box_extent.x, b.box_extent.y, b.box_extent.z,
            len(new_slots), mesh.get_num_triangles(0)))

    left = assets_in(staging)
    redirectors = [eal.load_asset(p) for p in left if class_of(p) == 'ObjectRedirector']
    if redirectors:
        tools.fixup_referencers(redirectors)
    if assets_in(staging):
        raise RuntimeError('Reste im Zwischenordner: %s' % assets_in(staging))
    eal.delete_directory(staging)
    eal.save_directory(root, only_if_is_dirty=False, recursive=True)
    lines.insert(0, '%s: %d Assets' % (name, len(assets_in(root))))
    unreal.log('###VERKEHR_IMPORT### ' + ' | '.join(lines))
    return lines


master = build_master()
choice = os.environ.get('WB_FAHRZEUG', 'alle')
names = list(REGISTRY) if choice == 'alle' else [choice]
report = []
for vehicle in names:
    if vehicle not in REGISTRY:
        raise RuntimeError('WB_FAHRZEUG=%s unbekannt (%s)' % (vehicle, ', '.join(REGISTRY)))
    report += import_vehicle(vehicle, master)
out = os.path.join(unreal.Paths.project_saved_dir(), 'Diagnose', 'traffic_cars_import.txt')
os.makedirs(os.path.dirname(out), exist_ok=True)
with open(out, 'w', encoding='utf-8') as stream:
    stream.write('\n'.join(report) + '\n')
