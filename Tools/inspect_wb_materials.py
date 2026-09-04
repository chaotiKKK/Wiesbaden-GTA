# Inspiziert die gebackenen M_Wb-Stadt-Materialien: pro Material die
# TextureSample-Knoten (aktuelle Textur + Sampler-Typ) und die Skalar-/Vektor-
# Parameter. So wird sichtbar, wo eine AAA-Fototextur eingesetzt werden kann,
# ohne den prozeduralen Graphen (Fenster/Markierungen) zu zerstoeren.
import unreal

mel = unreal.MaterialEditingLibrary
MATS = [
    "M_WbRoad", "M_WbSidewalk", "M_WbKerb", "M_WbPavedStone", "M_WbUnpaved",
    "M_WbLaneMarking", "M_WbTerrain",
    "M_WbFacade_Putz", "M_WbFacade_Backstein", "M_WbFacade_Sandstein",
    "M_WbFacade_Beton", "M_WbFacade_Glas", "M_WbFacade_Fachwerk",
]
BASE = "/Game/Materials/City/"

for name in MATS:
    mat = unreal.load_asset(BASE + name)
    if not mat:
        unreal.log("WBMAT %s: FEHLT" % name); continue
    samples = []
    others = {}
    for e in mel.get_material_expressions(mat):
        cls = e.get_class().get_name()
        if "TextureSample" in cls:
            t = None
            try:
                t = e.get_editor_property("texture")
            except Exception:
                pass
            st = "?"
            try:
                st = str(e.get_editor_property("sampler_type")).split(".")[-1]
            except Exception:
                pass
            samples.append("%s[%s]" % (t.get_name() if t else "None", st))
        else:
            others[cls] = others.get(cls, 0) + 1
    top = ", ".join("%s x%d" % (k, v) for k, v in sorted(others.items(), key=lambda kv: -kv[1])[:6])
    unreal.log("WBMAT %s: TEX={%s} | NODES={%s}" % (name, "; ".join(samples), top))
unreal.log("WBMAT: fertig.")
