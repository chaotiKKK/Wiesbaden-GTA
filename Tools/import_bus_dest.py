"""Importiert die Zielanzeige-Texturen und baut zwei Unlit-Materialien.

/Game/Vehicles/Bus/Ziel/
  T_WbBusZiel_Mainz, T_WbBusZiel_Nordfriedhof        (Texturen)
  M_WbBusZiel_Mainz, M_WbBusZiel_Nordfriedhof         (Unlit, Textur -> Emissive)

Der Bus-Actor laedt die beiden Materialien und legt je Fahrtrichtung das
passende auf ein Schild-Quad vor der Front. Aufruf headless via .cmd/Start-Process.
"""
import unreal, os

SRC = os.path.join(unreal.Paths.project_dir(), "Content", "Vehicles", "Bus", "Ziel", "Source")
DEST = "/Game/Vehicles/Bus/Ziel"
def log(m): unreal.log("###WBBUSZIEL### %s" % m)
EAL = unreal.EditorAssetLibrary
ATH = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary


def import_tex(png, name):
    t = unreal.AssetImportTask()
    t.filename = os.path.join(SRC, png)
    t.destination_path = DEST
    t.destination_name = name
    t.automated = True
    t.replace_existing = True
    t.save = True
    ATH.import_asset_tasks([t])
    a = EAL.load_asset(DEST + "/" + name)
    if a:
        try: a.set_editor_property("srgb", True)
        except Exception as e: log("srgb: %s" % e)
        EAL.save_loaded_asset(a)
    return a


def make_mat(matname, tex):
    mpath = DEST + "/" + matname
    if EAL.does_asset_exist(mpath): EAL.delete_asset(mpath)
    mat = ATH.create_asset(matname, DEST, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", True)
    node = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureSample, -380, 0)
    node.set_editor_property("texture", tex)
    MEL.connect_material_property(node, "RGB", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    log("Material %s -> %s" % (mpath, tex.get_name() if tex else "None"))
    return mat


t1 = import_tex("T_WbBusZiel_Mainz.png", "T_WbBusZiel_Mainz")
t2 = import_tex("T_WbBusZiel_Nordfriedhof.png", "T_WbBusZiel_Nordfriedhof")
if t1: make_mat("M_WbBusZiel_Mainz", t1)
if t2: make_mat("M_WbBusZiel_Nordfriedhof", t2)
log("ENDE")
