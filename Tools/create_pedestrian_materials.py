"""Materialien fuer die Fussgaenger-Zonen und Zuweisung an die vier Gangposen.

Fuenf Slots: 0 Haut, 1 Hemd, 2 Hose, 3 Haare, 4 Schuhe. Hemd/Hose/Hautton
kommen PRO INSTANZ aus den Custom-Data-Floats des ISM (der Spawner setzt sie
deterministisch aus dem Fussgaenger-Seed):
  CD0..2 = Hemdfarbe RGB, CD3..5 = Hosenfarbe RGB, CD6 = Hautton (0 dunkel..1 hell).
So bekommt jede Figur andere Kleidung, ohne je Person ein eigenes Material.
"""
import unreal
MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
MP = unreal.MaterialProperty
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
DIR = "/Game/Assets/People/Varied/Mats"
EAL.make_directory(DIR)

def new_mat(name):
    p = "%s/%s" % (DIR, name)
    if EAL.does_asset_exist(p):
        EAL.delete_asset(p)
    m = TOOLS.create_asset(name, DIR, unreal.Material, unreal.MaterialFactoryNew())
    # Ohne dieses Nutzungs-Flag liefert PerInstanceCustomData im -game-Lauf 0
    # (die Figuren blieben schwarz) - das Auto-Flag greift nur im Editor.
    m.set_editor_property("used_with_instanced_static_meshes", True)
    return m

def cd(m, idx, x, y):
    n = MEL.create_material_expression(m, unreal.MaterialExpressionPerInstanceCustomData, x, y)
    n.set_editor_property("data_index", idx)
    return n

def rough(m, v, x=-400, y=300):
    r = MEL.create_material_expression(m, unreal.MaterialExpressionConstant, x, y)
    r.set_editor_property("r", v)
    MEL.connect_material_property(r, "", MP.MP_ROUGHNESS)

def make_rgb_from_cd(name, i0, i1, i2):
    m = new_mat(name)
    c0, c1, c2 = cd(m, i0, -700, -100), cd(m, i1, -700, 40), cd(m, i2, -700, 180)
    ap1 = MEL.create_material_expression(m, unreal.MaterialExpressionAppendVector, -480, -40)
    MEL.connect_material_expressions(c0, "", ap1, "A")
    MEL.connect_material_expressions(c1, "", ap1, "B")
    ap2 = MEL.create_material_expression(m, unreal.MaterialExpressionAppendVector, -300, 20)
    MEL.connect_material_expressions(ap1, "", ap2, "A")
    MEL.connect_material_expressions(c2, "", ap2, "B")
    MEL.connect_material_property(ap2, "", MP.MP_BASE_COLOR)
    rough(m, 0.75)
    MEL.recompile_material(m); EAL.save_loaded_asset(m)
    return m

def make_skin(name, cd_index):
    m = new_mat(name)
    dark = MEL.create_material_expression(m, unreal.MaterialExpressionConstant3Vector, -600, -100)
    dark.set_editor_property("constant", unreal.LinearColor(0.42, 0.29, 0.22, 1.0))
    light = MEL.create_material_expression(m, unreal.MaterialExpressionConstant3Vector, -600, 60)
    light.set_editor_property("constant", unreal.LinearColor(0.96, 0.80, 0.68, 1.0))
    t = cd(m, cd_index, -600, 220)
    lerp = MEL.create_material_expression(m, unreal.MaterialExpressionLinearInterpolate, -360, 0)
    MEL.connect_material_expressions(dark, "", lerp, "A")
    MEL.connect_material_expressions(light, "", lerp, "B")
    MEL.connect_material_expressions(t, "", lerp, "Alpha")
    MEL.connect_material_property(lerp, "", MP.MP_BASE_COLOR)
    rough(m, 0.6)
    MEL.recompile_material(m); EAL.save_loaded_asset(m)
    return m

def make_const(name, r, g, b, ro=0.7):
    m = new_mat(name)
    c = MEL.create_material_expression(m, unreal.MaterialExpressionConstant3Vector, -400, 0)
    c.set_editor_property("constant", unreal.LinearColor(r, g, b, 1.0))
    MEL.connect_material_property(c, "", MP.MP_BASE_COLOR)
    rough(m, ro)
    MEL.recompile_material(m); EAL.save_loaded_asset(m)
    return m

skin    = make_skin("M_PedSkin", 6)
shirt   = make_rgb_from_cd("M_PedShirt", 0, 1, 2)
trouser = make_rgb_from_cd("M_PedTrouser", 3, 4, 5)
hair    = make_const("M_PedHair", 0.18, 0.12, 0.08)
shoe    = make_const("M_PedShoe", 0.07, 0.07, 0.08)
slots = [skin, shirt, trouser, hair, shoe]   # Reihenfolge = Slot 0..4

sub = None
if hasattr(unreal, "StaticMeshEditorSubsystem"):
    try: sub = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    except Exception: sub = None

def sm_of(name):
    d = "/Game/Assets/People/Varied/%s" % name
    if EAL.does_directory_exist(d):
        for p in EAL.list_assets(d, recursive=True):
            a = EAL.load_asset(p)
            if isinstance(a, unreal.StaticMesh):
                return a
    p = "/Game/Assets/People/Varied/%s" % name
    return EAL.load_asset(p) if EAL.does_asset_exist(p) else None

log = []
for n in [f"SM_WbPed2{t}_{i}" for t in ["","B","C"] for i in range(4)]:
    sm = sm_of(n)
    if not sm:
        log.append("MISSING %s" % n); continue
    sms = sm.static_materials
    k = min(len(slots), len(sms))
    if sub and hasattr(sub, "set_material"):
        for i in range(k): sub.set_material(sm, i, slots[i])
    else:
        newlist = []
        for i in range(len(sms)):
            src = slots[i] if i < k else sms[i].get_editor_property("material_interface")
            nm = unreal.StaticMaterial()
            nm.set_editor_property("material_interface", src)
            nm.set_editor_property("material_slot_name", sms[i].get_editor_property("material_slot_name"))
            newlist.append(nm)
        sm.set_editor_property("static_materials", newlist)
    EAL.save_loaded_asset(sm)
    after = [s.get_editor_property("material_interface").get_name() if s.get_editor_property("material_interface") else "None"
             for s in sm.static_materials]
    log.append("PATH %s | %s -> %s" % (sm.get_path_name(), n, after))

open("C:/freebuff/WiesbadenReal_Sicherung/WiesbadenReal/ppl.txt", "w").write("\n".join(log))
if unreal.SystemLibrary.get_command_line().find("-unattended") >= 0:
    unreal.SystemLibrary.quit_editor()
