"""Platzierungs-Audit auf dem IST-Stand - ohne das Verhalten zu aendern.

Der Audit zaehlt, wie oft die sechs Regeln der Spec
"2026-09-26-platzierungsregeln-design.md" am heutigen Stand verletzt werden,
und schreibt die Belegkoordinaten fuer die Kontrollbilder. Er aendert an der
Platzierung NICHTS - das ist die "Vorher"-Haelfte der Vorher-Nachher-Behauptung;
nach der Umsetzung muss dieselbe Pruefung jede Zahl auf 0 setzen.

WARUM DIESES SKRIFT UND KEIN NORMALER BAKE
  Der Audit braucht die DATEN (Strassen, Gebaeude, Ausstattung, Regions-
  Objekte), aber keine Kacheln, kein Landscape und keine gespeicherte Karte.
  Er laeuft darum in ein SCHMIERLEVEL - die gespielte Karte bleibt unberuehrt.

DATENPFADE
  Die Einstellungen kommen aus der Spielkarte, damit dieselbe Stadt geprueft
  wird wie die gebaute. Die Datenpfade werden auf diese Arbeitskopie gezogen
  (die Karte traegt noch Pfade des alten Rechners), und OSM wird AUSDRUECKLICH
  auf wiesbaden.osm.moebel.json umgebogen: nur dort stehen die 3.607
  Strassenmoebel-Knoten, die Regel R5 sonst gar nicht pruefen koennte.

Aufruf:  Tools\\check_placement.cmd    (nimmt den Engine-Lock, Log: placement_audit.log)

Ergebnisse
  placement_report.json       Verstoesse je Regel + bis zu 20 Belegkoordinaten
  placement_audit_result.txt  Einzeiler fuer die cmd-Kette (Python-Ausgaben
                              erreichen den cmd-Strom nicht zuverlaessig)
"""

import os
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from karte import standard_karte_pfad   # EINE Quelle: Config/DefaultEngine.ini

import os

import unreal

SOURCE = os.environ.get("WB_SOURCE_MAP", standard_karte_pfad())
SCRATCH_BASE = "/Game/Maps/__PlatzierungsAudit"
PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

LES = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def log(msg):
    unreal.log("###WBAUDIT### %s" % msg)


def finish(code):
    """Beendet den Editor - AUCH beim Abbruch (sonst bleibt er mit mehreren
    Gigabyte im Speicher stehen und blockiert das Projekt)."""
    if unreal.SystemLibrary.get_command_line().find("-unattended") >= 0:
        unreal.SystemLibrary.quit_editor()
    raise SystemExit(code)


def schreibe_resultat(text):
    pfad = os.path.join(PROJECT_ROOT, "placement_audit_result.txt")
    try:
        with open(pfad, "w", encoding="utf-8") as handle:
            handle.write(text + "\n")
    except Exception as exc:
        log("Ergebnisdatei nicht schreibbar: %s" % exc)


def find_builder():
    found = [a for a in EAS.get_all_level_actors()
             if isinstance(a, unreal.WiesbadenWorldBuilder)]
    return found[0] if found else None


