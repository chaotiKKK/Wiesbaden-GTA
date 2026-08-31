# Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#
# Erzeugt die drei Ersatztexturen, die jedes Grundmaterial als Vorgabewert
# braucht.
#
# Aufruf:
#   blender.exe -b -P make_default_textures.py -- --out <Ordner>
#
# Warum ueberhaupt eigene Dateien?
#
# Unreal verlangt, dass der ABTASTTYP eines Textureingangs zur KOMPRESSION der
# dort liegenden Textur passt. Tut er das nicht, uebersetzt die Engine das
# Material NICHT und zeichnet stumm das Standardmaterial - die Meldung steht
# allein im Protokoll:
#
#   "Sampler type is Linear Grayscale, should be Color for
#    /Engine/EngineResources/WhiteSquareTexture"
#
# Genau daran sind gleichzeitig gescheitert: M_WbFigur (Spielerfigur),
# M_VWBeetle_Master (Kaefer) und ueber die Metallic-Karte saemtliche
# Fassadenmaterialien. Die ganze Stadt war grau.
#
# Bei den Engine-Texturen ist die Kompression nicht aus dem Namen ablesbar -
# WhiteSquareTexture klingt neutral, ist aber ein FARBBILD mit
# Farbraum-Korrektur. Deshalb drei eigene, deren Einstellungen der Import
# selbst setzt: keine Rateaufgabe mehr.

import bpy
import os
import sys

SIZE = 4

# Name -> (R, G, B) im Speicher, also OHNE Farbraum-Korrektur
DEFAULTS = {
    # Rauheit, Metallic, Masken: 1,0 heisst "voll", der Regler davor
    # bestimmt die Staerke.
    "T_WbVorgabe_Weiss": (1.0, 1.0, 1.0),
    "T_WbVorgabe_Maske": (1.0, 1.0, 1.0),
    # Flache Normale: (0,5 / 0,5 / 1,0) ist "keine Abweichung".
    "T_WbVorgabe_Normal": (0.5, 0.5, 1.0),
}


def arg_value(name, default):
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if name in argv:
        return argv[argv.index(name) + 1]
    return default


def main():
    out_dir = os.path.abspath(arg_value("--out", "."))
    os.makedirs(out_dir, exist_ok=True)

    for name, rgb in DEFAULTS.items():
        img = bpy.data.images.new(name, SIZE, SIZE, alpha=False,
                                  float_buffer=False, is_data=True)
        img.pixels = [c for _ in range(SIZE * SIZE)
                      for c in (rgb[0], rgb[1], rgb[2], 1.0)]
        path = os.path.join(out_dir, "%s.png" % name)
        img.filepath_raw = path
        img.file_format = 'PNG'
        img.save()
        print("[Vorgabe] %s -> %s" % (rgb, path))

    print("[Vorgabe] FERTIG")


main()
