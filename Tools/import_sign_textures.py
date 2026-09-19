"""Importiert die Schild-PNGs als UE-Texturen (Assets) unter /Game/Textures/TrafficSigns.

Warum
-----
`WiesbadenSignAssets::ResolveTexture` laedt zur Laufzeit das ASSET
`/Game/Textures/TrafficSigns/Sign_<Id>` - nicht die PNG daneben. Die PNGs sind
die Quelle, das Import-Asset ist das, was die Engine findet; fehlt es, loggt das
Spiel "Schild-Textur nicht gefunden" und der Spawner laesst die Tafel weg. Genau
dieser Schritt fehlte: im Ordner lagen 134 PNGs, aber nur 66 Assets, und fuer 69
Zeichen der Stadt gab es damit keine Tafel.

Was das Skript tut
------------------
1. Importiert jede `Sign_*.png` ohne `.uasset` (idempotent: vorhandene Assets
   bleiben unberuehrt, `--alle` importiert alles neu).
2. Spiegelt die Textureinstellungen eines Referenz-Assets (`Sign_206`) auf die
   neuen Assets, damit sie sich wie die vorhandenen verhalten.
3. Prueft am Ende: Anzahl PNGs, Anzahl Assets, und laedt eines der neuen Assets.

Aufruf (headless ueber Tools/import_sign_textures.cmd):
  UnrealEditor-Cmd.exe <Projekt> -run=pythonscript -script=Tools/import_sign_textures.py -unattended
  Optionen als Umgebungsvariable: SIGN_IMPORT_MODE=info|alle
"""

import glob
import os

import unreal

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
QUELLE = os.path.join(ROOT, "Content", "Textures", "TrafficSigns")
ZIEL = "/Game/Textures/TrafficSigns"
REFERENZ = ZIEL + "/Sign_206"
# Diese Einstellungen werden vom Referenz-Asset uebernommen, damit neue
# Schild-Texturen sich exakt wie die vorhandenen verhalten (gemessen an
# Sign_206: TC_DEFAULT, sRGB, Alpha behalten, keine Mipmaps).
UEBERNOMMEN = ("compression_settings", "srgb", "compression_no_alpha", "mip_gen_settings")

tools = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
VORBILD = {}


def pngs():
    return sorted(glob.glob(os.path.join(QUELLE, "Sign_*.png")))


def ohne_asset():
    return [p for p in pngs() if not os.path.exists(p[:-4] + ".uasset")]


def info():
    """Zustand und Referenzeinstellungen ausgeben (Vor dem Import ansehen)."""
    alle = pngs()
    fehlend = ohne_asset()
    unreal.log("Schild-Texturen: %d PNG, davon %d ohne Asset." % (len(alle), len(fehlend)))
    ref = EAL.load_asset(REFERENZ)
    if ref:
        for feld in ("compression_settings", "srgb", "compression_no_alpha",
                     "mip_gen_settings", "texture_group", "lod_group", "never_stream"):
            try:
                unreal.log("Referenz %s: %s = %s" % (REFERENZ, feld, ref.get_editor_property(feld)))
            except Exception as e:  # noqa: BLE001 - Feld existiert je Version nicht
                unreal.log("Referenz %s: %s nicht lesbar (%s)" % (REFERENZ, feld, e))
    else:
        unreal.log_warning("Referenz-Asset fehlt: %s" % REFERENZ)

    mat = EAL.load_asset("/Game/Materials/City/M_WbSign")
    if mat:
        for feld in ("blend_mode", "two_sided", "shading_model", "opacity_mask_clip_value"):
            try:
                unreal.log("M_WbSign: %s = %s" % (feld, mat.get_editor_property(feld)))
            except Exception as e:  # noqa: BLE001
                unreal.log("M_WbSign: %s nicht lesbar (%s)" % (feld, e))
    else:
        unreal.log_warning("M_WbSign nicht gefunden.")
    return 0


def vorbild():
    """Einstellungen des Referenz-Assets lesen (leer, wenn es fehlt)."""
    ref = EAL.load_asset(REFERENZ)
    werte = {}
    if not ref:
        unreal.log_warning("Referenz-Asset fehlt: %s" % REFERENZ)
        return werte
    for feld in UEBERNOMMEN:
        try:
            werte[feld] = ref.get_editor_property(feld)
        except Exception as e:  # noqa: BLE001
            unreal.log_warning("Referenz: %s nicht lesbar (%s)" % (feld, e))
    return werte


def importiere(alle=False):
    global VORBILD
    VORBILD = vorbild()
    ziele = pngs() if alle else ohne_asset()
    if not ziele:
        unreal.log("Schild-Texturen: nichts zu tun - jedes PNG hat ein Asset.")
        return pruefe()

    aufgaben = []
    for pfad in ziele:
        name = os.path.basename(pfad)[:-4]
        aufgabe = unreal.AssetImportTask()
        aufgabe.set_editor_property("filename", pfad)
        aufgabe.set_editor_property("destination_path", ZIEL)
        aufgabe.set_editor_property("destination_name", name)
        aufgabe.set_editor_property("automated", True)
        aufgabe.set_editor_property("replace_existing", True)
        aufgabe.set_editor_property("save", True)
        aufgaben.append(aufgabe)

    unreal.log("Schild-Texturen: importiere %d PNGs nach %s." % (len(aufgaben), ZIEL))
    tools.import_asset_tasks(aufgaben)

    fehler = 0
    for pfad in ziele:
        name = os.path.basename(pfad)[:-4]
        tex = EAL.load_asset(ZIEL + "/" + name)
        if not tex:
            unreal.log_warning("Import fehlgeschlagen: %s" % name)
            fehler += 1
            continue
        for feld, wert in VORBILD.items():
            try:
                tex.set_editor_property(feld, wert)
            except Exception as e:  # noqa: BLE001
                unreal.log_warning("%s: %s nicht setzbar (%s)" % (name, feld, e))
        EAL.save_loaded_asset(tex)

    unreal.log("Schild-Texturen: %d importiert, %d fehlerhaft." % (len(ziele) - fehler, fehler))
    return pruefe() or (1 if fehler else 0)


def pruefe():
    alle = pngs()
    fehlend = ohne_asset()
    unreal.log("Schild-Texturen: %d PNG, %d ohne Asset." % (len(alle), len(fehlend)))
    for name in ("Sign_206", "Sign_1022-10", "Sign_274-25", "Sign_274-250"):
        tex = EAL.load_asset(ZIEL + "/" + name)
        unreal.log("  %s -> %s" % (name, "geladen" if tex else "FEHLT"))
    return 1 if fehlend else 0


def main():
    modus = os.environ.get("SIGN_IMPORT_MODE", "").lower()
    if modus == "info":
        return info()
    return importiere(alle=(modus == "alle"))


if __name__ == "__main__":
    import sys

    sys.exit(main())
