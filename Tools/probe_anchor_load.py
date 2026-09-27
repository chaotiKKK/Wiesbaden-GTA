"""Findet heraus, WIE die Karte in einem Commandlet wirklich geladen wird.

Anlass: verify_anchor_state.py stuerzt an
    guids = [d.get_editor_property("guid") for d in descs]
mit "TypeError: 'NoneType' object is not iterable". get_actor_descs() liefert
None - also ist die Karte gar nicht geladen, und jedes Ergebnis waere wieder
das wertlose "0 von 0" aus dem alten Alkis24-Lauf.

Statt zu raten, fragt dieses Skript die API ab und schreibt nach
Saved/Diagnose/anchor_probe.txt:
  - welche Ladewege es gibt und welchen davon load_level nimmt,
  - was danach in der Welt steht (Partioned? Chunk-Actors da?),
  - was get_actor_descs() zurueckgibt.

Zwei Lehren aus dem ersten Versuch, beide hier eingebaut:

  1. JEDER Aufruf kann fehlen. 5.8 exponiert nicht alles (die erste Fassung
     starb an World.get_package_name, das es nicht gibt). Deshalb laeuft
     alles ueber _safe(), und ein Fehler ist ein Messwert, kein Abbruch.
  2. unreal.log() erreicht im Commandlet NICHTS (es steht nicht im Log) -
     nur die Datei zaehlt. Deshalb wird am Ende geschrieben, und zwar auch
     dann, wenn etwas kaputtging.

Nur lesend, speichert nichts.
"""

import os
import sys
import traceback

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from karte import standard_karte_pfad   # EINE Quelle: Config/DefaultEngine.ini

import unreal

MAP = os.environ.get("WB_MAP", standard_karte_pfad())
RESULT_FILE = os.path.join(
    unreal.Paths.project_saved_dir(), "Diagnose", "anchor_probe.txt")

_lines = []


def log(msg):
    _lines.append(str(msg))


def _safe(label, fn, fmt="%r"):
    """Ruft etwas auf; ein Fehler ist ein Ergebnis, kein Abbruch."""
    try:
        wert = fn()
        log(("%-44s -> " + fmt) % (label, wert))
        return wert
    except Exception as e:
        log("%-44s !! %s: %s" % (label, type(e).__name__, e))
        return None


def _aktuelle_welt():
    """Die Welt, in der gerade gearbeitet wird - auf mehreren Wegen, weil
    keiner davon in jeder Umgebung der richtige ist."""
    for holer in (
            lambda: unreal.get_editor_subsystem(
                unreal.UnrealEditorSubsystem).get_editor_world(),
            lambda: unreal.EditorLevelLibrary.get_editor_world(),
            lambda: unreal.GEditor.get_editor_world_context().world()):
        w = _safe("  Welt-Holer", holer)
        if w is not None:
            return w
    return None


def _pfad_von(welt):
    if welt is None:
        return "keine"
    for bezug in ("get_path_name", "get_name"):
        try:
            return getattr(welt, bezug)()
        except Exception:
            continue
    return "unbekannt"


def main():
    os.makedirs(os.path.dirname(RESULT_FILE), exist_ok=True)
    log("Zielkarte: %s" % MAP)

    # -- 1. Welche Ladewege gibt es ueberhaupt? -----------------------------
    log("")
    log("--- Ladewege ---")
    if hasattr(unreal, "LevelEditorSubsystem"):
        _safe("LevelEditorSubsystem.load_level",
              lambda: unreal.get_editor_subsystem(
                  unreal.LevelEditorSubsystem).load_level(MAP))
        log("  Welt danach: %s" % _pfad_von(_aktuelle_welt()))

    if hasattr(unreal, "EditorLoadingAndSavingUtils"):
        _safe("EditorLoadingAndSavingUtils.load_map",
              lambda: unreal.EditorLoadingAndSavingUtils.load_map(MAP))
        log("  Welt danach: %s" % _pfad_von(_aktuelle_welt()))

    if hasattr(unreal, "EditorLevelLibrary"):
        _safe("EditorLevelLibrary.load_level",
              lambda: unreal.EditorLevelLibrary.load_level(MAP))
        log("  Welt danach: %s" % _pfad_von(_aktuelle_welt()))

    welt = _aktuelle_welt()
    geladen = MAP.lower() in _pfad_von(welt).lower() if welt else False
    log("Zielkarte geladen: %s" % ("JA" if geladen else "NEIN"))

    # -- 2. Was steht drin? -------------------------------------------------
    log("")
    log("--- Inhalt ---")
    if welt is not None:
        _safe("Welt.get_name", welt.get_name)
        _safe("Welt.get_path_name", welt.get_path_name)
        _safe("Welt.get_outer", lambda: welt.get_outer().get_name())
        # WorldPartition: das Package-Handle sagt, ob es ueberhaupt
        # eine partitionierte Karte ist.
        _safe("Welt.get_outer().get_outer()",
              lambda: welt.get_outer().get_outer().get_name())

    try:
        alle = unreal.get_editor_subsystem(
            unreal.EditorActorSubsystem).get_all_level_actors()
        log("get_all_level_actors(): %d" % (len(alle) if alle else 0))
        if alle:
            chunk = [a for a in alle if isinstance(a, unreal.WiesbadenCityChunk)]
            log("davon WiesbadenCityChunk: %d" % len(chunk))
            if chunk:
                log("erster Chunk: %s" % chunk[0].get_name())
    except Exception as e:
        log("get_all_level_actors !! %s: %s" % (type(e).__name__, e))

    # -- 3. WorldPartitionBlueprintLibrary ----------------------------------
    log("")
    log("--- WorldPartition ---")
    try:
        descs = unreal.WorldPartitionBlueprintLibrary.get_actor_descs()
        log("get_actor_descs() Typ: %s" % type(descs).__name__)
        log("get_actor_descs() Laenge: %s" % (len(descs) if descs else 0))
    except Exception as e:
        log("get_actor_descs !! %s: %s" % (type(e).__name__, e))

    for name in ("is_world_partitioned", "get_actor_descs",
                 "load_actors", "are_actors_loaded"):
        log("  WorldPartitionBlueprintLibrary.%s: %s"
            % (name, hasattr(unreal.WorldPartitionBlueprintLibrary, name)))


try:
    main()
except Exception:
    log("")
    log("HARTER FEHLER:")
    log(traceback.format_exc())
finally:
    with open(RESULT_FILE, "w", encoding="utf-8") as f:
        f.write("\n".join(_lines) + "\n")
