"""Erzeugt 4 einfache Farbmaterialien fuer die Landmarken (Kirchen)."""
import unreal
MEL=unreal.MaterialEditingLibrary; EAL=unreal.EditorAssetLibrary; MP=unreal.MaterialProperty
TOOLS=unreal.AssetToolsHelpers.get_asset_tools(); DIR="/Game/Materials/City"
def log(m): unreal.log("###LMMAT### %s"%m)
def make(name, r,g,b, rough, metal=0.0):
    p="%s/%s"%(DIR,name)
    if EAL.does_asset_exist(p): EAL.delete_asset(p)
    m=TOOLS.create_asset(name,DIR,unreal.Material,unreal.MaterialFactoryNew())
    def c1(v,x,y):
        n=MEL.create_material_expression(m,unreal.MaterialExpressionConstant,x,y); n.set_editor_property("r",v); return n
    col=MEL.create_material_expression(m,unreal.MaterialExpressionConstant3Vector,-400,0)
    col.set_editor_property("constant",unreal.LinearColor(r,g,b,1.0))
    MEL.connect_material_property(col,"",MP.MP_BASE_COLOR)
    MEL.connect_material_property(c1(rough,-400,250),"",MP.MP_ROUGHNESS)
    if metal>0: MEL.connect_material_property(c1(metal,-400,400),"",MP.MP_METALLIC)
    MEL.recompile_material(m); EAL.save_loaded_asset(m); log("%s"%p)
make("M_WbLmBrick", 0.46,0.15,0.10, 0.80)          # roter Backstein (heller, damit er nicht schwarz wirkt)
make("M_WbLmSlate", 0.24,0.26,0.32, 0.60)          # Schiefer (Turmspitzen/Dach) - sichtbares Blaugrau statt Schwarz
make("M_WbLmGold",  0.62,0.45,0.10, 0.24, 0.9)     # Gold (Zwiebelkuppeln)
make("M_WbLmWhite", 0.72,0.70,0.63, 0.80)          # weiss/creme Kirchenkoerper
log("FERTIG")
if unreal.SystemLibrary.get_command_line().find("-unattended")>=0: unreal.SystemLibrary.quit_editor()
