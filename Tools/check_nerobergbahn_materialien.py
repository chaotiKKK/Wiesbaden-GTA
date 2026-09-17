"""Prueft die Materialflags der Nerobergbahn-Assets in Unreal.

Warum: Der Wagen ist (wie alle Nerobergbahn-Meshes) mit `MeshBuilder.box()`
gebaut, dessen Flaechen nach INNEN zeigen - das ist die Projektkonvention.
Sichtbar ist im Spiel trotzdem alles. Entweder sind die Materialien
zweiseitig, oder es wird nicht gecullt. Fuer den Innenraum ist genau das die
entscheidende Frage: bei zweiseitigen Materialien braucht die Inneneinrichtung
keine eigene Windung.

Aufruf (voller Editor):
  UnrealEditor.exe WiesbadenReal.uproject
      -ExecCmds="py Tools/check_nerobergbahn_materialien.py" -unattended -nosplash
"""

import unreal

EAL = unreal.EditorAssetLibrary

MASTERS = ["/Game/Nerobergbahn/Materials/M_WbNb_Paint",
           "/Game/Nerobergbahn/Materials/M_WbNb_Tiled",
           "/Game/Nerobergbahn/Materials/M_WbNb_Decal"]


def log(msg):
    unreal.log("###WBNBMAT### %s" % msg)


for path in MASTERS:
    mat = EAL.load_asset(path)
    if mat is None:
        log("FEHLT: %s" % path)
        continue
    zwei = mat.get_editor_property("two_sided")
    blend = mat.get_editor_property("blend_mode")
    shading = mat.get_editor_property("shading_model")
    log("%-46s two_sided=%s blend=%s shading=%s"
        % (path.split("/")[-1], zwei, blend, shading))

# Der Wagen als Ganzes: Anzahl Slots und ob das Mesh zweifach vorliegt.
mesh = EAL.load_asset("/Game/Nerobergbahn/Meshes/SM_WbNbWagen")
if mesh is not None:
    statics = mesh.get_editor_property("static_materials")
    log("SM_WbNbWagen: %d Slots" % len(statics))
    for i, sm in enumerate(statics):
        m = sm.get_material_interface()
        name = m.get_name() if m else "?"
        zweifach = ""
        if isinstance(m, unreal.MaterialInstanceConstant):
            parent = m.get_editor_property("parent")
            if parent:
                zweifach = " parent_two_sided=%s" % parent.get_editor_property("two_sided")
        log("  Slot %2d %-18s %s%s"
            % (i, str(sm.get_editor_property("material_slot_name")), name, zweifach))

unreal.SystemLibrary.quit_editor()
