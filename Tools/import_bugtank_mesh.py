"""Importiert das BugTank-Insekt als Skeletal-Mesh samt Skelett und Animation.

Warum glTF und nicht FBX (gemessen am 30.09.2026): der FBX-Export der
Blender-Version 5.2.1 erzeugt Dateien, die selbst der zugehoerige FBX-Importer
nicht mehr liest (0 Objekte), und im Unreal-Import landet das Skelett als
Kette entlang -Y - alle 34 Knochen auf einer Linie, alle Beine identisch
(gemessen in Saved/Logs/wb_test_bugtankrig2.log, Zeilen 2181-2189). glTF
speichert die Ruhepose als invertierte Bind-Matrizen; der Interchange-Import
rechnet sie mit dem Faktor 100 von Metern auf Zentimeter hoch
(GltfUnitConversionMultiplier), das exportierte GLB ist daher bereits in
Metern abgelegt (siehe Blender/bugtank/export_bugtank_gltf.py).

Aufruf (Editor-Commandlet, Pfad absolut - relative Pfade loest die Engine
gegen das Engine-Binaerverzeichnis auf):

  UnrealEditor-Cmd.exe <projekt> -run=pythonscript -script="<abs>/Tools/import_bugtank_mesh.py"
                                   -GLB=<pfad> -unattended -nop4 -nullrhi

Der Import benennt den Hauptauftrag auf SK_BugTank_Insekt - dieser Name ist
die Naht, die WiesbadenBugTankPawn im Konstruktor per ConstructorHelpers laedt.
"""
import os
import sys

import unreal

PROJEKT_WURZEL = r"C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal"
GLB_STANDARD = (r"C:\freebuff\WiesbadenReal_Sicherung\Blender\bugtank\ausgabe"
                r"\SK_BugTank_Insekt.glb")
FBX_STANDARD = (r"C:\freebuff\WiesbadenReal_Sicherung\Blender\bugtank\ausgabe"
                r"\SK_BugTank_Insekt.fbx")
ZIELORDNER = "/Game/Vehicles/BugTank"
MESH_NAME = "SK_BugTank_Insekt"
SKELETON_NAME = "SK_BugTank_Insekt_Skeleton"
ANIM_NAME = "SK_BugTank_Insekt_Anim"


def log(text):
    unreal.log("[BugTankImport] %s" % text)


def setze_optional(obj, name, wert):
    """Property nur setzen, wenn es diese Engine-Version kennt - sonst soll
    eine fehlende Option den Import nicht abbrechen."""
    try:
        obj.set_editor_property(name, wert)
        return True
    except Exception as exc:
        log("Property %s nicht verfuegbar (%s)" % (name, exc))
        return False


def hole_argument(name, standard):
    """Schalter aus der Kommandozeile.

    Wichtig: im Commandlet landen die Schalter NICHT in sys.argv (gemessen am
    30.09.2026 - ein -GLB=<pfad> wurde ignoriert und stattdessen immer der
    Standardpfad benutzt). Die ganze Kommandozeile holt SystemLibrary.
    """
    zeile = unreal.SystemLibrary.get_command_line()
    for arg in zeile.split():
        if arg.startswith(name + "="):
            return arg.split("=", 1)[1].strip('"')
    for arg in sys.argv:
        if arg.startswith(name + "="):
            return arg.split("=", 1)[1]
    log("Schalter %s nicht in der Kommandozeile, benutze Standard %s" % (name, standard))
    return standard


def umbenennen(pfad, zielname):
    """Asset auf den Namen bringen, den der Pawn laedt (sonst zweite Wahrheit)."""
    objektpfad = pfad.split(".")[0]
    if objektpfad.endswith("/" + zielname):
        return objektpfad
    ziel = "%s/%s" % (ZIELORDNER, zielname)
    if unreal.EditorAssetLibrary.does_asset_exist(ziel):
        unreal.EditorAssetLibrary.delete_asset(ziel)
    if unreal.EditorAssetLibrary.rename_asset(objektpfad, zielname):
        return ziel
    log("Umbenennen fehlgeschlagen: %s -> %s" % (objektpfad, zielname))
    return None


MESH_NAME = hole_argument("-Name", MESH_NAME)
glb = hole_argument("-GLB", GLB_STANDARD)
if not os.path.exists(glb):
    log("GLB fehlt, weiche auf FBX aus: %s" % glb)
    glb = hole_argument("-FBX", FBX_STANDARD)
