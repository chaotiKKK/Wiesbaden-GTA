"""Nur die GEOMETRIE eines Static Mesh austauschen - Pfad, Materialien, Nanite bleiben.

Wozu: Tripo-Importe bringen Nanite-Meshes mit 0,3-1,5 Mio. Dreiecken. Das Asset
speichert diese Quellgeometrie vollstaendig (Ka-52-Rumpf: 1,37 Mio. Dreiecke =
35,6 MB), obwohl das Modell im Spiel nie so nah zu sehen ist. Dieses Skript
importiert die Quelle, vereinfacht sie auf das Dreiecksziel des Auftrags (siehe
simplify()) und setzt sie ein, ohne die gewachsene Material-Zuordnung eines
Neuimports zu riskieren.

Auftraege: Tools/mesh_diet.json. Ohne 'source' wird die im Asset gespeicherte
Quellgeometrie vor Ort vereinfacht (fuer Importe ohne erhaltene Quelldatei).
Liegt ein Mesh schon unter dem Ziel, bleibt es unberuehrt (wiederholbar).
Modus ueber WB_DIAET:
  pruefen  (Vorgabe) nur melden: Dreiecke, Kollision, Referenzen
  tauschen importieren, Materialien per Slot-Name uebernehmen, pruefen, ersetzen
WB_DIAET_NUR=<Teilpfad> beschraenkt auf passende Auftraege (z.B. Vehicles/Bus).

Abbruch statt stiller Verschlechterung, wenn
  - ein anderes Asset das Mesh referenziert (Loeschen wuerde es kappen),
  - die einfachen Kollisionsformen nach dem Import andere sind als vorher
    (Sockets sind in Python geschuetzt - Heli- und Bus-Code nutzen keine),
  - ein neuer Material-Slot keinen gleichnamigen alten (oder beim Spender) hat,
  - Groesse oder Mittelpunkt um mehr als 2 % abweichen (Rotorachse!).

UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=Tools/replace_mesh_geometry.py
Bericht: Saved/Diagnose/mesh_diet.txt
"""
import json
import os
import unreal

eal = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
project = unreal.Paths.project_dir()
content = unreal.Paths.project_content_dir()
mode = os.environ.get('WB_DIAET', 'pruefen')
report_path = os.path.join(unreal.Paths.project_saved_dir(), 'Diagnose', 'mesh_diet.txt')
TOLERANCE = 0.02
lines = []


def report(message):
    lines.append(str(message))
    unreal.log('###MESHDIET### ' + str(message))


def save_report():
    os.makedirs(os.path.dirname(report_path), exist_ok=True)
    with open(report_path, 'w', encoding='utf-8') as stream:
        stream.write('\n'.join(lines) + '\n')


def fail(message):
    report('ABBRUCH: ' + message)
    save_report()
    raise RuntimeError(message)


def disk_mb(asset):
    path = os.path.join(content, asset.replace('/Game/', '', 1) + '.uasset')
    return os.path.getsize(path) / 1048576.0 if os.path.isfile(path) else 0.0


def collision_kinds(mesh):
    """Einfache Kollisionsformen je Art, z.B. {'box_elems': 1} (leer = keine)."""
    body = mesh.get_editor_property('body_setup')
    if body is None:
        return {}
    geom = body.get_editor_property('agg_geom')
    kinds = {}
    for kind in ('sphere_elems', 'box_elems', 'sphyl_elems', 'convex_elems', 'tapered_capsule_elems'):
        count = len(geom.get_editor_property(kind))
        if count:
            kinds[kind] = count
    return kinds


def slots_of(mesh):
    return [(str(s.get_editor_property('material_slot_name')), s.get_editor_property('material_interface'))
            for s in mesh.get_editor_property('static_materials')]


def bounds_of(mesh):
    b = mesh.get_bounds()
    return b.origin, b.box_extent


