"""Importiert die fuenf Dachaufbauten des SebboTower (Satellitenschuessel,
Antennenmast, Dachreklame, Magazinstaender, Topfpflanze) und ihre Materialien.

Quelle der Meshes: Tools/Blender/make_sebbo_dach.py -> Data/Raw/SebboTower/
(FBX + sebbo_dach.json). Das Manifest nennt je Materialslot Grundfarbe und
ART; daraus entstehen hier drei Sorten Material:

  * Volltonlack (Metall/Weiss/Antrazit/Rot/Marine/Kuebel/Erde) - EIN Master
    M_WbSebo_Paint mit Farb-, Metall- und Rauheitsparameter, je Slot eine
    Instanz.
  * Blattwerk (ART `foliage`) - Master M_WbSebo_Blatt, GLEICHE Parameter,
    aber ZWEISEITIG. Das Blatt der Pflanze ist ein einzelner Streifen aus
    sieben Vierecken; ohne Two Sided waere die Pflanze von der einen Seite
    aus leer (Unreal cullt einseitige Flaechen, im Blender-Render sieht man
    das nie).
  * Aufgemalte Grafik (Wortmarke, AG-Logo, Magazin-Cover) - maskiertes,
    zweiseitiges Master M_WbSebo_Decal mit den transparenten PNG aus
    Tools/make_sebbo_dach_textures.py (Slots `SbLogo`, `SbLogoAG`, `MgCover`).

Zugeordnet wird ueber den SLOTNAMEN (aus dem FBX uebernommen), nicht ueber den
Index - so ist die Zuordnung unabhaengig von der Slotreihenfolge des Importers.

Anders als beim Nerobergbahn-Ensemble bleiben die Meshes MIT Kollision: auf
dem Dach laeuft der Spieler herum, Mast und Schuessel sollen nicht passierbar
sein. Auto-Kollision (Konvex) genuegt fuer diese einfachen Formen. Fuer die
Pflanze ist die Konvexhuelle der Kuebel plus Blattwerk eine grobe, aber
tragfaehige Annahme: sie steht am Dachrand, nicht im Laufweg.

Aufruf (im vollen Editor, nicht -run=pythonscript - set_material braucht ihn):
  UnrealEditor.exe WiesbadenReal.uproject
      -ExecCmds="py Tools/import_sebbo_dach.py" -unattended -nosplash -nop4
"""

import json
import os

import unreal

SRC = os.path.join(unreal.Paths.project_dir(), "Data", "Raw", "SebboTower")
MESH_DIR = "/Game/SebboTower/Meshes"
MAT_DIR = "/Game/SebboTower/Materials"
# Logo-PNG: Quelle im Projekt-Content, erzeugt von
# Tools/make_sebbo_dach_textures.py, importiert nach /Game/SebboTower/Textures.
LOGO_SRC = os.path.join(unreal.Paths.project_content_dir(),
                        "SebboTower", "Textures", "Source")
LOGO_DIR = "/Game/SebboTower/Textures"

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
MP = unreal.MaterialProperty
ATH = unreal.AssetToolsHelpers.get_asset_tools()

# Volltonlack-Sorte -> (Metallic, Roughness).
PAINT_PBR = {
    "paint": (0.10, 0.32),
    "glass": (0.05, 0.12),
    "metal": (0.90, 0.28),
    "dark":  (0.20, 0.55),
    "timber": (0.0, 0.80),
    "gravel": (0.0, 0.90),
    # Blattwerk: nichts metallisch, sehr rau - und NICHT cullen, siehe
    # build_blatt_master().
    "foliage": (0.0, 0.72),
}


def log(msg):
    unreal.log("###WBSDIMP### %s" % msg)


def build_paint_master():
    path = "%s/M_WbSebo_Paint" % MAT_DIR
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    mat = ATH.create_asset("M_WbSebo_Paint", MAT_DIR, unreal.Material,
                           unreal.MaterialFactoryNew())
    color = MEL.create_material_expression(
        mat, unreal.MaterialExpressionVectorParameter, -400, 0)
    color.set_editor_property("parameter_name", "BaseColor")
    color.set_editor_property("default_value", unreal.LinearColor(0.5, 0.5, 0.5, 1.0))
    MEL.connect_material_property(color, "", MP.MP_BASE_COLOR)

    metal = MEL.create_material_expression(
        mat, unreal.MaterialExpressionScalarParameter, -400, 250)
    metal.set_editor_property("parameter_name", "Metallic")
    metal.set_editor_property("default_value", 0.1)
    MEL.connect_material_property(metal, "", MP.MP_METALLIC)

    rough = MEL.create_material_expression(
        mat, unreal.MaterialExpressionScalarParameter, -400, 450)
    rough.set_editor_property("parameter_name", "Roughness")
    rough.set_editor_property("default_value", 0.35)
    MEL.connect_material_property(rough, "", MP.MP_ROUGHNESS)

    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    return mat


