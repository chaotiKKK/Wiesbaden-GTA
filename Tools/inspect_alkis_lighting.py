# Liest die Beleuchtungs-Actors der gebackenen Alkis4-Karte aus (Sonne + SkyLight),
# damit der Vorher-Zustand dokumentiert ist, bevor Sonnenstand/Ambient geaendert
# werden. Aendert nichts.
import unreal

MAP = "/Game/Maps/WiesbadenCity_Alkis4"
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
les.load_level(MAP)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

for a in eas.get_all_level_actors():
    if isinstance(a, unreal.DirectionalLight):
        c = a.get_component_by_class(unreal.DirectionalLightComponent)
        rot = a.get_actor_rotation()
        unreal.log("ALKISLIGHT Sonne: pitch=%.1f yaw=%.1f roll=%.1f intensity=%.2f" %
                   (rot.pitch, rot.yaw, rot.roll, c.get_editor_property("intensity")))
    elif isinstance(a, unreal.SkyLight):
        c = a.get_component_by_class(unreal.SkyLightComponent)
        unreal.log("ALKISLIGHT SkyLight: intensity=%.2f realtime=%s" %
                   (c.get_editor_property("intensity"),
                    c.get_editor_property("real_time_capture")))
unreal.log("ALKISLIGHT: fertig.")
