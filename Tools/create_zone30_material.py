"""Importiert die '30'-Markierungstextur und baut das maskierte Fahrbahnmaterial.

- Textur: Content/Textures/City/Source/T_WbZone30.png -> /Game/Textures/City/T_WbZone30
- Material: /Game/Materials/City/M_WbZone30
  * BlendMode Masked: nur die weissen Ziffern werden gemalt, der Asphalt bleibt
    ringsum sichtbar (kein zusaetzliches Straszenmaterial noetig).
  * BaseColor = Texturfarbe (weiss), OpacityMask = Textur-Alpha.

Eigenstaendig gebaut (nicht ueber build_materials.main()), damit die uebrigen
Stadtmaterialien inkl. offener WIP NICHT neu gebacken werden.

Aufruf:
  UnrealEditor.exe WiesbadenReal.uproject
      -ExecCmds="py Tools/create_zone30_material.py" -unattended -nosplash -nop4
"""

import unreal

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
MP = unreal.MaterialProperty

TEX_DIR = "/Game/Textures/City"
MAT_DIR = "/Game/Materials/City"
TEX_NAME = "T_WbZone30"
MAT_NAME = "M_WbZone30"
SRC_PNG = (unreal.Paths.project_content_dir() +
           "Textures/City/Source/T_WbZone30.png")


def log(m):
    unreal.log("###ZONE30MAT### %s" % m)


# --- Textur importieren -------------------------------------------------
task = unreal.AssetImportTask()
task.filename = SRC_PNG
task.destination_path = TEX_DIR
task.destination_name = TEX_NAME
task.automated = True
task.replace_existing = True
task.save = True
TOOLS.import_asset_tasks([task])
tex = EAL.load_asset("%s/%s" % (TEX_DIR, TEX_NAME))
if tex is None:
    log("FEHLER: Textur nicht importiert")
    raise SystemExit(1)
tex.set_editor_property("srgb", True)
EAL.save_loaded_asset(tex)
log("Textur importiert: %s/%s" % (TEX_DIR, TEX_NAME))

# --- Material bauen -----------------------------------------------------
path = "%s/%s" % (MAT_DIR, MAT_NAME)
if EAL.does_asset_exist(path):
    EAL.delete_asset(path)
mat = TOOLS.create_asset(MAT_NAME, MAT_DIR, unreal.Material,
                         unreal.MaterialFactoryNew())
if mat is None:
    log("FEHLER: Material nicht erstellt")
    raise SystemExit(1)

# Maskiert: der transparente Grund wird nicht gemalt.
mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)

sample = MEL.create_material_expression(
    mat, unreal.MaterialExpressionTextureSample, -400, 0)
sample.set_editor_property("texture", tex)
MEL.connect_material_property(sample, "RGB", MP.MP_BASE_COLOR)
MEL.connect_material_property(sample, "A", MP.MP_OPACITY_MASK)

rough = MEL.create_material_expression(
    mat, unreal.MaterialExpressionConstant, -400, 350)
rough.set_editor_property("r", 0.7)
MEL.connect_material_property(rough, "", MP.MP_ROUGHNESS)

MEL.recompile_material(mat)
EAL.save_loaded_asset(mat)
log("FERTIG: %s erstellt" % path)

if unreal.SystemLibrary.get_command_line().find("-unattended") >= 0:
    unreal.SystemLibrary.quit_editor()
