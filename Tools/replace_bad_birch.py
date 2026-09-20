"""Ersetzt das defekte Birken-Mesh SM_WbTree_07 durch eine Kopie eines guten Baums.

Befund (systematisch belegt): SM_WbTree_07 rendert in JEDER Material-/LOD-
Konfiguration als weiss/braunes Zacken-Spinnen-Gebilde - die Geometrie selbst ist
untauglich (die "modellierten Blaetter" stehen als radiale Spikes, keine Krone).
Material (M_WbPlant, zweiseitig) und LOD-Varianten aendern nur die Farbe, nicht
die Form.

Die Baeume sind in der gebackenen Karte gebacken und referenzieren das Asset
SM_WbTree_07 ueber seinen Pfad. Wir ersetzen dieses Asset durch eine Kopie von
SM_WbTree_01 (bewaehrter Blendswap-Baum) -> die ~1/7 "Birken"-Instanzen rendern
sofort als gute Baeume, OHNE Re-Bake.
"""

import unreal

EAL = unreal.EditorAssetLibrary
GOOD = "/Game/Vegetation/Meshes/SM_WbTree_01"
BAD = "/Game/Vegetation/Meshes/SM_WbTree_07"


def log(m):
    unreal.log("###REPLBIRCH### %s" % m)


if not EAL.does_asset_exist(GOOD):
    log("FEHLER: guter Baum %s fehlt" % GOOD)
    raise SystemExit(1)

# Defektes Birken-Asset entfernen (Referenzen zeigen weiter auf den Pfad).
if EAL.does_asset_exist(BAD):
    EAL.delete_asset(BAD)
    log("altes SM_WbTree_07 geloescht")

# Aufraeumen: die vom Fix-Versuch angelegten Birken-Instanzen (jetzt verwaist).
for junk in ("/Game/Vegetation/Materials/MI_SM_WbTree_07_Leaf",
             "/Game/Vegetation/Materials/MI_SM_WbTree_07_Bark",
             "/Game/Vegetation/Meshes/Tree_Birch_Leaf_Summer_Mat",
             "/Game/Vegetation/Meshes/Tree_Birch_Bark_Mat"):
    if EAL.does_asset_exist(junk):
        EAL.delete_asset(junk)
        log("verwaist geloescht: %s" % junk)

# Kopie des guten Baums an den Pfad des defekten legen.
dup = EAL.duplicate_asset(GOOD, BAD)
if dup is None:
    log("FEHLER: Duplizieren fehlgeschlagen")
    raise SystemExit(1)
EAL.save_loaded_asset(dup)
log("SM_WbTree_07 ist jetzt eine Kopie von SM_WbTree_01 (guter Baum). FERTIG.")

if unreal.SystemLibrary.get_command_line().find("-unattended") >= 0:
    unreal.SystemLibrary.quit_editor()
