"""Prueft im GESAMTEN Stadtgebiet, ob die Fahrbahn auf dem Gelaende liegt.

Anlass: "Auf der Platter Strasse Richtung Taunusstein sitzen Fahrbahn und
Gehwege nicht im Terrain, sondern in der Luft."

Das Strassennetz liegt NUR nach einem Bau im Speicher des WorldBuilders - die
gebackene Karte bringt es nicht mit. Deshalb baut dieses Skript die Stadt
zuerst neu (Strassen und Gelaende genuegen, Gebaeude und Ausstattung bleiben
aus) und prueft danach.

Der Bau laeuft in ein SCHMIERLEVEL, nicht in die Spielkarte: geprueft werden
soll das Verhaeltnis von Fahrbahn zu Gelaende, und dafuer braucht es keine
gespeicherte Stadt.

Aufruf (VOLLER Editor - der Landscape-Zugriff braucht ihn):
  UnrealEditor.exe WiesbadenReal.uproject
      -ExecCmds="py Tools/check_heights.py" -unattended -nosplash
"""

import os

import unreal

SOURCE = os.environ.get("WB_SOURCE_MAP", "/Game/Maps/WiesbadenCity_Alkis3")
SCRATCH_BASE = "/Game/Maps/__HoehenPruefung"

LES = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def log(msg):
    unreal.log("###WBHOEHE### %s" % msg)


def finish(code):
    """Beendet den Editor - AUCH beim Abbruch.

    Ohne das bleibt der Editor nach einem Abbruch mit mehreren Gigabyte im
    Speicher stehen und blockiert das Projekt: SystemExit beendet nur das
    Python-Skript, nicht den Prozess.
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

    new_level gibt bei bereits vorhandenem Asset False zurueck und schreibt
    nur "An asset already exists at this location" ins Protokoll - ohne
    Ausnahme. Wird das nicht geprueft, laeuft der Bau in der noch geladenen
    Spielkarte weiter, die Messung trifft den dort gebackenen alten Landscape,
    und der Bericht liefert zweimal hintereinander byteweise identische
    Zahlen. Genau so ist eine laengst eingebaute Reparatur zweimal als
    wirkungslos erschienen.

    Loeschen allein genuegt nicht - unmittelbar danach lehnt new_level den
    Pfad weiterhin ab. Darum wird durchnummeriert, bis einer angenommen wird.
    """
    for suffix in range(0, 20):
        path = SCRATCH_BASE if suffix == 0 else "%s_%d" % (SCRATCH_BASE, suffix)

        if unreal.EditorAssetLibrary.does_asset_exist(path):
            unreal.EditorAssetLibrary.delete_asset(path)

        if not LES.new_level(path):
            continue

        # Der Rueckgabewert allein hat sich als zu gutglaeubig erwiesen:
        # nachsehen, welche Karte tatsaechlich geladen ist.
        world = unreal.EditorLevelLibrary.get_editor_world()
        if world is None or path.split("/")[-1] not in world.get_path_name():
            continue

        # Es darf jetzt KEIN Gelaende in der Welt liegen. Liegt doch eines da,
        # stammt es aus der Spielkarte und wuerde die Messung verfaelschen.
        leftover = [a for a in EAS.get_all_level_actors()
                    if isinstance(a, unreal.LandscapeProxy)]
        if leftover:
            log("Schmierlevel %s enthaelt %d Landscape-Aktoren - naechster "
                "Name." % (path, len(leftover)))
            continue

        return path

    return None


# -- 1) Einstellungen aus der Spielkarte ------------------------------------
#
# Ueber ein levelunabhaengiges Zwischenlager, weil get_editor_property bei
# STRUKTUREN einen Verweis in den Quell-Actor liefert. Nach dem Levelwechsel
# zeigt der ins Freigegebene - dieser Fehler hat den Stadtneubau schon einmal
# nach 13 Sekunden mit einer Zugriffsverletzung beendet.
LES.load_level(SOURCE)
source_builder = find_builder()
if source_builder is None:
    log("ABBRUCH: kein WorldBuilder in %s" % SOURCE)
    finish(1)

