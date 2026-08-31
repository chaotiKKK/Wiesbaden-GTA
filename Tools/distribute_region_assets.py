"""Verteilt die Regionsobjekte einer gebackenen Stadt auf ihre Zell-Actors.

Bis zum 26.08.2026 hingen alle 1.532.254 Baeume in EINER Instanz-Komponente
am AWiesbadenWorldBuilder - einem Actor, den World Partition nie streamt. Der
gesamte Bestand war damit jederzeit geladen. Gemessen an einer Stelle, an der
ausser einer Handvoll winziger Kegel am Horizont kein Baum im Bild stand:

    1.532.254 Instanzen   82 ms Bildzeit
      153.226 Instanzen   64 ms
            0 Instanzen   54 ms

Die Kosten haengen also an der VERWALTETEN Menge, nicht an der sichtbaren -
eine Sichtweitenbegrenzung haette daran nichts geaendert.

Neu gebaute Staedte bekommen die Verteilung schon beim Aufteilen; dieses
Skript holt sie fuer die vorhandene Karte nach. Der Alternativweg waere ein
kompletter Neubau - laut Saved/BuildHistory/CityBuilds.csv 40 bis 64 Minuten.

Aufruf:
  UnrealEditor-Cmd.exe WiesbadenReal.uproject -run=pythonscript
      -script="Tools/distribute_region_assets.py" -unattended -nosplash
"""

import os

import unreal

# Kartenpfad zentral.
#
# Die Karte heisst seit dem Neubau vom 31.08. WiesbadenCity_Alkis3; die alte wurde
# entfernt. Werkzeuge, die noch auf sie zeigten, luden ins Leere UND
# meldeten es nicht - build_materials.py schrieb daraufhin
# "Landscape-Material neu verknuepft: 0 Actor(en)" statt 1.
#
# Ueber die Umgebungsvariable WB_MAP umstellbar, damit der naechste
# Kartenwechsel nicht wieder vier Dateien anfassen muss.
MAP = os.environ.get("WB_MAP", "/Game/Maps/WiesbadenCity_Alkis3")

EAL = unreal.EditorAssetLibrary
LES = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def log(msg):
    unreal.log("###WBDIST### %s" % msg)


LES.load_level(MAP)
log("Karte geladen: %s" % MAP)

# World-Partition-Actors ausdruecklich laden.
#
# `get_all_level_actors` sieht in einer partitionierten Karte nur das, was
# gerade geladen IST. Im Kommandozeilenlauf ist das der WorldBuilder und sonst
# fast nichts - der erste Versuch meldete darum "keine Zell-Actors in der
# Karte", obwohl 1.393 davon in eigenen Paketen auf der Platte liegen.
#
# Der naheliegende Weg ueber `AWiesbadenWorldBuilder::CityChunks` scheidet aus:
# Das Array ist mit Bedacht Transient. Haelt der nicht raeumlich geladene
# WorldBuilder harte Verweise auf alle Zellen, zieht World Partition beim
# Laden jede einzelne nach - und das Streaming, um das es hier gerade geht,
# waere wirkungslos.
descs = unreal.WorldPartitionBlueprintLibrary.get_actor_descs()
guids = [d.get_editor_property("guid") for d in descs]
handles = unreal.WorldPartitionBlueprintLibrary.load_actors(guids)
log("World-Partition-Actors geladen: %d von %d Beschreibungen."
    % (len(handles) if handles else 0, len(descs)))

builders = [a for a in EAS.get_all_level_actors()
            if isinstance(a, unreal.WiesbadenWorldBuilder)]
if len(builders) != 1:
    log("ABBRUCH: %d WorldBuilder gefunden, erwartet genau einer." % len(builders))
    raise SystemExit(1)

builder = builders[0]

chunks = [a for a in EAS.get_all_level_actors()
          if isinstance(a, unreal.WiesbadenCityChunk)]
if not chunks:
    log("ABBRUCH: keine Zell-Actors in der Karte.")
    raise SystemExit(1)

before = sum(c.get_region_asset_count() for c in chunks)
log("Zell-Actors: %d (Regionsobjekte vorher: %d)" % (len(chunks), before))

builder.distribute_region_assets_to_chunks()

# Nachzaehlen statt der Meldung glauben. Eine Verteilung, die nichts verteilt,
# meldet sich sonst genauso ruhig wie eine erfolgreiche.
after = 0
filled = 0
for c in chunks:
    n = c.get_region_asset_count()
    after += n
    if n > 0:
        filled += 1
log("Nach der Verteilung: %d Objekte in %d von %d Zellen." % (after, filled, len(chunks)))

if after == 0:
    log("ABBRUCH: nichts verteilt - nicht gespeichert.")
    raise SystemExit(1)

unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
log("Gespeichert.")
log("FERTIG")
