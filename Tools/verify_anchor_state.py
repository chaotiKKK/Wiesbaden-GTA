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

Aufruf - ueber das Skript, das die drei Fallen schon abhandelt
(Ergebnis loeschen, echten Fehlschlag melden, Befund ausgeben):

  Tools\\verify_anchor.cmd

Handaufruf, falls noetig. DER SKRIPTPFAD MUSS VOLLSTAENDIG SEIN: die Engine
loest einen relativen Pfad gegen Engine\\Binaries\\Win64 auf und meldet dann
nur "Could not load Python file" (Exit 127, keine Ergebnisdatei):

  "C:\\Program Files\\Epic Games\\UE_5.8\\Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe" ^
    "C:\\freebuff\\WiesbadenReal_Sicherung\\WiesbadenReal\\WiesbadenReal.uproject" ^
    -run=pythonscript ^
    -script="C:\\freebuff\\WiesbadenReal_Sicherung\\WiesbadenReal\\Tools\\verify_anchor_state.py" ^
    -unattended -nop4 -nosplash -nullrhi

Karte: die Standardkarte aus Config/DefaultEngine.ini (Tools/karte.py). Ein
WB_MAP ist nur erlaubt, wenn es WIRKLICH auf /Game/... beginnt - Git Bash
schreibt "/Game/Maps/X" zu "C:/Program Files/Git/Game/Maps/X" um, load_level
liefert dann False und dieser Lauf misst die leere Ebene /Temp/Untitled_0.
Deshalb prueft main() die geladene Karte und bricht ab, statt eine Zahl zu
liefern, die nichts bedeutet (genau so entstand der wertlose "0 von 0").

Ergebnis: Saved/Diagnose/anchor_verify.txt (bzw. anchor_verify_<Karte>.txt
beim Aufruf mit Kartenargument - Tools\verify_anchor.cmd Alkis31).
Schreibt dieses Skript nichts,
ist der Lauf FEHLGESCHLAGEN - das ist Absicht, nicht ein Fehler.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from karte import standard_karte_pfad   # EINE Quelle: Config/DefaultEngine.ini

import unreal

MAP = os.environ.get("WB_MAP", standard_karte_pfad())

EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
LES = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)

# Jede gemessene Karte bekommt ihr eigenes Ergebnis: verify_anchor.cmd
# haengt den Kartennamen an (WB_SUFFIX), damit zwei Messungen
# nebeneinander bestehen koennen, statt sich gegenseitig zu ersetzen.
# Ohne Argument bleibt der alte Name. Unerwartete Zeichen fliegen raus:
# der Wert stammt aus einem Kartennamen und darf keinen Pfad bilden.
SUFFIX = "".join(c for c in os.environ.get("WB_SUFFIX", "")
                  if c.isalnum() or c == "_")

RESULT_FILE = os.path.join(
    unreal.Paths.project_saved_dir(), "Diagnose",
    "anchor_verify%s.txt" % SUFFIX)


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


def geladener_kartenpfad():
    """Der Paketpfad der gerade offenen Karte, oder "" wenn nichts offen ist.

    Bewusst ueber mehrere Wege: 5.8 exponiert je nach Aufrufkontext nicht
    denselben Weg, und ein Fehler hier darf die Messung nicht zerstoeren -
    er darf nur bedeuten, dass nichts geladen ist.
    """
    try:
        level = unreal.get_editor_subsystem(
            unreal.LevelEditorSubsystem).get_current_level()
        if level is not None:
            return level.get_path_name()
    except Exception:
        pass
    try:
        return unreal.EditorLevelLibrary.get_editor_world().get_path_name()
    except Exception:
        return ""


def main():
    os.makedirs(os.path.dirname(RESULT_FILE), exist_ok=True)
    # Auch hier gilt: die Datei gehoert zu diesem Lauf oder zu keinem. Ohne
    # dieses Loeschen laesst ein abgebrochener Direktaufruf das Ergebnis des
    # Vortags als Messung stehen (das Skript schreibt erst ganz am Ende).
    if os.path.exists(RESULT_FILE):
        os.remove(RESULT_FILE)
    LES.load_level(MAP)

    # DIE eine Probe, an der der Lauf steht. Ohne sie schreibt dieses Skript
    # eine Zahl, die gemessen aussieht und nichts bedeutet: bei leerer Ebene
    # meldet das Skript 0 Zell-Actors und 0 leere Komponenten am Ursprung -
    # das haette wie ein vollstaendig geheiltes Weltpartition ausgesehen.
    offen = geladener_kartenpfad()
    if MAP.lower() not in offen.lower():
        raise RuntimeError(
            "Karte nicht geladen: gewollt %s, offen %r. Haeufigste Ursache: "
            "WB_MAP kam aus Git Bash und wurde zu 'C:/Program Files/Git/Game/"
            "...' umgeschrieben - dann WB_MAP weglassen und die Karte aus "
            "Config/DefaultEngine.ini nehmen (Tools/karte.py)." % (MAP, offen))
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