def describe(asset):
    mesh = eal.load_asset(asset)
    if not isinstance(mesh, unreal.StaticMesh):
        fail('%s ist kein Static Mesh' % asset)
    referencers = [r for r in eal.find_package_referencers_for_asset(asset, load_assets_to_confirm=True)
                   if r.split('.')[0] != asset]
    origin, extent = bounds_of(mesh)
    info = {
        'mesh': mesh,
        'tris': mesh.get_num_triangles(0),
        'collision': collision_kinds(mesh),
        'referencers': referencers,
        'origin': origin,
        'extent': extent,
    }
    report('%s: %.2f MB, %d Dreiecke (Ersatz-Mesh), Nanite=%s, einfache Kollision %s, '
           'Referenzen %s, Groesse %.0f x %.0f x %.0f cm' % (
               asset, disk_mb(asset), info['tris'],
               mesh.get_editor_property('nanite_settings').get_editor_property('enabled'),
               info['collision'] or 'keine', referencers or 'keine',
               extent.x * 2, extent.y * 2, extent.z * 2))
    return info


def import_source(source, folder):
    path = os.path.join(project, source)
    if not os.path.isfile(path):
        fail('Quelle fehlt: %s' % path)
    task = unreal.AssetImportTask()
    task.filename = path
    task.destination_path = folder
    task.automated = True
    task.replace_existing = True
    task.save = False
    if path.lower().endswith('.fbx'):
        ui = unreal.FbxImportUI()
        ui.import_mesh = True
        ui.import_as_skeletal = False
        ui.import_animations = False
        ui.import_materials = False
        ui.import_textures = False
        data = unreal.FbxStaticMeshImportData()
        data.combine_meshes = False
        data.auto_generate_collision = False
        data.build_nanite = True
        data.normal_import_method = unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS
        ui.static_mesh_import_data = data
        task.options = ui
    tools.import_asset_tasks([task])


def find_imported(folder, name):
    meshes = []
    for path in eal.list_assets(folder, recursive=True, include_folder=False):
        asset = eal.load_asset(path.split('.')[0])
        if isinstance(asset, unreal.StaticMesh):
            meshes.append(asset)
    named = [m for m in meshes if m.get_name() == name]
    if len(named) == 1:
        return named[0]
    if len(meshes) == 1:
        return meshes[0]
    fail('%s nicht eindeutig im Import %s: %s' % (name, folder, [m.get_name() for m in meshes]))


def simplify(mesh, triangles):
    """Quellgeometrie des (frisch importierten) Mesh auf `triangles` vereinfachen.

    Unreals attributbewusster Vereinfacher, der UV-Naehte als Naehte haelt: Blenders
    Collapse-Dezimierung zog die UVs an den Naehten in schwarze Atlasbereiche
    (schwarze Sprenkel am Bus) und machte Streifenkanten wellig. glTF liefert
    jede Naht als offenen Rand - erst verschweissen, dann bleibt die Naht eine
    Attribut-Naht, die der Vereinfacher nicht ueberqueren darf.
    """
    lib = unreal.GeometryScript_AssetUtils
    read = unreal.GeometryScriptCopyMeshFromAssetOptions()
    read.apply_build_settings = False
    lod = unreal.GeometryScriptMeshReadLOD()
    lod.lod_type = unreal.GeometryScriptLODType.SOURCE_MODEL
    lod.lod_index = 0
    result = lib.copy_mesh_from_static_mesh(mesh, unreal.DynamicMesh(), read, lod)
    dyn = result[0] if isinstance(result, tuple) else result
    before = unreal.GeometryScript_MeshQueries.get_num_triangle_i_ds(dyn)
    if before <= triangles * 1.05:
        return before, before          # schon klein genug - nichts schreiben
    weld = unreal.GeometryScriptWeldEdgesOptions()
    weld.tolerance = 0.001
    weld.only_unique_pairs = True
    unreal.GeometryScript_MeshRepair.weld_mesh_edges(dyn, weld)
    opts = unreal.GeometryScriptSimplifyMeshOptions()
    opts.method = unreal.GeometryScriptRemoveMeshSimplificationType.ATTRIBUTE_AWARE
    # Entlang einer Naht einklappen haelt die Naht als Linie, verschiebt sie aber
    # leicht. Fuer die Ka-52 unsichtbar; ganz gesperrt blieb ihr Rumpf bei 312k
    # statt 150k haengen. Beim Bus lag die Orange/Blau-Grenze AUF Naehten und
    # wurde treppig, gesperrt verschmierte die Schattierung - er bleibt voll
    # (siehe _nicht_reduziert in Tools/mesh_diet.json). Jedes neue Ziel darum am
    # Vorher/Nachher-Bild im Spiel pruefen.
    opts.allow_seam_collapse = True
    opts.allow_seam_smoothing = False
    opts.allow_seam_splits = False
    opts.auto_compact = True
    unreal.GeometryScript_MeshSimplification.apply_simplify_to_triangle_count(dyn, triangles, opts)
    after = unreal.GeometryScript_MeshQueries.get_num_triangle_i_ds(dyn)
    write = unreal.GeometryScriptCopyMeshToAssetOptions()
    write.enable_recompute_normals = False
    write.enable_recompute_tangents = False
    write.replace_materials = False
    write.apply_nanite_settings = False
    write.emit_transaction = False
    target = unreal.GeometryScriptMeshWriteLOD()
    target.lod_index = 0
    lib.copy_mesh_to_static_mesh(dyn, mesh, write, target)
    return before, after


