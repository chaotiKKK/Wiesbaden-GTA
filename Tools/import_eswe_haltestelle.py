"""Importiert die ESWE-Haltestelle (Wartehalle, Haltemast, DFI-Stele) nach Unreal.

Quelle: `Data/Raw/EsweHalte/*.fbx` plus `eswe_haltestelle.json`, erzeugt von
`Tools/Blender/make_eswe_haltestelle.py`. Aufruf (VOLLER Editor, wie bei den
Strassenmoebeln - `set_material` braucht ihn):

    Tools/import_eswe_haltestelle.cmd

Ergebnis: `/Game/Props/EsweHalte/SM_WbEswe*` mit je Slot einer Material-
Instanz unter `/Game/Props/EsweHalte/Materials/MI_WbEswe*`. Drei Master: Lack
(deckend), Glas (durchsichtig) und Leucht (City-Light-Vitrine, unbeleuchtet
selbstleuchtend). Masse wie bei den Moebeln OHNE Skalierung (Blender-FBX
schreibt Zentimeter); am Ende gegen das Manifest geprueft, dazu die
Seitenlage: "vom Bordstein weg" muss im Unreal-Mesh auf +Y liegen (die
Wartehalle reicht dann von etwa -30 bis +160 cm).
"""
import json
import os

import unreal

MESH_DIR = "/Game/Props/EsweHalte"
MAT_DIR = "/Game/Props/EsweHalte/Materials"
QUELLE = os.path.join(unreal.Paths.project_dir(), "Data", "Raw", "EsweHalte")

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
MP = unreal.MaterialProperty
ATH = unreal.AssetToolsHelpers.get_asset_tools()

ERGEBNIS = os.path.join(unreal.Paths.project_dir(), "import_eswe_haltestelle_result.txt")
ZEILEN = []


def log(msg):
    unreal.log("[ESWE-Import] %s" % msg)
    ZEILEN.append(str(msg))


def quit_unattended():
    if "-unattended" in unreal.SystemLibrary.get_command_line():
        unreal.SystemLibrary.quit_editor()


def build_master(shader):
    """Master je Shader-Art mit BaseColor/Metallic/Roughness als Parameter."""
    name = {"lack": "M_WbEsweLack", "glas": "M_WbEsweGlas", "leucht": "M_WbEsweLeucht"}[shader]
    pfad = "%s/%s" % (MAT_DIR, name)
    if EAL.does_asset_exist(pfad):
        EAL.delete_asset(pfad)
    mat = ATH.create_asset(name, MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
    farbe = MEL.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -400, 0)
    farbe.set_editor_property("parameter_name", "BaseColor")
    farbe.set_editor_property("default_value", unreal.LinearColor(0.5, 0.5, 0.5, 1.0))
    if shader == "leucht":
        # Hinterleuchtetes Plakat: unbeleuchtet, nachts wie tags gleich hell.
        mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
        mal = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -150, 0)
        mal.set_editor_property("const_b", 2.0)
        MEL.connect_material_expressions(farbe, "", mal, "A")
        MEL.connect_material_property(mal, "", MP.MP_EMISSIVE_COLOR)
    else:
        MEL.connect_material_property(farbe, "", MP.MP_BASE_COLOR)
        metall = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -400, 250)
        metall.set_editor_property("parameter_name", "Metallic")
        metall.set_editor_property("default_value", 0.0)
        MEL.connect_material_property(metall, "", MP.MP_METALLIC)
        rauh = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -400, 450)
        rauh.set_editor_property("parameter_name", "Roughness")
        rauh.set_editor_property("default_value", 0.4)
        MEL.connect_material_property(rauh, "", MP.MP_ROUGHNESS)
    if shader == "glas":
        # Klarglas: durchsichtig, zweiseitig (von innen wie aussen sichtbar).
        mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
        mat.set_editor_property("two_sided", True)
        deck = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, -400, 650)
        deck.set_editor_property("r", 0.22)
        MEL.connect_material_property(deck, "", MP.MP_OPACITY)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    return mat


