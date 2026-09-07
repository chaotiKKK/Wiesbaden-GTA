"""Erzeugt Materialien fuer die formalen Parks (Wasserbecken, Hecken, Kies)."""
import unreal
MEL=unreal.MaterialEditingLibrary; EAL=unreal.EditorAssetLibrary; MP=unreal.MaterialProperty
TOOLS=unreal.AssetToolsHelpers.get_asset_tools(); DIR="/Game/Materials/City"
def log(m): unreal.log("###PARKMAT### %s"%m)
def make(name, r,g,b, rough, metal=0.0, spec=None):
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
    if spec is not None: MEL.connect_material_property(c1(spec,-400,550),"",MP.MP_SPECULAR)
    MEL.recompile_material(m); EAL.save_loaded_asset(m); log("%s"%p)
make("M_WbLmWater", 0.05,0.19,0.30, 0.22)   # Beckenwasser: klares Blau, leicht matt (spiegelt nicht rein den Himmel)
make("M_WbLmHedge", 0.06,0.17,0.05, 0.85)             # dunkelgruene Formhecke
make("M_WbLmGravel",0.56,0.51,0.43, 0.92)             # heller Kiesweg (formaler Park)
log("FERTIG")
if unreal.SystemLibrary.get_command_line().find("-unattended")>=0: unreal.SystemLibrary.quit_editor()
