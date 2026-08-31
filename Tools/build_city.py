"""Baut die Stadt neu - ohne Editor-Oberflaeche.

`BuildCity` blockiert bis zum Ende (der Aufruf pumpt die Nachrichtenschleife
und wartet auf den Worker), laesst sich also aus einem Kommandozeilenlauf
heraus starten. Laut Saved/BuildHistory/CityBuilds.csv dauert ein Lauf 40 bis
64 Minuten.

Gebaut wird in ein FRISCHES Level und gespeichert unter einem eigenen Pfad.
Die laufende Stadt bleibt damit unangetastet: Sie belegt 12 GB in 1.994
Actor-Paketen, und ein misslungener Neubau darueber waere nicht mehr
rueckgaengig zu machen. Umgeschaltet wird erst nach dem Abgleich.

Einstellungen kommen aus der VORHANDENEN Karte, damit dieselbe Stadt entsteht
und nicht versehentlich eine andere - Quelldateien, Zellgroesse,
Gelaendeaufloesung.

Umgebungsvariablen:
    WB_TARGET_MAP   Zielpfad (Vorgabe /Game/Maps/WiesbadenCity_Alkis3)
    WB_SOURCE_MAP   Karte, aus der die Einstellungen kommen
                    (Vorgabe /Game/Maps/WiesbadenCity_Alkis)

Aufruf:
  UnrealEditor-Cmd.exe WiesbadenReal.uproject -run=pythonscript
      -script="Tools/build_city.py" -unattended -nosplash
"""

import os

import unreal

TARGET = os.environ.get("WB_TARGET_MAP", "/Game/Maps/WiesbadenCity_Alkis3")
SOURCE = os.environ.get("WB_SOURCE_MAP", "/Game/Maps/WiesbadenCity_Alkis3")

LES = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

# Einstellungen, die den Bau steuern. Bewusst eine feste Liste statt "alles
# kopieren": Ergebnisfelder wie RoadNetwork oder RegionAssetLayout mit zu
# uebernehmen wuerde die alte Stadt in den Neubau schleppen.
# Achtung bei den Wahrheitswerten: Unreal streicht das fuehrende "b" eines
# C++-Namens, wenn es die Eigenschaft nach Python durchreicht. bImportDem
# heisst hier "import_dem", nicht "b_import_dem" - mit dem falschen Namen
# meldet get_editor_property "Failed to find property", und ein Skript, das
# solche Fehler nur protokolliert, baut die Stadt danach mit den VORGABEN
# statt mit den uebernommenen Einstellungen.
SETTINGS = [
    "osm_file_path", "city_prompt", "import_dem", "dem_file_path",
    "alkis_file_path", "use_wiesbaden_origin", "vertical_reference_meters",
    "road_type_config_path", "generate_roads", "generate_buildings",
    "generate_terrain", "create_landscape_actor",
    "landscape_subsection_size_quads", "landscape_num_subsections",
    "landscape_z_scale", "terrain_preview_grid_size", "generate_furniture",
    "generate_city_chunks", "city_chunk_size_meters",
    "generate_region_assets", "create_road_collision", "create_collision",
    "sign_texture_folder", "sign_texture_parameter_name",
    "road_settings", "building_settings", "terrain_settings",
    "furniture_settings", "region_asset_settings", "traffic_settings",
    "pedestrian_settings", "traffic_light_settings",
]


def log(msg):
    unreal.log("###WBBUILD### %s" % msg)


def find_builder():
    found = [a for a in EAS.get_all_level_actors()
             if isinstance(a, unreal.WiesbadenWorldBuilder)]
    return found[0] if found else None


# -- 1) Einstellungen aus der bestehenden Karte lesen ----------------------
LES.load_level(SOURCE)
source_builder = find_builder()
if source_builder is None:
    log("ABBRUCH: kein WorldBuilder in %s" % SOURCE)
    raise SystemExit(1)