SETTINGS = [
    "osm_file_path", "import_dem", "dem_file_path", "alkis_file_path",
    "use_wiesbaden_origin", "vertical_reference_meters",
    "road_type_config_path", "create_landscape_actor",
    "landscape_subsection_size_quads", "landscape_num_subsections",
    "landscape_z_scale", "terrain_preview_grid_size",
    "road_settings", "terrain_settings",
]

holder = unreal.new_object(unreal.WiesbadenWorldBuilder)
for name in SETTINGS:
    try:
        holder.set_editor_property(name, source_builder.get_editor_property(name))
    except Exception as exc:
        log("Einstellung %s nicht lesbar: %s" % (name, exc))

log("Einstellungen uebernommen.")

# -- 2) Schmierlevel, nur Strassen und Gelaende -----------------------------
scratch = make_scratch_level()
if scratch is None:
    log("ABBRUCH: kein leeres Schmierlevel anzulegen.")
    finish(1)

log("Schmierlevel %s ist leer und bereit." % scratch)

builder = EAS.spawn_actor_from_class(unreal.WiesbadenWorldBuilder, unreal.Vector(0, 0, 0))
if builder is None:
    log("ABBRUCH: WorldBuilder liess sich nicht anlegen.")
    finish(1)

for name in SETTINGS:
    try:
        builder.set_editor_property(name, holder.get_editor_property(name))
    except Exception as exc:
        log("Einstellung %s nicht setzbar: %s" % (name, exc))

# -- Datenpfade auf DIESE Arbeitskopie ziehen -------------------------------
#
# Der Quell-WorldBuilder in der Karte traegt noch die absoluten Pfade des
# alten Rechners (C:/Users/ssonn/aivideo/...). Auf diesem Rechner zeigen sie
# ins Leere: build_city() scheitert nach 0,1 s mit "Datei nicht gefunden", das
# Strassennetz bleibt leer und die Hoehenpruefung misst gegen nichts. Hier
# werden die Datenpfade auf die Dateien dieser Arbeitskopie gesetzt - relativ
# zum Projektverzeichnis, damit die Pruefung rechnerunabhaengig laeuft.
proj = unreal.Paths.project_dir()  # absolut, endet mit '/'
path_overrides = {
    "osm_file_path": proj + "Data/Raw/OSM/wiesbaden.osm.json",
    "alkis_file_path": proj + "Data/Raw/ALKIS/wiesbaden.alkis.json",
    "dem_file_path": proj + "Data/Raw/DEM/N50E008.hgt",
    "road_type_config_path": proj + "Content/Config/WiesbadenRoadTypes.json",
}
for name, value in path_overrides.items():
    if os.path.isfile(value):
        try:
            builder.set_editor_property(name, value)
            log("Pfad %s -> %s" % (name, value))
        except Exception as exc:
            log("Pfad %s nicht setzbar: %s" % (name, exc))
    else:
        log("Pfad %s: lokale Datei fehlt (%s) - Quellwert bleibt." % (name, value))

# Nur das Noetige bauen. Gebaeude, Ausstattung und Regionen-Assets kosten den
# Grossteil der Bauzeit und aendern an der Hoehenfrage nichts.
builder.set_editor_property("generate_roads", True)
builder.set_editor_property("generate_terrain", True)
builder.set_editor_property("generate_buildings", False)
builder.set_editor_property("generate_furniture", False)
builder.set_editor_property("generate_region_assets", False)
builder.set_editor_property("generate_city_chunks", False)
builder.set_editor_property("auto_save_city_as_map", False)

log("Bau startet (nur Strassen und Gelaende).")
builder.build_city()
log("Bau zurueck: %s" % builder.get_editor_property("last_build_summary"))

# -- 3) Pruefen -------------------------------------------------------------
builder.check_road_terrain_heights()
log("FERTIG - Bericht in hoehen_report.json")

finish(0)
