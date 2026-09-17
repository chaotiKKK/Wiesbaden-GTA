"""Zeigt, was die Materialinstanzen des Bus-Meshes wirklich tragen.

Befund bisher: SM_Bus nutzt 41 MaterialInstanceConstants; als Basismaterial kam
/InterchangeAssets/gltf/M_Default heraus - ein graues Material ohne Textur. Hier
wird geprueft, ob die Instanzen ihre Texturen als PARAMETER tragen (dann fehlt
nur das passende Elternmaterial) oder ob die Texturen unbenutzt herumliegen.

  UnrealEditor-Cmd.exe WiesbadenReal.uproject -run=pythonscript
      -script=Tools/inspect_bus_materials.py -stdout -unattended -nopause -nop4
"""
import unreal

MESH = "/Game/Vehicles/Bus/SM_Bus.SM_Bus"
EAL = unreal.EditorAssetLibrary


def log(msg):
    unreal.log("###WBBUSMAT### %s" % msg)


mesh = EAL.load_asset(MESH)
if mesh is None:
    log("FEHLER: %s nicht ladbar." % MESH)
    raise SystemExit(1)

entries = mesh.get_editor_property("static_materials")
log("Mesh hat %d Materialeintraege." % len(entries))

chain = []
for i, entry in enumerate(entries[:3]):
    mat = entry.get_editor_property("material_interface")
    log("--- Eintrag %d: %s (%s)" % (i, mat.get_path_name(), mat.get_class().get_name()))
    for prop in ("parent", "texture_parameter_values", "scalar_parameter_values",
                 "vector_parameter_values"):
        try:
            value = mat.get_editor_property(prop)
        except Exception as exc:
            log("   %s: nicht lesbar (%s)" % (prop, exc))
            continue
        if prop == "parent":
            p = value.get_path_name() if value else "NICHTS"
            log("   %s = %s" % (prop, p))
            chain.append(p)
        else:
            log("   %s = %d Eintraege" % (prop, len(value)))
            for item in list(value)[:4]:
                try:
                    name = item.get_editor_property("parameter_info").get_editor_property("name")
                except Exception:
                    name = str(item)
                val = item.get_editor_property("parameter_value")
                log("      %s -> %s" % (name, val.get_path_name() if hasattr(val, "get_path_name") else val))

# Alle Eltern der 41 Eintraege sammeln: eine Antwort statt einer Stichprobe.
parents = {}
for entry in entries:
    mat = entry.get_editor_property("material_interface")
    if mat is None:
        continue
    try:
        p = mat.get_editor_property("parent")
    except Exception:
        p = None
    key = p.get_path_name() if p else "NICHTS"
    parents[key] = parents.get(key, 0) + 1
log("Elternmaterialien aller Eintraege:")
for k, n in sorted(parents.items(), key=lambda kv: -kv[1]):
    log("   %4d x %s" % (n, k))
log("ENDE")