def build_blatt_master():
    """Zweiseitiges Master fuer das Blattwerk der Topfpflanze.

    Sonst identisch zu build_paint_master(), nur `two_sided`: die Blaetter
    sind einzelne Streifen aus wenigen Vierecken, kein geschlossener Koerper.
    Unreal cullt die Rueckseite, dann fehlt von der einen Seite die halbe
    Pflanze - und im Blender-Kontrollrender ist das nicht zu sehen, weil
    EEVEE nicht cullt.
    """
    path = "%s/M_WbSebo_Blatt" % MAT_DIR
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    mat = ATH.create_asset("M_WbSebo_Blatt", MAT_DIR, unreal.Material,
                           unreal.MaterialFactoryNew())
    mat.set_editor_property("two_sided", True)

    color = MEL.create_material_expression(
        mat, unreal.MaterialExpressionVectorParameter, -400, 0)
    color.set_editor_property("parameter_name", "BaseColor")
    color.set_editor_property("default_value", unreal.LinearColor(0.1, 0.2, 0.05, 1.0))
    MEL.connect_material_property(color, "", MP.MP_BASE_COLOR)

    metal = MEL.create_material_expression(
        mat, unreal.MaterialExpressionScalarParameter, -400, 250)
    metal.set_editor_property("parameter_name", "Metallic")
    metal.set_editor_property("default_value", 0.0)
    MEL.connect_material_property(metal, "", MP.MP_METALLIC)

    rough = MEL.create_material_expression(
        mat, unreal.MaterialExpressionScalarParameter, -400, 450)
    rough.set_editor_property("parameter_name", "Roughness")
    rough.set_editor_property("default_value", 0.72)
    MEL.connect_material_property(rough, "", MP.MP_ROUGHNESS)

    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    return mat


