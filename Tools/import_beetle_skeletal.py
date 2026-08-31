"""Importiert das Kaefer-Skelett-Mesh fuer Chaos Vehicles.

Erzeugt wird es von Tools/Blender/rig_beetle.py: Karosserie an einer Wurzel,
vier Raeder an je einem eigenen Knochen (Wheel_FL/FR/BL/BR).

Zwei Punkte entscheiden ueber Erfolg oder Fehlersuche:

* import_uniform_scale = 1.0. Der Blender-Export liefert bereits Zentimeter.
  Mit der Vorgabe waere der Kaefer hundertfach zu gross - das ist in diesem
  Projekt schon einmal passiert (ein 175 m langes Auto).
* Ein Physik-Asset MUSS entstehen. Chaos Vehicles braucht die Koerper der
  Knochen; ohne sie startet das Fahrzeug nicht.

Aufruf (VOLLER Editor - der Importer legt das Physik-Asset nur dort an):
  UnrealEditor.exe WiesbadenReal.uproject -ExecCmds="py Tools/import_beetle_skeletal.py" -unattended -nosplash
"""

import os

import unreal

SRC = os.path.join(unreal.Paths.project_dir(), "Data", "Raw", "Beetle", "SK_VWBeetle.fbx")
DEST = "/Game/Vehicles/Beetle"

EAL = unreal.EditorAssetLibrary

# Erwartete Knochen. Stimmen sie nicht, passt spaeter die Radzuordnung nicht,
# und der Fehler zeigt sich erst als Fahrzeug, das auf der Stelle steht.
EXPECTED_BONES = ["VehicleRoot", "Wheel_FL", "Wheel_FR", "Wheel_BL", "Wheel_BR"]


def log(msg):
    unreal.log("###WBSK### %s" % msg)


if not os.path.exists(SRC):
    log("ABBRUCH: %s fehlt - erst Tools/Blender/rig_beetle.py laufen lassen." % SRC)
    raise SystemExit(1)

# Vorhandene Assets erst weg.
#
# Ein Ersetz-Import behaelt Skelett und Physik-Asset des alten Standes. Nach
# einer Aenderung an der Knochenlage waere das genau der Fall, den man nicht
# bemerkt: Das Mesh ist neu, das Skelett alt, und die Raeder sitzen falsch.
for old_asset in ("SK_VWBeetle", "SK_VWBeetle_Skeleton", "SK_VWBeetle_PhysicsAsset",
                  "PHYS_VWBeetle"):
    old_path = "%s/%s" % (DEST, old_asset)
    if EAL.does_asset_exist(old_path):
        EAL.delete_asset(old_path)
        log("entfernt: %s" % old_path)

task = unreal.AssetImportTask()
task.filename = SRC
task.destination_path = DEST
task.destination_name = "SK_VWBeetle"
task.automated = True
task.replace_existing = True
task.save = True

options = unreal.FbxImportUI()
options.set_editor_property("import_mesh", True)
options.set_editor_property("import_as_skeletal", True)
options.set_editor_property("import_materials", False)
options.set_editor_property("import_textures", False)
options.set_editor_property("import_animations", False)
options.set_editor_property("create_physics_asset", True)
options.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)

