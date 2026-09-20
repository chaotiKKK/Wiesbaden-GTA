"""Legt ein WEGWERF-System an der Stelle von NS_WeatherRain an.

Zweck: den Weg C++ -> Asset -> sichtbares Partikel EINMAL ganz durchpruefen,
bevor jemand Stunden im Niagara-Editor verbringt. Das kopierte Fountain-System
hat die verlangten User-Parameter NICHT - genau richtig, so zeigt derselbe Lauf
auch, ob die Vertragswarnung anschlaegt.

Aufruf mit "anlegen" oder "loeschen" ueber ARGV.
"""
import sys
import unreal

EAL = unreal.EditorAssetLibrary
SRC = "/Niagara/DefaultAssets/Templates/Systems/FountainLightweight"
DST = "/Game/Niagara/NS_WeatherRain"

unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(["/Niagara"], True)

mode = "anlegen"
for a in sys.argv:
    if a in ("anlegen", "loeschen"):
        mode = a

if mode == "loeschen":
    ok = EAL.delete_asset(DST) if EAL.does_asset_exist(DST) else True
    unreal.log(f"Wegwerf-System geloescht: {ok}")
else:
    if EAL.does_asset_exist(DST):
        unreal.log("WARNUNG: NS_WeatherRain existiert bereits - nichts angelegt.")
    else:
        new = EAL.duplicate_asset(SRC, DST)
        EAL.save_asset(DST)
        unreal.log(f"Wegwerf-System angelegt: {new!r}")
