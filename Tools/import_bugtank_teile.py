"""Importiert die statischen Insekt-Teile als Static Meshes.

Warum Teile statt eines Skeletal-Meshes (gemessen am 30.09.2026): der
Unreal-5.8-Import setzt die Knochenorientierung eines Blender-Rigs um und
uebernimmt nur die Knochenlaenge - FBX wie glTF, beide Parser (Belege:
Saved/Logs/wb_test_bugtankrig5.log und wb_test_bugtankrig6.log). Deshalb
zerlegt Blender/bugtank/export_bugtank_teile.py das Mesh in 15 starre Teile
mit denselben Gelenkpunkten; dieser Import legt sie als SM_Insekt_<Teil> ab,
und der Pawn dreht sie prozedural um ihren Gelenkpunkt.

Aufruf:
  UnrealEditor-Cmd.exe <projekt> -run=pythonscript -script="<abs>/Tools/import_bugtank_teile.py"
                                   -GLB=<pfad> -unattended -nop4 -nullrhi
"""
import os
import sys

import unreal

ZIELORDNER = "/Game/Vehicles/BugTank"
GLB_STANDARD = (r"C:\freebuff\WiesbadenReal_Sicherung\Blender\bugtank\ausgabe"
                r"\SM_BugTank_Teile.glb")
TEILE = ["Body",
         "BeinL1_Ober", "BeinL1_Unter", "BeinL2_Ober", "BeinL2_Unter",
         "BeinL3_Ober", "BeinL3_Unter", "BeinR1_Ober", "BeinR1_Unter",
         "BeinR2_Ober", "BeinR2_Unter", "BeinR3_Ober", "BeinR3_Unter",
         "AntenneL", "AntenneR"]


def log(text):
    unreal.log("[BugTankTeile] %s" % text)


def hole_argument(name, standard):
    """Schalter aus der Kommandozeile - im Commandlet nicht in sys.argv
    (gemessen am 30.09.2026), deshalb die ganze Zeile holen."""
    zeile = unreal.SystemLibrary.get_command_line()
    for arg in zeile.split():
        if arg.startswith(name + "="):
            return arg.split("=", 1)[1].strip('"')
    log("Schalter %s fehlt, benutze Standard %s" % (name, standard))
    return standard


glb = hole_argument("-GLB", GLB_STANDARD)
log("Quelldatei: %s" % glb)
if not os.path.exists(glb):
    raise RuntimeError("GLB fehlt: %s" % glb)

# Alte Assets des Vorlaufs entfernen. Sonst entsteht ein Scheinbefund: der
# Import legt die Teile als Insekt_<Teil> ab, das Umbenennen ueberspringt
# vorhandene SM_Insekt_<Teil>, und die Bounds-Anzeige laest dann das alte Asset
# lesen - genau das ist am 01.10.2026 passiert (Log wb_import_bugtank_teile.log:
# Bounds identisch zum Lauf mit export_yup=False, waehrend die neue GLB korrekt
# Y-up war).
for teil in TEILE:
    alt_name = "%s/SM_Insekt_%s" % (ZIELORDNER, teil)
    if unreal.EditorAssetLibrary.does_asset_exist(alt_name):
        if unreal.EditorAssetLibrary.delete_asset(alt_name):
            log("Altes Asset entfernt: %s" % alt_name)
        else:
            log("ALTES ASSET NICHT ENTFERNBAR: %s" % alt_name)
alt_ordner = "%s/SM_BugTank_Teile" % ZIELORDNER
if unreal.EditorAssetLibrary.does_directory_exist(alt_ordner):
    unreal.EditorAssetLibrary.delete_directory(alt_ordner)
    log("Alten Importordner entfernt: %s" % alt_ordner)

aufgabe = unreal.AssetImportTask()
aufgabe.set_editor_property("filename", glb)
aufgabe.set_editor_property("destination_path", ZIELORDNER)
aufgabe.set_editor_property("automated", True)
aufgabe.set_editor_property("replace_existing", True)
aufgabe.set_editor_property("save", True)

pipeline = unreal.InterchangeGenericAssetsPipeline()
for name, wert in (("asset_type_sub_folders", False),
                   ("scene_name_sub_folder", False),
                   ("use_source_name_for_asset", False)):
    try:
        pipeline.set_editor_property(name, wert)
    except Exception as exc:
        log("Property %s nicht verfuegbar (%s)" % (name, exc))
aufgabe.set_editor_property("options", pipeline)

unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([aufgabe])

importiert = list(aufgabe.get_editor_property("imported_object_paths"))
log("Importiert: %s" % ", ".join(importiert))

# Interchange legt nach Typ sortierte Unterordner an; der Pawn laedt flach.
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

fehlende = []
for teil in TEILE:
    name = "SM_Insekt_%s" % teil
    quelle = "%s/Insekt_%s" % (ZIELORDNER, teil)
    ziel = "%s/%s" % (ZIELORDNER, name)
    if unreal.EditorAssetLibrary.does_asset_exist(ziel):
        continue
    if unreal.EditorAssetLibrary.rename_asset(quelle, ziel):
        log("Benannt: %s -> %s" % (quelle, ziel))
    else:
        fehlende.append(teil)

for teil in TEILE:
    pfad = "%s/SM_Insekt_%s" % (ZIELORDNER, teil)
    asset = unreal.load_asset(pfad)
    if not asset:
        log("###WBTPARTS### FEHLT: %s" % pfad)
        continue
    log("###WBTPARTS### %-14s Asset %s" % (teil, asset.get_path_name()))
    try:
        box = asset.get_bounds()
        log("###WBTPARTS### %-14s Box %s .. %s" % (
            teil,
            tuple(round(v, 2) for v in (box.origin.x - box.box_extent.x,
                                        box.origin.y - box.box_extent.y,
                                        box.origin.z - box.box_extent.z)),
            tuple(round(v, 2) for v in (box.origin.x + box.box_extent.x,
                                        box.origin.y + box.box_extent.y,
                                        box.origin.z + box.box_extent.z))))
    except Exception as exc:
        log("###WBTPARTS### %-14s Bounds nicht lesbar (%s)" % (teil, exc))

unreal.EditorAssetLibrary.save_directory(ZIELORDNER)
log("###WBTPARTS### FERTIG, fehlende Teile: %s" % (fehlende or "keine"))