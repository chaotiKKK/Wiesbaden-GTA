"""Verankert die leeren Komponenten aller Zell-Actors der gebackenen Karte.

Der WP-Streaming-Fix hat die Ursache der 3x4-km-Bounds belegt: leere
Komponenten (BuildingMesh ohne Gebaeude, die drei ungenutzten Basis-HISMs
Trees/Waterfront/Industrial) tragen Punkt-Bounds an ihrer Position - und der
Chunk-Actor spawnt am Ursprung. GetComponentsBoundingBox vereinigt diese
Ursprungspunkte mit der Geometrie, World Partition kann die Zelle nicht
raeumlich trennen: 602 von 664 geladenen Chunks lagen >2 km vom Spieler.

Die WPH-Cell-Zuordnung ist GEBACKEN (ActorDescs in der Map) - der Fix im
Code allein heilt die Karte nicht. Dieses Skript ist der Re-Bake ohne
Neubau: Karte laden, alle WP-Actors laden, je Chunk AnchorStreamingBounds
aufrufen, Map speichern. Beim Speichern schreibt WP die ActorDescs mit den
neuen (kleinen) Bounds neu - danach gilt die Zellzuordnung dem Inhalt.

Aufruf:
  UnrealEditor-Cmd.exe WiesbadenReal.uproject -run=pythonscript
      -script="Tools/anchor_chunk_bounds.py" -unattended -nosplash

Karte ueber Umgebungsvariable WB_MAP umstellbar (Default: die aktuelle).
"""

import os
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from karte import standard_karte_pfad   # EINE Quelle: Config/DefaultEngine.ini

import os

import unreal

MAP = os.environ.get("WB_MAP", standard_karte_pfad())

EAL = unreal.EditorAssetLibrary
LES = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


RESULT_FILE = os.path.join(os.path.dirname(__file__), "..", "anchor_bounds_result.txt")

_results = []


def log(msg):
    unreal.log("###WBANCHOR### %s" % msg)
    _results.append(msg)


def flush_results():
    # unreal.log/print erscheinen im Cmdlet-Stream NICHT (Fallstrick im
    # AGENTS.md) - das Ergebnis deshalb in eine Datei schreiben.
    with open(RESULT_FILE, "w", encoding="utf-8") as f:
        f.write("\n".join(_results) + "\n")


def bounds_reaches_origin(actor):
    """True, wenn die Actor-Bounds den Kartenursprung (0, 0) einschliessen.

    Genau das war die Falle: die Punkt-Bounds der leeren Komponenten am
    Ursprung spannten jede Bounds zur Geometrie zurueck.
    """
    origin, extent = actor.get_actor_bounds(False)
    return (abs(origin.x) <= extent.x and abs(origin.y) <= extent.y)


LES.load_level(MAP)
log("Karte geladen: %s" % MAP)

# World-Partition-Actors ausdruecklich laden - gleiche Begruendung wie
# distribute_region_assets.py: get_all_level_actors sieht nur Geladenes.
descs = unreal.WorldPartitionBlueprintLibrary.get_actor_descs()
guids = [d.get_editor_property("guid") for d in descs]
handles = unreal.WorldPartitionBlueprintLibrary.load_actors(guids)
log("World-Partition-Actors geladen: %d von %d Beschreibungen."
    % (len(handles) if handles else 0, len(descs)))

chunks = [a for a in EAS.get_all_level_actors()
          if isinstance(a, unreal.WiesbadenCityChunk)]
if not chunks:
    log("ABBRUCH: keine Zell-Actors in der Karte.")
    flush_results()
    raise SystemExit(1)

before = sum(1 for c in chunks if bounds_reaches_origin(c))

anchored = 0
for chunk in chunks:
    # UFUNCTION -> als snake_case-Methode an Python gebunden. Das C++
    # markiert das Package per Modify(true) selbst schmutzig.
    chunk.anchor_streaming_bounds()
    anchored += 1

# Speichern - hier schreibt WP die ActorDescs mit den neuen Bounds neu.
bSaved = unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
log("save_dirty_packages: %s" % ("ok" if bSaved else "FEHLER"))

after = sum(1 for c in chunks if bounds_reaches_origin(c))

log("FERTIG: %d Chunks verankert. Bounds-ueber-Ursprung vorher %d, nachher %d."
    % (anchored, before, after))

flush_results()
