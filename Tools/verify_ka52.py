import unreal

DEST = "/Game/Vehicles/Ka52"
def log(m): unreal.log_warning("###WBKA52V### %s" % m)
EAL = unreal.EditorAssetLibrary

for p in sorted(EAL.list_assets(DEST, recursive=True)):
    a = EAL.load_asset(p)
    if isinstance(a, unreal.StaticMesh):
        b = a.get_bounds()
        org = b.origin
        ext = b.box_extent
        log("%s  origin=(%.1f, %.1f, %.1f)  extent=(%.1f, %.1f, %.1f)  verts~%s" % (
            p, org.x, org.y, org.z, ext.x, ext.y, ext.z,
            a.get_editor_property("static_materials").__len__() if hasattr(a, "get_editor_property") else "?"))
log("ENDE")
