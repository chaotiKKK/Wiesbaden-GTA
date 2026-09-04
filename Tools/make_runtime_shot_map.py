# Legt eine dedizierte, BELEUCHTETE Karte fuer den Laufzeit-Stadtbuild an
# (__AaaRuntimeShot). Der Laufzeit-Pfad (WiesbadenCitySubsystem + CityActor)
# spawnt KEINE Sonne - das macht sonst nur der Editor-Bake ueber
# AWiesbadenWorldBuilder::EnsureLightingActors. Ohne Licht bliebe die Stadt
# schwarz. Hier werden dieselben Lichtakteure mit denselben Parametern angelegt,
# damit der kombinierte Effekt aus Beleuchtung + verdrahteten AAA-Materialien
# sichtbar wird. Die Karte hat KEINE gebackene Stadt -> das Subsystem erzwingt
# den Laufzeit-Build (ShouldRunRuntimeBuild: bCityBakedInLevel=false).
import unreal

MAP = "/Game/Maps/__AaaRuntimeShot"

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

# Frische, leere Karte (vorhandene zuerst entfernen - sonst schlaegt new_level fehl).
if unreal.EditorAssetLibrary.does_asset_exist(MAP):
    unreal.EditorAssetLibrary.delete_asset(MAP)
les.new_level(MAP)

HIGH = unreal.Vector(0.0, 0.0, 20000.0)


def spawn(cls, loc=HIGH, rot=unreal.Rotator(0, 0, 0)):
    return eas.spawn_actor_from_class(cls, loc, rot)


# -- Sonne (Pitch -42, Yaw -35; Atmosphaeren-Sonne) -------------------------
# Rotator per Keyword, damit pitch=-42 sicher stimmt (positional ist die
# Reihenfolge mehrdeutig -> sonst stand die Sonne am Horizont = Daemmerung).
sun = spawn(unreal.DirectionalLight, HIGH, unreal.Rotator(pitch=-42.0, yaw=-35.0, roll=0.0))
sunc = sun.get_component_by_class(unreal.DirectionalLightComponent)
sunc.set_mobility(unreal.ComponentMobility.MOVABLE)
# 10 lx = UE5-Default; WeatherFX skaliert zur Laufzeit. Falls WeatherFX auf der
# Laufzeitkarte nicht greift, sorgt der hoehere Wert fuer echtes Tageslicht.
sunc.set_intensity(10.0)
sunc.set_editor_property("atmosphere_sun_light", True)

# -- Himmelslicht (3.5; Echtzeit-Capture) -----------------------------------
sky = spawn(unreal.SkyLight)
skyc = sky.get_component_by_class(unreal.SkyLightComponent)
skyc.set_mobility(unreal.ComponentMobility.MOVABLE)
skyc.set_editor_property("real_time_capture", True)
skyc.set_intensity(3.5)

# -- Himmels-Atmosphaere + Hoehennebel --------------------------------------
spawn(unreal.SkyAtmosphere)
fog = spawn(unreal.ExponentialHeightFog, unreal.Vector(0, 0, 0))
fogc = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
fogc.set_editor_property("fog_density", 0.008)
fogc.set_editor_property("fog_height_falloff", 0.15)
fogc.set_editor_property("start_distance", 4000.0)

# -- PlayerStart nahe dem Ursprung (Stadt wird um den Georeferenz-Origin
#    gebaut). Etwas erhoeht, damit der Pawn nicht im Boden steckt. ----------
spawn(unreal.PlayerStart, unreal.Vector(0.0, 0.0, 500.0))

# -- Helikopter fuer die Luftaufnahme. WbHeli BESITZT einen vorhandenen Heli
#    (spawnt keinen) -> hier einen platzieren, damit die Konsolenbefehle
#    WbHeli/WbHeliGoto zur Laufzeit greifen. Startposition ~40 m ueber Origin. --
heli_cls = unreal.load_class(None, "/Script/WiesbadenReal.WiesbadenHelicopter")
if heli_cls:
    spawn(heli_cls, unreal.Vector(0.0, 0.0, 4000.0))
    unreal.log("RUNTIMESHOT: Helikopter platziert.")
else:
    unreal.log_warning("RUNTIMESHOT: Heli-Klasse nicht ladbar.")

les.save_current_level()
unreal.log("RUNTIMESHOT: %s mit Sonne/Himmel/Atmosphaere/Nebel/PlayerStart gespeichert." % MAP)