def close(a, b, scale):
    return abs(a - b) <= TOLERANCE * max(scale, 1.0)


config = json.load(open(os.path.join(project, 'Tools', 'mesh_diet.json'), encoding='utf-8'))
jobs = config['jobs']
only = os.environ.get('WB_DIAET_NUR', '')
if only:
    jobs = [job for job in jobs if only in job['asset']]
report('Modus: %s, %d Auftraege' % (mode, len(jobs)))
before = {job['asset']: describe(job['asset']) for job in jobs}
for job in jobs:
    info = before[job['asset']]
    if info['referencers']:
        fail('%s wird von %s referenziert' % (job['asset'], info['referencers']))

if mode != 'tauschen':
    save_report()
    raise SystemExit(0)

imported = {}
total_before = total_after = 0.0
for job in jobs:
    asset = job['asset']
    if not job.get('source'):
        # Vor Ort: keine Quelldatei mehr vorhanden (z.B. Landmarks/Heli*) - die
        # im Asset gespeicherte Quellgeometrie selbst vereinfachen. Materialien,
        # Kollision und Nanite bleiben am Asset.
        mesh = before[asset]['mesh']
        mb_before = disk_mb(asset)
        if job.get('material'):
            # Das Material, das der Laufzeit-Code ohnehin auf Slot 0 setzt, wird
            # Vorgabe des Assets - das importierte bleibt so unsichtbar, und seine
            # Texturen werden verwaist (siehe 'verwaist' unten).
            slots = slots_of(mesh)
            if len(slots) != 1:
                fail('%s: Material-Vorgabe nur fuer 1 Slot, hat %d' % (asset, len(slots)))
            material = eal.load_asset(job['material'])
            if material is None:
                fail('Material fehlt: %s' % job['material'])
            mesh.set_material(0, material)
        tris_source, tris_game = simplify(mesh, job['triangles'])
        o0, e0 = before[asset]['origin'], before[asset]['extent']
        o1, e1 = bounds_of(mesh)
        scale = max(e0.x, e0.y, e0.z)
        for axis in ('x', 'y', 'z'):
            if not close(getattr(e0, axis), getattr(e1, axis), scale) or not close(getattr(o0, axis), getattr(o1, axis), scale):
                fail('%s: Masse weichen ab - alt Mitte %s Halbmass %s, neu Mitte %s Halbmass %s' % (asset, o0, e0, o1, e1))
        if collision_kinds(mesh) != before[asset]['collision']:
            fail('%s: Kollision alt %s, neu %s' % (asset, before[asset]['collision'], collision_kinds(mesh)))
        eal.save_asset(asset, only_if_is_dirty=False)
        total_before += mb_before
        total_after += disk_mb(asset)
        report('%s vor Ort: Quelle %d -> %d Dreiecke, %.2f -> %.2f MB' % (
            asset, tris_source, tris_game, mb_before, disk_mb(asset)))
        continue
    folder = asset.rsplit('/', 1)[0] + '/_DiaetImport/' + os.path.splitext(os.path.basename(job['source']))[0]
    if folder not in imported:
        if eal.does_directory_exist(folder):
            eal.delete_directory(folder)
        import_source(job['source'], folder)
        imported[folder] = True
    new = find_imported(folder, job['object'])
    old = before[asset]['mesh']
    if job.get('triangles'):
        tris_source, tris_game = simplify(new, job['triangles'])
        report('%s: Quelle %d -> %d Dreiecke' % (asset, tris_source, tris_game))
        if tris_game > job['triangles'] * 1.05:
            fail('%s: Vereinfachung erreicht %d statt %d (Naehte blockieren?)' % (asset, tris_game, job['triangles']))

    # Materialien per Slot-Name vom alten Mesh; fehlt einer, vom Material-
    # Spender des Auftrags (Bus: SM_Bus traegt die korrigierten Instanzen fuer
    # alle Tripo-Teile, wie in Tools/import_eswebus_wheels.py).
    old_slots = dict(slots_of(old))
    if job.get('material_donor'):
        donor = eal.load_asset(job['material_donor'])
        if not isinstance(donor, unreal.StaticMesh):
            fail('Material-Spender fehlt: %s' % job['material_donor'])
        for name, material in slots_of(donor):
            old_slots.setdefault(name, material)
    new_slots = slots_of(new)
    missing = [name for name, _ in new_slots if name not in old_slots]
    if missing:
        fail('%s: neue Slots ohne alten Namensvetter: %s (alt: %s)' % (asset, missing, sorted(old_slots)))
    for index, (name, _) in enumerate(new_slots):
        new.set_material(index, old_slots[name])
    new.set_editor_property('nanite_settings', old.get_editor_property('nanite_settings'))
    if collision_kinds(new) != before[asset]['collision']:
        fail('%s: Kollision alt %s, neu %s' % (asset, before[asset]['collision'], collision_kinds(new)))

    # Groesse und Mittelpunkt muessen bleiben (Rotorachse, Radaufstand).
    o0, e0 = before[asset]['origin'], before[asset]['extent']
    o1, e1 = bounds_of(new)
    scale = max(e0.x, e0.y, e0.z)
    for axis in ('x', 'y', 'z'):
        if not close(getattr(e0, axis), getattr(e1, axis), scale) or not close(getattr(o0, axis), getattr(o1, axis), scale):
            fail('%s: Masse weichen ab - alt Mitte %s Halbmass %s, neu Mitte %s Halbmass %s' % (asset, o0, e0, o1, e1))

    mb_before = disk_mb(asset)
    new_path = new.get_path_name().split('.')[0]
    eal.save_asset(new_path, only_if_is_dirty=False)
    if not eal.delete_asset(asset):
        fail('%s liess sich nicht loeschen' % asset)
    if not eal.rename_asset(new_path, asset):
        fail('%s liess sich nicht an %s verschieben' % (new_path, asset))
    eal.save_asset(asset, only_if_is_dirty=False)
    mesh = eal.load_asset(asset)
    mb_after = disk_mb(asset)
    total_before += mb_before
    total_after += mb_after
    report('%s getauscht: %.2f -> %.2f MB, %d Slots uebernommen, Ersatz-Mesh %d Dreiecke' % (
        asset, mb_before, mb_after, len(new_slots), mesh.get_num_triangles(0)))

