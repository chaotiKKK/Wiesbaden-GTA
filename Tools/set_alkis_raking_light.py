# Setzt die Beleuchtung der gebackenen Alkis4-Karte auf mehr TIEFE:
#  - Sonne von fast senkrecht (pitch=-88, Zenit -> flach) auf ein tiefes,
#    RAKENDES Streiflicht (pitch=-18) -> lange Schatten, plastische Fassaden.
#  - SkyLight-Ambient von 3.5 auf 1.5 -> Schatten werden nicht mehr flach
#    aufgefuellt, mehr Kontrast/Tiefe.
# WeatherFX bleibt unberuehrt: es setzt NUR Sonnenfarbe + -intensitaet
# (WiesbadenWeatherFX.cpp Z.450-451), NICHT Rotation und NICHT das SkyLight.
# Daher kein Konflikt - die Rotation/Ambient-Aenderung bleibt bestehen.
import unreal

MAP = "/Game/Maps/WiesbadenCity_Alkis4"
SUN_PITCH = -18.0
SUN_YAW = -35.0
SKY_INTENSITY = 1.5

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
les.load_level(MAP)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

EAL = unreal.EditorAssetLibrary
changed = []
changed_sun = changed_sky = 0
for a in eas.get_all_level_actors():
    if isinstance(a, unreal.DirectionalLight):
        a.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=SUN_PITCH, yaw=SUN_YAW), False)
        a.modify(True)   # WP-External-Actor als geaendert markieren
        changed.append(a)
        changed_sun += 1
        unreal.log("RAKING: Sonne -> pitch=%.1f yaw=%.1f" % (SUN_PITCH, SUN_YAW))
    elif isinstance(a, unreal.SkyLight):
        c = a.get_component_by_class(unreal.SkyLightComponent)
        c.set_intensity(SKY_INTENSITY)
        a.modify(True)
        changed.append(a)
        changed_sky += 1
        unreal.log("RAKING: SkyLight-Ambient -> %.2f" % SKY_INTENSITY)

# Die External-Actor-Pakete der geaenderten Actors gezielt speichern
# (save_current_level nimmt sie bei einer WP-Map nicht mit).
pkgs = [a.get_package() for a in changed]
for p in pkgs:
    unreal.log("RAKING: Paket %s" % p.get_name())
saved = unreal.EditorLoadingAndSavingUtils.save_packages(pkgs, False)
unreal.log("RAKING: fertig (%d Sonne(n), %d SkyLight(s), save_packages=%s)." %
           (changed_sun, changed_sky, saved))
