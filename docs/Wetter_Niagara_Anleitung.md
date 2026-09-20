# Regen und Schnee im Niagara-Editor anlegen

Ziel: `NS_WeatherRain` und `NS_WeatherSnow` unter `/Game/Niagara/`, passend zu
`Content/Config/WeatherFXCatalog.json`. Danach zeigt `-WbWeather=Rain`
sichtbaren Regen.

> **Es regnet bereits — ohne Niagara.** Seit dem 18.09.2026 zeichnet ein
> Post-Process-Material den Niederschlag im Bildraum
> (`/Game/Materials/PostProcess/M_WbWeatherOverlay`, erzeugt von
> `Tools/add_weather_postprocess.py`, gesteuert von
> `UWiesbadenWeatherFXComponent::UpdateOverlay`). Das braucht keine Handarbeit
> und ist sofort in jeder Karte da.
>
> Diese Anleitung bleibt trotzdem gueltig: Bildschirm-Niederschlag hat keine
> Tiefe. Er verschwindet nicht hinter Haeusern, wird von Bruecken und Tunneln
> nicht abgeschirmt und traegt keine Spritzer. Wer das will, braucht Partikel
> — also die Systeme unten. Beide Wege laufen nebeneinander; das Overlay
> haengt nicht an den NS_-Assets.

---

## Vorab: warum das von Hand sein muss

Alles andere in diesem Projekt (Materialien, Meshes, Bake) entsteht per Python.
Niagara nicht - nachgeprueft, nicht vermutet
(`Tools/probe_niagara_templates.py`, Ergebnis in
`Saved/probe_niagara_templates.txt`):

| Zugang | Ergebnis |
|---|---|
| `unreal.NiagaraEditorLibrary` | existiert nicht |
| `unreal.NiagaraSystemFactoryNew` | existiert, legt aber nur ein **leeres** System an |
| `set_editor_property("emitters_to_add_to_new_system", ...)` | `Failed to find property` |
| `unreal.NiagaraSystem` | kennt weder Emitter noch `fixed_bounds` noch `exposed_parameters` |

Emitter, Module und User-Parameter sind aus Python nicht erreichbar. Der Editor
ist der einzige Weg.

## Vorab: ein Defekt, der vorher stumm war

Die Wetter-Komponente haengt am **GameMode**, und der hat keine
Wurzelkomponente. Der alte Code machte

```cpp
USceneComponent* AttachRoot = GetOwner()->GetRootComponent();   // == nullptr
if (!bWanted || !AttachRoot) { return; }                        // immer raus
```

- `SpawnSystemAttached` wurde also **nie** gerufen. Ein fehlerfrei gebautes
`NS_WeatherRain` waere trotzdem unsichtbar geblieben. Das ist behoben: der
Effekt wird frei gespawnt und je Tick auf die **Kameraposition** gesetzt.

Das hat eine Folge fuer den Bau unten: das System steht immer dort, wo du
stehst. Die Partikel muessen also **um den Ursprung des Systems herum**
entstehen, nicht ueber der Stadt. Genau deshalb reichen die festen Bounds von
+-200 m aus dem Katalog.

---

## Teil 1 - `NS_WeatherRain`

### 1. Ordner und System anlegen

1. Content Browser -> `Content` -> Rechtsklick -> **New Folder** -> `Niagara`
   (falls noch nicht da).
2. In `Content/Niagara` Rechtsklick -> **FX -> Niagara System**.
3. Im Assistenten: **New system from a set of selected emitters**.
4. In der Liste `Fountain` suchen -> auf **+** klicken (er wandert nach unten in
   die Liste "Emitters to Add") -> **Finish**.
5. Namen vergeben: `NS_WeatherRain`. Doppelklick oeffnet den Editor.

> Warum `Fountain` und nicht die Vorlage `RecycleParticlesInView`? Fountain
> bringt genau die vier Module mit, die der Katalog verlangt - Spawn Rate,
> Add Velocity, Gravity Force, Sprite Renderer. Es ist damit nichts versteckt,
> das man spaeter sucht. Die Kamera-Nachfuehrung erledigt der C++-Code.

### 2. Emitter umbenennen

Im **System Overview** (mittlere Spalte) Rechtsklick auf den Emitter-Knoten
`Fountain` -> **Rename** -> `Rain`.

### 3. Die drei User-Parameter anlegen

