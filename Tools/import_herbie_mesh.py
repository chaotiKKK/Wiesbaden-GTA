import unreal
EAL=unreal.EditorAssetLibrary
TOOLS=unreal.AssetToolsHelpers.get_asset_tools()
def log(m): unreal.log("###HIMP### %s"%m)
FBX=r"C:/Users/HP/Downloads/_herbie/out/SM_Herbie.fbx"
DEST="/Game/Vehicles/Beetle"; NAME="SM_Herbie"
opts=unreal.FbxImportUI()
opts.import_mesh=True; opts.import_as_skeletal=False; opts.import_materials=True; opts.import_textures=True
opts.mesh_type_to_import=unreal.FBXImportType.FBXIT_STATIC_MESH
sd=opts.static_mesh_import_data
sd.import_uniform_scale=1.0; sd.combine_meshes=True; sd.generate_lightmap_u_vs=True; sd.auto_generate_collision=False
t=unreal.AssetImportTask()
t.filename=FBX; t.destination_path=DEST; t.destination_name=NAME
t.automated=True; t.replace_existing=True; t.save=True; t.options=opts
TOOLS.import_asset_tasks([t])
m=EAL.load_asset("%s/%s"%(DEST,NAME))
if not isinstance(m,unreal.StaticMesh):
    log("FEHLER: nicht als StaticMesh importiert"); 
else:
    b=m.get_bounds()
    log("Importiert: SM_Herbie, %d LODs, Hoehe/Laenge/Breite ~ %.2f/%.2f/%.2f m"%(
        m.get_num_lods(), b.box_extent.z*2/100, b.box_extent.y*2/100, b.box_extent.x*2/100))
    for sm in m.get_editor_property("static_materials"):
        mi=sm.material_interface
        log("  Slot '%s' -> %s"%(sm.material_slot_name, mi.get_name() if mi else "None"))
if unreal.SystemLibrary.get_command_line().find("-unattended")>=0: unreal.SystemLibrary.quit_editor()