smd = options.skeletal_mesh_import_data
smd.set_editor_property("import_uniform_scale", 1.0)
smd.set_editor_property("import_morph_targets", False)
smd.set_editor_property("update_skeleton_reference_pose", False)
smd.set_editor_property("use_t0_as_ref_pose", True)
smd.set_editor_property("preserve_smoothing_groups", True)
smd.set_editor_property("normal_import_method",
                        unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
options.set_editor_property("skeletal_mesh_import_data", smd)
task.options = options

unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

path = "%s/SK_VWBeetle" % DEST
mesh = EAL.load_asset(path)
if mesh is None:
    log("ABBRUCH: Import ergab kein Asset.")
    raise SystemExit(1)

skeleton = mesh.get_editor_property("skeleton")
bounds = mesh.get_bounds()
extent = bounds.box_extent

log("Skelett-Mesh: %s" % path)
log("Masse: %.1f x %.1f x %.1f cm (halbe Ausdehnung %.1f/%.1f/%.1f)"
    % (extent.x * 2, extent.y * 2, extent.z * 2, extent.x, extent.y, extent.z))

names = []
if skeleton:
    names = [str(n) for n in skeleton.get_editor_property("bone_tree")] if False else []

# Knochen ueber die HIERARCHIE pruefen, nicht ueber eine Namensliste.
#
# Die Namen sind von Python aus nicht als Liste lesbar (bone_tree liefert nur
# undurchsichtige BoneNode-Strukturen), wohl aber ueber Eltern und Kinder. Das
# genuegt und prueft sogar mehr: dass die vier Radknochen GESCHWISTER sind.
# Waeren sie eine Kette, wuerde die Bewegung eines Rades die anderen mitziehen.
# Der Wurzelknochen heisst jetzt VehicleRoot, weil das Armature-OBJEKT so
# heisst - Blenders Exporter macht es zur Skelettwurzel. Vorher hiess es
# SK_VWBeetle, und VehicleRoot war ein zusaetzlicher Knochen darunter; der
# Fahrgestell-Koerper sass damit eine Ebene unter der Wurzel.
children = [str(c) for c in mesh.get_bone_children("VehicleRoot")]
log("Kinder von VehicleRoot: %s" % ", ".join(children) if children else "KEINE")

fehlend = [b for b in EXPECTED_BONES if b != "VehicleRoot" and b not in children]
if fehlend:
    log("ABBRUCH: Radknochen fehlen unter VehicleRoot: %s" % ", ".join(fehlend))
    raise SystemExit(1)

for bone in children:
    parent = str(mesh.get_bone_parent(bone))
    if parent != "VehicleRoot":
        log("ABBRUCH: %s haengt an %s statt an VehicleRoot." % (bone, parent))
        raise SystemExit(1)

# -- Kollisionskoerper aus der KAROSSERIE, nicht aus dem ganzen Fahrzeug ----
#
# Unreal erzeugt den Koerper aus allem, was im Mesh steckt - die Raeder
# eingeschlossen. Gemessen am fertigen Fahrzeug: Koerper von -36,5 bis
# +186,8 cm relativ zum Ursprung, Radaufstand bei -34,3 cm. Der Kasten reicht
# also 2,2 cm TIEFER als die Reifen; der Wagen ruht darauf wie auf einem
# Schlitten, die Raeder erreichen die Fahrbahn nie. Im Protokoll sah das so
# aus: vier Raeder "Luft", Motor bei 4600 Umdrehungen, Geschwindigkeit 0.
#
# Deshalb ein zweites Mesh nur mit der Karosserie
# (Tools/Blender/rig_beetle.py schreibt es mit). Sein Koerper endet ueber den
# Raedern, und da Physik-Assets ueber KNOCHENNAMEN zugeordnet werden, passt er
# anschliessend auf das vollstaendige Fahrzeug.
#
# Der Umweg ueber ein zweites Mesh ist noetig, weil die Koerper eines
# Physik-Assets von Unreals Python-Schnittstelle aus nicht erreichbar sind -
# weder ueber skeletal_body_setups noch bodies noch body_setup (geprueft).
chassis_src = os.path.join(os.path.dirname(SRC), "SK_VWBeetle_Chassis.fbx")
if os.path.exists(chassis_src):
    chassis_task = unreal.AssetImportTask()
    chassis_task.filename = chassis_src
    chassis_task.destination_path = DEST
    chassis_task.destination_name = "SK_VWBeetle_Chassis"
    chassis_task.automated = True
    chassis_task.replace_existing = True
    chassis_task.save = True
    chassis_task.options = options
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([chassis_task])

    chassis = EAL.load_asset("%s/SK_VWBeetle_Chassis" % DEST)
    chassis_subsystem = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
    if chassis and chassis_subsystem:
        extent = chassis.get_bounds().box_extent
        chassis_physics = chassis_subsystem.create_physics_asset(chassis)
        if chassis_physics:
            chassis_subsystem.assign_physics_asset(mesh, chassis_physics)
            EAL.save_loaded_asset(chassis_physics)
            EAL.save_loaded_asset(mesh)
            log("Kollisionskoerper aus der Karosserie: halbe Ausdehnung "
                "%.1f x %.1f x %.1f cm." % (extent.x, extent.y, extent.z))
        else:
            log("ACHTUNG: Karosserie-Physik-Asset liess sich nicht erzeugen.")
else:
    log("ACHTUNG: %s fehlt - der Koerper umschliesst die Raeder mit." % chassis_src)

physics = mesh.get_editor_property("physics_asset")

if physics is None:
    # Der Importer legt es im unbeaufsichtigten Lauf nicht immer an, obwohl
    # create_physics_asset gesetzt ist. Chaos Vehicles braucht die Koerper der
    # Knochen aber zwingend - ohne sie startet das Fahrzeug nicht, und der
    # Fehler zeigt sich als Auto, das im Boden steht.
    # UEBER DAS SUBSYSTEM, nicht ueber die Factory.
    #
    # `unreal.PhysicsAssetFactory` hat in UE 5.8 keine Eigenschaft, ueber die
    # sich das Ziel-Mesh setzen liesse - "Failed to find property
    # 'target_skeletal_mesh'". Die Factory legt damit ein Asset ohne Bezug an,
    # was schlimmer waere als keines.
    #
    # `SkeletalMeshEditorSubsystem::create_physics_asset` macht beides in
    # einem Schritt: Koerper aus den Knochen erzeugen und dem Mesh zuweisen.
    subsystem = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
    if subsystem is not None:
        try:
            created = subsystem.create_physics_asset(mesh)
        except Exception as e:
            created = None
            log("create_physics_asset: %s" % e)

        if created:
            subsystem.assign_physics_asset(mesh, created)
            EAL.save_loaded_asset(created)
            EAL.save_loaded_asset(mesh)
            physics = mesh.get_editor_property("physics_asset")
            log("Physik-Asset nachtraeglich angelegt.")
    else:
        log("SkeletalMeshEditorSubsystem nicht verfuegbar - im VOLLEN Editor laufen lassen.")

log("Physik-Asset: %s" % (physics.get_path_name() if physics else "FEHLT"))

log("FERTIG")

if os.environ.get("WB_QUIT"):
    unreal.SystemLibrary.quit_editor()
