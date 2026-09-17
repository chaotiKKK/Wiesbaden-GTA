import unreal, os

DEST = "/Game/Vehicles/Ka52"
FBX = os.path.join(unreal.Paths.project_dir(), "Content", "Data", "Raw", "Ka52", "ka52_ue.fbx")
def log(m): unreal.log("###WBKA52### %s" % m)
EAL = unreal.EditorAssetLibrary

EAL.make_directory(DEST)

# --- Import-Optionen: statisch (kein Skelett), Materialien importieren ------
opts = unreal.FbxImportUI()
opts.import_mesh = True
opts.import_animations = False
opts.import_materials = True
opts.import_textures = True
opts.import_as_skeletal = False
fbx = unreal.FbxStaticMeshImportData()
fbx.combine_meshes = False            # 3 Meshes bleiben 3 Meshes
fbx.auto_generate_collision = False
fbx.build_nanite = True
fbx.normal_import_method = unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS
opts.static_mesh_import_data = fbx

t = unreal.AssetImportTask()
t.filename = FBX
t.destination_path = DEST
t.options = opts
t.automated = True
t.replace_existing = True
t.save = True
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])

# --- Pruefen: welche Meshes landeten wo -------------------------------------
found = {}
for p in EAL.list_assets(DEST, recursive=True):
    a = EAL.load_asset(p)
    if isinstance(a, unreal.StaticMesh):
        found[p] = a
        b = a.get_bounds()
        log("SM %s  UU X=%.1f Y=%.1f Z=%.1f" % (p, b.box_extent.x * 2, b.box_extent.y * 2, b.box_extent.z * 2))

want = {"Rotor_Upper", "Rotor_Lower", "Fuselage"}
names = {str(p).split('/')[-1].split('.')[0] for p in found}
missing = want - names
if missing:
    log("FEHLER fehlende Meshes: %s" % missing)
else:
    log("alle 3 Meshes da")

mats = [str(p) for p in EAL.list_assets(DEST, recursive=True)
        if isinstance(EAL.load_asset(p), unreal.Material)]
log("Materialien: %s" % mats)
log("ENDE")
