"""NUR-LESEN: Bounds/Orientierung von SM_Herbie (und Vergleich Beetle-Body).
Schreibt nach herbie_bounds.json (prints kommen im Cmd-Stream nicht an)."""
import unreal, json, os
EAL = unreal.EditorAssetLibrary
out = {}
for name in ["SM_Herbie", "SM_VWBeetle1969_Body", "SM_VWBeetle_Wheel"]:
    m = EAL.load_asset("/Game/Vehicles/Beetle/" + name)
    if not isinstance(m, unreal.StaticMesh):
        out[name] = None
        continue
    b = m.get_bounds()                 # BoxSphereBounds (Import-Transform beruecksichtigt)
    o = b.origin
    e = b.box_extent                   # Halbmasse je Achse
    out[name] = {
        "origin_cm": [round(o.x, 1), round(o.y, 1), round(o.z, 1)],
        "full_size_cm": [round(2*e.x, 1), round(2*e.y, 1), round(2*e.z, 1)],
        "num_lods": m.get_num_lods(),
        "num_tris_lod0": m.get_num_triangles(0),
    }
open(os.path.join(unreal.Paths.project_dir(), "herbie_bounds.json"), "w").write(json.dumps(out, indent=1))
unreal.log("###HERBIE### geschrieben")
if unreal.SystemLibrary.get_command_line().find("-unattended") >= 0:
    unreal.SystemLibrary.quit_editor()
