"""Begrenzt die Stadt-Texturen auf 1K.

Der erste Lauf mit SM6 und den neuen PBR-Texturen ist am Speicher gestorben:

    Ran out of memory allocating 4096 bytes.
    Die Auslagerungsdatei ist zu klein, um diesen Vorgang durchzufuehren.

Der Rechner hat 15,7 GB, die Grafikkarte 3965 MB, und der Texturvorrat steht
auf 1000 MB. 22 Materialien mit bis zu fuenf Kanaelen in 2K sind darin nicht
zu halten - eine 2K-Normalmap belegt komprimiert rund 5,5 MB, eine Farbkarte
2,7 MB.

1K statt 2K viertelt den Bedarf. Sichtbar ist der Unterschied dort, wo man
dicht davorsteht - bei einer Fassade, die ueber sechs Meter kachelt, oder
Asphalt ueber vier, ist er gering. Ein Spiel, das laeuft, ist einem
schaerferen vorzuziehen, das abstuerzt.

Die Quelldateien bleiben in 2K liegen: Sollte sich der Speicher als
unproblematisch erweisen, genuegt hier eine Zahl.

Aufruf:
  UnrealEditor-Cmd.exe WiesbadenReal.uproject -run=pythonscript
      -script="Tools/limit_texture_size.py" -unattended -nosplash
"""

import unreal

TEX_DIR = "/Game/Textures/City"
MAX_SIZE = 1024

EAL = unreal.EditorAssetLibrary


def log(msg):
    unreal.log("###WBSIZE### %s" % msg)


assets = EAL.list_assets(TEX_DIR, recursive=True, include_folder=False)
changed = 0

for path in assets:
    texture = EAL.load_asset(path)
    if not isinstance(texture, unreal.Texture2D):
        continue

    before = (texture.blueprint_get_size_x(), texture.blueprint_get_size_y())
    texture.set_editor_property("max_texture_size", MAX_SIZE)

    # Streaming ausdruecklich AN. Ohne es liegt jede Textur vollstaendig im
    # Grafikspeicher, unabhaengig davon, ob sie gerade sichtbar ist - bei
    # 3965 MB Karte ist das der Unterschied zwischen laeuft und laeuft nicht.
    texture.set_editor_property("never_stream", False)

    EAL.save_loaded_asset(texture)
    changed += 1
    log("%-40s %dx%d -> hoechstens %d" % (path.split("/")[-1], before[0], before[1], MAX_SIZE))

log("FERTIG: %d Texturen auf hoechstens %d begrenzt." % (changed, MAX_SIZE))