Diese Namen sind ein **Vertrag**: `WiesbadenWeatherFX.cpp` setzt genau sie, und
`ValidateSystem()` meldet im Protokoll, wenn einer fehlt. Tippfehler fallen
also auf - aber erst im Spiel.

Im System Overview oben den Knoten **User Parameters** anklicken, dann im
Details-Panel rechts je Parameter auf **+**:

| Name | Typ | Startwert |
|---|---|---|
| `RainSpawnRate` | Float | `0.0` |
| `WindSpeed` | Float | `3.0` |
| `SunLightColor` | Linear Color | `(1, 1, 1, 1)` |

Sie erscheinen danach als `User.RainSpawnRate` usw. **Ohne** `User.` tippen -
den Namensraum setzt Niagara selbst.

### 4. Spawn Rate an den User-Parameter binden

Emitter `Rain` -> Gruppe **Emitter Update** -> Modul **Spawn Rate**.

Neben dem Eingabefeld `SpawnRate` auf das kleine Dreieck klicken ->
**Link Inputs** -> `User.RainSpawnRate`.

> Der Katalog nennt diese Stufe `EmitterSpawn`. Im Editor liegt **Spawn Rate**
> in **Emitter Update** - es ist eine laufende Rate, kein einmaliger Wert. Die
> Bezeichnung im Katalog ist lose; der Modulname stimmt.

Damit gilt: `RainSpawnRate = 0` -> kein Partikel. Genau das erwartet der
C++-Code, um den Effekt abzuschalten.

### 5. Wo die Tropfen entstehen

Gruppe **Particle Spawn**:

* Modul **Sphere Location** -> Muelleimer-Symbol -> loeschen.
* Auf **+** neben "Particle Spawn" -> **Location -> Box Location** hinzufuegen.
  * `Box Size` = `(4000, 4000, 400)`
  * `Offset` = `(0, 0, 1200)` - die Tropfen entstehen 12 m **ueber** der Kamera.

### 6. Fallgeschwindigkeit und Schwerkraft

Gruppe **Particle Spawn**, Modul **Add Velocity**:

* `Velocity Mode` auf **Linear** stellen.
* `Velocity` = `(0, 0, -1400)` cm/s  <- Katalogwert.

Gruppe **Particle Update**, Modul **Gravity Force**:

* `Gravity` = `(0, 0, -980)` cm/s^2  <- Katalogwert.

> **Add Velocity gehoert in Particle Spawn, nicht in Particle Update.** Der
> Katalog listet es unter `updateStage`; dort addiert das Modul die 1400 cm/s
> in *jedem* Bild erneut, und die Tropfen beschleunigen ins Bodenlose. Der
> Wert stimmt, die Stufe im Katalog ist falsch - sie ist mit diesem Hinweis
> korrigiert.

### 7. Lebensdauer, Groesse, Farbe

Gruppe **Particle Spawn**, Modul **Initialize Particle**:

* `Lifetime` = `1.5` s - reicht, um von +12 m bis deutlich unter die Kamera
  zu fallen.
* `Sprite Size Mode` = **Non-Uniform**, `Sprite Size` = `(2.5, 45)` -
  ein Streifen, kein Punkt.
* `Color` = hellgrau-blau `(0.55, 0.62, 0.72)`, **Alpha `0.35`** - Regen ist
  durchsichtig; volle Deckkraft sieht aus wie Konfetti.

Fuer die Farbtemperatur (der C++-Code setzt sie nach Sonnenstand): neben
`Color` das Dreieck -> **Link Inputs** -> `User.SunLightColor`. Dann bestimmt
das Umgebungslicht die Faerbung.

### 8. Der Streifen muss in Fallrichtung liegen

Gruppe **Render**, Modul **Sprite Renderer**:

* `Alignment` = **Velocity Aligned**
* `Facing Mode` = **Face Camera Plane**
* `Material` = `/Niagara/DefaultAssets/DefaultSpriteMaterial`

Ohne "Velocity Aligned" stehen die Streifen quer - es regnet dann seitwaerts,
egal wie die Geschwindigkeit eingestellt ist.

### 9. Wind

Modul **Add Velocity** (Particle Spawn), Eintrag `Velocity` aufklappen:

* bei **X** das Dreieck -> **Dynamic Inputs -> Multiply Float**
* `A` -> **Link Inputs** -> `User.WindSpeed`
* `B` = `100.0`

