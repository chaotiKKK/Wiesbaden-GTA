# Tauscht in M_WbBuildingWall (Standard-Putz-Fassade) die Wand-Textursamples
# IN-PLACE auf die hoeherwertigen AAA-Plaster-Texturen - der komplette
# prozedurale Fenster-/Etagen-Graph bleibt unangetastet. So bekommt die Wand
# echte PBR-Oberflaeche (Putz-Farbe + staerkere Normal-Map) und wirkt nicht mehr
# flach, ohne dass die Fenster verloren gehen.
import unreal
mel = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary

MAT = "/Game/Materials/City/M_WbBuildingWall"
AAA = "/Game/Materials/AAA/Textures"

mat = unreal.load_asset(MAT)
plaster_c = unreal.load_asset(AAA + "/FacadePlaster_C")
plaster_n = unreal.load_asset(AAA + "/FacadePlaster_N")
if not (mat and plaster_c and plaster_n):
    unreal.log_warning("WALLWIRE: Asset(s) fehlen - Abbruch."); raise SystemExit

swapped = 0
for e in mel.get_material_expressions(mat):
    if "TextureSample" not in e.get_class().get_name():
        continue
    t = e.get_editor_property("texture")
    name = t.get_name() if t else ""
    if name == "T_Putz_Color":
        e.set_editor_property("texture", plaster_c)
        swapped += 1
        unreal.log("WALLWIRE: T_Putz_Color -> FacadePlaster_C")
    elif name == "T_Putz_Normal":
        e.set_editor_property("texture", plaster_n)
        swapped += 1
        unreal.log("WALLWIRE: T_Putz_Normal -> FacadePlaster_N")

if swapped < 2:
    unreal.log_warning("WALLWIRE: nur %d Textur(en) getauscht (erwartet 2) - pruefen!" % swapped)

mel.recompile_material(mat)
EAL.save_loaded_asset(mat)
unreal.log("WALLWIRE: M_WbBuildingWall Wand auf AAA-Plaster getauscht (%d Texturen)." % swapped)
