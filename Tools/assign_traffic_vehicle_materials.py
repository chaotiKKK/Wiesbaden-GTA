"""Ersetzt die glTF-importierten Fahrzeug-Materialien durch einfache, sicher
gefaerbte UE-Materialien (Lack je Typ, dunkles Glas, schwarze Reifen) und weist
sie den Slots zu (0 Lack, 1 Glas, 2 Reifen). So sind die Farben unabhaengig von
den Eigenheiten des glTF-Material-Imports.
"""
import unreal
MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
MP = unreal.MaterialProperty
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
DIR = "/Game/Vehicles/Traffic/Mats"
EAL.make_directory(DIR)

def make(name, r, g, b, rough, metal):
    p = "%s/%s" % (DIR, name)
    if EAL.does_asset_exist(p):
        EAL.delete_asset(p)
    m = TOOLS.create_asset(name, DIR, unreal.Material, unreal.MaterialFactoryNew())
    col = MEL.create_material_expression(m, unreal.MaterialExpressionConstant3Vector, -400, 0)
    col.set_editor_property("constant", unreal.LinearColor(r, g, b, 1.0))
    MEL.connect_material_property(col, "", MP.MP_BASE_COLOR)
    rr = MEL.create_material_expression(m, unreal.MaterialExpressionConstant, -400, 200)
    rr.set_editor_property("r", rough)
    MEL.connect_material_property(rr, "", MP.MP_ROUGHNESS)
    if metal > 0.0:
        mm = MEL.create_material_expression(m, unreal.MaterialExpressionConstant, -400, 350)
        mm.set_editor_property("r", metal)
        MEL.connect_material_property(mm, "", MP.MP_METALLIC)
    MEL.recompile_material(m)
    EAL.save_loaded_asset(m)
    return m

glass = make("M_VehGlass", 0.04, 0.06, 0.09, 0.12, 0.0)
tire  = make("M_VehTire",  0.02, 0.02, 0.02, 0.9, 0.0)
paints = {
    "SM_TrafficTransporter": make("M_VehPaintWhite",  0.90, 0.90, 0.92, 0.45, 0.1),
    "SM_TrafficKombi":       make("M_VehPaintBlue",   0.10, 0.18, 0.42, 0.40, 0.2),
    "SM_TrafficBus":         make("M_VehPaintOrange",  0.82, 0.32, 0.10, 0.45, 0.1),
}

def sm_of(base):
    d = "/Game/Vehicles/Traffic/%s" % base
    for p in EAL.list_assets(d, recursive=True):
        a = EAL.load_asset(p)
        if isinstance(a, unreal.StaticMesh):
            return a
    return None

sub = None
if hasattr(unreal, "StaticMeshEditorSubsystem"):
    try:
        sub = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    except Exception:
        sub = None

log = []
for base, paint in paints.items():
    sm = sm_of(base)
    if not sm:
        log.append("MISSING %s" % base); continue
    slots = [paint, glass, tire]
    sms = sm.static_materials
    n = min(3, len(sms))
    done = False
    # 1) Subsystem-Weg, falls vorhanden.
    if sub and hasattr(sub, "set_material"):
        for i in range(n):
            sub.set_material(sm, i, slots[i])
        done = True
    # 2) Frische StaticMaterial-Structs (Slotnamen erhalten).
    if not done:
        newlist = []
        for i in range(len(sms)):
            src = slots[i] if i < n else sms[i].get_editor_property("material_interface")
            nsm = unreal.StaticMaterial()
            nsm.set_editor_property("material_interface", src)
            nsm.set_editor_property("material_slot_name", sms[i].get_editor_property("material_slot_name"))
            newlist.append(nsm)
        sm.set_editor_property("static_materials", newlist)
    EAL.save_loaded_asset(sm)
    # Ruecklesen zur Bestaetigung.
    after = ["%s" % (s.get_editor_property("material_interface").get_name()
                     if s.get_editor_property("material_interface") else "None")
             for s in sm.static_materials]
    log.append("%s slots-> %s" % (base, after))

open("C:/freebuff/WiesbadenReal_Sicherung/WiesbadenReal/veh_mats.txt", "w").write("\n".join(log))
if unreal.SystemLibrary.get_command_line().find("-unattended") >= 0:
    unreal.SystemLibrary.quit_editor()
