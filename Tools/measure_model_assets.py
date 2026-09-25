"""Groesse importierter Modelle messen: Dreiecke, Nanite, LODs, Texturkanten.

Wozu: Tripo-/Sketchfab-Importe landen mit Millionen Dreiecken und 2k-/4k-
Texturen im Repo, auch wenn das Modell im Spiel klein zu sehen ist. Vor jedem
Verkleinern steht diese Messung - sie zeigt, WO die Megabytes stecken.

UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=Tools/measure_model_assets.py
Ergebnis: Saved/Diagnose/model_assets.tsv (eine Zeile je Asset) + Log-Zusammenfassung.
"""
import os
import unreal

eal = unreal.EditorAssetLibrary
ROOTS = ['/Game/Vehicles', '/Game/Assets/People', '/Game/Assets/Landmarks', '/Game/Textures/People']
content = os.path.join(unreal.Paths.project_content_dir())
out = os.path.join(unreal.Paths.project_saved_dir(), 'Diagnose', 'model_assets.tsv')


def disk_mb(path):
    rel = path.replace('/Game/', '', 1).split('.')[0] + '.uasset'
    f = os.path.join(content, rel)
    return os.path.getsize(f) / 1048576.0 if os.path.isfile(f) else 0.0


rows = []
for root in ROOTS:
    for path in eal.list_assets(root, recursive=True, include_folder=False):
        path = path.split('.')[0]
        data = eal.find_asset_data(path)
        cls = str(data.asset_class_path.asset_name)
        info = ''
        if cls == 'StaticMesh':
            m = eal.load_asset(path)
            nanite = m.get_editor_property('nanite_settings').get_editor_property('enabled')
            info = 'tris=%d lods=%d nanite=%s' % (m.get_num_triangles(0), m.get_num_lods(), nanite)
        elif cls == 'SkeletalMesh':
            m = eal.load_asset(path)
            sub = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
            info = 'verts=%d lods=%d' % (sub.get_num_verts(m, 0), sub.get_lod_count(m))
        elif cls == 'Texture2D':
            t = eal.load_asset(path)
            info = 'size=%dx%d' % (t.blueprint_get_size_x(), t.blueprint_get_size_y())
        rows.append((disk_mb(path), cls, path, info))
rows.sort(reverse=True)
os.makedirs(os.path.dirname(out), exist_ok=True)
with open(out, 'w', encoding='utf-8') as f:
    for mb, cls, path, info in rows:
        f.write('%.2f\t%s\t%s\t%s\n' % (mb, cls, path, info))
unreal.log('###MODEL_ASSETS### %d Assets, %.1f MB -> %s' % (len(rows), sum(r[0] for r in rows), out))
