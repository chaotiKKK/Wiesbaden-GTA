import unreal

DEST = "/Game/Vehicles/Ka52"
TEX_SRC = unreal.Paths.project_content_dir() + "Data/Raw/Ka52/textures/"
def log(m): unreal.log_warning("###WBKA52T### %s" % m)
EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()

# --- 1) Texturen importieren --------------------------------------------------
for name in ("military_helicopter_3d_model_basecolor",
             "military_helicopter_3d_model_normal",
             "military_helicopter_3d_model_roughness",
             "military_helicopter_3d_model_metallic"):
    src = TEX_SRC + name + ".JPEG"
    dst = DEST + "/T_" + name
    if EAL.does_asset_exist(dst):
        log("Textur existiert: %s" % dst)
        continue
    t = unreal.AssetImportTask()
    t.filename = src
    t.destination_path = DEST
    t.destination_name = "T_" + name
    t.automated = True
    t.replace_existing = True
    t.save = True
    AT.import_asset_tasks([t])
    log("Textur importiert: %s" % dst)

# Normal-Map auf Normal-Compression setzen
tex_normal = EAL.load_asset(DEST + "/T_military_helicopter_3d_model_normal")
if tex_normal:
    tex_normal.set_editor_property("compression_settings",
        unreal.TextureCompressionSettings.TC_NORMALMAP)
    EAL.save_loaded_asset(tex_normal)

# --- 2) Material fuer Rumpf+Rotoren bauen (eine Instanz reicht: gleiches PBR)
MAT = DEST + "/M_Ka52PBR"
mat = None
if EAL.does_asset_exist(MAT):
    mat = EAL.load_asset(MAT)
else:
    factory = unreal.MaterialFactoryNew()
    mat = AT.create_asset("M_Ka52PBR", DEST, unreal.Material, factory)

mvl = unreal.MaterialEditingLibrary
try:
    nodes = mvl.get_material_property_input_nodes(mat, unreal.MaterialProperty.MP_BASE_COLOR)
except AttributeError:
    # 5.8: Property nicht verfuegbar - als 'noch nicht verdrahtet' behandeln
    nodes = []
if not nodes:
    def tex_sample(name, x, y, normal=False):
        e = mvl.create_material_expression(mat, unreal.MaterialExpressionTextureSample, x, y)
        e.set_editor_property("texture", EAL.load_asset(DEST + "/T_military_helicopter_3d_model_" + name))
        if normal:
            try:
                e.set_editor_property("sampler_type", unreal.TextureSamplerType.SAMPLERTYPE_NORMAL)
            except AttributeError:
                pass  # Property existiert nicht in 5.8 - Compression-Flag der Textur reicht
        return e

    tc_base = tex_sample("basecolor", -600, -200)
    tc_norm = tex_sample("normal", -600, 100, normal=True)
    tc_rough = tex_sample("roughness", -600, 400)
    tc_metal = tex_sample("metallic", -600, 700)

    mvl.connect_material_property(tc_base, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
    mvl.connect_material_property(tc_norm, "RGB", unreal.MaterialProperty.MP_NORMAL)
    mvl.connect_material_property(tc_rough, "RGB", unreal.MaterialProperty.MP_ROUGHNESS)
    mvl.connect_material_property(tc_metal, "RGB", unreal.MaterialProperty.MP_METALLIC)
    mvl.recompile_material(mat)
    log("M_Ka52PBR verdrahtet")
else:
    log("M_Ka52PBR hatte bereits Base-Color-Input")

# FALLE (2026-09-17): create_asset legt das Material nur IM SPEICHER an. Ohne
# diesen Save existiert /Game/Vehicles/Ka52/M_Ka52PBR auf der Platte NICHT -
# die Meshes werden trotzdem gespeichert und zeigen dann auf ein fehlendes
# Asset (grauer Rumpf, Texturen ungenutzt). Der Log meldete damals Erfolg.
if not EAL.save_loaded_asset(mat):
    log("FEHLER: M_Ka52PBR liess sich nicht speichern")
else:
    log("M_Ka52PBR gespeichert: %s" % ("vorhanden" if EAL.does_asset_exist(MAT) else "FEHLT TROTZDEM"))

# --- 3) Material an den Meshes setzen (ALLE Slots) ---------------------------
#
# Der FBX-Import legt je Mesh einen Slot mit dem Tripo-Restmaterial an; beim
# Rotor sind es teils mehrere (Nabe/Blatt). Alle Slots bekommen dasselbe PBR -
# ein leeres Restmaterial wuerde sonst einzelne Faces grau lassen.
def slot_material_names(asset):
    names = []
    for entry in asset.get_editor_property("static_materials"):
        mi = entry.get_editor_property("material_interface")
        names.append(mi.get_name() if mi else "LEER")
    return names


for mesh_name in ("Fuselage", "Rotor_Upper", "Rotor_Lower"):
    sm = EAL.load_asset("%s/%s.%s" % (DEST, mesh_name, mesh_name))
    if sm is None:
        log("FEHLER Mesh fehlt: %s" % mesh_name)
        continue
    slots = sm.get_editor_property("static_materials")
    for i in range(len(slots)):
        sm.set_material(i, mat)
    EAL.save_loaded_asset(sm)
    log("%s -> %s (Slots: %s)" % (mesh_name, mat.get_name(), ", ".join(slot_material_names(sm))))

log("ENDE")

log("ENDE")