def make_instance(master, name, spec):
    """Eine Material-Instanz je Slotname (WbMoebelGelb, WbMoebelRot, ...)."""
    inst_name = "MI_%s" % name
    pfad = "%s/%s" % (MAT_DIR, inst_name)
    if EAL.does_asset_exist(pfad):
        EAL.delete_asset(pfad)
    inst = ATH.create_asset(inst_name, MAT_DIR, unreal.MaterialInstanceConstant,
                            unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(inst, master)
    rgb = spec["base_color"]
    MEL.set_material_instance_vector_parameter_value(
        inst, "BaseColor", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
    if spec.get("shader", "lack") != "leucht":
        MEL.set_material_instance_scalar_parameter_value(inst, "Metallic", spec["metallic"])
        MEL.set_material_instance_scalar_parameter_value(inst, "Roughness", spec["roughness"])
    EAL.save_loaded_asset(inst)
    return inst


def import_tasks(manifest):
    tasks = []
    for eintrag in manifest["meshes"]:
        fbx = os.path.join(QUELLE, eintrag["fbx"])
        if not os.path.isfile(fbx):
            log("FEHLT: %s" % fbx)
            continue
        ziel = "%s/%s" % (MESH_DIR, eintrag["name"])
        if EAL.does_asset_exist(ziel):
            EAL.delete_asset(ziel)

        task = unreal.AssetImportTask()
        task.filename = fbx
        task.destination_path = MESH_DIR
        task.destination_name = eintrag["name"]
        task.automated = True
        task.replace_existing = True
        task.save = True

        optionen = unreal.FbxImportUI()
        optionen.set_editor_property("import_mesh", True)
        optionen.set_editor_property("import_textures", False)
        optionen.set_editor_property("import_materials", False)
        optionen.set_editor_property("import_as_skeletal", False)
        optionen.set_editor_property("mesh_type_to_import",
                                     unreal.FBXImportType.FBXIT_STATIC_MESH)
        smd = unreal.FbxStaticMeshImportData()
        smd.set_editor_property("import_uniform_scale", 1.0)
        smd.set_editor_property("combine_meshes", True)
        smd.set_editor_property("generate_lightmap_u_vs", True)
        smd.set_editor_property("auto_generate_collision", False)
        optionen.set_editor_property("static_mesh_import_data", smd)
        task.options = optionen
        tasks.append((task, eintrag))
    return tasks


def main():
    manifest_pfad = os.path.join(QUELLE, "eswe_haltestelle.json")
    if not os.path.isfile(manifest_pfad):
        log("ABBRUCH: %s fehlt - erst Tools/Blender/make_eswe_haltestelle.py laufen lassen."
            % manifest_pfad)
        return 1
    with open(manifest_pfad, encoding="utf-8") as f:
        manifest = json.load(f)

    master = {s: build_master(s) for s in ("lack", "glas", "leucht")}
    instanzen = {name: make_instance(master[spec.get("shader", "lack")], name, spec)
                 for name, spec in manifest["materials"].items()}
    log("%d Material-Instanzen an 3 Mastern (Lack/Glas/Leucht)." % len(instanzen))

    tasks = import_tasks(manifest)
    if not tasks:
        log("ABBRUCH: keine FBX gefunden in %s" % QUELLE)
        return 1
    ATH.import_asset_tasks([t for t, _ in tasks])

    fehler = 0
    for _task, eintrag in tasks:
        mesh = EAL.load_asset("%s/%s" % (MESH_DIR, eintrag["name"]))
        if mesh is None:
            log("FEHLER: %s nicht importiert" % eintrag["name"])
            fehler += 1
            continue

        slots = mesh.get_editor_property("static_materials")
        for i, slot in enumerate(slots):
            slot_name = str(slot.get_editor_property("material_slot_name"))
            inst = instanzen.get(slot_name)
            if inst is None:
                # Blender haengt bei mehreren Slots ein ".001" an - der
                # Grundname bleibt aber eindeutig.
                inst = instanzen.get(slot_name.split(".")[0])
            if inst is None:
                log("  %s Slot %d '%s' ohne Material" % (eintrag["name"], i, slot_name))
                fehler += 1
                continue
            mesh.set_material(i, inst)

        # Kollision: die Wartehalle stoppt den Spieler an Glas und Pfosten, man
        # kann aber HINEIN (vorn offen) - darum die Dreiecke selbst als
        # Kollision (ein Huellkoerper wuerde den Innenraum versperren). Mast
        # und Stele tragen keine.
        body = mesh.get_editor_property("body_setup")
        if eintrag["name"] == "SM_WbEsweWartehalle" and body is not None:
            body.set_editor_property("collision_trace_flag",
                                     unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
        else:
            mesh.set_editor_property("body_setup", None)
        EAL.save_loaded_asset(mesh)

        box = mesh.get_bounding_box()
        groesse = box.max - box.min
        soll_hoehe = eintrag["hoehe_m"] * 100.0
        abweichung = abs(groesse.z - soll_hoehe)
        marke = "" if abweichung <= 2.0 else "  <== MASS WEICHT AB (soll %.0f cm)" % soll_hoehe
        if abweichung > 2.0:
            fehler += 1
        log("%-26s %6.0f x %6.0f x %6.0f cm, %d Slots%s"
            % (eintrag["name"], groesse.x, groesse.y, groesse.z, len(slots), marke))
        # Seitenlage: die Wartehalle muss nach +Y reichen (vom Bordstein weg).
        log("    Unreal-Box X %.0f..%.0f  Y %.0f..%.0f  (Blender Y %.0f..%.0f cm)"
            % (box.min.x, box.max.x, box.min.y, box.max.y,
               eintrag["y_min_m"] * 100.0, eintrag["y_max_m"] * 100.0))

    log("Fertig: %d Meshes, %d Fehler." % (len(tasks), fehler))
    return fehler


if __name__ == "__main__":
    try:
        code = main()
    except Exception as fehler:      # noqa: BLE001 - Ergebnis muss in die Datei
        log("AUSNAHME: %s" % fehler)
        code = 1
    # Unreal-Python-Ausgaben erreichen den cmd-Strom nicht zuverlaessig -
    # das Ergebnis muss in eine Datei, sonst ist der Lauf nicht beurteilbar.
    with open(ERGEBNIS, "w", encoding="utf-8") as f:
        f.write("\n".join(ZEILEN) + "\n")
    quit_unattended()
