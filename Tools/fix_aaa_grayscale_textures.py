# Behebt den Compile-Fehler ALLER M_AAA_-Materialien: die Roughness-/AO-Texturen
# (*_R, *_AO) wurden als sRGB-FARBE importiert, im Material aber als
# Linear-Grayscale gesampelt -> "Sampler type is Linear Grayscale, should be
# Linear Color" -> Material kompiliert nicht -> Default-Material (graues
# Schachbrett) im Laufzeit-Build. Fix: die Grauwert-Texturen auf
# TC_GRAYSCALE + srgb=False stellen, dann kompilieren die Materialien mit dem
# Linear-Grayscale-Sampler sauber. Danach alle M_AAA_-Materialien neu kompilieren.
import unreal
mel = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary

TEX = "/Game/Materials/AAA/Textures"
MAT_ROOT = "/Game/Materials/AAA"

fixed_tex = 0
for path in EAL.list_assets(TEX, recursive=False, include_folder=False):
    name = path.split("/")[-1].split(".")[0]
    if name.endswith("_R") or name.endswith("_AO"):
        t = unreal.load_asset(path)
        if not t:
            continue
        t.set_editor_property("srgb", False)
        t.set_editor_property("compression_settings",
                              unreal.TextureCompressionSettings.TC_GRAYSCALE)
        EAL.save_loaded_asset(t)
        fixed_tex += 1
        unreal.log("GRAYFIX: %s -> TC_GRAYSCALE, srgb=False" % name)

# Alle M_AAA_-Materialien neu kompilieren (Sampler passt jetzt zur Textur).
recompiled = 0
for path in EAL.list_assets(MAT_ROOT, recursive=False, include_folder=False):
    name = path.split("/")[-1].split(".")[0]
    if name.startswith("M_AAA_"):
        m = unreal.load_asset(path)
        if isinstance(m, unreal.Material):
            mel.recompile_material(m)
            EAL.save_loaded_asset(m)
            recompiled += 1

unreal.log("GRAYFIX: %d Grauwert-Texturen korrigiert, %d M_AAA_-Materialien neu kompiliert." % (fixed_tex, recompiled))
