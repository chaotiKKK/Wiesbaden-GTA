import unreal
mesh = unreal.EditorAssetLibrary.load_asset("/Game/Waffen/Meshes/SM_Waffe_Pistole")
box = mesh.get_bounding_box()
zeilen = ["Box-Type: %s" % type(box), "Box-Attrs: %s" % [a for a in dir(box) if not a.startswith("__")]]
for attr in ("min", "max", "center", "extent", "size"):
    try:
        zeilen.append("%s = %s" % (attr, box.get_editor_property(attr)))
    except Exception as e:
        zeilen.append("%s -> %s" % (attr, e))
open(r"C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Saved\Diagnose\mesh_api_probe.txt", "w", encoding="utf-8").write("\n".join(zeilen) + "\n")
unreal.SystemLibrary.quit_editor()
