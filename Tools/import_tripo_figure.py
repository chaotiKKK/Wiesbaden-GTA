"""Geriggte Tripo-Figuren nach Unreal importieren.

Welche: Umgebungsvariable WB_FIGUR -
  <Name>   eine Kundenfigur aus Tools/kunden_figuren.json
  kunden   alle Kundenfiguren aus Tools/kunden_figuren.json
  Denno    Denno (die Ladeninhaberin, Tools/Blender/rig_denno.py)

Kunden (Lieferkunden und Ladengaeste) kommen aus
Tools/Blender/build_customer_figure.py (Data/Raw/Kunden/<Name>/SK_<Name>.fbx +
tex/) nach /Game/Assets/People/Kunden/<Name> - dort findet das Spiel jede
vollstaendige Figur selbst (WiesbadenCustomerFigures); eine neue Figur braucht
keine C++-Aenderung. Jede Kundenfigur muss die vier Bewegungen Idle, Walk,
Wave und Sit tragen. Denno kommt aus Data/Raw/Denno nach /Game/Assets/People/Denno.

Ergebnis je Figur, jedes Asset genau einmal:

  Meshes/      SK_<Figur>, SK_<Figur>_Skeleton
  Animations/  A_<Figur>_<Bewegung>   (jede Aktion der FBX)
  Materials/   M_<Figur>_Part*  (Instanzen von /Game/Materials/People/M_WbFigur)
  Textures/    T_<Figur>_Part*

Materialien NICHT aus dem FBX: M_WbFigur traegt das Flag "Used with Skeletal
Mesh" (Tools/import_sebbo.py) - ohne es zeichnet Unreal eine animierte Figur
kommentarlos im grauen Standardmaterial. Die glTF-Mastermaterialien der
Engine lassen sich dafuer nicht speichern.

Wie Tools/import_denno_shop.py wird der Figurenordner bei jedem Lauf GANZ
geloescht und in einen Zwischenordner importiert.

  set WB_FIGUR=kunden
  UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=Tools/import_tripo_figure.py
"""
import glob
import json
import os
import unreal

eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
MASTER = '/Game/Materials/People/M_WbFigur'
# Tripo liefert keine Rauheitskarte; das glTF-Material hatte Rauheit 0,9.
ROUGHNESS = 0.9


def assets_in(path):
    return [p.split('.')[0] for p in eal.list_assets(path, recursive=True, include_folder=False)]


def class_of(path):
    return str(eal.find_asset_data(path).asset_class_path.asset_name)


