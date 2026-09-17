"""Gibt dem Bus-Aussenmesh ein eigenes Master-Material - und damit seine Texturen.

BEFUND (Tools/inspect_bus_materials.py + inspect_bus_nanite.py):
`/Game/Vehicles/Bus/SM_Bus` hat Nanite, und alle 41 Materialeintraege sind
MaterialInstanceConstants auf `/InterchangeAssets/gltf/MaterialInstances/
MI_Default_Opaque_DS` - das Default-Material des glTF-Importers, das im
ENGINE-Inhalt liegt. Die Instanzen tragen ihre echten Texturen selbst
(`BaseColorTexture` = eswe_bus_lang_gelenkbus_tripo_part_N_basecolor), aber:

  * das Elternmaterial hat kein Nanite-Flag -> Unreal ersetzt es auf dem
    Nanite-Mesh durch das Default-Material. Im Log je Material:
    "Material .../tripo_part_N_material missing usage flag Nanite! Default
    Material will be used in game." Der Bus rendert dann grau, ohne Textur.
  * ein Engine-Material als Eltern heisst ausserdem: das Aussehen des Busses
    haengt an Plugin-Inhalt, nicht am Projekt.

Fix: ein Master-Material im Projekt mit genau den Parameter-Namen, die die
Instanzen schon ueberschreiben (BaseColorTexture, MetallicFactor,
RoughnessFactor), Nanite-Flag an, und alle Instanzen darauf umhaengen. Die
Texturen bleiben, wo sie sind - die Instanzen tragen sie weiter.

Aufruf:
  UnrealEditor-Cmd.exe WiesbadenReal.uproject -run=pythonscript
      -script=Tools/fix_bus_materials.py -stdout -unattended -nopause -nop4
"""
import unreal

MESH = "/Game/Vehicles/Bus/SM_Bus.SM_Bus"
MASTER_DIR = "/Game/Vehicles/Bus"
MASTER = "M_WbBusBody"

EAL = unreal.EditorAssetLibrary
ATH = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
MP = unreal.MaterialProperty


def log(msg):
    unreal.log("###WBBUSFIX### %s" % msg)


# -- 1) Master-Material ------------------------------------------------------
mpath = "%s/%s" % (MASTER_DIR, MASTER)
if EAL.does_asset_exist(mpath):
    EAL.delete_asset(mpath)
mat = ATH.create_asset(MASTER, MASTER_DIR, unreal.Material, unreal.MaterialFactoryNew())
if mat is None:
    log("ABBRUCH: %s liess sich nicht anlegen." % mpath)
    raise SystemExit(1)

# Nanite-Flag: ohne das ersetzt UE das Material auf dem Nanite-Bus durch Grau.
try:
    mat.set_editor_property("used_with_nanite", True)
except Exception as exc:
    log("Nanite-Flag nicht setzbar: %s" % exc)
mat.set_editor_property("two_sided", False)

base = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, -700, -60)
base.set_editor_property("parameter_name", "BaseColorTexture")
rough = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -700, 180)
rough.set_editor_property("parameter_name", "RoughnessFactor")
rough.set_editor_property("default_value", 0.8)
metal = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -700, 300)
metal.set_editor_property("parameter_name", "MetallicFactor")
metal.set_editor_property("default_value", 0.0)

ok = True
# BaseColor/Roughness/Metallic anschliessen (Textur-Ausgang heisst "RGB").
if not MEL.connect_material_property(base, "RGB", MP.MP_BASE_COLOR):
    log("FEHLER: BaseColorTexture -> BaseColor liess sich NICHT verbinden.")
    ok = False
for node, prop, what in ((rough, MP.MP_ROUGHNESS, "RoughnessFactor -> Roughness"),
                         (metal, MP.MP_METALLIC, "MetallicFactor -> Metallic")):
    if not MEL.connect_material_property(node, "", prop):
        log("FEHLER: %s liess sich NICHT verbinden." % what)
        ok = False
MEL.recompile_material(mat)
EAL.save_loaded_asset(mat)
try:
    node = MEL.get_material_property_input_node(mat, MP.MP_BASE_COLOR)
    log("Master %s: BaseColor <- %s, Nanite-Flag=%s"
        % (mpath, node.get_class().get_name() if node else "NICHTS",
           mat.get_editor_property("used_with_nanite")))
except Exception as exc:
    log("Master %s: Gegenprobe nicht moeglich (%s)" % (mpath, exc))

# -- 2) Instanzen umhaengen --------------------------------------------------
mesh = EAL.load_asset(MESH)
if mesh is None:
    log("ABBRUCH: %s nicht ladbar." % MESH)
    raise SystemExit(1)

moved, already, failed, noinst = 0, 0, 0, 0
textures = set()
for entry in mesh.get_editor_property("static_materials"):
    inst = entry.get_editor_property("material_interface")
    if not isinstance(inst, unreal.MaterialInstanceConstant):
        noinst += 1
        continue
    # Die Textur der Instanz merken: sie muss das Umhaengen ueberleben.
    for tv in inst.get_editor_property("texture_parameter_values"):
        try:
            name = tv.get_editor_property("parameter_info").get_editor_property("name")
        except Exception:
            name = "?"
        if name == "BaseColorTexture":
            textures.add(tv.get_editor_property("parameter_value").get_name())
    try:
        current = inst.get_editor_property("parent")
        if current is not None and current.get_path_name() == mat.get_path_name():
            already += 1
            continue
        inst.set_editor_property("parent", mat)
        EAL.save_loaded_asset(inst)
        now = inst.get_editor_property("parent")
        if now is not None and now.get_path_name() == mat.get_path_name():
            moved += 1
        else:
            failed += 1
            log("NICHT umgehaengt: %s" % inst.get_path_name())
    except Exception as exc:
        failed += 1
        log("FEHLER bei %s: %s" % (inst.get_path_name(), exc))

log("ENDE: %d umgehaengt, %d waren schon, %d fehlgeschlagen, %d ohne Instanz; %d Texturen in den Instanzen."
    % (moved, already, failed, noinst, len(textures)))
if failed or noinst:
    log("ACHTUNG: nicht alle Eintraege konnten umgehaengt werden - siehe Zeilen oben.")
