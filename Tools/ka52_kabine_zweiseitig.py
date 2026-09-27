"""Die vier Kabinenmaterialien der Ka-52 zweiseitig schalten.

ANLASS (gemessen am 26.09.2026): In der Cockpit-Ansicht war keine Kabine zu
sehen - nur ein flaches graues Band. Drei Messungen zusammen:

  1. Die Augen liegen IN der Kabine (Ka52GeraetTest: Augenhoehe 133 cm ueber
     dem Boden, Kasten 71..220 cm, Sitzkissen X +9..+59 / Y -371..-421).
  2. Die Huellle ist geschlossen (0 nicht-gepaarte Kanten) und NICHT
     einheitlich gewickelt: von der Pilotenaus zeigen 166 der 342 Flaechen
     vom Auge weg (Rueckseiten, die der Renderer verwirft) und 130 zum Auge
     hin. Sichtbar bleiben damit nur die Innenflaechen der gegenueber-
     liegenden Wand - ein flaches Band, kein Cockpit.
  3. Der Renderer verwirft Rueckseiten, weil die Materialien aus dem FBX
     einseitig angelegt wurden (Import-Material ohne two_sided).

Damit der Pilotensitz Tafel, Sitze und Rahmen sieht, muessen die vier
Kabinenmaterialien zweiseitig sein. Das ist eine Asset-Aenderung, deshalb
steht sie hier als Skript und nicht als einmaliger Editor-Klick.

Aufruf:
  UnrealEditor-Cmd <projekt> -run=pythonscript -script=<diese datei>
"""

import unreal

MATERIALIEN = [
    "/Game/Vehicles/Ka52/Ka52_Instrument",
    "/Game/Vehicles/Ka52/Ka52_Sitzbezug",
    "/Game/Vehicles/Ka52/Ka52_Metall",
    "/Game/Vehicles/Ka52/Ka52_Schirm",
]

gespeichert = []
AUS = r"C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Saved\tmp\kabine_zweiseitig.txt"
zeilen = []


def sag(t):
    zeilen.append(str(t))
    unreal.log(str(t))


# Die vier Assets sind MaterialInstanceConstant (aus dem FBX-Import). Eine
# Instanz hat KEIN two_sided - die Eigenschaft sitzt am Elternmaterial. Darum
# die Kette hochlaufen und dort setzen.
for pfad in MATERIALIEN:
    inst = unreal.load_asset(pfad)
    if inst is None:
        sag("FEHLT: " + pfad)
        continue
    sag("%s: Klasse %s" % (inst.get_name(), inst.get_class().get_name()))
    ziel = inst
    tiefe = 0
    while ziel is not None and tiefe < 5:
        try:
            ziel.get_editor_property("two_sided")
            break
        except Exception:
            try:
                ziel = ziel.get_editor_property("parent")
            except Exception:
                ziel = None
        tiefe += 1
    if ziel is None:
        sag("  kein Elternmaterial mit two_sided gefunden")
        continue
    vorher = ziel.get_editor_property("two_sided")
    if vorher:
        sag("  %s: bereits zweiseitig" % ziel.get_name())
        gespeichert.append(ziel.get_path_name())
        continue
    ziel.set_editor_property("two_sided", True)
    # post_edit_change gibt es in Python nicht; recompile_material baut den
    # Shader neu, sonst bleibt die alte Variante im laufenden Material.
    try:
        unreal.MaterialEditingLibrary.recompile_material(ziel)
    except Exception as e:
        sag("  recompile fehlgeschlagen: %s" % e)
    try:
        unreal.MaterialEditingLibrary.update_material_instance(inst)
    except Exception as e:
        sag("  Instanz-Update fehlgeschlagen: %s" % e)
    gespeichert.append(ziel.get_path_name())
    sag("  %s: two_sided %s -> %s" % (ziel.get_name(), vorher,
                                      ziel.get_editor_property("two_sided")))

# Speichern: ohne das bleiben die Aenderungen nur in der Sitzung. WICHTIG ist
# das Elternmaterial - es liegt NICHT im Kabinenordner (der Import legt es
# beim ersten FBX neben dem Mesh ab), ein save_directory auf
# /Game/Vehicles/Ka52 erreicht es nicht.
for name in gespeichert:
    pfad = unreal.EditorAssetLibrary.save_asset(name, only_if_is_dirty=False)
    sag("gesichert: %s -> %s" % (name, pfad))

with open(AUS, "w", encoding="utf-8") as f:
    f.write("\n".join(zeilen) + "\n")
unreal.log("KABINE ZWEISEITIG GESCHRIEBEN: " + AUS)