def make_scratch_level():
    """Leeres Schmierlevel anlegen - und NACHWEISEN, dass es geladen ist.

    new_level gibt bei vorhandenem Asset False zurueck und schreibt nur eine
    Warnung. Wird das nicht geprueft, baut der Audit in der noch geladenen
    Spielkarte weiter und haelt deren Zustand fuer seinen eigenen.
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
            log("Schmierlevel %s enthaelt %d Landscape-Aktoren - naechster Name."
                % (path, len(leftover)))
            continue

        return path

    return None


# -- 1) Einstellungen aus der Spielkarte -----------------------------------
#
# Ueber ein levelunabhaengiges Zwischenlager, weil get_editor_property bei
# STRUKTUREN einen Verweis in den Quell-Actor liefert. Nach dem Levelwechsel
# zeigt der ins Freigegebene - dieser Fehler hat den Stadtneubau schon einmal
# nach 13 Sekunden mit einer Zugriffsverletzung beendet.
LES.load_level(SOURCE)
source_builder = find_builder()
if source_builder is None:
    log("ABBRUCH: kein WorldBuilder in %s" % SOURCE)
    schreibe_resultat("FEHLER: kein WorldBuilder in %s" % SOURCE)
    finish(1)

SETTINGS = [
    "osm_file_path", "alkis_file_path", "city_prompt", "import_dem",
    "dem_file_path", "use_wiesbaden_origin", "vertical_reference_meters",
    "road_type_config_path", "road_settings", "building_settings",
    "terrain_settings", "furniture_settings", "region_asset_settings",
]

holder = unreal.new_object(unreal.WiesbadenWorldBuilder)
for name in SETTINGS:
    try:
        holder.set_editor_property(name, source_builder.get_editor_property(name))
    except Exception as exc:
        log("Einstellung %s nicht lesbar: %s" % (name, exc))

log("Einstellungen uebernommen.")

# -- 2) Schmierlevel -------------------------------------------------------
scratch = make_scratch_level()
if scratch is None:
    log("ABBRUCH: kein leeres Schmierlevel anzulegen.")
    schreibe_resultat("FEHLER: kein leeres Schmierlevel.")
    finish(1)

log("Schmierlevel %s ist leer und bereit." % scratch)

builder = EAS.spawn_actor_from_class(unreal.WiesbadenWorldBuilder,
                                     unreal.Vector(0, 0, 0))
if builder is None:
    log("ABBRUCH: WorldBuilder liess sich nicht anlegen.")
    schreibe_resultat("FEHLER: WorldBuilder fehlt.")
    finish(1)

for name in SETTINGS:
    try:
        builder.set_editor_property(name, holder.get_editor_property(name))
    except Exception as exc:
        log("Einstellung %s nicht setzbar: %s" % (name, exc))

# -- 3) Datenpfade auf DIESE Arbeitskopie ziehen ---------------------------
#
# OSM wird hier erzwungen: nur wiesbaden.osm.moebel.json enthaelt die
# nachgezogenen Strassenmoebel-Knoten (3.607). Die Karte selbst stellt
# wiesbaden.osm.forest.json - der Grund, warum die Moebel in keiner Karte
# stehen.
proj = unreal.Paths.project_dir().replace("\\", "/")
LOCAL_OSM = proj + "Data/Raw/OSM/wiesbaden.osm.moebel.json"
if not os.path.isfile(LOCAL_OSM.replace("/", os.sep)):
    log("ABBRUCH: fehlender Datensatz %s" % LOCAL_OSM)
    schreibe_resultat("FEHLER: Datensatz fehlt: %s" % LOCAL_OSM)
    finish(1)


def lokalisiere(pfad):
    """Alten Rechner-Pfad auf diese Arbeitskopie abbauen.

    Geprueft wird nur das Vorkommen von Data/Raw/ - der Dateiname (etwa
    wiesbaden.alkis.lod2.json) bleibt damit erhalten, was die Karte tatsaechlich
    verwendet hat.
    """
    norm = (pfad or "").replace("\\", "/")
    marker = "Data/Raw/"
    if marker in norm:
        return proj + marker + norm.split(marker, 1)[1]
    return pfad


for name in ("osm_file_path", "alkis_file_path", "dem_file_path",
             "road_type_config_path"):
    try:
        alt = builder.get_editor_property(name)
    except Exception as exc:
        log("%s nicht lesbar: %s" % (name, exc))
        continue
    neu = lokalisiere(alt)
    if neu and os.path.isfile(neu.replace("/", os.sep)):
        try:
            builder.set_editor_property(name, neu)
            log("Pfad %s -> %s" % (name, neu))
        except Exception as exc:
            log("Pfad %s nicht setzbar: %s" % (name, exc))
    else:
        log("Pfad %s lokal nicht vorhanden (%s) - Quellwert bleibt." % (name, neu))

try:
    builder.set_editor_property("osm_file_path", LOCAL_OSM)
    log("OSM erzwungen -> %s (mit den 3.607 Moebelknoten)" % LOCAL_OSM)
except Exception as exc:
    log("OSM-Pfad nicht setzbar: %s" % exc)
    schreibe_resultat("FEHLER: OSM-Pfad nicht setzbar: %s" % exc)
    finish(1)

# -- 4) Nur Daten bauen, nichts in die Welt schreiben ----------------------
#
# Gebaeude, Ausstattung und Regions-Objekte SIND die Pruefungsgrundlage - sie
# bleiben an. Kacheln, Landscape, Kollision und Speichern bleiben aus: der
# Audit braucht sie nicht und die Spielkarte darf sich nicht aendern.
for name, value in [
    ("generate_roads", True),
    ("generate_buildings", True),
    ("generate_terrain", True),
    ("generate_region_assets", True),
    ("generate_furniture", True),
    ("generate_pickup_spots", False),
    ("create_landscape_actor", False),
    ("create_road_collision", False),
    ("create_collision", False),
    ("generate_city_chunks", False),
    ("auto_save_city_as_map", False),
]:
    try:
        builder.set_editor_property(name, value)
    except Exception as exc:
        log("Schalter %s nicht setzbar: %s" % (name, exc))

log("Bau startet (nur Daten, kein Bake).")
builder.build_city()
log("Bau zurueck: %s" % builder.get_editor_property("last_build_summary"))

# -- 5) Ergebnis -----------------------------------------------------------
#
# Der Audit laeuft IM BuildCity, solange der OSM-Datensatz noch da ist, und
# nur mit dem Schalter -WbPlacementAudit (siehe check_placement.cmd).
summary = ""
try:
    summary = builder.get_editor_property("last_placement_audit_summary")
except Exception as exc:
    summary = "FEHLER: Summary nicht lesbar: %s" % exc

if not summary:
    summary = ("FEHLER: kein Audit-Ergebnis - wurde der Lauf ohne "
               "-WbPlacementAudit gestartet?")

log(summary)
schreibe_resultat(summary)
finish(0)
