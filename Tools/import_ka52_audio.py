"""Importiert die Ka-52-Flugsounds als SoundWave-Assets nach /Game/Audio/Ka52.

Quelle sind die WAV aus Tools/make_ka52_audio.py:
  ka52_rotor.wav   -> S_Ka52_Rotor   Schleife: Rotorblatt-Ticken
  ka52_engine.wav  -> S_Ka52_Engine  Schleife: TV3-117-Turbine
  ka52_mg.wav      -> S_Ka52_MG      einmal: 2A42-Schuss
  ka52_wind.wav    -> S_Ka52_Wind    Schleife: Fahrtwind

Die Schleifen werden am importierten Asset gesetzt, nicht nur im WAV-Header:
UE liest die Schleife aus dem Asset, ein gesetzter RIFF-Loop-Punkt allein
macht aus dem Asset keinen endlosen Ton. Der Name der Property wird
erprobt und protokolliert, weil sie zwischen den Versionen unterschiedlich
heißen kann - "unbekannt" darf im Bericht nicht als Erfolg durchgehen.

Wie beim Modell-Import (import_ka52_cockpit.py) geht das Ergebnis nach
DATEI, nicht nach stdout: Prints aus -run=pythonscript erreichen weder
Konsole noch Log (AGENTS.md).
"""

import os
import traceback

import unreal

DEST = "/Game/Audio/Ka52"
QUELLE = os.path.join(unreal.Paths.project_dir(), "Content", "Data", "Raw",
                      "Ka52", "audio")
BERICHT = os.path.join(unreal.Paths.project_dir(), "Saved", "Diagnose",
                       "ka52", "import_ka52_audio_ergebnis.txt")
EAL = unreal.EditorAssetLibrary

# (Quelldatei, Zielname, Schleife?)
AUFTRAG = (
    ("ka52_rotor.wav", "S_Ka52_Rotor", True),
    ("ka52_engine.wav", "S_Ka52_Engine", True),
    ("ka52_mg.wav", "S_Ka52_MG", False),
    ("ka52_wind.wav", "S_Ka52_Wind", True),
)

zeilen = []


def sag(text):
    zeilen.append(text)
    unreal.log("###WBKA52A### %s" % text)


def abschreiben(fehler=None):
    os.makedirs(os.path.dirname(BERICHT), exist_ok=True)
    with open(BERICHT, "w", encoding="utf-8") as fh:
        fh.write("Import Ka-52 Flugsounds\n")
        fh.write("ERGEBNIS: %s\n" % ("FEHLER" if fehler else "OK"))
        if fehler:
            fh.write("FEHLER: %s\n" % fehler)
        fh.write("\n".join(zeilen) + "\nENDE\n")


def setze_schleife(wave, gewuenscht):
    """Setzt bzw. loescht die Schleife und sagt, ueber welchen Weg."""
    if not gewuenscht:
        for name in ("looping", "bLooping"):
            try:
                wave.set_editor_property(name, False)
                return "aus"
            except Exception:
                continue
        return "kannte keine Schleifen-Property"
    for name in ("looping", "bLooping"):
        try:
            wave.set_editor_property(name, True)
            return name
        except Exception:
            continue
    return "PROPERTY UNBEKANNT"


def main():
    EAL.make_directory(DEST)
    for datei, ziel, schleife in AUFTRAG:
        quelle = os.path.join(QUELLE, datei)
        if not os.path.exists(quelle):
            raise RuntimeError("WAV fehlt: %s (python Tools/make_ka52_audio.py)"
                               % quelle)

        task = unreal.AssetImportTask()
        task.filename = quelle
        task.destination_path = DEST
        task.destination_name = ziel
        task.automated = True
        task.replace_existing = True
        task.save = True
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

        pfad = "%s/%s.%s" % (DEST, ziel, ziel)
        asset = EAL.load_asset(pfad)
        if asset is None:
            raise RuntimeError("Asset fehlt nach dem Import: %s" % pfad)
        if not isinstance(asset, unreal.SoundWave):
            raise RuntimeError("%s ist %s, kein SoundWave"
                               % (pfad, type(asset).__name__))

        weg = setze_schleife(asset, schleife)
        sag("%-16s -> %s  %.2f s  %d Hz  %d Kanal(le)  Schleife=%s"
            % (datei, ziel, asset.get_editor_property("duration"),
               asset.get_editor_property("sample_rate"),
               asset.get_editor_property("num_channels"), weg))
        if schleife and weg not in ("looping", "bLooping"):
            raise RuntimeError(
                "Schleife liess sich nicht setzen (%s) - %s wuerde im Spiel "
                "nach einmal Abspielen verstummen" % (weg, ziel))

    sag("alle vier Flugsounds da")
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)


try:
    main()
    abschreiben()
except Exception:  # noqa: BLE001 - der Bericht soll den Grund nennen
    abschreiben(traceback.format_exc())
    raise
