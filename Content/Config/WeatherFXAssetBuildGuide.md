# WeatherFX-Assets im Niagara-Editor bauen

Diese Anleitung fuehrt durch das manuelle Erzeugen der fuenf Wetter-Assets nach
`WeatherFXCatalog.json` (editorfaehige Blaupause). Danach laedt die
`UWiesbadenWeatherFXComponent` die Assets **automatisch** aus den kanonischen
Pfaden (kein Details-Panel-Setup noetig) und `ValidateSystem` prueft den
Parameter-Vertrag.

**Konformitaet vorab headless pruefen (jederzeit):**
```
node Tools/verify_weatherfx_catalog.mjs        -> 69/69 PASS
```

**Editor-Verifikation nach dem Bauen (alle Tests):**
```
UnrealEditor-Cmd.exe WiesbadenReal.uproject -ExecCmds="Automation RunTests WiesbadenReal.Weather; Quit" -unattended -nop4 -nullrhi
```
- `WiesbadenReal.Weather.FXCatalogModules` prueft die Modul-Referenzen,
- `WiesbadenReal.Weather.FXValidation` + `FXAssetPaths` den Vertrag,
- zur Laufzeit warnt `ValidateSystem` bei fehlenden User-Parametern und
  `WarnMissingFixedBounds` bei fehlenden Fixed Bounds (je System einmalig).

---

## Gemeinsame Schritte (fuer jedes Asset)

1. Content Browser: Ordner `Content/Niagara` anlegen.
2. Rechtsklick -> FX -> **Niagara System** -> "New system from a template" ->
   "Empty" auswaehlen (leeres System; Emitter von Hand anlegen).
3. System auf den Zielnamen umbenennen (siehe je Asset) und speichern.
4. In das System wechseln, Emitter laut Katalog anlegen (CPU-Sim!).
5. Module per Stack-Editor hinzufuegen (Pfade unter `Engine/Plugins/FX/
   Niagara/Content/Modules/...` sind in Klammern angegeben).
6. **User-Parameter** je Katalog anlegen (Parameter-Editor, Namespace `User`):
   Name, Typ, Default, Min, Max exakt wie gelistet.
7. **Fixed Bounds** am System setzen (Details -> Fixed Bounds aktivieren):
   Center + Extent exakt wie gelistet (sonst Culling-Warnung).
8. Speichern. Zielpfad je Asset: `path`-Feld.

---

## 1. NS_WeatherRain (Regen) -> /Game/Niagara/NS_WeatherRain

- **Emitter** `Rain`, CPU-Sim.
- **Emitter Spawn:** `SpawnRate` (`Modules/Emitter/SpawnRate`) <- `User.RainSpawnRate`.
- **Particle Update:** `AddVelocity` (`Modules/Spawn/Velocity/AddVelocity`) mit
  Vektor (0, 0, -1400) cm/s; `GravityForce` (`Modules/Update/Forces/GravityForce`)
  mit (0, 0, -980) cm/s^2.
- **Renderer:** Sprite (Material mit RainSpawnRate-Skalierung der Streifenlaenge).
- **User-Parameter:**
  | Name | Typ | Default | Min | Max |
  |---|---|---|---|---|
  | RainSpawnRate | Float | 0 | 0 | 2000 |
  | WindSpeed | Float | 3 | 0 | 30 |
  | SunLightColor | LinearColor | [1,1,1,1] | - | - |
- **Fixed Bounds:** Center [0,0,0], Extent [20000, 20000, 10000].

## 2. NS_WeatherSnow (Schnee) -> /Game/Niagara/NS_WeatherSnow

- **Emitter** `Snow`, CPU-Sim.
- **Emitter Spawn:** `SpawnRate` <- `User.SnowSpawnRate`.
- **Particle Update:** `AddVelocity` (0,0,-250); `GravityForce` (0,0,-60);
  `Drag` (`Modules/Update/Forces/Drag`) mit 0.6.
