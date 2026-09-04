# Verstaerkt die Normal-Map-Wirkung der AAA-Putzfassade, damit die Oberflaeche
# auch bei flachem Licht plastischer wirkt - OHNE den Fenster-Graphen (Basisfarbe)
# anzutasten. Es wird NUR der Normalen-Pfad (MP_NORMAL) angefasst:
#
#   FacadePlaster_N -> Mask(RG) * Staerke  ) -> Append -> MP_NORMAL
#   FacadePlaster_N -> Mask(B)             )
#
# Die Tangenten-XY werden skaliert (steilere Buckel), Z bleibt; die Engine
# normalisiert die Normale automatisch. Basisfarbe/Fenster bleiben unveraendert.
import unreal

mel = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
CITY = "/Game/Materials/City/"
STRENGTH = 2.2          # Tangenten-Skalierung (>1 = staerkere Relief-Wirkung)
NORMAL_TEX = "FacadePlaster_N"
MATS = ["M_WbBuildingWall", "M_WbFacade_Putz"]

for name in MATS:
    mat = unreal.load_asset(CITY + name)
    if not mat:
        unreal.log_warning("NORMBOOST %s: Material fehlt." % name); continue

    # Den FacadePlaster_N-Sample finden.
    nsample = None
    for e in mel.get_material_expressions(mat):
        if "TextureSample" in e.get_class().get_name():
            t = e.get_editor_property("texture")
            if t and t.get_name() == NORMAL_TEX:
                nsample = e
                break
    if not nsample:
        unreal.log_warning("NORMBOOST %s: kein %s-Sample gefunden - uebersprungen." % (name, NORMAL_TEX))
        continue

    px, py = nsample.get_editor_property("material_expression_editor_x"), nsample.get_editor_property("material_expression_editor_y")

    # Mask(RG) -> * Staerke
    mrg = mel.create_material_expression(mat, unreal.MaterialExpressionComponentMask, px + 250, py - 60)
    mrg.set_editor_property("r", True); mrg.set_editor_property("g", True)
    mrg.set_editor_property("b", False); mrg.set_editor_property("a", False)
    mel.connect_material_expressions(nsample, "RGB", mrg, "")
    k = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, px + 250, py + 40)
    k.set_editor_property("r", STRENGTH)
    mul = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, px + 430, py - 40)
    mel.connect_material_expressions(mrg, "", mul, "A")
    mel.connect_material_expressions(k, "", mul, "B")

    # Mask(B)
    mb = mel.create_material_expression(mat, unreal.MaterialExpressionComponentMask, px + 250, py + 120)
    mb.set_editor_property("r", False); mb.set_editor_property("g", False)
    mb.set_editor_property("b", True); mb.set_editor_property("a", False)
    mel.connect_material_expressions(nsample, "RGB", mb, "")

    # Append(XY*Staerke, Z) -> MP_NORMAL (ersetzt die bisherige Normalen-Verbindung)
    app = mel.create_material_expression(mat, unreal.MaterialExpressionAppendVector, px + 620, py + 20)
    mel.connect_material_expressions(mul, "", app, "A")
    mel.connect_material_expressions(mb, "", app, "B")
    mel.connect_material_property(app, "", unreal.MaterialProperty.MP_NORMAL)

    mel.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    unreal.log("NORMBOOST %s: Normal-Staerke x%.1f (Fenster/Basisfarbe unveraendert)." % (name, STRENGTH))

unreal.log("NORMBOOST: fertig.")
