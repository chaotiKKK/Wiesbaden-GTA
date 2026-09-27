# NUR-LESEN: Bestandsaufnahme Kaefer + Helikopter (Texturen/Materialien/
# Mesh-Material-Slots). Schreibt nach probe_vehicle_assets_result.json, weil
# Python-prints im Cmd-Stream NICHT ankommen.
import json
import os
import struct

import unreal

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
OUT = os.path.join(unreal.Paths.project_dir(), "probe_vehicle_assets_result.json")

result = {"beetle": {}, "heli": {}}

# ---- Kaefer -------------------------------------------------------------
beetle_tex_dir = "/Game/Vehicles/Beetle/Textures"
result["beetle"]["textures"] = []
for p in EAL.list_assets(beetle_tex_dir, recursive=True, include_folder=False):
    t = EAL.load_asset(p)
    if isinstance(t, unreal.Texture2D):
        result["beetle"]["textures"].append({
            "name": p.split("/")[-1],
            "size": [t.blueprint_get_size_x(), t.blueprint_get_size_y()],
            "srgb": t.get_editor_property("srgb"),
            "compression": str(t.get_editor_property("compression_settings")),
            "max_size": t.get_editor_property("max_texture_size"),
        })

# Materialinstanzen: Parameter je Instanz auflisten
result["beetle"]["instances"] = {}
for p in EAL.list_assets("/Game/Vehicles/Beetle", recursive=False, include_folder=False):
    name = p.split("/")[-1]
    if not name.startswith("MI_VWBeetle"):
        continue
    mi = EAL.load_asset(p)
    if not isinstance(mi, unreal.MaterialInstanceConstant):
        continue
    parent = mi.get_editor_property("parent")
    vecs = {}
    for vp in MEL.get_vector_parameter_names(mi):
        v = MEL.get_vector_parameter_value(mi, vp)
        vecs[vp] = [round(v.r, 4), round(v.g, 4), round(v.b, 4), round(v.a, 4)]
    texs = {}
    # UE 5.8-Python: der Getter heisst get_material_instance_texture_parameter_value
    # (Fallback auf den Kurznamen, falls vorhanden).
    get_tex_val = (getattr(MEL, "get_material_instance_texture_parameter_value", None)
                   or getattr(MEL, "get_texture_parameter_value", None))
    for tp in MEL.get_texture_parameter_names(mi):
        tex = get_tex_val(mi, tp) if get_tex_val else None
        texs[tp] = tex.get_name() if tex else None
    result["beetle"]["instances"][name] = {"parent": parent.get_name() if parent else None,
                                           "vector_params": vecs,
                                           "texture_params": texs}

# Meshes: Materialslots je Beetle-Mesh
result["beetle"]["meshes"] = {}
for mesh_name in ["SM_VWBeetle1969", "SM_VWBeetle1969_Body", "SM_VWBeetle_Wheel"]:
    m = EAL.load_asset("/Game/Vehicles/Beetle/" + mesh_name)
    if isinstance(m, unreal.StaticMesh):
        smats = m.get_editor_property("static_materials")
        slots = []
        for sm in smats:
            mat = sm.get_editor_property("material")
            slots.append({"slot_name": str(sm.material_slot_name),
                          "material": mat.get_path_name() if mat else None})
        result["beetle"]["meshes"][mesh_name] = {"slots": slots}

# ---- Helikopter ---------------------------------------------------------
heli_meshes = {}
for path in ["/Game/Assets/Landmarks/HeliBody/StaticMeshes/SM_HeliBody",
             "/Game/Assets/Landmarks/HeliRotorUpper/StaticMeshes/SM_HeliRotorUpper",
             "/Game/Assets/Landmarks/HeliRotorLower/StaticMeshes/SM_HeliRotorLower"]:
    m = EAL.load_asset(path)
    if isinstance(m, unreal.StaticMesh):
        smats = m.get_editor_property("static_materials")
        slots = [{"slot_name": str(sm.material_slot_name),
                  "material": (sm.get_editor_property("material").get_path_name()
                               if sm.get_editor_property("material") else None)}
                 for sm in smats]
        heli_meshes[path.split("/")[-1]] = {"slots": slots}
result["heli"]["meshes"] = heli_meshes

mh = EAL.load_asset("/Game/Materials/City/M_WbHelicopter")
result["heli"]["paint_material"] = type(mh).__name__ if mh else None

# GLB-Quellen: material-Namen grob aus dem JSON-Chunk greppen
def glb_materials(path):
    names = []
    with open(path, "rb") as f:
        data = f.read()
    idx = 0
    while True:
        idx = data.find(b'"name"', idx)
        if idx < 0:
            break
        end = data.find(b'",', idx)
        end2 = data.find(b'"}', idx)
        end = min(e for e in (end, end2) if e > 0) if (end > 0 or end2 > 0) else -1
        snippet = data[idx:idx + 120].decode("utf-8", "replace")
        names.append(snippet[:110])
        idx += 6
        if len(names) > 40:
            break
    return names[:40]

glb_dir = r"C:\freebuff\WiesbadenReal_Sicherung\Quellen\wbnracing\alternativen 5.8\Content\Assets\Source"
for g in ["HeliBody.glb", "HeliRotorUpper.glb", "HeliRotorLower.glb"]:
    p = os.path.join(glb_dir, g)
    if os.path.exists(p):
        result["heli"]["glb_" + g] = glb_materials(p)[:25]

with open(OUT, "w") as f:
    json.dump(result, f, indent=1, default=str)