def import_figure(figure, src_dir, root, required):
    staging = root + '/_Import'
    fbx = os.path.join(src_dir, 'SK_%s.fbx' % figure)
    report = os.path.join(unreal.Paths.project_saved_dir(), 'Diagnose', '%s_import.txt' % figure.lower())

    def move(path, folder, name):
        dst = '%s/%s/%s' % (root, folder, name)
        if not eal.rename_asset(path, dst):
            raise RuntimeError('Verschieben fehlgeschlagen: %s -> %s' % (path, dst))
        return dst

    if not os.path.isfile(fbx):
        raise FileNotFoundError(fbx)
    master = eal.load_asset(MASTER)
    if master is None:
        raise RuntimeError('Grundmaterial fehlt: %s (Tools/import_sebbo.py)' % MASTER)
    if eal.does_directory_exist(root):
        eal.delete_directory(root)
    eal.make_directory(root)

    # --- Texturen ---------------------------------------------------------------
    tex_tasks = []
    for png in sorted(glob.glob(os.path.join(src_dir, 'tex', 'T_%s_Part*.png' % figure))):
        t = unreal.AssetImportTask()
        t.filename = png
        t.destination_path = root + '/Textures'
        t.destination_name = os.path.splitext(os.path.basename(png))[0]
        t.automated = True
        t.replace_existing = True
        t.save = True
        tex_tasks.append(t)
    if not tex_tasks:
        raise RuntimeError('Keine Texturen unter %s/tex' % src_dir)
    tools.import_asset_tasks(tex_tasks)

    # --- Materialien: Instanzen von M_WbFigur -----------------------------------
    materials = {}
    for t in tex_tasks:
        part = t.destination_name.replace('T_%s_' % figure, '')          # Part0 ...
        texture = eal.load_asset('%s/Textures/%s' % (root, t.destination_name))
        name = 'M_%s_%s' % (figure, part)
        inst = tools.create_asset(name, root + '/Materials', unreal.MaterialInstanceConstant,
                                  unreal.MaterialInstanceConstantFactoryNew())
        mel.set_material_instance_parent(inst, master)
        mel.set_material_instance_texture_parameter_value(inst, 'BaseColor', texture)
        mel.set_material_instance_scalar_parameter_value(inst, 'RauheitFaktor', ROUGHNESS)
        eal.save_loaded_asset(inst)
        materials[name] = inst

    # --- Skelett-Mesh + Bewegungen ----------------------------------------------
    task = unreal.AssetImportTask()
    task.filename = fbx
    task.destination_path = staging
    task.destination_name = 'SK_%s' % figure
    task.automated = True
    task.replace_existing = True
    task.save = True
    options = unreal.FbxImportUI()
    options.set_editor_property('import_mesh', True)
    options.set_editor_property('import_textures', False)
    options.set_editor_property('import_materials', False)
    options.set_editor_property('import_as_skeletal', True)
    options.set_editor_property('import_animations', True)
    options.set_editor_property('create_physics_asset', False)
    options.set_editor_property('mesh_type_to_import', unreal.FBXImportType.FBXIT_SKELETAL_MESH)
    smd = options.skeletal_mesh_import_data
    # Der Export liefert bereits Zentimeter (rig_sebbo.py: ein zweiter Faktor gab
    # einen 175 m langen Kaefer).
    smd.set_editor_property('import_uniform_scale', 1.0)
    smd.set_editor_property('import_morph_targets', False)
    smd.set_editor_property('update_skeleton_reference_pose', False)
    anim = options.anim_sequence_import_data
    anim.set_editor_property('import_uniform_scale', 1.0)
    anim.set_editor_property('animation_length', unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
    anim.set_editor_property('remove_redundant_keys', True)
    task.options = options
    tools.import_asset_tasks([task])

    found = {class_of(p): p for p in assets_in(staging) if class_of(p) in ('SkeletalMesh', 'Skeleton')}
    if 'SkeletalMesh' not in found or 'Skeleton' not in found:
        raise RuntimeError('Import unvollstaendig: %s' % sorted((class_of(p), p) for p in assets_in(staging)))
    move(found['Skeleton'], 'Meshes', 'SK_%s_Skeleton' % figure)
    move(found['SkeletalMesh'], 'Meshes', 'SK_%s' % figure)
    anim_paths = {}
    marker = '%s_' % figure
    for path in assets_in(staging):
        if class_of(path) != 'AnimSequence':
            continue
        # Der FBX-Importer benennt Bewegungen <Ziel><Armatur>_<Aktion>, z. B.
        # SK_DennoDennoRig_Denno_Sweep - die Aktion <Figur>_<Bewegung> steht am Ende.
        leaf = path.rsplit('/', 1)[1]
        at = leaf.rfind(marker)
        if at < 0:
            raise RuntimeError('Unerwartete Bewegung: %s' % path)
        kind = leaf[at + len(marker):]
        anim_paths[kind] = move(path, 'Animations', 'A_%s_%s' % (figure, kind))
    missing = [k for k in required if k not in anim_paths]
    if not anim_paths or missing:
        raise RuntimeError('%s: Bewegungen fehlen %s (vorhanden %s)' % (figure, missing, sorted(anim_paths)))

    mesh = eal.load_asset('%s/Meshes/SK_%s' % (root, figure))
    slots = list(mesh.get_editor_property('materials'))
    for slot in slots:
        name = str(slot.get_editor_property('material_slot_name'))
        chosen = next((m for n, m in materials.items() if n.lower() in name.lower()), None)
        if chosen is None:
            raise RuntimeError('Materialschlitz %s ohne Material' % name)
        slot.set_editor_property('material_interface', chosen)
    mesh.set_editor_property('materials', slots)
    eal.save_loaded_asset(mesh)

    left = assets_in(staging)
    redirectors = [eal.load_asset(p) for p in left if class_of(p) == 'ObjectRedirector']
    if redirectors:
        tools.fixup_referencers(redirectors)
    left = assets_in(staging)
    if left:
        raise RuntimeError('Reste im Zwischenordner: %s' % left)
    eal.delete_directory(staging)
    eal.save_directory(root, only_if_is_dirty=False, recursive=True)

    bounds = mesh.get_bounds()
    lines = ['%s: %d Assets' % (figure, len(assets_in(root))),
             'SK_%s: origin (%.0f, %.0f, %.0f) size %.0f x %.0f x %.0f cm, %d Schlitze' % (
                 figure, bounds.origin.x, bounds.origin.y, bounds.origin.z,
                 bounds.box_extent.x * 2, bounds.box_extent.y * 2, bounds.box_extent.z * 2, len(slots))]
    for kind, path in sorted(anim_paths.items()):
        seq = eal.load_asset(path)
        lines.append('%s: %.2f s' % (path, seq.get_play_length()))
    lines += ['%s (%s)' % (p, class_of(p)) for p in sorted(assets_in(root))]
    os.makedirs(os.path.dirname(report), exist_ok=True)
    with open(report, 'w', encoding='utf-8') as stream:
        stream.write('\n'.join(lines) + '\n')
    unreal.log('###FIGUR_IMPORT### ' + ' | '.join(lines[:3 + len(anim_paths)]))
    return anim_paths


PROJECT = unreal.Paths.project_dir()
REGISTRY = json.load(open(os.path.join(PROJECT, 'Tools', 'kunden_figuren.json'), encoding='utf-8'))['figuren']
CUSTOMER_ANIMS = ['Idle', 'Walk', 'Wave', 'Sit']
choice = os.environ.get('WB_FIGUR', 'kunden')
if choice == 'Denno':
    import_figure('Denno', os.path.join(PROJECT, 'Data', 'Raw', 'Denno'), '/Game/Assets/People/Denno', [])
else:
    names = list(REGISTRY) if choice == 'kunden' else [choice]
    for name in names:
        if name not in REGISTRY:
            raise RuntimeError('WB_FIGUR=%s unbekannt (Tools/kunden_figuren.json: %s)' % (name, ', '.join(REGISTRY)))
        # Iris lag bis 25.09.2026 unter /Game/Assets/People/Iris - der alte Ordner geht mit.
        legacy = '/Game/Assets/People/%s' % name
        if eal.does_directory_exist(legacy):
            eal.delete_directory(legacy)
        import_figure(name, os.path.join(PROJECT, 'Data', 'Raw', 'Kunden', name),
                      '/Game/Assets/People/Kunden/%s' % name, CUSTOMER_ANIMS)
