"""Importiert die acht Waffen-Meshes aus Tools/Blender/build_weapons.py nach Unreal.

Quelle: Content/Data/Raw/Waffen/SM_Waffe_*.fbx (Blender-Export; Mass in cm ueber
        scale_length 0.01 - wie beim Ka52-Cockpit gemessen, sonst 100-fach).
Ziel:   /Game/Waffen/Meshes/SM_Waffe_<Name>

Jede Waffe wird als StaticMesh importiert (Skalierung 1.0 - die FBX bringt das
Mass mit), Materialien und Texturen kommen mit (die .fbm-Ordner liegen neben
den FBX). Danach wird das Mass GEMESSEN und gegen den Bau-Bericht geprueft:
schon einmal kam ein falscher UnitScaleFactor als 100-fache Groesse an, und
ohne Messung sah die Zahl in beiden Faelle identisch aus.

Die Spec-Tabelle (WiesbadenWeaponSpec.cpp, MeshAssetPath) zeigt auf diese
Assets; die Waffenkomponente laedt sie und faellt ohne sie auf die prozedurale
Huelle zurueck. Idempotent: ein erneuter Lauf ersetzt die Assets.

Headless: Tools\\import_weapons.cmd
  UnrealEditor-Cmd <proj> -run=pythonscript -script=<dieses Skript, absolut>
Erwartung im Log: je Waffe eine "###WAFFENIMP###"-Zeile und am Ende
  "###WAFFENIMP### ENDE ok=<n>/8" - ok < 8 heisst, Ursache steht darueber.
"""
import os

import unreal

ROOT = r"C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal"
SRC = os.path.join(ROOT, "Content", "Data", "Raw", "Waffen")
DEST = "/Game/Waffen/Meshes"

# (Dateiname, Soll-Laenge der groessten Kante in cm aus bau_waffen_bericht.txt)
WAFFEN = [
    ("SM_Waffe_Pistole", 27.9),
    ("SM_Waffe_Gewehr", 66.6),
    ("SM_Waffe_MG", 86.4),
    ("SM_Waffe_Laserpistole", 24.5),
    ("SM_Waffe_Lichtschwert", 143.5),
    ("SM_Waffe_Raketenwerfer", 114.2),
    ("SM_Waffe_Granatwerfer", 48.3),
    ("SM_Waffe_Plasmacutter", 32.8),
]


def log(text):
    unreal.log("###WAFFENIMP### %s" % text)


def mesh_groesse(mesh):
    """Groesste Kante des Asset-Box in UE-Zentimetern (oder None)."""
    try:
        bounds = mesh.get_editor_property("bounds")
        box = bounds.get_editor_property("box")
        size = box.get_size()
        return max(size.x, size.y, size.z)
    except Exception as fehler:
        log("Messung fehlgeschlagen: %s" % fehler)
        return None


def main():
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    ok = 0

    for name, soll in WAFFEN:
        fbx = os.path.join(SRC, name + ".fbx")
        if not os.path.isfile(fbx):
            log("FEHLT: %s" % fbx)
            continue

        opts = unreal.FbxImportUI()
        opts.import_mesh = True
        opts.import_as_skeletal = False
        opts.import_materials = True
        opts.import_textures = True
        opts.mesh_type_to_import = unreal.FBXImportType.FBXIT_STATIC_MESH
        daten = opts.static_mesh_import_data
        daten.import_uniform_scale = 1.0          # das Mass bringt die FBX mit
        daten.combine_meshes = True
        daten.generate_lightmap_u_vs = False      # keine Lightmaps fuer Handobjekte
        daten.auto_generate_collision = False     # die Waffe traegt nichts

        task = unreal.AssetImportTask()
        task.filename = fbx
        task.destination_path = DEST
        task.destination_name = name
        task.automated = True
        task.replace_existing = True
        task.save = True
        task.options = opts

        tools.import_asset_tasks([task])

        asset = "%s/%s" % (DEST, name)
        mesh = unreal.EditorAssetLibrary.load_asset(asset)
        if not isinstance(mesh, unreal.StaticMesh):
            log("FEHLER: %s nicht als StaticMesh angekommen" % asset)
            continue

        ist = mesh_groesse(mesh)
        if ist is None:
            log("%s importiert, Masse nicht lesbar" % name)
            continue

        # 8 % Toleranz: die Bau-Berichte sind auf 0,1 cm gerundet, der Import
        # rundet noch einmal. 100-fach-Fehler faellt damit sicher auf.
        abweichung = abs(ist - soll) / soll if soll > 0 else 1.0
        if abweichung <= 0.08:
            ok += 1
            log("%s ok: groesste Kante %.1f cm (Soll %.1f)" % (name, ist, soll))
        else:
            log("FEHLER: %s Mass falsch: %.1f cm statt %.1f cm - "
                "UnitScaleFactor pruefen!" % (name, ist, soll))

    log("ENDE ok=%d/%d" % (ok, len(WAFFEN)))
    if "-unattended" in unreal.SystemLibrary.get_command_line():
        unreal.SystemLibrary.quit_editor()


main()
