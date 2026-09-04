# Justiert die Textur-Kachelung der AAA-verdrahteten Boden-/Fahrbahn-Materialien,
# damit die Masstaebe zum tatsaechlichen Inhalt der AAA-Texturen passen.
#
# Hintergrund: rewire_baked_aaa.py hat in den M_Wb*-Materialien die alten
# lokalen Textursamples gegen die AAA-Saetze getauscht - OHNE die Kachelung
# anzufassen. Die Kachelung war aber auf die alten Texturen abgestimmt. Die
# AAA-Quellen bilden andere Weltgroessen ab (aus Saved/_aaa_source gemessen):
#
#   Asphalt012      ~2 m Patch  -> M_WbRoad stand auf 4 m/Kachel (2x zu grob)
#   Grass001        ~1 m Patch  -> M_WbTerrain stand auf 3 m/Kachel (zu gross)
#   PavingStones037 ~2 m Patch  -> M_WbSidewalk 2 m/Kachel PASST (unveraendert)
#   Plaster001      skalenneutral -> Fassaden unveraendert (Fenstergraph-Risiko)
#
# Die Stadt-UVs laufen in METERN (surface_from_texture), also ist die Kachelung
# 1/Meter-pro-Kachel. Geaendert wird NUR die TextureCoordinate-Kachelung der
# betroffenen Materialien - Textursamples, Vertexfarben (Fahrbahnmarkierung,
# Wischspuren) und Graphen bleiben unangetastet.
#
# Aufruf (Editor oder -run=pythonscript):
#   UnrealEditor-Cmd.exe WiesbadenReal.uproject -run=pythonscript
#       -script="Tools/adjust_city_tiling.py" -unattended -nosplash -nop4

import os

import unreal

mel = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
CITY = "/Game/Materials/City/"

# Material -> Meter pro Kachel (beide Achsen in Metern bei Boden/Fahrbahn).
TARGET_METERS = {
    "M_WbRoad": 2.5,      # Asphalt feiner, Aggregat realistisch
    "M_WbTerrain": 1.5,   # Gras kleiner, Halme realistisch statt weichgezoomt
}


def log(msg):
    unreal.log("###WBTILE### %s" % msg)


def main():
    for name, meters in TARGET_METERS.items():
        mat = unreal.load_asset(CITY + name)
        if not mat:
            log("WARNUNG: %s fehlt" % name)
            continue

        tiling = 1.0 / float(meters)
        touched = 0
        for e in mel.get_material_expressions(mat):
            if "TextureCoordinate" in e.get_class().get_name():
                e.set_editor_property("u_tiling", tiling)
                e.set_editor_property("v_tiling", tiling)
                touched += 1

        if touched == 0:
            log("WARNUNG: %s ohne TextureCoordinate - nichts geaendert" % name)
            continue

        mel.recompile_material(mat)
        EAL.save_loaded_asset(mat)
        log("%s: %.1f m/Kachel (Tiling %.3f) auf %d TexCoord gesetzt."
            % (name, meters, tiling, touched))

    # Abschluss-Sentinel (Editor-Log erreicht den stdout-Redirect nicht sicher).
    try:
        root = unreal.Paths.project_dir()
        with open(os.path.join(root, "Saved", "tiling_done.txt"), "w", encoding="utf-8") as f:
            f.write("ok\n")
    except OSError as exc:
        log("Sentinel nicht geschrieben: %s" % exc)

    log("FERTIG.")
    if os.environ.get("WB_QUIT"):
        unreal.SystemLibrary.quit_editor()


main()
