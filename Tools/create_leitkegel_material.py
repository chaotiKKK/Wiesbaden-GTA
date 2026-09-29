"""Material fuer die Leitkegel des Geschicklichkeitsparcours (AWiesbadenParcours).

Grundfarbe UND Eigenleuchten kommen aus demselben Parameter "Farbe": der Kegel
ist in der Sonne orange (nicht milchweiss wie das Lampenglas, dessen
Grundfarbe fest ist) und leuchtet mit "Glow" leicht nach - fluoreszierend,
im Gegenlicht nicht schwarz, nachts sichtbar.

    UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<abs. Pfad zu diesem Skript>

Ergebnis in Saved/Diagnose/leitkegel_material.txt (print erreicht den
Cmd-Strom nicht).
"""
import os

import unreal

eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
mp = unreal.MaterialProperty
tools = unreal.AssetToolsHelpers.get_asset_tools()

root = '/Game/Materials/City'
name = 'M_WbLeitkegel'
path = root + '/' + name
ergebnis = os.path.join(unreal.Paths.project_saved_dir(), 'Diagnose', 'leitkegel_material.txt')

if eal.does_asset_exist(path):
    eal.delete_asset(path)
mat = tools.create_asset(name, root, unreal.Material, unreal.MaterialFactoryNew())

farbe = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -600, -120)
farbe.set_editor_property('parameter_name', 'Farbe')
farbe.set_editor_property('default_value', unreal.LinearColor(1.0, 0.28, 0.02, 1))
glow = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -600, 120)
glow.set_editor_property('parameter_name', 'Glow')
glow.set_editor_property('default_value', 0.4)
emit = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -300, 60)
mel.connect_material_expressions(farbe, '', emit, 'A')
mel.connect_material_expressions(glow, '', emit, 'B')
mel.connect_material_property(farbe, '', mp.MP_BASE_COLOR)
mel.connect_material_property(emit, '', mp.MP_EMISSIVE_COLOR)
rough = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -300, 260)
rough.set_editor_property('r', 0.45)
mel.connect_material_property(rough, '', mp.MP_ROUGHNESS)

mel.recompile_material(mat)
gespeichert = eal.save_asset(path, only_if_is_dirty=False)

os.makedirs(os.path.dirname(ergebnis), exist_ok=True)
with open(ergebnis, 'w', encoding='utf-8') as f:
    f.write('%s gespeichert=%s vorhanden=%s\n' % (path, gespeichert, eal.does_asset_exist(path)))
