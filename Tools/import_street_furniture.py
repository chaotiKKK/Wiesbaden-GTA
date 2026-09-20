"""Importiert die Strassenmoebel-Meshes nach Unreal und weist Materialien zu.

Quelle: `Data/Raw/Moebel/*.fbx` plus `street_furniture.json`, erzeugt von
`Tools/Blender/make_street_furniture.py`.

Aufruf (VOLLER Editor - `set_material` braucht ihn, der Kommandlet-Weg
`-run=pythonscript` liefert keine gueltige Zuweisung):

    Tools\\import_street_furniture.cmd

Ergebnis: `/Game/Assets/Furniture/SM_WbFurn_*` mit je Slot einer
Material-Instanz unter `/Game/Assets/Furniture/Materials/MI_WbFurn_*`.

WARUM DIE MASSE NICHT SKALIERT WERDEN: Blenders FBX-Export schreibt
Zentimeter, UE liest Zentimeter. Ein `import_uniform_scale = 100` in der
Annahme, Blender liefere Meter, ergaebe eine 180 m lange Bank (derselbe Fehler
hat beim Kaefer ein 414-m-Auto erzeugt). Darum Faktor 1,0 - und am Ende
werden die tatsaechlichen Masse gegen das Manifest geprueft.
"""
import json
import os

import unreal

MESH_DIR = "/Game/Assets/Furniture"
MAT_DIR = "/Game/Assets/Furniture/Materials"
QUELLE = os.path.join(unreal.Paths.project_dir(), "Data", "Raw", "Moebel")

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
MP = unreal.MaterialProperty
ATH = unreal.AssetToolsHelpers.get_asset_tools()

ERGEBNIS = os.path.join(unreal.Paths.project_dir(), "import_street_furniture_result.txt")
ZEILEN = []


def log(msg):
    unreal.log("[Moebel-Import] %s" % msg)
    ZEILEN.append(str(msg))


def quit_unattended():
    if "-unattended" in unreal.SystemLibrary.get_command_line():
        unreal.SystemLibrary.quit_editor()


def build_master():
    """Ein Lack-Master mit BaseColor/Metallic/Roughness als Parameter."""
    pfad = "%s/M_WbFurniture" % MAT_DIR
    if EAL.does_asset_exist(pfad):
        EAL.delete_asset(pfad)
    mat = ATH.create_asset("M_WbFurniture", MAT_DIR, unreal.Material,
                           unreal.MaterialFactoryNew())
    farbe = MEL.create_material_expression(
        mat, unreal.MaterialExpressionVectorParameter, -400, 0)
    farbe.set_editor_property("parameter_name", "BaseColor")
    farbe.set_editor_property("default_value", unreal.LinearColor(0.5, 0.5, 0.5, 1.0))
    MEL.connect_material_property(farbe, "", MP.MP_BASE_COLOR)

    metall = MEL.create_material_expression(
        mat, unreal.MaterialExpressionScalarParameter, -400, 250)
    metall.set_editor_property("parameter_name", "Metallic")
    metall.set_editor_property("default_value", 0.0)
    MEL.connect_material_property(metall, "", MP.MP_METALLIC)

    rauh = MEL.create_material_expression(
        mat, unreal.MaterialExpressionScalarParameter, -400, 450)
    rauh.set_editor_property("parameter_name", "Roughness")
    rauh.set_editor_property("default_value", 0.4)
    MEL.connect_material_property(rauh, "", MP.MP_ROUGHNESS)

    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    return mat


def make_instance(master, name, spec):
    """Eine Material-Instanz je Slotname (WbMoebelGelb, WbMoebelRot, ...)."""
    inst_name = "MI_WbFurn_%s" % name.replace("WbMoebel", "")
    pfad = "%s/%s" % (MAT_DIR, inst_name)
    if EAL.does_asset_exist(pfad):
        EAL.delete_asset(pfad)
    inst = ATH.create_asset(inst_name, MAT_DIR, unreal.MaterialInstanceConstant,
                            unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(inst, master)
    rgb = spec["base_color"]
    MEL.set_material_instance_vector_parameter_value(
        inst, "BaseColor", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
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
    manifest_pfad = os.path.join(QUELLE, "street_furniture.json")
    if not os.path.isfile(manifest_pfad):
        log("ABBRUCH: %s fehlt - erst Tools/Blender/make_street_furniture.py laufen lassen."
            % manifest_pfad)
        return 1
    with open(manifest_pfad, encoding="utf-8") as f:
        manifest = json.load(f)

    master = build_master()
    instanzen = {name: make_instance(master, name, spec)
                 for name, spec in manifest["materials"].items()}
    log("%d Material-Instanzen am Master M_WbFurniture." % len(instanzen))

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

        # Moebel tragen keine Kollision: der Spieler laeuft ueber den Gehweg,
        # und 4.500 Kollisionskoerper kosten mehr, als sie einbringen.
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
