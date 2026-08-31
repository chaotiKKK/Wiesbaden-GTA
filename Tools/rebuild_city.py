"""Baut die komplette Stadt neu und speichert sie als Spielkarte.

Anlass: Zwei Reparaturen wirken erst nach einem Neubau -

  * die Gelaende-Hoehenskalierung (das Gelaende wurde ueber 373 m ueber Null
    flach abgeschnitten, siehe AGENTS.md), und
  * das Versetzen von Schildern, Pfosten und Gehwegteilen, die auf der
    Fahrbahn standen.

Gebaut wird in ein SCHMIERLEVEL und danach unter einem NEUEN Kartennamen
gespeichert. Die bisherige Karte bleibt unangetastet, bis der Neubau
beurteilt ist: ein Bau ueber zwanzig Minuten darf nicht die einzige
funktionierende Stadt ueberschreiben.

Aufruf (VOLLER Editor - der Kommandlet-Weg ist an 42 GiB virtuellem Speicher
gescheitert):
  UnrealEditor.exe WiesbadenReal.uproject
      -ExecCmds="py Tools/rebuild_city.py" -unattended -nosplash
"""

import os

import unreal

SOURCE = os.environ.get("WB_SOURCE_MAP", "/Game/Maps/WiesbadenCity_Alkis3")
TARGET = os.environ.get("WB_TARGET_MAP", "/Game/Maps/WiesbadenCity_Alkis4")
SCRATCH_BASE = "/Game/Maps/__StadtNeubau"

LES = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

# Alles, was die Spielkarte an abgestimmten Einstellungen traegt. Wird der
# Neubau mit blossen Klassenvorgaben gefahren, kommt eine andere Stadt heraus
# als die, die gespielt wurde - und der Vergleich waere wertlos.
SETTINGS = [
    "osm_file_path", "import_dem", "dem_file_path", "alkis_file_path",
    "use_wiesbaden_origin", "vertical_reference_meters",
    "road_type_config_path", "create_landscape_actor",
    "landscape_subsection_size_quads", "landscape_num_subsections",
    "landscape_z_scale", "terrain_preview_grid_size",
    "road_settings", "terrain_settings",
]


def log(msg):
    unreal.log("###WBSTADT### %s" % msg)


def finish(code):
    """Beendet den Editor - AUCH beim Abbruch.

    SystemExit beendet nur das Skript. Ohne diesen Schritt bleibt der Editor
    mit mehreren Gigabyte stehen und blockiert das Projekt.
    """
    if unreal.SystemLibrary.get_command_line().find("-unattended") >= 0:
        unreal.SystemLibrary.quit_editor()
    raise SystemExit(code)


def find_builder():
    found = [a for a in EAS.get_all_level_actors()
             if isinstance(a, unreal.WiesbadenWorldBuilder)]
    return found[0] if found else None


def make_scratch_level():
    """Legt ein garantiert LEERES Schmierlevel an und liefert seinen Pfad.

    new_level meldet ein bereits vorhandenes Asset nur als Protokollzeile und
    gibt False zurueck - ohne Ausnahme. Wird das uebersehen, laeuft der Bau in
    der noch geladenen Spielkarte weiter und schreibt in sie hinein. Loeschen
    allein genuegt nicht; darum wird durchnummeriert.
    """
    for suffix in range(0, 20):
        path = SCRATCH_BASE if suffix == 0 else "%s_%d" % (SCRATCH_BASE, suffix)

        if unreal.EditorAssetLibrary.does_asset_exist(path):
            unreal.EditorAssetLibrary.delete_asset(path)

        if not LES.new_level(path):
            continue

        world = unreal.EditorLevelLibrary.get_editor_world()
        if world is None or path.split("/")[-1] not in world.get_path_name():
            continue

        leftover = [a for a in EAS.get_all_level_actors()
                    if isinstance(a, unreal.LandscapeProxy)]
        if leftover:
            continue

        return path

    return None


# -- 1) Einstellungen der Spielkarte uebernehmen ----------------------------
#
# Ueber ein levelunabhaengiges Zwischenlager: get_editor_property liefert bei
# STRUKTUREN einen Verweis in den Quell-Actor. Nach dem Levelwechsel zeigt der
# ins Freigegebene - dieser Fehler hat einen Stadtneubau schon einmal nach
# 13 Sekunden mit einer Zugriffsverletzung beendet.
LES.load_level(SOURCE)
source_builder = find_builder()
if source_builder is None:
    log("ABBRUCH: kein WorldBuilder in %s" % SOURCE)
    finish(1)

holder = unreal.new_object(unreal.WiesbadenWorldBuilder)
for name in SETTINGS:
    try:
        holder.set_editor_property(name, source_builder.get_editor_property(name))
    except Exception as exc:
        log("Einstellung %s nicht lesbar: %s" % (name, exc))

log("Einstellungen aus %s uebernommen." % SOURCE)

# -- 2) Sauberes Schmierlevel ----------------------------------------------
scratch = make_scratch_level()
if scratch is None:
    log("ABBRUCH: kein leeres Schmierlevel anzulegen.")
    finish(1)

log("Schmierlevel %s bereit." % scratch)

builder = EAS.spawn_actor_from_class(unreal.WiesbadenWorldBuilder, unreal.Vector(0, 0, 0))
if builder is None:
    log("ABBRUCH: WorldBuilder liess sich nicht anlegen.")
    finish(1)

for name in SETTINGS:
    try:
        builder.set_editor_property(name, holder.get_editor_property(name))
    except Exception as exc:
        log("Einstellung %s nicht setzbar: %s" % (name, exc))

# -- 3) Die VOLLE Stadt ----------------------------------------------------
builder.set_editor_property("generate_roads", True)
builder.set_editor_property("generate_terrain", True)
builder.set_editor_property("generate_buildings", True)
builder.set_editor_property("generate_furniture", True)
builder.set_editor_property("generate_region_assets", True)
builder.set_editor_property("generate_city_chunks", True)
builder.set_editor_property("map_asset_path", TARGET)
builder.set_editor_property("auto_save_city_as_map", True)

log("Bau startet - Ziel %s. Das dauert." % TARGET)
builder.build_city()
log("Bau zurueck: %s" % builder.get_editor_property("last_build_summary"))

# -- 4) Gleich mitmessen ---------------------------------------------------
#
# Das Strassennetz liegt nur JETZT im Speicher des Builders. Ein spaeterer
# Lauf muesste die ganze Stadt erneut bauen, nur um dieselbe Frage zu
# beantworten.
builder.check_road_terrain_heights()
log("Hoehenpruefung durchgefuehrt - Bericht in hoehen_report.json")

if not unreal.EditorAssetLibrary.does_asset_exist(TARGET):
    log("ABBRUCH: Karte %s wurde NICHT geschrieben." % TARGET)
    finish(1)

log("FERTIG - Karte %s liegt vor." % TARGET)
finish(0)