- **Renderer:** Sprite, weisse runde Partikel.
- **User-Parameter:** SnowSpawnRate (0..1200), WindSpeed (3, 0..30),
  SunLightColor (LinearColor [1,1,1,1]).
- **Fixed Bounds:** Extent [20000, 20000, 10000].

## 3. NS_WeatherFog (Nebel) -> /Game/Niagara/NS_WeatherFog

- **Emitter** `FogPatch`, CPU-Sim (grosser weicher Sprite-Klotz).
- **Emitter Spawn:** `SpawnBurst_Instantaneous`
  (`Modules/Emitter/SpawnBurst_Instantaneous`) Count 1, SpawnTime 0.
- **Particle Update:** `Color` (`Modules/Update/Color/Color`) - **Alpha-Kanal =
  Opacity** an `User.FogDensity` (0..1) gebunden (realer Ersatz fuer
  "SetOpacity").
- **Renderer:** Sprite, gross, weiche Kante, Alpha-Blend.
- **User-Parameter:** FogDensity (0, 0..1), WindSpeed (3, 0..30),
  SunLightColor (LinearColor).
- **Fixed Bounds:** Extent [30000, 30000, 3000].

## 4. NS_WeatherClouds (Wolken) -> /Game/Niagara/NS_WeatherClouds

- **Emitter** `CloudLayer`, CPU-Sim (Himmelsschale ueber der Stadt).
- **Emitter Spawn:** `SpawnBurst_Instantaneous` Count 1, SpawnTime 0.
- **Particle Update:** `Color` - **Alpha = Opacity** an `User.CloudOpacity` (0..1).
- **Renderer:** Sprite/MeshRenderer, grosse flache Quads, Alpha-Blend.
- **User-Parameter:** CloudOpacity (0.15, 0..1), WindSpeed (3, 0..30),
  SunLightColor (LinearColor).
- **Fixed Bounds:** Extent [60000, 60000, 20000] - **zwingend**, sonst
  Distanz-Culling.

## 5. NS_WeatherStorm (Gewitter) -> /Game/Niagara/NS_WeatherStorm

- **Emitter 1** `Lightning`, CPU-Sim.
  - **Emitter Spawn:** `SpawnBurst_Instantaneous` - **ein Blitz pro Loop**:
    Emitter-Lifetime = `User.LightningInterval`, Burst bei Loop-Start
    (SpawnTime 0, Count 1); 0 = kein Loop/kein Blitz (realer Ersatz fuer
    "SpawnBurstInterval").
  - **Particle Update:** `ParticleState` (`Modules/Update/Lifetime/ParticleState`)
    Lifetime 0.15 s (realer Ersatz fuer "SetLifeTime"); `UpdateAge` ergaenzen;
    `RibbonWidth` (`Modules/Ribbons/RibbonWidth`) 2..6 cm.
  - **Renderer:** Ribbon/Beam, helles Cyan-Weiss, Additive.
- **Emitter 2** `RainHeavy`, CPU-Sim.
  - **Emitter Spawn:** `SpawnRate` <- `User.RainSpawnRate` (FX-Seite ~1200).
  - **Renderer:** Sprite wie NS_WeatherRain.
- **User-Parameter:** LightningInterval (0, 0..30), RainSpawnRate (0, 0..2000),
  WindSpeed (3, 0..30), SunLightColor (LinearColor).
- **Fixed Bounds:** Extent [20000, 20000, 12000].

---

## Nach dem Bauen

1. Alle fuenf Assets unter `Content/Niagara/` gespeichert? (Pfade aus dem
   `path`-Feld).
2. `node Tools/verify_weatherfx_catalog.mjs` -> 69/69 PASS.
3. Editor-Tests laufen lassen (Befehl oben) - `FXCatalogModules` muss gruen
   sein; `FXValidation`/`FXAssetPaths` ebenso.
4. Beim Stadt-Spawn prueft `ValidateSystem` die User-Parameter und
   `WarnMissingFixedBounds` die Fixed Bounds (Warnungen = Vertragsbruch).
