"""Importiert die drei Trenn-Stuecke des Plasmacutters nach Unreal.

Quelle: Content/Data/Raw/Cutpieces/SM_Cutpiece_*.fbx (Blender-Export,
        Tools/Blender/build_cutpieces.py; Mass in cm ueber scale_length 0.01).
Ziel:   /Game/Waffen/Cutpieces/SM_Cutpiece_<Name>

Jedes Stueck wird als StaticMesh importiert (Skalierung 1.0 - die FBX bringt
das Mass mit), Materialien und Texturen kommen mit (.fbm neben den FBX).
Danach wird das Mass GEMESSEN und gegen den Bau-Bericht geprueft: schon
beim Waffen-Import kam ein falscher UnitScaleFactor als 100-fache Groesse
an, und ohne Messung sahen beide Zahlen identisch aus.

AWiesbadenCuttable laedt diese Assets und faellt ohne sie auf die
Engine-Cubes zurueck. Idempotent: ein erneuter Lauf ersetzt die Assets.

Headless: Tools\\\\import_cutpieces.cmd
Beleg ist die Datei Saved/Diagnose/cutpieces_mass.txt, NICHT das Log: in
-run=pythonscript-Laeufen kommen weder unreal.log-Zeilen noch Python-Prints
zuverlaessig im Log an (gemessen 26.09.2026 - dieselbe Luecke wie beim
Waffen-Import). Dort steht je Stueck die gemessene Kante neben der Soll-
Masse aus bau_cutpieces_bericht.txt; "ABWEICHUNG" heisst UnitScaleFactor.
"""
import os

import unreal

ROOT = r"C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal"
SRC = os.path.join(ROOT, "Content", "Data", "Raw", "Cutpieces")
DEST = "/Game/Waffen/Cutpieces"
ZIEL = r"C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Saved\Diagnose\cutpieces_mass.txt"

# (Dateiname, Soll-Groessen cm x/y/z aus bau_cutpieces_bericht.txt)
STUECKE = [
    ("SM_Cutpiece_Unten", 56.0, 56.0, 57.5),
    ("SM_Cutpiece_Oben", 56.0, 56.0, 72.0),
    ("SM_Cutpiece_Glut", 52.0, 52.0, 2.0),
]


def log(text):
    unreal.log("###CUTIMP### %s" % text)


def mesh_masse(mesh):
    """Groessen x/y/z der Asset-Box in UE-Zentimetern (oder None).

    get_bounding_box() ist eine METHODE und size eine Property - so heisst
    es in der UE-5.8-Python-API (gemessen am Waffen-Import 26.09.2026).
    get_editor_property("bounds") gibt es bei StaticMesh nicht.
    """
    # Box kennt in UE 5.8 nur .min/.max - kein .size und kein get_size()
    # (gemessen 26.09.2026 mit Tools/probe_box_api.py; die Bounds-Property
    # gibt es beim StaticMesh gar nicht).
    try:
        box = mesh.get_bounding_box()
        lo = box.min
        hi = box.max
        return (hi.x - lo.x, hi.y - lo.y, hi.z - lo.z)
    except Exception as fehler:
        log("Messung fehlgeschlagen: %s" % fehler)
        return ("fehler", str(fehler))


def main():
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    ok = 0
    bericht = []

    for name, soll_x, soll_y, soll_z in STUECKE:
        fbx = os.path.join(SRC, name + ".fbx")
        if not os.path.isfile(fbx):
            log("FEHLT: %s" % fbx)
            bericht.append("%-20s FEHLT: %s" % (name, fbx))
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
        daten.generate_lightmap_u_vs = False
        daten.auto_generate_collision = True      # das Stueck faellt physikalisch

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
            bericht.append("%-20s FEHLER: nicht als StaticMesh importiert" % name)
            continue

        ist = mesh_masse(mesh)
        if ist is None or ist[0] == "fehler":
            log("%s importiert, Masse nicht lesbar" % name)
            bericht.append("%-20s Messung nicht lesbar: %s"
                           % (name, ist[1] if ist else "?"))
            continue

        # 8 % Toleranz je Kante: Berichte sind auf 0,1 cm gerundet, der Import
        # rundet noch einmal. 100-fach-Fehler faellt damit sicher auf.
        gut = True
        for kante, soll in zip(("x", "y", "z"), (soll_x, soll_y, soll_z)):
            if soll <= 0:
                continue
            abweichung = abs(ist["xyz".index(kante)] - soll) / soll
            if abweichung > 0.08:
                gut = False
        if gut:
            ok += 1
            log("%s ok: %.1f x %.1f x %.1f cm (Soll %.1f x %.1f x %.1f)"
                % (name, ist[0], ist[1], ist[2], soll_x, soll_y, soll_z))
            bericht.append("%-20s %7.2f x %7.2f x %7.2f cm  Soll %5.1f x %5.1f x %5.1f  ok"
                           % (name, ist[0], ist[1], ist[2], soll_x, soll_y, soll_z))
        else:
            log("FEHLER: %s Mass falsch: %.1f x %.1f x %.1f cm statt "
                "%.1f x %.1f x %.1f - UnitScaleFactor pruefen!"
                % (name, ist[0], ist[1], ist[2], soll_x, soll_y, soll_z))
            bericht.append("%-20s %7.2f x %7.2f x %7.2f cm  Soll %5.1f x %5.1f x %5.1f  "
                           "ABWEICHUNG (UnitScaleFactor pruefen)"
                           % (name, ist[0], ist[1], ist[2], soll_x, soll_y, soll_z))

    log("ENDE ok=%d/%d" % (ok, len(STUECKE)))
    bericht.append("")
    bericht.append("ENDE ok=%d/%d" % (ok, len(STUECKE)))

    kopf = ("Masse der importierten Cutpiece-Meshes (cm)\n"
            "Soll: Content/Data/Raw/Cutpieces/bau_cutpieces_bericht.txt\n")
    with open(ZIEL, "w", encoding="utf-8") as f:
        f.write(kopf + "\n".join(bericht) + "\n")

    unreal.SystemLibrary.quit_editor()


main()
