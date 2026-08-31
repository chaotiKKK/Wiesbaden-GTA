"""Importiert die Herbie-Lackierung fuer das SPIELERAUTO.

Quelle: Data/Raw/Beetle/Herbie (Tools/Blender/make_herbie.py) - vier
Albedo-Texturen mit Rennstreifen und Startnummer 53, gebacken auf die
UV-Abwicklung des Kaefers.

Erzeugt je Kachel eine Materialinstanz MI_VWBeetleHerbie_<Kachel>: Kopie der
vorhandenen MI_VWBeetle_<Kachel> (gleiche Normal-, Rauheits-, Metallkarten),
nur die Grundfarbe ist die Herbie-Albedo. Der Verkehr behaelt die alten
Instanzen - nur der Spieler faehrt Herbie (AWiesbadenCar tauscht die Schlitze
in BeginPlay).

Aufruf (VOLLER Editor):
  UnrealEditor.exe WiesbadenReal.uproject
      -ExecCmds="py Tools/import_herbie.py" -unattended -nosplash
"""

import os

import unreal

SRC = os.path.join(unreal.Paths.project_dir(), "Data", "Raw", "Beetle", "Herbie")
DEST = "/Game/Vehicles/Beetle"
TILES = ["1001", "1002", "1003", "1004"]

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()


def log(msg):
    unreal.log("###WBHERBIE### %s" % msg)


def import_texture(tile):
    path = os.path.join(SRC, "T_Herbie_%s_albedo.png" % tile)
    if not os.path.exists(path):
        log("Textur fehlt: %s" % path)
        return None

    asset_name = "T_VWBeetleHerbie_%s" % tile
    dest = "%s/%s" % (DEST, asset_name)

    task = unreal.AssetImportTask()
    task.filename = path
    task.destination_path = DEST
    task.destination_name = asset_name
    task.automated = True
    task.replace_existing = True
    task.save = True
    TOOLS.import_asset_tasks([task])

    texture = EAL.load_asset(dest)
    if texture is None:
        log("Import fehlgeschlagen: %s" % path)
        return None
    texture.set_editor_property("srgb", True)
    texture.set_editor_property(
        "compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
    texture.set_editor_property("max_texture_size", 2048)
    EAL.save_loaded_asset(texture)
    return texture


def clone_instance(tile, albedo):
    source_path = "%s/MI_VWBeetle_%s" % (DEST, tile)
    source = EAL.load_asset(source_path)
    if source is None:
        log("Quellinstanz fehlt: %s" % source_path)
        return None

    dest_name = "MI_VWBeetleHerbie_%s" % tile
    dest_path = "%s/%s" % (DEST, dest_name)
    if EAL.does_asset_exist(dest_path):
        EAL.delete_asset(dest_path)

    inst = TOOLS.create_asset(dest_name, DEST,
                              unreal.MaterialInstanceConstant,
                              unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(inst, source.get_editor_property("parent"))

    # Alle Texturzuweisungen der Quelle uebernehmen, dann die Grundfarbe
    # durch die Herbie-Albedo ersetzen. So bleiben Normalen, Rauheit und
    # Metall der Kachel unveraendert.
    for entry in source.get_editor_property("texture_parameter_values"):
        info = entry.get_editor_property("parameter_info")
        name = str(info.get_editor_property("name"))
        value = entry.get_editor_property("parameter_value")
        MEL.set_material_instance_texture_parameter_value(inst, name, value)

    MEL.set_material_instance_texture_parameter_value(inst, "BaseColor", albedo)
    EAL.save_loaded_asset(inst)
    log("Instanz: %s" % dest_name)
    return inst


for tile in TILES:
    albedo = import_texture(tile)
    if albedo is not None:
        clone_instance(tile, albedo)

log("FERTIG")

if unreal.SystemLibrary.get_command_line().find("-unattended") >= 0:
    unreal.SystemLibrary.quit_editor()
