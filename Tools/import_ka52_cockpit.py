"""Importiert Cockpit-Innenraum und 30-mm-Kanone der Ka-52 nach UE.

Quelle sind zwei FBX aus der Blender-Pipeline
(Tools/Blender/build_ka52_cockpit.py):
  ka52_cockpit.fbx     -> /Game/Vehicles/Ka52/SM_Ka52Cockpit
  ka52_gun_turret.fbx  -> /Game/Vehicles/Ka52/SM_Ka52GunTurret

Die Zieleinamen werden ueber destination_name erzwungen, weil die
C++-Komponenten sie per ConstructorPath laden. Ein Import ohne erzwungenen
Namen liefert bei zwei Meshes in einer FBX einen anderen Namen und der
Konstruktor laeuft ins Leere (gemessen am 26.09.: der Import brachte
"ka52_cockpit_gun", den niemand referenziert).

ERGEBNIS NACH DATEI, NICHT NACH STDOUT: Prints aus -run=pythonscript
erreichen weder die Konsole noch das Log (AGENTS.md). Der CMD-Wrapper
bewertet darum Saved/Diagnose/ka52/import_ka52_cockpit_ergebnis.txt und
nicht mehr eine Zeile im Log.
"""

import os
import traceback

import unreal

DEST = "/Game/Vehicles/Ka52"
BERICHT = os.path.join(unreal.Paths.project_dir(), "Saved", "Diagnose",
                       "ka52", "import_ka52_cockpit_ergebnis.txt")
QUELLE = os.path.join(unreal.Paths.project_dir(), "Content", "Data", "Raw", "Ka52")
EAL = unreal.EditorAssetLibrary

# Quell-FBX -> Zielname. Reihenfolge = Baufolge.
AUFTRAG = (
    ("ka52_cockpit.fbx", "SM_Ka52Cockpit", True),
    ("ka52_gun_turret.fbx", "SM_Ka52GunTurret", False),
)

zeilen = []


def sag(text):
    """Schreibt mit nach aussen - der Bericht ist das einzige Beweismittel."""
    zeilen.append(text)
    unreal.log("###WBKA52C### %s" % text)


def abschreiben(fehler=None):
    os.makedirs(os.path.dirname(BERICHT), exist_ok=True)
    with open(BERICHT, "w", encoding="utf-8") as fh:
        fh.write("Import Ka-52 Cockpit und Kanone\n")
        if fehler:
            fh.write("ERGEBNIS: FEHLER\n")
            fh.write("FEHLER: %s\n" % fehler)
        else:
            fh.write("ERGEBNIS: OK\n")
        fh.write("\n".join(zeilen) + "\n")
        fh.write("ENDE\n")


def importieren():
    for datei, ziel, nanite in AUFTRAG:
        quelle = os.path.join(QUELLE, datei)
        if not os.path.exists(quelle):
            raise RuntimeError("FBX fehlt: %s (Blender-Lauf noetig)" % quelle)

        # Statisch (kein Skelett), keine Kollision aus dem FBX: Cockpit und
        # Waffenraum sind Darstellung, die Kollision macht der Rumpfkoerper
        # des Pawns. Materialien werden MITgenommen - das Cockpit bringt
        # seine Stoffe mit (Sitzbezug, Metall, Instrumente, Displays) und
        # sieht sonst aus wie ein weisser Kasten.
        opts = unreal.FbxImportUI()
        opts.import_mesh = True
        opts.import_animations = False
        opts.import_materials = True
        opts.import_textures = True
        opts.import_as_skeletal = False
        fbx = unreal.FbxStaticMeshImportData()
        fbx.auto_generate_collision = False
        # Nanite nur fuer die Kabine: sie ist gross genug, um davon zu
        # profitieren, und traegt das Tageslicht am besten. Die Kanone ist
        # handvoll Dreiecke und haengt an einem bewegten Bauteil - da ist
        # der klassische Weg einer ohne Ueberraschung.
        fbx.build_nanite = nanite
        fbx.normal_import_method = (unreal.FBXNormalImportMethod
                                    .FBXNIM_IMPORT_NORMALS)
        opts.static_mesh_import_data = fbx

        task = unreal.AssetImportTask()
        task.filename = quelle
        task.destination_path = DEST
        task.destination_name = ziel
        task.options = opts
        task.automated = True
        task.replace_existing = True
        task.save = True
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        sag("importiert: %s -> %s/%s" % (datei, DEST, ziel))


def messen():
    """Masse der gelandeten Meshes - ein Mesh an (0,0,0) waere ein Fehlschlag."""
    pfad_zu_name = {}
    for datei, ziel, _ in AUFTRAG:
        pfad = "%s/%s.%s" % (DEST, ziel, ziel)
        asset = EAL.load_asset(pfad)
        if asset is None:
            raise RuntimeError("Asset fehlt nach dem Import: %s" % pfad)
        if not isinstance(asset, unreal.StaticMesh):
            raise RuntimeError("%s ist %s, kein StaticMesh"
                               % (pfad, type(asset).__name__))
        b = asset.get_bounds()
        pfad_zu_name[ziel] = pfad
        sag("%s  X %.1f..%.1f  Y %.1f..%.1f  Z %.1f..%.1f cm (Org %.1f/%.1f/%.1f)"
            % (ziel, b.origin.x - b.box_extent.x, b.origin.x + b.box_extent.x,
               b.origin.y - b.box_extent.y, b.origin.y + b.box_extent.y,
               b.origin.z - b.box_extent.z, b.origin.z + b.box_extent.z,
               b.origin.x, b.origin.y, b.origin.z))
        mats = [str(m.get_editor_property("material_slot_name"))
                for m in asset.get_editor_property("static_materials")]
        sag("   %d Materialplaetze: %s" % (len(mats), mats))
    return pfad_zu_name


def altwegraeumen():
    """Den Altbestand aus dem Fehl-Import entfernen.

    ka52_cockpit_gun.uasset war das Ergebnis des Laufes vom 26.09. 01:48:
    ein einziges Mesh aus beiden FBX-Teilen, von niemandem referenziert.
    Liegen geblieben sieht es aber aus wie ein fertiges Asset.
    """
    alt = "%s/ka52_cockpit_gun" % DEST
    if EAL.does_asset_exist(alt):
        ok = EAL.delete_asset(alt)
        sag("Altbestand entfernt: %s (erfolgreich=%s)" % (alt, ok))
    alt_fbx = os.path.join(QUELLE, "ka52_cockpit_gun.fbx")
    if os.path.exists(alt_fbx):
        try:
            os.remove(alt_fbx)
            sag("Altes FBX entfernt: %s" % os.path.basename(alt_fbx))
        except OSError as fehler:
            sag("Altes FBX konnte nicht entfernt werden: %s" % fehler)


def main():
    EAL.make_directory(DEST)
    altwegraeumen()
    importieren()
    messen()
    sag("beide Meshes da: Cockpit und Kanone")
    # Sicherheitsnetz: ohne das bleiben neue Assets bis zum Editor-Abmelden
    # nur im Speicher (UE 5.8 hat save_actor nicht, save_dirty_packages
    # schon - siehe AGENTS.md).
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)


try:
    main()
    abschreiben()
except Exception:  # noqa: BLE001 - der Bericht soll den Grund nennen
    abschreiben(traceback.format_exc())
    raise