log("Quelldatei: %s" % glb)
if not os.path.exists(glb):
    raise RuntimeError("Quelldatei fehlt: %s" % glb)

aufgabe = unreal.AssetImportTask()
aufgabe.set_editor_property("filename", glb)
aufgabe.set_editor_property("destination_path", ZIELORDNER)
aufgabe.set_editor_property("destination_name", MESH_NAME)
aufgabe.set_editor_property("automated", True)
aufgabe.set_editor_property("replace_existing", True)
aufgabe.set_editor_property("save", True)
# Interchange-Pipeline: ohne diese legt der Import die Assets in Unterordner
# (/Game/Vehicles/BugTank/SK_BugTank_Insekt/SkeletalMeshes/...), weil er nach
# Assettyp gruppiert - gemessen am 30.09.2026 im Log wb_import_bugtank2.log.
# Der Pawn laedt einen flachen Pfad, deshalb Ordner- und Namensoptionen aus.
pipeline = unreal.InterchangeGenericAssetsPipeline()
setze_optional(pipeline, "asset_type_sub_folders", False)
setze_optional(pipeline, "scene_name_sub_folder", False)
setze_optional(pipeline, "use_source_name_for_asset", False)
try:
    gemeinsam = pipeline.get_editor_property("common_skeletal_meshes_and_animations_properties")
    setze_optional(gemeinsam, "import_animations", True)
    setze_optional(gemeinsam, "import_anim_blueprints", False)
except Exception as exc:
    log("Skeletal-Eigenschaften nicht lesbar (%s)" % exc)
aufgabe.set_editor_property("options", pipeline)

werkzeug = unreal.AssetToolsHelpers.get_asset_tools()
werkzeug.import_asset_tasks([aufgabe])

importiert = list(aufgabe.get_editor_property("imported_object_paths"))
log("Importiert: %s" % ", ".join(importiert))

# Interchange legt die Assets nach Importtyp sortiert in Unterordner
# (/Game/Vehicles/BugTank/SK_BugTank_Insekt/SkeletalMeshes/...); die
# Ordneroptionen der Pipeline greifen in 5.8 nicht. Der Pawn laedt einen
# flachen Pfad, deshalb werden die Ergebnisse einmal nach oben gezogen -
# rename_asset verschiebt mit und erhaelt die Verweise.
for pfad in list(importiert):
    objektpfad = pfad.split(".")[0]
    if not objektpfad.startswith(ZIELORDNER + "/"):
        continue
    rest = objektpfad[len(ZIELORDNER) + 1:]
    if "/" not in rest:
        continue
    blatt = rest.split("/")[-1]
    ziel = "%s/%s" % (ZIELORDNER, blatt)
    if unreal.EditorAssetLibrary.rename_asset(objektpfad, ziel):
        log("Verschoben: %s -> %s" % (objektpfad, ziel))
    else:
        log("Verschieben fehlgeschlagen: %s -> %s" % (objektpfad, ziel))

# Groessenbeleg direkt nach dem Import: der Importrechner kann Einheiten
# falsch umrechnen, dann waere das Modell 100-mal zu klein oder zu gross.
mesh_asset = unreal.load_asset("%s/%s" % (ZIELORDNER, MESH_NAME))
if mesh_asset:
    log("Mesh geladen: %s (%s)" % (mesh_asset.get_name(), mesh_asset.get_class().get_name()))
    for methode in ("get_bounds", "get_imported_bounds"):
        try:
            grenzen = getattr(mesh_asset, methode)()
            log("Importierte Bounds (%s): %s" % (methode, grenzen))
            break
        except Exception as exc:
            log("Bounds %s nicht auslesbar (%s)" % (methode, exc))
    try:
        log("Materialien: %s" % ", ".join(
            m.get_name() for m in mesh_asset.get_editor_property("materials")))
    except Exception as exc:
        log("Materialien nicht auslesbar (%s)" % exc)
else:
    log("FEHLER: %s/%s wurde nicht angelegt" % (ZIELORDNER, MESH_NAME))

for pfad in importiert:
    log("Asset: %s vorhanden=%s" % (pfad.split(".")[0],
        unreal.EditorAssetLibrary.does_asset_exist(pfad.split(".")[0])))

unreal.EditorAssetLibrary.save_directory(ZIELORDNER)
log("BugTankImport FERTIG")