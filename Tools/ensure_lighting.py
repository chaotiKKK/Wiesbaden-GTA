"""
Stellt die Beleuchtung der gebackenen Stadt-Map her.

MUSS nach jedem Stadt-Neubau laufen: build_alkis.py legt mit new_level() ein
frisches Level an und speichert es als Ziel-Map. Alles, was vorher an
Lichtakteuren konfiguriert war, ist danach weg - die Stadt rendert dann
vollstaendig schwarz, obwohl Geometrie und Materialien in Ordnung sind.

Aufruf:
  UnrealEditor-Cmd.exe WiesbadenReal.uproject -run=pythonscript
      -script="Tools/ensure_lighting.py" -unattended -nosplash
"""
import os
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from karte import standard_karte_pfad   # EINE Quelle: Config/DefaultEngine.ini

import os

import unreal

# Kartenpfad zentral.
#
# Welche Karte gemeint ist, sagt Config/DefaultEngine.ini (Tools/karte.py); die alte wurde
# entfernt. Werkzeuge, die noch auf sie zeigten, luden ins Leere UND
# meldeten es nicht - build_materials.py schrieb daraufhin
# "Landscape-Material neu verknuepft: 0 Actor(en)" statt 1.
#
# Ueber die Umgebungsvariable WB_MAP umstellbar, damit der naechste
# Kartenwechsel nicht wieder vier Dateien anfassen muss.
MAP = os.environ.get("WB_MAP", standard_karte_pfad())


def log(msg):
    unreal.log("###WB### %s" % msg)


def set_movable(actor):
    """Mobility sitzt an der Root-Komponente, nicht am Actor."""
    root = actor.get_editor_property("root_component")
    if root:
        root.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)


def ensure_lighting():
    EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors = EAS.get_all_level_actors()

    def find(cls):
        return [a for a in actors if a and isinstance(a, cls)]

    have = {
        "DirectionalLight": find(unreal.DirectionalLight),
        "SkyLight": find(unreal.SkyLight),
        "SkyAtmosphere": find(unreal.SkyAtmosphere),
        "ExponentialHeightFog": find(unreal.ExponentialHeightFog),
    }
    for k, v in have.items():
        log("BESTAND %s: %d" % (k, len(v)))

    high = unreal.Vector(0.0, 0.0, 20000.0)

    # -- Sonne -------------------------------------------------------------
    if have["DirectionalLight"]:
        sun = have["DirectionalLight"][0]
    else:
        # Pitch -42 Grad: mittlerer Sonnenstand, wirft lesbare Schatten.
        sun = EAS.spawn_actor_from_class(
            unreal.DirectionalLight, high, unreal.Rotator(0.0, -42.0, -35.0))
        sun.set_actor_label("Sonne")
        log("ANGELEGT DirectionalLight")
    set_movable(sun)
    comp = sun.get_component_by_class(unreal.DirectionalLightComponent)
    if comp:
        # 10 lx = UE5-Default einer platzierten Sonne. WeatherFX skaliert
        # diesen Wert zur Laufzeit ueber MaxSunIntensity nach Sonnenstand.
        comp.set_editor_property("intensity", 10.0)
        comp.set_editor_property("atmosphere_sun_light", True)
        comp.set_editor_property("cast_shadows", True)
        comp.set_editor_property("dynamic_shadow_distance_movable_light", 30000.0)

    # -- Himmelslicht ------------------------------------------------------
    if have["SkyLight"]:
        sky = have["SkyLight"][0]
    else:
        sky = EAS.spawn_actor_from_class(unreal.SkyLight, high)
        sky.set_actor_label("Himmelslicht")
        log("ANGELEGT SkyLight")
    set_movable(sky)
    skc = sky.get_component_by_class(unreal.SkyLightComponent)
    if skc:
        # Ohne Umgebungslicht werden alle verschatteten Fassaden schwarz.
        skc.set_editor_property("real_time_capture", True)
        # 6.0 statt des UE-Defaults 1.0. Beide sichtbaren Fassadenseiten einer
        # Strassenschlucht liegen fast immer im Schatten und werden NUR vom
        # Himmelslicht aufgehellt; bei 1.0 liefen sie gegen Schwarz, weil die
        # begrenzte Belichtung auf die sonnenbeschienene Flaeche eingestellt ist.
        skc.set_editor_property("intensity", 6.0)

    # -- Atmosphaere -------------------------------------------------------
    if not have["SkyAtmosphere"]:
        a = EAS.spawn_actor_from_class(unreal.SkyAtmosphere, high)
        a.set_actor_label("Atmosphaere")
        log("ANGELEGT SkyAtmosphere")

    if not have["ExponentialHeightFog"]:
        f = EAS.spawn_actor_from_class(
            unreal.ExponentialHeightFog, unreal.Vector(0.0, 0.0, 0.0))
        f.set_actor_label("Hoehennebel")
        fc = f.get_component_by_class(unreal.ExponentialHeightFogComponent)
        if fc:
            fc.set_editor_property("fog_density", 0.008)
            fc.set_editor_property("fog_height_falloff", 0.15)
            fc.set_editor_property("start_distance", 4000.0)
        log("ANGELEGT ExponentialHeightFog")



def ensure_exposure():
    """
    Begrenzt die Auto-Belichtung.

    Ohne Begrenzung hebt sie die Szene so weit an, dass die Farben verwaschen:
    Ziegeldaecher werden lachsfarben, Gehwege fast weiss. Grund ist die
    niedrige mittlere Helligkeit - Asphalt und Wiese haben physikalisch kleine
    Albedo-Werte, die Automatik gleicht das aus und nimmt dabei den helleren
    Flaechen den Kontrast.
    """
    EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    existing = [a for a in EAS.get_all_level_actors()
                if a and isinstance(a, unreal.PostProcessVolume)]
    log("BESTAND PostProcessVolume: %d" % len(existing))

    volume = existing[0] if existing else EAS.spawn_actor_from_class(
        unreal.PostProcessVolume, unreal.Vector(0.0, 0.0, 0.0))
    if volume is None:
        return
    if not existing:
        volume.set_actor_label("Belichtung")
        log("ANGELEGT PostProcessVolume")

    volume.set_editor_property("unbound", True)

    settings = volume.get_editor_property("settings")
    settings.set_editor_property("override_auto_exposure_method", True)
    settings.set_editor_property("auto_exposure_method",
                                 unreal.AutoExposureMethod.AEM_HISTOGRAM)
    # Enges Fenster statt fester Belichtung: Tag/Nacht bleibt moeglich.
    settings.set_editor_property("override_auto_exposure_min_brightness", True)
    settings.set_editor_property("auto_exposure_min_brightness", 0.6)
    settings.set_editor_property("override_auto_exposure_max_brightness", True)
    settings.set_editor_property("auto_exposure_max_brightness", 1.6)
    settings.set_editor_property("override_auto_exposure_bias", True)
    settings.set_editor_property("auto_exposure_bias", 0.0)
    volume.set_editor_property("settings", settings)


unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(MAP)
log("Map geladen: %s" % MAP)
ensure_lighting()
ensure_exposure()
unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
log("FERTIG")
