"""Dennos Laden (Sedanplatz 5) + Denno-Figur nach /Game/Buildings/DennoShop importieren.

Quellen aus Tools/Blender/build_denno_shop.py und build_denno_figure.py
(Data/Raw/Denno/*.glb). Ergebnis, jedes Asset genau einmal:

  /Game/Buildings/DennoShop/Meshes/SM_DennoShop_{Shell,Glass,Cafe,Salon}, SM_Denno
  /Game/Buildings/DennoShop/Materials/M_Denno_*
  /Game/Buildings/DennoShop/Textures/T_Denno_Part*

Der glTF-Import legt je GLB einen eigenen Ordner <glb>/StaticMeshes|Materials|
Textures an - gleichnamige Materialien (Messing, Anthrazit, Weiss ...) kaemen so
bis zu dreimal vor. Darum: der Ordner wird bei jedem Lauf GANZ geloescht (keine
verwaisten Reste alter Laeufe), in einen Zwischenordner importiert, jedes
Material je Name einmal behalten und alle Mesh-Slots darauf umgehaengt, dann
alles in die drei Zielordner verschoben und der Zwischenordner geloescht.

UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=Tools/import_denno_shop.py
"""
import os
import unreal

eal = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
root = '/Game/Buildings/DennoShop'
staging = root + '/_Import'
src = os.path.join(unreal.Paths.project_dir(), 'Data', 'Raw', 'Denno')
report = os.path.join(unreal.Paths.project_saved_dir(), 'Diagnose', 'denno_shop_import.txt')
ITEMS = [('denno_shop_shell', 'SM_DennoShop_Shell'), ('denno_shop_glass', 'SM_DennoShop_Glass'),
         ('denno_shop_cafe', 'SM_DennoShop_Cafe'), ('denno_shop_salon', 'SM_DennoShop_Salon'),
         ('denno_figure', 'SM_Denno')]


def assets_in(path):
    return [p.split('.')[0] for p in eal.list_assets(path, recursive=True, include_folder=False)]


# --- sauberer Anfang: alte Laeufe (auch andere Ordnerlayouts) restlos weg ---
if eal.does_directory_exist(root):
    eal.delete_directory(root)
eal.make_directory(root)

tasks = []
for glb, name in ITEMS:
    path = os.path.join(src, glb + '.glb')
    if not os.path.isfile(path):
        raise FileNotFoundError(path)
    t = unreal.AssetImportTask()
    t.filename = path
    t.destination_path = staging
    t.destination_name = name
    t.automated = True
    t.replace_existing = True
    t.save = True
    tasks.append(t)
tools.import_asset_tasks(tasks)

# --- Materialien entdoppeln: je Name das erste behalten, Slots umhaengen ---
keep = {}
meshes = []
for glb, name in ITEMS:
    mesh = eal.load_asset('%s/%s/StaticMeshes/%s' % (staging, glb, name))
    if not isinstance(mesh, unreal.StaticMesh):
        raise RuntimeError('Import fehlgeschlagen: %s' % name)
    meshes.append(mesh)
    for i, slot in enumerate(mesh.get_editor_property('static_materials')):
        mat = slot.get_editor_property('material_interface')
        if mat is None:
            continue
        kept = keep.setdefault(mat.get_name(), mat)
        if kept != mat:
            mesh.set_material(i, kept)
    settings = mesh.get_editor_property('nanite_settings')
    settings.set_editor_property('enabled', False)
    mesh.set_editor_property('nanite_settings', settings)
    eal.save_loaded_asset(mesh)
kept_paths = {m.get_path_name().split('.')[0] for m in keep.values()}
doubles = 0
for path in assets_in(staging):
    asset = eal.find_asset_data(path)
    if str(asset.asset_class_path.asset_name).startswith('Material') and path not in kept_paths:
        eal.delete_asset(path)
        doubles += 1

# --- in die Zielordner; Texturen zuerst, dann was sie benutzt ---
TARGET = {'Texture2D': 'Textures', 'StaticMesh': 'Meshes'}
moved = []
for kind in ['Texture2D', 'Material', 'StaticMesh']:
    for path in assets_in(staging):
        cls = str(eal.find_asset_data(path).asset_class_path.asset_name)
        if (cls == kind) or (kind == 'Material' and cls.startswith('Material')):
            dst = '%s/%s/%s' % (root, TARGET.get(kind, 'Materials'), path.rsplit('/', 1)[1])
            if not eal.rename_asset(path, dst):
                raise RuntimeError('Verschieben fehlgeschlagen: %s -> %s' % (path, dst))
            moved.append(dst)
left = assets_in(staging)
redirectors = [eal.load_asset(p) for p in left
               if str(eal.find_asset_data(p).asset_class_path.asset_name) == 'ObjectRedirector']
if redirectors:
    tools.fixup_referencers(redirectors)
left = assets_in(staging)
if left:
    raise RuntimeError('Reste im Zwischenordner: %s' % left)
eal.delete_directory(staging)
eal.save_directory(root, only_if_is_dirty=False, recursive=True)

lines = ['%d Assets, %d doppelte Materialien entfernt' % (len(moved), doubles)]
for glb, name in ITEMS:
    target = '%s/Meshes/%s' % (root, name)
    mesh = eal.load_asset(target)
    if not isinstance(mesh, unreal.StaticMesh):
        lines.append('FEHLT %s' % target)
        continue
    ext = mesh.get_bounds().box_extent
    org = mesh.get_bounds().origin
    slots = mesh.get_editor_property('static_materials')
    lines.append('%s: origin (%.0f, %.0f, %.0f) size %.0f x %.0f x %.0f cm, %d Dreiecke, %d Slots: %s' % (
        target, org.x, org.y, org.z, ext.x * 2, ext.y * 2, ext.z * 2,
        mesh.get_num_triangles(0), len(slots),
        ', '.join(s.get_editor_property('material_interface').get_path_name().split('.')[0]
                  for s in slots)))
lines += sorted(assets_in(root))
os.makedirs(os.path.dirname(report), exist_ok=True)
with open(report, 'w', encoding='utf-8') as stream:
    stream.write('\n'.join(lines) + '\n')
unreal.log('###DENNO_SHOP### ' + ' | '.join(lines[:1]))