Der C++-Code liefert Wind in **m/s**, Niagara rechnet in **cm/s** - die 100 ist
die Umrechnung, nicht willkuerlich.

### 10. Feste Bounds - der Schritt, den man vergisst

Im System Overview ganz oben den **System**-Knoten anklicken. Im Details-Panel:

* `Fixed Bounds` ankreuzen
* `Min` = `(-20000, -20000, -10000)`
* `Max` = `( 20000,  20000,  10000)`

Ein CPU-Emitter ohne feste Bounds wird beim Distanz-Culling ausgeblendet: der
Regen verschwindet, sobald man den Kopf dreht. Der Code warnt danach zwar
(`WarnMissingFixedBounds`), aber erst zur Laufzeit.

### 11. Speichern

**Save** im Niagara-Editor. Der Asset-Pfad muss exakt
`/Game/Niagara/NS_WeatherRain` sein - den laedt `GetDefaultAssetPath()`.

---

## Teil 2 - `NS_WeatherSnow`

Schnee ist derselbe Aufbau mit anderen Zahlen. Am schnellsten per Kopie:

1. Im Content Browser Rechtsklick auf `NS_WeatherRain` -> **Duplicate** ->
   `NS_WeatherSnow`.
2. Oeffnen, Emitter `Rain` -> **Rename** -> `Snow`.
3. **User Parameters**: `RainSpawnRate` umbenennen in `SnowSpawnRate`.
   Danach die Bindung in **Spawn Rate** pruefen - bei einer Umbenennung kann
   sie auf `None` zurueckfallen; dann neu auf `User.SnowSpawnRate` setzen.
   `WindSpeed` und `SunLightColor` bleiben.
4. Werte aendern:

| Stelle | Regen | **Schnee** |
|---|---|---|
| Add Velocity -> Velocity Z | -1400 | **-250** |
| Gravity Force -> Gravity Z | -980 | **-60** |
| Particle Update -> **Drag** | (keins) | **0.6** hinzufuegen |
| Initialize Particle -> Lifetime | 1,5 s | **8 s** |
| Sprite Size | `(2.5, 45)` non-uniform | **Uniform `6`** |
| Color / Alpha | `(0.55,0.62,0.72)` / 0.35 | **weiss `(1,1,1)` / 0.85** |
| Box Location -> Offset Z | 1200 | **1800** |
| Sprite Renderer -> Alignment | Velocity Aligned | **Unaligned** |

**Drag** hinzufuegen: **+** neben "Particle Update" -> **Forces -> Drag** ->
`Drag` = `0.6`. Das ist der Grund, warum Schnee taumelt statt zu fallen.

5. Fixed Bounds sind aus der Kopie schon richtig. **Save**.

---

## Teil 3 - Nachpruefen

### Im Editor

```
UnrealEditor-Cmd.exe WiesbadenReal.uproject -run=pythonscript ^
  -script=Tools/check_weather_niagara.py -unattended -nullrhi
```

Das Skript sagt, ob die Assets am richtigen Pfad liegen. Die User-Parameter
kann es **nicht** lesen - `unreal.NiagaraSystem` gibt sie nicht heraus. Diese
Pruefung macht das Spiel.

### Im Spiel - das ist der eigentliche Beweis

```
UnrealEditor-Cmd.exe WiesbadenReal.uproject <Default-Karte aus DefaultEngine.ini> ^
  -game -WbWeather=Rain -WbGoto=Kaiser-Friedrich-Ring -WbScreenshot
```

Danach in `Saved/Logs/WiesbadenReal.log`:

| Zeile | Bedeutung |
|---|---|
| `WeatherFX: Partikel-Systeme Regen=1 ...` | Asset gefunden und geladen |
| `... Regen=0 ...` | Asset fehlt oder liegt am falschen Pfad |
| `WeatherFX: ... FEHLT - dieser Effekt bleibt unsichtbar.` | Pfad pruefen |
| `WeatherFX: Effekt 'Regen' ... Parameter ... fehlen` | Name eines User-Parameters falsch geschrieben |
| Warnung zu `Fixed Bounds` | Schritt 10 vergessen |

Und dann das Bild ansehen. Kein Protokoll ersetzt das: fallende Tropfen sieht
man, oder man sieht sie nicht.
