# B: Setzt das Gebaeude-Dachmaterial auf ZWEISEITIG, damit Daecher nicht mehr von
# einer Seite transparent sind (einseitige Geometrie -> Backface fehlt). Wirkt
# ueber die Material-Referenz auf ALLE gebackenen Daecher (kein Re-Bake noetig).
# Headless: UnrealEditor-Cmd -ExecCmds="py set_roof_twosided.py, Quit"
import unreal

PATH = "/Game/Materials/City/M_WbBuildingRoof"


def log(m):
    unreal.log("###ROOF2SIDE### %s" % m)


mat = unreal.load_asset(PATH)
if not mat:
    log("FEHLER: Material nicht ladbar: %s" % PATH)
else:
    log("geladen: %s (Klasse %s)" % (PATH, mat.get_class().get_name()))
    if isinstance(mat, unreal.Material):
        mat.set_editor_property("two_sided", True)
        unreal.MaterialEditingLibrary.recompile_material(mat)
        log("UMaterial.two_sided = %s" % mat.get_editor_property("two_sided"))
    elif isinstance(mat, unreal.MaterialInstanceConstant):
        # Bei Material-Instanzen ueber die Base-Property-Overrides.
        ov = mat.get_editor_property("base_property_overrides")
        ov.set_editor_property("override_two_sided", True)
        ov.set_editor_property("two_sided", True)
        mat.set_editor_property("base_property_overrides", ov)
        log("MI two_sided override gesetzt")
    else:
        log("WARN: unerwartete Klasse, versuche direkt two_sided")
        try:
            mat.set_editor_property("two_sided", True)
        except Exception as e:
            log("konnte two_sided nicht setzen: %s" % e)
    ok = unreal.EditorAssetLibrary.save_asset(PATH, only_if_is_dirty=False)
    log("gespeichert=%s" % ok)
