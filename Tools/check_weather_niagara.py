"""Prueft die Wetter-Niagara-Systeme gegen Content/Config/WeatherFXCatalog.json.

Was dieses Skript kann und was nicht - beides nachgemessen, nicht vermutet
(Tools/probe_niagara_templates.py):

  KANN     Liegt das Asset am Pfad, den GetDefaultAssetPath() laedt? Das ist
           der Fehler, der am haeufigsten passiert (falscher Ordner,
           Tippfehler im Namen) und der im Spiel nur als "nichts zu sehen"
           auffaellt.

  KANN NICHT  Die User-Parameter und die Fixed Bounds lesen. unreal.NiagaraSystem
           exportiert weder get_exposed_parameters() noch die Eigenschaften
           fixed_bounds / b_fixed_bounds nach Python. Eine fruehere Fassung
           dieses Skripts hat es versucht und still "<nicht auslesbar>"
           gemeldet - das sah aus wie eine Pruefung und war keine.

Diese beiden Punkte prueft deshalb das SPIEL: ValidateSystem() und
WarnMissingFixedBounds() in WiesbadenWeatherFX.cpp schreiben beim ersten Spawn
eine Warnung ins Protokoll. Der Ablauf steht in
docs/Wetter_Niagara_Anleitung.md, Teil 3.

Aufruf:
  UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<diese Datei>
"""

import json

import unreal

EAL = unreal.EditorAssetLibrary

PROJECT = r"C:/freebuff/WiesbadenReal_Sicherung/WiesbadenReal"
CATALOG = PROJECT + "/Content/Config/WeatherFXCatalog.json"
RESULT = PROJECT + "/Saved/weather_niagara.txt"

# Was der C++-Code setzt (ApplyParams) und verlangt (GetRequiredUserParameters),
# praefixfrei. Steht hier, damit das Ergebnis die Namen zum Abtippen nennt -
# pruefen kann das Skript sie nicht.
REQUIRED = {
    "NS_WeatherRain": ["WindSpeed", "SunLightColor", "RainSpawnRate"],
    "NS_WeatherSnow": ["WindSpeed", "SunLightColor", "SnowSpawnRate"],
    "NS_WeatherStorm": ["WindSpeed", "SunLightColor", "RainSpawnRate", "LightningInterval"],
}


def main():
    out = []

    with open(CATALOG, encoding="utf-8") as f:
        catalog = json.load(f)
    specs = {a["id"]: a for a in catalog.get("assets", [])}

    found = 0
    for asset_id, required in REQUIRED.items():
        path = f"/Game/Niagara/{asset_id}"
        spec = specs.get(asset_id)
        out.append("")
        out.append(f"== {asset_id} ==")

        if not EAL.does_asset_exist(path):
            out.append(f"   FEHLT: {path}")
            out.append("   -> docs/Wetter_Niagara_Anleitung.md")
            continue

        asset = EAL.load_asset(path)
        if not isinstance(asset, unreal.NiagaraSystem):
            out.append(f"   FALSCHE KLASSE: {type(asset).__name__} statt NiagaraSystem")
            continue

        found += 1
        out.append(f"   Vorhanden: {path}")
        out.append(f"   Muss exponieren (vom Spiel geprueft): {', '.join('User.' + p for p in required)}")

        if spec and spec.get("bounds", {}).get("type") == "Fixed":
            e = spec["bounds"]["extent"]
            out.append(f"   Fixed Bounds laut Katalog: +-({e[0]}, {e[1]}, {e[2]}) cm "
                       f"- im Editor am System-Knoten setzen")

    out.insert(0, f"Wetter-Niagara: {found} von {len(REQUIRED)} Assets liegen am richtigen Pfad.")
    out.append("")
    out.append("User-Parameter und Bounds pruefen NICHT hier, sondern im Spiel:")
    out.append("  -game -WbWeather=Rain  ->  Saved/Logs/WiesbadenReal.log, Zeilen 'WeatherFX:'")

    with open(RESULT, "w", encoding="utf-8") as f:
        f.write("\n".join(out))
    for line in out:
        unreal.log(line)


main()
