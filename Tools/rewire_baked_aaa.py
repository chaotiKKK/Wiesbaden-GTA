# Verdrahtet die gebackenen M_Wb-Stadt-Materialien IN-PLACE auf die AAA-Foto-
# texturen um: in jedem Material werden die vorhandenen Textur-Samples (Farbe/
# Normal/Roughness) durch die passenden AAA-Texturen ersetzt. Der prozedurale
# Graph bleibt unangetastet - Fahrbahnmarkierungen (M_WbRoad, VertexColor-Lerp)
# und Fenster/Etagen (M_WbFacade_Putz) ueberleben. So bekommt die GEBACKENE Karte
# die AAA-Optik OHNE Re-Bake (die Chunks referenzieren diese Assets per Pfad).
#
# Nur Phase 1: Materialien mit echten Foto-Textur-Samples. Die rein prozeduralen
# (M_WbPavedStone/Unpaved/Facade_Backstein/Sandstein/Fachwerk: nur T_WbNoise)
# bleiben vorerst - dort gaebe es keine Textur zum Tauschen (Phase 2).
import unreal

mel = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
CITY = "/Game/Materials/City/"
TEX = "/Game/Materials/AAA/Textures/"

# Material -> { aktueller Textur-Name : AAA-Textur-Name }
REWIRE = {
    "M_WbRoad": {
        "T_Fahrbahn_Asphalt_Color": "RoadAsphalt_C",
        "T_Fahrbahn_Asphalt_Normal": "RoadAsphalt_N",
        "T_Fahrbahn_Asphalt_Roughness": "RoadAsphalt_R",
    },
    "M_WbSidewalk": {
        "T_Gehweg_Platten_Color": "Paving_C",
        "T_Gehweg_Platten_Normal": "Paving_N",
        "T_Gehweg_Platten_Roughness": "Paving_R",
    },
    "M_WbKerb": {
        "T_Bordstein_Beton_Color": "FacadeConcrete_C",
        "T_Bordstein_Beton_Normal": "FacadeConcrete_N",
        "T_Bordstein_Beton_Roughness": "FacadeConcrete_R",
    },
    "M_WbTerrain": {
        "T_Gelaende_Wiese_Color": "TerrainGrass_C",
        "T_Gelaende_Wiese_Normal": "TerrainGrass_N",
        "T_Gelaende_Wiese_Roughness": "TerrainGrass_R",
    },
    "M_WbFacade_Putz": {
        "T_Putz_Color": "FacadePlaster_C",
        "T_Putz_Normal": "FacadePlaster_N",
    },
    "M_WbFacade_Beton": {
        "T_Facade_Buerohaus_Color": "FacadeConcrete_C",
        "T_Facade_Buerohaus_Normal": "FacadeConcrete_N",
        "T_Facade_Buerohaus_Roughness": "FacadeConcrete_R",
    },
}

# AAA-Texturen einmal laden (Cache).
tex_cache = {}
def aaa(name):
    if name not in tex_cache:
        tex_cache[name] = unreal.load_asset(TEX + name)
    return tex_cache[name]

total = 0
for mat_name, swaps in REWIRE.items():
    mat = unreal.load_asset(CITY + mat_name)
    if not mat:
        unreal.log_warning("REWIRE %s: Material fehlt" % mat_name); continue
    swapped = 0
    for e in mel.get_material_expressions(mat):
        if "TextureSample" not in e.get_class().get_name():
            continue
        cur = e.get_editor_property("texture")
        cur_name = cur.get_name() if cur else ""
        if cur_name in swaps:
            new_tex = aaa(swaps[cur_name])
            if new_tex:
                e.set_editor_property("texture", new_tex)
                swapped += 1
            else:
                unreal.log_warning("REWIRE %s: AAA-Textur %s fehlt" % (mat_name, swaps[cur_name]))
    mel.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    total += swapped
    unreal.log("REWIRE %s: %d/%d Texturen getauscht." % (mat_name, swapped, len(swaps)))

unreal.log("REWIRE: fertig, %d Texturen ueber %d Materialien getauscht." % (total, len(REWIRE)))
