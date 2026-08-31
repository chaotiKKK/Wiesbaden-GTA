"""Importiert die Texturen des Downtown City MegaKit (Quaternius, CC0).

Quelle: Data/Raw/CityKit/Textures - entpackt aus dem Standard-Paket.

Gebraucht werden sie fuer die Fake-Interior-Fenster: statt aufgemalter
Rechtecke bekommen die Fenster einen INNENRAUM mit Tiefe. Der Kit liefert
dafuer drei fertige Raumbilder (dunkel, beleuchtet 1, beleuchtet 2) sowie
Jalousien und Vorhaenge als Vorhaenge davor.

Die uebrigen Texturen (Ziegel, Beton, Dachschiefer, Zierrat, Trim) kommen
mit, weil die 153 Modelle des Kits sie brauchen - und weil eine echte
Ziegeltextur besser ist als die geratene Farbe im Shader.

ORM ist eine PACKUNG: R = Ambient Occlusion, G = Roughness, B = Metallic.
Sie muss als TC_MASKS ohne Farbraum-Korrektur ankommen, sonst sind alle drei
Werte verschoben - und zwar STUMM, wie schon bei den Stadtmaterialien.

Aufruf (VOLLER Editor):
  UnrealEditor.exe WiesbadenReal.uproject
      -ExecCmds="py Tools/import_citykit.py" -unattended -nosplash
"""

import os

import unreal

PROJECT = unreal.Paths.project_dir()
SRC = os.path.join(PROJECT, "Data", "Raw", "CityKit", "Textures")
TEX_DIR = "/Game/Textures/CityKit"

EAL = unreal.EditorAssetLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()

TC = unreal.TextureCompressionSettings

# Endung im Dateinamen -> (Kompression, sRGB, Groesse)
#
# Die Innenraumbilder bleiben bei 1024: sie werden durch ein Fenster von
# wenigen Bildpunkten gesehen, aber ihre Tiefenwirkung lebt von den Kanten.
RULES = [
    ("_ORM",        (TC.TC_MASKS,     False, 1024)),
    ("_Normal",     (TC.TC_NORMALMAP, False, 1024)),
    ("_BaseColor",  (TC.TC_DEFAULT,   True,  1024)),
    ("interior",    (TC.TC_DEFAULT,   True,  1024)),
    ("Blinds",      (TC.TC_DEFAULT,   True,   512)),
    ("Curtains",    (TC.TC_DEFAULT,   True,   512)),
    ("Decals",      (TC.TC_DEFAULT,   True,  1024)),
    ("Noise",       (TC.TC_GRAYSCALE, False,  512)),
]

DEFAULT_RULE = (TC.TC_DEFAULT, True, 1024)


def log(msg):
    unreal.log("###WBKIT### %s" % msg)


def rule_for(name):
    for needle, rule in RULES:
        if needle.lower() in name.lower():
            return rule
    return DEFAULT_RULE


def import_texture(path):
    name = os.path.splitext(os.path.basename(path))[0]
    # Punkte im Namen sind in Unreal-Assetnamen nicht erlaubt.
    asset_name = name.replace(".", "_")
    dest = "%s/%s" % (TEX_DIR, asset_name)

    task = unreal.AssetImportTask()
    task.filename = path
    task.destination_path = TEX_DIR
    task.destination_name = asset_name
    task.automated = True
    task.replace_existing = True
    task.save = True
    TOOLS.import_asset_tasks([task])

    texture = EAL.load_asset(dest)
    if texture is None:
        log("Import fehlgeschlagen: %s" % os.path.basename(path))
        return None

    compression, srgb, size = rule_for(name)
    texture.set_editor_property("compression_settings", compression)
    texture.set_editor_property("srgb", srgb)
    texture.set_editor_property("max_texture_size", size)
    texture.set_editor_property("never_stream", False)
    EAL.save_loaded_asset(texture)
    return asset_name, compression, size


def main():
    if not os.path.isdir(SRC):
        log("ABBRUCH: %s fehlt - erst das MegaKit-Zip entpacken." % SRC)
        return

    if not EAL.does_directory_exist(TEX_DIR):
        EAL.make_directory(TEX_DIR)

    # Unreal-Normals hat Vorrang: der Kit liefert die Normalmaps doppelt,
    # einmal fuer OpenGL (Y nach oben) und einmal fuer Unreal (Y nach unten).
    # Die falsche Fassung sieht nicht kaputt aus, sondern nur beleuchtet sich
    # verkehrt herum - der unauffaelligste aller Fehler.
    preferred = {}
    ue_dir = os.path.join(SRC, "Unreal-Normals")
    if os.path.isdir(ue_dir):
        for entry in os.listdir(ue_dir):
            preferred[os.path.splitext(entry)[0].lower()] = os.path.join(ue_dir, entry)
        log("Unreal-Normalmaps gefunden: %d" % len(preferred))

    files = []
    for entry in sorted(os.listdir(SRC)):
        full = os.path.join(SRC, entry)
        if not os.path.isfile(full):
            continue
        stem = os.path.splitext(entry)[0].lower()
        files.append(preferred.get(stem, full))

    done = 0
    for path in files:
        ext = os.path.splitext(path)[1].lower()
        if ext not in (".png", ".jpg", ".jpeg", ".hdr"):
            continue
        result = import_texture(path)
        if result:
            asset_name, compression, size = result
            log("%-34s %s  max %d" % (asset_name, str(compression).split(".")[-1], size))
            done += 1

    log("FERTIG: %d Texturen unter %s" % (done, TEX_DIR))


main()

if unreal.SystemLibrary.get_command_line().find("-unattended") >= 0:
    unreal.SystemLibrary.quit_editor()