for folder in imported:
    leftovers = [p for p in eal.list_assets(folder, recursive=True, include_folder=False)
                 if str(eal.find_asset_data(p.split('.')[0]).asset_class_path.asset_name) == 'ObjectRedirector']
    if leftovers:
        fail('Umleitungen im Zwischenordner: %s' % leftovers)
    eal.delete_directory(folder.rsplit('/', 1)[0])
# Verwaiste Import-Reste (Materialien/Texturen, die kein Mesh mehr traegt).
# Geloescht wird nur, was wirklich niemand mehr referenziert.
for folder in config.get('verwaist', []) if not os.environ.get('WB_DIAET_NUR') else []:
    for path in eal.list_assets(folder, recursive=True, include_folder=False):
        path = path.split('.')[0]
        users = [r for r in eal.find_package_referencers_for_asset(path, load_assets_to_confirm=True)
                 if r.split('.')[0] != path]
        if users:
            fail('%s wird noch benutzt von %s' % (path, users))
        mb = disk_mb(path)
        if not eal.delete_asset(path):
            fail('%s liess sich nicht loeschen' % path)
        total_before += mb
        report('%s verwaist, entfernt (%.2f MB)' % (path, mb))
    eal.delete_directory(folder)
report('Summe: %.1f -> %.1f MB' % (total_before, total_after))
save_report()