# Zwischenlager, das den Levelwechsel ueberlebt.
#
# HIER lag ein Absturz, der den Neubau nach 13 Sekunden mit
# EXCEPTION_ACCESS_VIOLATION beendete (Copy<FBuildingGenerationSettings>).
#
# `get_editor_property` liefert bei STRUKTUREN einen Verweis in den Speicher
# des Quell-Actors, keine eigenstaendige Kopie. Das Skript las die Werte, lud
# danach ein neues Level - womit der Quell-Actor zerstoert wird - und schrieb
# sie erst dann in den neuen Actor. Zu diesem Zeitpunkt zeigten die Verweise
# ins Freigegebene; die gelesene Adresse endete auf ...fff8.
#
# Einfache Werte (Zahlen, Wahrheitswerte, Zeichenketten) sind davon nicht
# betroffen - deshalb ist der Fehler jahrelang nicht aufgefallen, sondern
# erst, als eine Struktur eine TMap enthielt.
#
# Ein mit new_object erzeugtes Objekt gehoert KEINEM Level und ueberlebt den
# Wechsel. Solange beide Actors leben, ist die Kopie gueltig.
holder = unreal.new_object(unreal.WiesbadenWorldBuilder)

values = {}
unreadable = []
for name in SETTINGS:
    try:
        holder.set_editor_property(name, source_builder.get_editor_property(name))
        values[name] = name
    except Exception as e:
        unreadable.append(name)
        log("Einstellung %s nicht lesbar: %s" % (name, e))

log("Einstellungen uebernommen: %d von %d" % (len(values), len(SETTINGS)))

# Abbrechen, nicht weiterbauen.
#
# Eine nicht uebernommene Einstellung heisst: Der Neubau laeuft mit der
# Vorgabe. Bei "generate_buildings" waere das eine Stadt ohne Haeuser - nach
# 40 bis 64 Minuten. Lieber sofort stehenbleiben.
if unreadable:
    log("ABBRUCH: %d Einstellungen nicht lesbar: %s" % (len(unreadable), ", ".join(unreadable)))
    raise SystemExit(1)
for key in ("osm_file_path", "alkis_file_path", "dem_file_path",
            "city_chunk_size_meters", "generate_city_chunks",
            "generate_roads", "generate_buildings", "generate_terrain",
            "generate_region_assets", "generate_furniture"):
    log("  %s = %s" % (key, holder.get_editor_property(key)))

# -- 2) Frisches Level, Bauobjekt darin -----------------------------------
LES.new_level(TARGET)
log("Neues Level angelegt: %s" % TARGET)

builder = EAS.spawn_actor_from_class(unreal.WiesbadenWorldBuilder,
                                     unreal.Vector(0, 0, 0))
if builder is None:
    log("ABBRUCH: WorldBuilder liess sich nicht anlegen.")
    raise SystemExit(1)

# Aus dem Zwischenlager in den neuen Actor. Das Zwischenlager lebt noch -
# es haengt an keinem Level -, die Strukturverweise sind also gueltig.
for name in values:
    try:
        builder.set_editor_property(name, holder.get_editor_property(name))
    except Exception as e:
        log("Einstellung %s nicht setzbar: %s" % (name, e))

builder.set_editor_property("map_asset_path", TARGET)
builder.set_editor_property("auto_save_city_as_map", True)

# -- 3) Bauen (blockiert) --------------------------------------------------
log("BuildCity startet - laut Bauhistorie 40 bis 64 Minuten.")
builder.build_city()

log("BuildCity zurueck. Zusammenfassung: %s"
    % builder.get_editor_property("last_build_summary"))
log("Gebacken: %s, Auto-Save erfolgreich: %s, Fehler: %s"
    % (builder.get_editor_property("city_baked"),
       builder.get_editor_property("auto_save_succeeded"),
       builder.get_editor_property("last_error")))
log("FERTIG")
