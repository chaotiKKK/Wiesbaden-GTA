"""Importiert die animierte Sebbo-Spielfigur: Skelett-Mesh plus Bewegungen.

Erzeugt wird beides von Tools/Blender/rig_sebbo.py: neun Knochen (Root,
Hips, Spine, je Bein Thigh/Shin/Foot) und drei Bewegungen (Sebbo_Idle,
Sebbo_Walk, Sebbo_Swing) in EINER FBX-Datei.

Die Materialien kommen NICHT aus dem FBX, sondern werden vom bereits
importierten statischen Modell uebernommen (MI_Sebbo_Scan/Hose/Schuhe aus
Tools/import_sebbo.py) - dort stimmen Abtasttypen und Texturen schon.

Aufruf (VOLLER Editor):
  UnrealEditor.exe WiesbadenReal.uproject
      -ExecCmds="py Tools/import_sebbo_skeletal.py" -unattended -nosplash
"""

import os

import unreal

SRC = os.path.join(unreal.Paths.project_dir(), "Data", "Raw", "Sebbo", "SK_Sebbo.fbx")
DEST = "/Game/Assets/People"
MAT_DIR = "/Game/Materials/People"

EAL = unreal.EditorAssetLibrary

EXPECTED_BONES = ["Root", "Hips", "Spine",
                  "Thigh_L", "Shin_L", "Foot_L",
                  "Thigh_R", "Shin_R", "Foot_R"]

EXPECTED_ANIMS = ["Sebbo_Idle", "Sebbo_Walk", "Sebbo_Swing"]


def log(msg):
    unreal.log("###WBSEBBOSK### %s" % msg)


if not os.path.exists(SRC):
    log("ABBRUCH: %s fehlt - erst Tools/Blender/rig_sebbo.py laufen lassen." % SRC)
    raise SystemExit(1)

# Alte Assets weg - ein Ersetz-Import behaelt sonst das alte Skelett.
for old_asset in ("SK_Sebbo", "SK_Sebbo_Skeleton", "SK_Sebbo_PhysicsAsset"):
    old_path = "%s/%s" % (DEST, old_asset)
    if EAL.does_asset_exist(old_path):
        EAL.delete_asset(old_path)
        log("entfernt: %s" % old_path)
for anim in EXPECTED_ANIMS:
    for candidate in ("%s/%s" % (DEST, anim), "%s/SK_Sebbo_Anim" % DEST):
        if EAL.does_asset_exist(candidate):
            EAL.delete_asset(candidate)
            log("entfernt: %s" % candidate)

task = unreal.AssetImportTask()
task.filename = SRC
task.destination_path = DEST
task.destination_name = "SK_Sebbo"
task.automated = True
task.replace_existing = True
task.save = True

options = unreal.FbxImportUI()
options.set_editor_property("import_mesh", True)
options.set_editor_property("import_textures", False)
options.set_editor_property("import_materials", False)
options.set_editor_property("import_as_skeletal", True)
options.set_editor_property("import_animations", True)
options.set_editor_property("mesh_type_to_import",
                            unreal.FBXImportType.FBXIT_SKELETAL_MESH)

smd = options.skeletal_mesh_import_data
# Der Export liefert bereits Zentimeter; ein zweiter Faktor hier hat beim
# Kaefer ein 175 m langes Auto ergeben.
smd.set_editor_property("import_uniform_scale", 1.0)
smd.set_editor_property("import_morph_targets", False)
smd.set_editor_property("update_skeleton_reference_pose", False)
smd.set_editor_property("normal_import_method",
                        unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS)

anim = options.anim_sequence_import_data
anim.set_editor_property("import_uniform_scale", 1.0)
anim.set_editor_property("animation_length",
                         unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
anim.set_editor_property("remove_redundant_keys", False)

task.options = options
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

mesh = EAL.load_asset("%s/SK_Sebbo" % DEST)
if mesh is None:
    log("ABBRUCH: Import fehlgeschlagen.")
    raise SystemExit(1)

# -- Knochen pruefen ---------------------------------------------------------
skeleton = mesh.get_editor_property("skeleton")
if skeleton is None:
    log("ABBRUCH: kein Skelett entstanden.")
    raise SystemExit(1)

# -- Materialien vom statischen Modell uebernehmen ---------------------------
#
# Zuordnung nach NAMEN, nicht nach Reihenfolge - dieselbe Regel wie beim
# statischen Import, aus demselben Grund: Blender sortiert die Schlitze um.
instances = {
    "Sebbo_Scan": EAL.load_asset("%s/MI_Sebbo_Scan" % MAT_DIR),
    "Sebbo_Hose": EAL.load_asset("%s/MI_Sebbo_Hose" % MAT_DIR),
    "Sebbo_Schuhe": EAL.load_asset("%s/MI_Sebbo_Schuhe" % MAT_DIR),
}

materials = list(mesh.get_editor_property("materials"))
for i, slot in enumerate(materials):
    name = str(slot.get_editor_property("material_slot_name"))
    chosen = None
    for key, inst in instances.items():
        if inst is not None and key.lower() in name.lower():
            chosen = inst
            break
    if chosen is None:
        chosen = instances.get("Sebbo_Scan")
        log("Schlitz %d '%s' nicht zuzuordnen - Scanmaterial gesetzt" % (i, name))
    slot.set_editor_property("material_interface", chosen)
    log("Schlitz %d '%s' -> %s" % (i, name, chosen.get_name() if chosen else "-"))
mesh.set_editor_property("materials", materials)
EAL.save_loaded_asset(mesh)

# -- Bewegungen pruefen ------------------------------------------------------
found = []
for anim_name in EXPECTED_ANIMS:
    for candidate in ("%s/%s" % (DEST, anim_name),
                      "%s/SK_Sebbo_%s" % (DEST, anim_name)):
        if EAL.does_asset_exist(candidate):
            found.append(candidate)
            break

log("Mesh: %s/SK_Sebbo" % DEST)
log("Bewegungen gefunden: %d von %d (%s)"
    % (len(found), len(EXPECTED_ANIMS), ", ".join(found)))

if len(found) < len(EXPECTED_ANIMS):
    # Alles auflisten, was der Import stattdessen angelegt hat - der
    # FBX-Importer haengt Praefixe an, und geraten wird hier nicht.
    everything = [a for a in EAL.list_assets(DEST, recursive=False)]
    log("Inhalt von %s: %s" % (DEST, ", ".join(everything)))

log("FERTIG")

if unreal.SystemLibrary.get_command_line().find("-unattended") >= 0:
    unreal.SystemLibrary.quit_editor()
