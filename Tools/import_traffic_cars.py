"""Verkehrsfahrzeuge (Tools/Blender/build_traffic_cars.py) nach Unreal.

Je Fahrzeug aus Tools/verkehr_fahrzeuge.json (Umgebungsvariable WB_FAHRZEUG =
<Name>, <Name1>,<Name2> oder alle, Vorgabe alle) nach /Game/Vehicles/Traffic/<Name>:

  Meshes/     SM_<Name>_Body, SM_<Name>_Wheel_FL/FR/RL/RR  (Nanite)
  Materials/  M_<Name>_PartN  (Instanzen von /Game/Vehicles/Traffic/Mats/M_WbTrafficCar)
              M_<Name>_PartN_Lack  (Karosserie-Teile mit Lack: von M_WbTrafficCarLack)
  Textures/   T_<Name>_PartN, L_<Name>_PartN (Lackmaske)

LACK: Tools/traffic_paint_masks.py legt je Teil eine Lackmaske an (Lack = 1;
Scheiben, Reifen, Chrom, Leuchten = 0) und lack.json. Die Karosserie-Schlitze
dieser Teile bekommen M_WbTrafficCarLack: der Verkehr gibt je Instanz die
Lackfarbe als PerInstanceCustomData 0-2 und 3 = umfaerben (0 = Werkslack) -
gerechnet wie umfaerben() im Maskenskript, nur linear statt sRGB. Die Raeder
behalten immer M_WbTrafficCar. WB_NUR_LACK=1 wendet nur den Lack auf schon
importierte Fahrzeuge an (Meshes und Texturen bleiben). WB_NUR_RAEDER=1 liest
nur die vier Rad-Meshes neu ein (nach Tools/Blender/straighten_traffic_wheels.py).

Der Rad-Ursprung ist der Fahrzeugursprung (Radstandmitte am Boden): die
Radmitte ist die Mitte der Rad-Bounds - daraus rechnet der Verkehr Drehung und
Einschlag (UTrafficVehicleSpawnerComponent).

M_WbTrafficCar traegt used_with_instanced_static_meshes + used_with_nanite -
der Verkehr zeichnet als InstancedStaticMesh; ohne das Flag rendert Unreal im
-game kommentarlos das Standardmaterial.

Wie die anderen Importe: der Fahrzeugordner wird GANZ geloescht und ueber einen
Zwischenordner neu aufgebaut. Bericht: Saved/Diagnose/traffic_cars_import.txt.

  set WB_FAHRZEUG=alle   (optional WB_NUR_LACK=1)
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
LACK_MASTER = MATS + '/M_WbTrafficCarLack'
LACK_LEER = MATS + '/L_WbLackLeer'
# Wie Tools/traffic_paint_masks.py (LUM) - Helligkeit fuer das Verhaeltnis zum Grundlack.
LUM = (0.2126, 0.7152, 0.0722)
# umfaerben() begrenzt das Verhaeltnis in sRGB auf 2,5 -> linear 2,5^2,2.
LACK_VERHAELTNIS_MAX = 7.5
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


def srgb_linear(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def import_mask(png, dest_path, dest_name):
    """Lackmaske: linear (kein sRGB), Masken-Kompression."""
    t = unreal.AssetImportTask()
    t.filename = png
    t.destination_path = dest_path
    t.destination_name = dest_name
    t.automated = True
    t.replace_existing = True
    t.save = False
    tools.import_asset_tasks([t])
    tex = eal.load_asset('%s/%s' % (dest_path, dest_name))
    if tex is None:
        raise RuntimeError('Maske %s nicht importiert' % png)
    tex.set_editor_property('srgb', False)
    tex.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_MASKS)
    eal.save_loaded_asset(tex)
    return tex


def build_lack_master():
    """Wie M_WbTrafficCar, dazu je Instanz umlackiert:
    Farbe = lerp(Textur, saturate(Lack * clamp(Hell(Textur) / LackRefHell, 0, 7,5)), Maske * CD3)."""
    if eal.does_asset_exist(LACK_MASTER):
        if os.environ.get('WB_MASTER_NEU') != '1':
            return eal.load_asset(LACK_MASTER)
        eal.delete_asset(LACK_MASTER)
    eal.make_directory(MATS)
    leer = eal.load_asset(LACK_LEER) if eal.does_asset_exist(LACK_LEER) else import_mask(
        os.path.join(PROJECT, 'Data', 'Raw', 'Verkehr', 'L_WbLackLeer.png'), MATS, 'L_WbLackLeer')
    mat = tools.create_asset('M_WbTrafficCarLack', MATS, unreal.Material, unreal.MaterialFactoryNew())

    def ausdruck(klasse, x, y, **werte):
        e = mel.create_material_expression(mat, klasse, x, y)
        for k, v in werte.items():
            e.set_editor_property(k, v)
        return e

    def verbinde(von, nach, eingang, ausgang=''):
        if not mel.connect_material_expressions(von, ausgang, nach, eingang):
            raise RuntimeError('Verbindung %s -> %s.%s' % (von.get_name(), nach.get_name(), eingang))

    color = ausdruck(unreal.MaterialExpressionTextureSampleParameter2D, -1500, -300,
                     parameter_name='BaseColor', texture=eal.load_asset('/Engine/EngineResources/WhiteSquareTexture'))
    maske = ausdruck(unreal.MaterialExpressionTextureSampleParameter2D, -1500, 100,
                     parameter_name='LackMaske', texture=leer,
                     sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
    lum = ausdruck(unreal.MaterialExpressionConstant3Vector, -1500, -500,
                   constant=unreal.LinearColor(LUM[0], LUM[1], LUM[2], 1.0))
    hell = ausdruck(unreal.MaterialExpressionDotProduct, -1200, -450)
    verbinde(color, hell, 'A', 'RGB')
    verbinde(lum, hell, 'B')
    ref = ausdruck(unreal.MaterialExpressionScalarParameter, -1200, -300,
                   parameter_name='LackRefHell', default_value=0.2)
    teil = ausdruck(unreal.MaterialExpressionDivide, -1000, -400)
    verbinde(hell, teil, 'A')
    verbinde(ref, teil, 'B')
    begrenzt = ausdruck(unreal.MaterialExpressionClamp, -850, -400, min_default=0.0, max_default=LACK_VERHAELTNIS_MAX)
    verbinde(teil, begrenzt, '')
    cd = [ausdruck(unreal.MaterialExpressionPerInstanceCustomData, -1200, -150 + 90 * i, data_index=i)
          for i in range(4)]
    rg = ausdruck(unreal.MaterialExpressionAppendVector, -1000, -150)
    verbinde(cd[0], rg, 'A')
    verbinde(cd[1], rg, 'B')
    rgb = ausdruck(unreal.MaterialExpressionAppendVector, -850, -150)
    verbinde(rg, rgb, 'A')
    verbinde(cd[2], rgb, 'B')
    mal = ausdruck(unreal.MaterialExpressionMultiply, -650, -300)
    verbinde(rgb, mal, 'A')
    verbinde(begrenzt, mal, 'B')
    lackiert = ausdruck(unreal.MaterialExpressionSaturate, -500, -300)
    verbinde(mal, lackiert, '')
    anteil = ausdruck(unreal.MaterialExpressionMultiply, -650, 50)
    verbinde(maske, anteil, 'A', 'R')
    verbinde(cd[3], anteil, 'B')
    mix = ausdruck(unreal.MaterialExpressionLinearInterpolate, -300, -200)
    verbinde(color, mix, 'A', 'RGB')
    verbinde(lackiert, mix, 'B')
    verbinde(anteil, mix, 'Alpha')
    mel.connect_material_property(mix, '', MP.MP_BASE_COLOR)
    rough = ausdruck(unreal.MaterialExpressionScalarParameter, -700, 250,
                     parameter_name='Rauheit', default_value=BODY_ROUGHNESS)
    mel.connect_material_property(rough, '', MP.MP_ROUGHNESS)
    spec = ausdruck(unreal.MaterialExpressionScalarParameter, -700, 400, parameter_name='Glanz', default_value=0.5)
    mel.connect_material_property(spec, '', MP.MP_SPECULAR)
    mat.set_editor_property('used_with_instanced_static_meshes', True)
    mat.set_editor_property('used_with_nanite', True)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    return mat


def apply_paint(name, lack_master):
    """Karosserie-Schlitze der Teile aus lack.json auf M_<Name>_PartN_Lack umstellen."""
    src = os.path.join(PROJECT, 'Data', 'Raw', 'Verkehr', name)
    root = '/Game/Vehicles/Traffic/%s' % name
    bericht = os.path.join(src, 'lack.json')
    if not os.path.isfile(bericht):
        raise FileNotFoundError('%s - erst python Tools/traffic_paint_masks.py %s' % (bericht, name))
    lack = json.load(open(bericht, encoding='utf-8'))
    ref_hell = sum(w * srgb_linear(c) for w, c in zip(LUM, lack['rgb']))
    teile = set(lack['teile'])
    body_path = '%s/Meshes/SM_%s_Body' % (root, name)
    body = eal.load_asset(body_path)
    if body is None:
        raise RuntimeError('%s fehlt - erst ohne WB_NUR_LACK importieren' % body_path)
    # Erst die Schlitze auf die Werks-Instanzen zurueck, dann alte Lack-Assets weg.
    slots = list(body.get_editor_property('static_materials'))
    for slot in slots:
        part = str(slot.get_editor_property('material_slot_name')).split('_Part')[-1]
        slot.set_editor_property('material_interface', eal.load_asset('%s/Materials/M_%s_Part%s' % (root, name, part)))
    body.set_editor_property('static_materials', slots)
    for alt in assets_in(root):
        leaf = alt.rsplit('/', 1)[1]
        if leaf.startswith('L_%s_Part' % name) or (leaf.startswith('M_%s_Part' % name) and leaf.endswith('_Lack')):
            eal.delete_asset(alt)
    instances = {}
    for slot in slots:
        part = str(slot.get_editor_property('material_slot_name')).split('_Part')[-1]
        if part not in teile:
            continue
        if part not in instances:
            maske = import_mask(os.path.join(src, 'tex', 'L_%s_Part%s.png' % (name, part)),
                                root + '/Textures', 'L_%s_Part%s' % (name, part))
            inst = tools.create_asset('M_%s_Part%s_Lack' % (name, part), root + '/Materials',
                                      unreal.MaterialInstanceConstant,
                                      unreal.MaterialInstanceConstantFactoryNew())
            mel.set_material_instance_parent(inst, lack_master)
            mel.set_material_instance_texture_parameter_value(
                inst, 'BaseColor', eal.load_asset('%s/Textures/T_%s_Part%s' % (root, name, part)))
            mel.set_material_instance_texture_parameter_value(inst, 'LackMaske', maske)
            mel.set_material_instance_scalar_parameter_value(inst, 'LackRefHell', ref_hell)
            mel.set_material_instance_scalar_parameter_value(inst, 'Rauheit', BODY_ROUGHNESS)
            eal.save_loaded_asset(inst)
            instances[part] = inst
        slot.set_editor_property('material_interface', instances[part])
    body.set_editor_property('static_materials', slots)
    eal.save_loaded_asset(body)
    lackiert = sum(1 for s in slots if str(s.get_editor_property('material_slot_name')).split('_Part')[-1] in teile)
    zeile = '%s: Lack auf %d von %d Karosserie-Schlitzen (%d Teile), Grundlack-Helligkeit linear %.4f' % (
        name, lackiert, len(slots), len(instances), ref_hell)
    unreal.log('###VERKEHR_LACK### ' + zeile)
    return [zeile]


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


def reimport_wheels(name):
    """Nur die vier Rad-Meshes neu einlesen (nach Tools/Blender/straighten_traffic_wheels.py);
    Karosserie, Texturen, Lack bleiben. Materialien je Schlitz wie beim Import."""
    src = os.path.join(PROJECT, 'Data', 'Raw', 'Verkehr', name)
    root = '/Game/Vehicles/Traffic/%s' % name
    lines = []
    for w in WHEELS:
        leaf = 'SM_%s_Wheel_%s' % (name, w)
        t = unreal.AssetImportTask()
        t.filename = os.path.join(src, leaf + '.fbx')
        t.destination_path = root + '/Meshes'
        t.destination_name = leaf
        t.automated = True
        t.replace_existing = True
        t.save = False
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
        t.options = ui
        tools.import_asset_tasks([t])
        mesh = eal.load_asset('%s/Meshes/%s' % (root, leaf))
        if mesh is None:
            raise RuntimeError('%s nicht eingelesen' % leaf)
        slots = list(mesh.get_editor_property('static_materials'))
        for slot in slots:
            part = str(slot.get_editor_property('material_slot_name')).split('_Part')[-1]
            inst = eal.load_asset('%s/Materials/M_%s_Part%s' % (root, name, part))
            if inst is None:
                raise RuntimeError('%s: Material M_%s_Part%s fehlt' % (leaf, name, part))
            slot.set_editor_property('material_interface', inst)
        mesh.set_editor_property('static_materials', slots)
        nanite = mesh.get_editor_property('nanite_settings')
        nanite.set_editor_property('enabled', True)
        mesh.set_editor_property('nanite_settings', nanite)
        eal.save_loaded_asset(mesh)
        b = mesh.get_bounds()
        lines.append('%s: Radmitte (%.1f, %.1f, %.1f), halbe Breite %.1f cm' % (
            leaf, b.origin.x, b.origin.y, b.origin.z, b.box_extent.y))
    unreal.log('###VERKEHR_RAEDER### ' + ' | '.join(lines))
    return lines


master = build_master()
lack_master = build_lack_master()
nur_lack = os.environ.get('WB_NUR_LACK') == '1'
nur_raeder = os.environ.get('WB_NUR_RAEDER') == '1'
choice = os.environ.get('WB_FAHRZEUG', 'alle')
names = list(REGISTRY) if choice == 'alle' else [n.strip() for n in choice.split(',') if n.strip()]
report = []
for vehicle in names:
    if vehicle not in REGISTRY:
        raise RuntimeError('WB_FAHRZEUG=%s unbekannt (%s)' % (vehicle, ', '.join(REGISTRY)))
    if nur_raeder:
        report += reimport_wheels(vehicle)
        continue
    if not nur_lack:
        report += import_vehicle(vehicle, master)
    report += apply_paint(vehicle, lack_master)
out = os.path.join(unreal.Paths.project_saved_dir(), 'Diagnose', 'traffic_cars_import.txt')
os.makedirs(os.path.dirname(out), exist_ok=True)
with open(out, 'w', encoding='utf-8') as stream:
    stream.write('\n'.join(report) + '\n')
