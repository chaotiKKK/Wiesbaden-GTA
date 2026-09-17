"""Belegt den Bus-Materialfix: schreibt, was SM_Bus wirklich benutzt.

Warum eine DATEI: `unreal.log()` aus einem pythonscript-Commandlet erreicht den
umgeleiteten cmd-Strom in diesem Projekt NICHT (wb_fix_bus_materials.log enthaelt
keine einzige ###-Zeile, obwohl der Commandlet "executed successfully" meldet).
Belege gehoeren darum in eine Datei, die man nach dem Lauf liest.

Geprueft wird:
  * Elternmaterial JEDER Materialinstanz des Bus-Meshes (soll: Projekt-Master)
  * Nanite-Flag des Masters (ohne das ersetzt UE es auf dem Nanite-Mesh durch Grau)
  * BaseColor-Eingang des Masters (Textur oder nichts)
  * BaseColorTexture jeder Instanz (die Textur darf beim Umhaengen nicht verloren gehen)

Aufruf: Tools\\verify_bus_materials.cmd  ->  Saved/Diagnose/bus_material_check.txt
"""
import unreal

MESH = "/Game/Vehicles/Bus/SM_Bus.SM_Bus"
MASTER = "/Game/Vehicles/Bus/M_WbBusBody"
OUT = "C:/freebuff/WiesbadenReal_Sicherung/WiesbadenReal/Saved/Diagnose/bus_material_check.txt"

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
MP = unreal.MaterialProperty

lines = []


def out(msg):
    lines.append(str(msg))


def tex_name(value):
    try:
        return value.get_name()
    except Exception:
        return "?"


out("Bus-Materialpruefung %s" % MESH)
out("=" * 70)

master = EAL.load_asset(MASTER)
if master is None:
    out("MASTER FEHLT: %s" % MASTER)
else:
    node = MEL.get_material_property_input_node(master, MP.MP_BASE_COLOR)
    out("Master %s:" % MASTER)
    out("  Nanite-Flag (used_with_nanite) = %s" % master.get_editor_property("used_with_nanite"))
    out("  BaseColor <- %s" % (node.get_class().get_name() if node else "NICHTS"))

mesh = EAL.load_asset(MESH)
if mesh is None:
    out("ABBRUCH: Mesh nicht ladbar.")
else:
    entries = mesh.get_editor_property("static_materials")
    parents = {}
    with_tex, without_tex = 0, 0
    textures = set()
    for entry in entries:
        inst = entry.get_editor_property("material_interface")
        if not isinstance(inst, unreal.MaterialInstanceConstant):
            parents["(keine Instanz: %s)" % inst.get_class().get_name()] = \
                parents.get("(keine Instanz: %s)" % inst.get_class().get_name(), 0) + 1
            continue
        parent = inst.get_editor_property("parent")
        pname = parent.get_path_name() if parent is not None else "(kein Elternmaterial)"
        parents[pname] = parents.get(pname, 0) + 1
        found = None
        for tv in inst.get_editor_property("texture_parameter_values"):
            try:
                if tv.get_editor_property("parameter_info").get_editor_property("name") == "BaseColorTexture":
                    found = tv.get_editor_property("parameter_value")
            except Exception:
                pass
        if found is not None:
            with_tex += 1
            textures.add(tex_name(found))
        else:
            without_tex += 1

    out("")
    out("Mesh: %d Materialeintraege." % len(entries))
    out("Elternmaterialien (%d verschiedene):" % len(parents))
    for pname in sorted(parents):
        out("  %4d x %s" % (parents[pname], pname))
    out("")
    out("BaseColorTexture gesetzt: %d Instanzen, fehlend: %d" % (with_tex, without_tex))
    out("verschiedene Texturen: %d" % len(textures))
    for t in sorted(textures)[:6]:
        out("  %s" % t)

    ok_parent = parents == {MASTER} or (len(parents) == 1 and MASTER in list(parents)[0])
    out("")
    out("URTEIL Elternmaterial: %s" % ("ok - alle Instanzen haengen am Projekt-Master"
                                       if ok_parent else "NICHT ok - siehe Liste oben"))
    out("URTEIL Texturen: %s" % ("ok - keine Instanz hat ihre BaseColor verloren"
                                 if without_tex == 0 else
                                 "NICHT ok - %d Instanzen ohne BaseColorTexture" % without_tex))

with open(OUT, "w", encoding="utf-8") as handle:
    handle.write("\n".join(lines) + "\n")
