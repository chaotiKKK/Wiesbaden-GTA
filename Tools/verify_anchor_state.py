"""Prueft den gespeicherten Verankerungszustand einer Karte - NUR LESEND.

Anlass: Ein zweiter anchor_bounds-Durchlauf auf Alkis25 meldete wieder
"Bounds-ueber-Ursprung vorher 2010", obwohl der erste Durchlauf 2010
Pakete geschrieben hat. Entweder persistiert die Verankerung nicht
(Platzhalter statt geladener Actors) oder die Restfaelle sind echt
grosse Zellen. Dieser Lauf beantwortet das, OHNE zu speichern:

  - laedt die Karte und die WP-Actors,
  - zaehlt die Zell-Actors, deren Bounds den Ursprung einschliessen,
  - schreibt die ersten fuenf davon mit Position/Ausdehnung in
    Saved/Diagnose/anchor_verify.txt (Prints erreichen den Cmdlet-Stream
    nicht, siehe AGENTS.md).

Aufruf (Ergebnis in Saved/Diagnose/anchor_verify.txt, Exit 0 = gelesen):
  UnrealEditor-Cmd.exe <uproject> -run=pythonscript \
      -script=Tools/verify_anchor_state.py
Karte ueber WB_MAP (Default: die Standardkarte aus Config/DefaultEngine.ini).
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from karte import standard_karte_pfad   # EINE Quelle: Config/DefaultEngine.ini

import unreal

MAP = os.environ.get("WB_MAP", standard_karte_pfad())

EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
LES = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)

RESULT_FILE = os.path.join(
    unreal.Paths.project_saved_dir(), "Diagnose", "anchor_verify.txt")


def _dump_component(log, comp):
    """Schreibt eine Zeile Komponentenort (Welt + relativ, serialisiert wird
    das Relative). Jeder API-Aufruf ist optional - 5.8 exponiert nicht
    alles durchgaengig - deshalb faengt der Aufrufer Fehler ab."""
    rel = comp.get_editor_property("relative_location")
    try:
        world = comp.get_component_transform().translation
        world_s = "Welt (%.0f, %.0f, %.0f)" % (world.x, world.y, world.z)
    except Exception:
        world_s = "Welt unbekannt"
    # isinstance, nicht is_a - auf Python-Objekten gibt es kein is_a.
    if isinstance(comp, unreal.HierarchicalInstancedStaticMeshComponent):
        content = "Instanzen=%d" % comp.get_instance_count()
    elif isinstance(comp, unreal.ProceduralMeshComponent):
        content = "Sections=%d" % comp.get_num_sections()
    elif isinstance(comp, unreal.StaticMeshComponent):
        mesh = comp.get_editor_property("static_mesh")
        content = "Mesh=%s" % (mesh.get_name() if mesh else "-")
    else:
        content = "-"
    log("    %-28s %s relativ (%.0f, %.0f, %.0f) %s"
        % (comp.get_name(), world_s, rel.x, rel.y, rel.z, content))

_lines = []


def log(msg):
    _lines.append(msg)


def bounds_reaches_origin(actor):
    origin, extent = actor.get_actor_bounds(False)
    return abs(origin.x) <= extent.x and abs(origin.y) <= extent.y


def main():
    os.makedirs(os.path.dirname(RESULT_FILE), exist_ok=True)
    LES.load_level(MAP)
    log("Karte geladen: %s" % MAP)

    descs = unreal.WorldPartitionBlueprintLibrary.get_actor_descs()
    guids = [d.get_editor_property("guid") for d in descs]
    handles = unreal.WorldPartitionBlueprintLibrary.load_actors(guids)
    log("WP-Actors geladen: %d von %d Beschreibungen."
        % (len(handles) if handles else 0, len(descs)))

    chunks = [a for a in EAS.get_all_level_actors()
              if isinstance(a, unreal.WiesbadenCityChunk)]
    log("Zell-Actors im geladenen Zustand: %d" % len(chunks))

    reaching = [c for c in chunks if bounds_reaches_origin(c)]
    log("Bounds-ueber-Ursprung: %d von %d" % (len(reaching), len(chunks)))

    # Die ECHTE World-Partition-Bedingung, unabhaengig von geladenen Meshes:
    # eine LEERE Komponente (keine Sections, kein Mesh, keine Instanzen) darf
    # nicht am Kartenursprung stehen - ihr Punkt-Bounds zieht die Actor-Box
    # ueber Kilometer bis zum Ursprung. "Bounds-ueber-Ursprung" oben zaehlt
    # dagegen auch Komponenten MIT Mesh, deren Asset im Commandlet nicht
    # geladen ist und die deshalb faelschlich Punkt-Bounds bei 0 melden -
    # im Spiel laden sie und tragen die Zellgeometrie in Weltkoordinaten.
    at_origin = 0
    empty_total = 0
    for c in chunks:
        for comp in c.get_components_by_class(unreal.SceneComponent):
            if isinstance(comp, unreal.HierarchicalInstancedStaticMeshComponent):
                if comp.get_instance_count() > 0:
                    continue
            elif isinstance(comp, unreal.ProceduralMeshComponent):
                if comp.get_num_sections() > 0:
                    continue
            elif isinstance(comp, unreal.StaticMeshComponent):
                if comp.get_editor_property("static_mesh") is not None:
                    continue
            else:
                # Root und der Spawner tragen selbst keine Bounds.
                continue
            rel = comp.get_editor_property("relative_location")
            empty_total += 1
            if abs(rel.x) < 100.0 and abs(rel.y) < 100.0:
                at_origin += 1
    log("LEERE Komponenten: %d insgesamt, davon %d am Kartenursprung "
        "(das ist der World-Partition-Bruch; 0 = geheilt)"
        % (empty_total, at_origin))

    for c in reaching[:5]:
        origin, extent = c.get_actor_bounds(False)
        loc = c.get_actor_location()
        log("  %s: Ort (%.0f, %.0f, %.0f) Bounds-Mitte (%.0f, %.0f) "
            "Ausdehnung (%.0f, %.0f, %.0f)"
            % (c.get_name(), loc.x, loc.y, loc.z,
               origin.x, origin.y, extent.x, extent.y, extent.z))

    # Gegenprobe: wie gross ist der Median der uebrigen? Ein echter
    # Ausreisser hebt sich davon ab.
    if len(chunks) > len(reaching):
        normal = [c.get_actor_bounds(False)[1].x for c in chunks
                  if not bounds_reaches_origin(c)]
        normal.sort()
        log("Median Ausdehnung X der uebrigen: %.0f cm" % normal[len(normal) // 2])

    # Wo sitzen die Komponenten wirklich? Ohne diese Zeilen ist die
    # Bounds-Zahl nicht deutbar: ein Actor am Ursprung mit einem LEEREN
    # Komponentensatz und ungeladenen Meshes meldet dieselbe Box wie ein
    # wirklich falsch verankerter Chunk.
    for c in chunks[:3]:
        loc = c.get_actor_location()
        log("Komponentenorte von %s (Actor %.0f, %.0f, %.0f):"
            % (c.get_name(), loc.x, loc.y, loc.z))
        for comp in c.get_components_by_class(unreal.SceneComponent):
            try:
                _dump_component(log, comp)
            except Exception as e:
                # Diagnose darf den Lauf nie abbrechen: lieber eine Zeile
                # "nicht lesbar" als gar kein Ergebnis.
                log("    %-28s nicht lesbar: %s" % (comp.get_name(), e))

    with open(RESULT_FILE, "w", encoding="utf-8") as f:
        f.write("\n".join(_lines) + "\n")


main()