def build_decal_master():
    """Maskiertes, zweiseitiges Master-Material fuer die Wortmarke.

    Der Grund der Textur ist vollstaendig transparent, gemalt sind nur die
    weissen Buchstaben (OpacityMask aus dem Alpha). Zweiseitig, weil die
    Flaeche 1 cm vor der Schildplatte liegt und aus beiden Blickrichtungen
    sauber aussehen muss.
    """
    path = "%s/M_WbSebo_Decal" % MAT_DIR
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    mat = ATH.create_asset("M_WbSebo_Decal", MAT_DIR, unreal.Material,
                           unreal.MaterialFactoryNew())
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    mat.set_editor_property("two_sided", True)

    sample = MEL.create_material_expression(
        mat, unreal.MaterialExpressionTextureSampleParameter2D, -560, 0)
    sample.set_editor_property("parameter_name", "BaseColorTex")
    sample.set_editor_property("sampler_type",
                               unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    MEL.connect_material_property(sample, "RGB", MP.MP_BASE_COLOR)
    MEL.connect_material_property(sample, "A", MP.MP_OPACITY_MASK)

    metal = MEL.create_material_expression(
        mat, unreal.MaterialExpressionScalarParameter, -560, 300)
    metal.set_editor_property("parameter_name", "Metallic")
    metal.set_editor_property("default_value", 0.05)
    MEL.connect_material_property(metal, "", MP.MP_METALLIC)

    rough = MEL.create_material_expression(
        mat, unreal.MaterialExpressionScalarParameter, -560, 480)
    rough.set_editor_property("parameter_name", "Roughness")
    rough.set_editor_property("default_value", 0.45)
    MEL.connect_material_property(rough, "", MP.MP_ROUGHNESS)

    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    return mat


def import_textures(assets):
    """Importiert die im Manifest genannten Texturen; Name -> Textur-Asset."""
    namen = []
    for asset in assets:
        for m in asset["materials"]:
            t = m.get("texture")
            if t and t not in namen:
                namen.append(t)

    texturen = {}
    for name in namen:
        quelle = os.path.join(LOGO_SRC, name)
        if not os.path.exists(quelle):
            log("WARNUNG: Textur fehlt: %s - erst Tools/make_sebbo_dach_"
                "textures.py laufen lassen." % quelle)
            continue
        ziel = os.path.splitext(name)[0]
        task = unreal.AssetImportTask()
        task.filename = quelle
        task.destination_path = LOGO_DIR
        task.destination_name = ziel
        task.automated = True
        task.replace_existing = True
        task.save = True
        ATH.import_asset_tasks([task])
        tex = EAL.load_asset("%s/%s" % (LOGO_DIR, ziel))
        if tex is None:
            log("WARNUNG: Textur nicht importiert: %s" % name)
            continue
        # sRGB: die PNG-Farbe ist bereits der sRGB-Wert der Lackfarbe.
        tex.set_editor_property("srgb", True)
        EAL.save_loaded_asset(tex)
        texturen[name] = tex
        log("Textur importiert: %s/%s" % (LOGO_DIR, ziel))
    return texturen


def make_decal_instance(master, spec, texturen):
    slot = spec["name"]
    name = "MI_Sb_%s" % slot
    path = "%s/%s" % (MAT_DIR, name)
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    inst = ATH.create_asset(name, MAT_DIR, unreal.MaterialInstanceConstant,
                            unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(inst, master)
    tex = texturen.get(spec.get("texture"))
    if tex is None:
        log("WARNUNG: %s ohne Textur (Manifest-Eintrag 'texture' fehlt)" % name)
    else:
        MEL.set_material_instance_texture_parameter_value(inst, "BaseColorTex", tex)
    MEL.set_material_instance_scalar_parameter_value(inst, "Metallic", 0.05)
    MEL.set_material_instance_scalar_parameter_value(inst, "Roughness", 0.45)
    EAL.save_loaded_asset(inst)
    return inst


def make_paint_instance(master, blatt_master, slot):
    """Materialinstanz eines Volltons - je nach ART mit anderem Master."""
    art = slot["kind"]
    name = "MI_Sb_%s" % slot["name"]
    path = "%s/%s" % (MAT_DIR, name)
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    inst = ATH.create_asset(name, MAT_DIR, unreal.MaterialInstanceConstant,
                            unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(
        inst, blatt_master if art == "foliage" else master)
    rgb = slot["base_color"]
    MEL.set_material_instance_vector_parameter_value(
        inst, "BaseColor", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
    metal, rough = PAINT_PBR[art]
    MEL.set_material_instance_scalar_parameter_value(inst, "Metallic", metal)
    MEL.set_material_instance_scalar_parameter_value(inst, "Roughness", rough)
    EAL.save_loaded_asset(inst)
    return inst


def main():
    manifest_path = os.path.join(SRC, "sebbo_dach.json")
    if not os.path.exists(manifest_path):
        log("ABBRUCH: %s fehlt - erst make_sebbo_dach.py laufen lassen."
            % manifest_path)
        raise SystemExit(1)
    with open(manifest_path, "r", encoding="utf-8") as f:
        assets = json.load(f)["assets"]

    paint_master = build_paint_master()
    blatt_master = build_blatt_master()
    decal_master = build_decal_master()
    texturen = import_textures(assets)
    log("Master-Materialien angelegt (Lack, Blatt, Grafik).")

    tasks = []
    for asset in assets:
        task = unreal.AssetImportTask()
        task.filename = os.path.join(SRC, asset["fbx"])
        task.destination_path = MESH_DIR
        task.destination_name = asset["name"]
        task.automated = True
        task.replace_existing = True
        task.save = True

        options = unreal.FbxImportUI()
        options.set_editor_property("import_mesh", True)
        options.set_editor_property("import_textures", False)
        options.set_editor_property("import_materials", False)
        options.set_editor_property("import_as_skeletal", False)
        options.set_editor_property("mesh_type_to_import",
                                    unreal.FBXImportType.FBXIT_STATIC_MESH)
        smd = options.static_mesh_import_data
        smd.set_editor_property("import_uniform_scale", 1.0)
        smd.set_editor_property("combine_meshes", True)
        smd.set_editor_property("generate_lightmap_u_vs", True)
        # MIT Kollision: der Spieler laeuft auf dem Dach herum.
        smd.set_editor_property("auto_generate_collision", True)
        options.set_editor_property("static_mesh_import_data", smd)
        task.options = options
        tasks.append((task, asset))

    ATH.import_asset_tasks([t for t, _ in tasks])

    paint_cache = {}
    decal_cache = {}

    for task, asset in tasks:
        mesh_path = "%s/%s" % (MESH_DIR, asset["name"])
        mesh = EAL.load_asset(mesh_path)
        if mesh is None:
            log("Import fehlgeschlagen: %s" % asset["name"])
            continue

        slot_kind = {m["name"]: m for m in asset["materials"]}
        statics = mesh.get_editor_property("static_materials")
        for i, sm in enumerate(statics):
            slot_name = str(sm.get_editor_property("material_slot_name"))
            spec = slot_kind.get(slot_name)
            if spec is None:
                log("  %s Slot %d '%s' ohne Manifest-Eintrag"
                    % (asset["name"], i, slot_name))
                continue
            if spec["kind"] == "decal":
                if slot_name not in decal_cache:
                    decal_cache[slot_name] = make_decal_instance(
                        decal_master, spec, texturen)
                inst = decal_cache[slot_name]
            else:
                if slot_name not in paint_cache:
                    paint_cache[slot_name] = make_paint_instance(
                        paint_master, blatt_master, spec)
                inst = paint_cache[slot_name]
            mesh.set_material(i, inst)
            log("  %-22s Slot %d %-12s -> %s" % (asset["name"], i,
                                                 slot_name, inst.get_name()))

        EAL.save_loaded_asset(mesh)

        bb = mesh.get_bounding_box()
        size = bb.max - bb.min
        log("%-24s importiert, Groesse %.0f x %.0f x %.0f cm, %d Slots"
            % (asset["name"], size.x, size.y, size.z, len(statics)))

    # Abschluss-Sentinel: das Editor-Protokoll erreicht den stdout-Redirect
    # nicht zuverlaessig, eine Datei schon.
    try:
        with open(os.path.join(SRC, "import_done.txt"), "w", encoding="utf-8") as f:
            f.write("ok\n")
    except OSError as exc:
        log("Sentinel nicht geschrieben: %s" % exc)

    log("FERTIG.")
    if os.environ.get("WB_QUIT"):
        unreal.SystemLibrary.quit_editor()


main()
