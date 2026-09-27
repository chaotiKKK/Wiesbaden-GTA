"""Fassaden-Varianten mit Ladenausschnitt: /Game/Materials/City/Cut/M_WbFacade_<X>_Cut.

WOZU: Ein Laden im Erdgeschoss eines GEBACKENEN Hauses (Dennos Laden am
Sedanplatz 5) ist von aussen unsichtbar - die Fassade ist eine geschlossene
Chunk-Flaeche, die Fenster sind nur ins Material gemalt. Ohne Re-Bake laesst
sich das Haus nicht aufschneiden.

Stattdessen: Kopie jedes M_WbFacade_* als MASKED-Material. Eine Opacity Mask
verwirft die Pixel in einem ORIENTIERTEN Kasten (Mitte ShopCenter, Laengs-
achse ShopAxisU, Halbmasse ShopHalf in cm; ShopHalf = 0 -> nichts geschnitten).
Der Laden-Actor setzt diese Varianten nur auf die EINE Chunk-Komponente, die das
Haus traegt - alle uebrigen Fassaden der Stadt bleiben opak und unveraendert.
Alles andere (Fensterraster, Fensterlicht-Parameter) erbt die Kopie.

UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=Tools/create_facade_cut_materials.py
"""
import json
import os
import unreal

eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
mp = unreal.MaterialProperty
SRC = '/Game/Materials/City'
DST = '/Game/Materials/City/Cut'
NAMES = ['Putz', 'Beton', 'Glas', 'Backstein', 'Sandstein', 'Fachwerk']

# Der Ausschnitt-Code kommt aus der gemeinsamen Quelle Tools/denno_shop.json;
# der Test WiesbadenReal.World.DennoShop.CutMaterials vergleicht ihn mit dem Code
# in den Assets (und jede Kopie strukturell mit ihrem Original).
CODE = json.loads(open(os.path.join(unreal.Paths.project_dir(), 'Tools', 'denno_shop.json'),
                       encoding='utf-8').read())['ausschnitt_hlsl']

eal.make_directory(DST)
done = []
for n in NAMES:
    src = '%s/M_WbFacade_%s' % (SRC, n)
    dst = '%s/M_WbFacade_%s_Cut' % (DST, n)
    if not eal.does_asset_exist(src):
        done.append('%s FEHLT' % src)
        continue
    if eal.does_asset_exist(dst):
        eal.delete_asset(dst)
    mat = eal.duplicate_asset(src, dst)
    mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
    mat.set_editor_property('opacity_mask_clip_value', 0.5)

    pos = mel.create_material_expression(mat, unreal.MaterialExpressionWorldPosition, -3200, 1600)
    params = {}
    for i, pname in enumerate(['ShopCenter', 'ShopAxisU', 'ShopHalf']):
        p = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -3200, 1750 + i * 150)
        p.set_editor_property('parameter_name', pname)
        p.set_editor_property('default_value', unreal.LinearColor(0, 0, 0, 0) if pname != 'ShopAxisU'
                              else unreal.LinearColor(1, 0, 0, 0))
        params[pname] = p
    custom = mel.create_material_expression(mat, unreal.MaterialExpressionCustom, -2900, 1700)
    custom.set_editor_property('code', CODE)
    custom.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    custom.set_editor_property('description', 'LadenAusschnitt')
    inputs = []
    for iname in ['P', 'C', 'A', 'H']:
        ci = unreal.CustomInput()
        ci.set_editor_property('input_name', iname)
        inputs.append(ci)
    custom.set_editor_property('inputs', inputs)
    mel.connect_material_expressions(pos, '', custom, 'P')
    mel.connect_material_expressions(params['ShopCenter'], 'RGB', custom, 'C')
    mel.connect_material_expressions(params['ShopAxisU'], 'RGB', custom, 'A')
    mel.connect_material_expressions(params['ShopHalf'], 'RGB', custom, 'H')
    mel.connect_material_property(custom, '', mp.MP_OPACITY_MASK)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    done.append('%s blend=%s' % (dst, mat.get_editor_property('blend_mode')))
unreal.log('###FACADE_CUT### ' + ' | '.join(done))
