"""Iris - die Kundin der Denno-Lieferungen - nach /Game/Assets/People/Iris importieren.

Quelle aus Tools/Blender/build_customer_iris.py (Data/Raw/Kunde/iris_customer.glb,
drei Posen derselben Figur). Ergebnis, jedes Asset genau einmal:

  /Game/Assets/People/Iris/Meshes/SM_Iris_{Stand,StrideA,StrideB}
  /Game/Assets/People/Iris/Materials/M_Iris_Part*
  /Game/Assets/People/Iris/Textures/T_Iris_Part*

Wie Tools/import_denno_shop.py: der Ordner wird bei jedem Lauf GANZ geloescht,
in einen Zwischenordner importiert und nach Art in die drei Zielordner
verschoben. Die drei Posen teilen sich Materialien und Texturen (eine GLB).

UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=Tools/import_customer_iris.py
"""
import os
import unreal

eal = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
root = '/Game/Assets/People/Iris'
staging = root + '/_Import'
src = os.path.join(unreal.Paths.project_dir(), 'Data', 'Raw', 'Kunde', 'iris_customer.glb')
report = os.path.join(unreal.Paths.project_saved_dir(), 'Diagnose', 'iris_import.txt')
MESHES = ['SM_Iris_Stand', 'SM_Iris_StrideA', 'SM_Iris_StrideB']


def assets_in(path):
    return [p.split('.')[0] for p in eal.list_assets(path, recursive=True, include_folder=False)]


def class_of(path):
    return str(eal.find_asset_data(path).asset_class_path.asset_name)


if not os.path.isfile(src):
    raise FileNotFoundError(src)
if eal.does_directory_exist(root):
    eal.delete_directory(root)
eal.make_directory(root)

task = unreal.AssetImportTask()
task.filename = src
task.destination_path = staging
task.automated = True
task.replace_existing = True
task.save = True
tools.import_asset_tasks([task])

# Nanite aus: drei kleine Figuren (16k Dreiecke) - wie Denno.
found = {}
for path in assets_in(staging):
    if class_of(path) == 'StaticMesh':
        mesh = eal.load_asset(path)
        settings = mesh.get_editor_property('nanite_settings')
        settings.set_editor_property('enabled', False)
        mesh.set_editor_property('nanite_settings', settings)
        eal.save_loaded_asset(mesh)
        found[path.rsplit('/', 1)[1]] = path
missing = [m for m in MESHES if m not in found]
if missing:
    raise RuntimeError('Import unvollstaendig, es fehlen %s (gefunden: %s)' % (missing, sorted(found)))

# --- in die Zielordner; Texturen zuerst, dann was sie benutzt ---
TARGET = {'Texture2D': 'Textures', 'StaticMesh': 'Meshes'}
moved = []
for kind in ['Texture2D', 'Material', 'StaticMesh']:
    for path in assets_in(staging):
        cls = class_of(path)
        if (cls == kind) or (kind == 'Material' and cls.startswith('Material')):
            dst = '%s/%s/%s' % (root, TARGET.get(kind, 'Materials'), path.rsplit('/', 1)[1])
            if not eal.rename_asset(path, dst):
                raise RuntimeError('Verschieben fehlgeschlagen: %s -> %s' % (path, dst))
            moved.append(dst)
left = assets_in(staging)
redirectors = [eal.load_asset(p) for p in left if class_of(p) == 'ObjectRedirector']
if redirectors:
    tools.fixup_referencers(redirectors)
left = assets_in(staging)
if left:
    raise RuntimeError('Reste im Zwischenordner: %s' % left)
eal.delete_directory(staging)
eal.save_directory(root, only_if_is_dirty=False, recursive=True)

lines = ['%d Assets' % len(moved)]
for name in MESHES:
    target = '%s/Meshes/%s' % (root, name)
    mesh = eal.load_asset(target)
    if not isinstance(mesh, unreal.StaticMesh):
        lines.append('FEHLT %s' % target)
        continue
    ext = mesh.get_bounds().box_extent
    org = mesh.get_bounds().origin
    slots = mesh.get_editor_property('static_materials')
    lines.append('%s: origin (%.0f, %.0f, %.0f) size %.0f x %.0f x %.0f cm, %d Dreiecke, %d Slots' % (
        target, org.x, org.y, org.z, ext.x * 2, ext.y * 2, ext.z * 2,
        mesh.get_num_triangles(0), len(slots)))
lines += ['%s (%s)' % (p, class_of(p)) for p in sorted(assets_in(root))]
os.makedirs(os.path.dirname(report), exist_ok=True)
with open(report, 'w', encoding='utf-8') as stream:
    stream.write('\n'.join(lines) + '\n')
unreal.log('###IRIS_IMPORT### ' + ' | '.join(lines[:4]))
