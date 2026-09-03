# Wiesbaden Real

Open-World-Action-Spiel im Stil von GTA V, das die Landeshauptstadt Wiesbaden
(Hessen) photorealistisch als Spielwelt abbildet — generiert aus echten
GIS-Daten (OpenStreetMap + Copernicus/SRTM-Höhendaten).

**Engine:** Unreal Engine 5.8.1+ · **Sprache:** C++ (Core) + Blueprints ·
**Maßstab:** 1 Unreal Unit = 1 cm · **Georeferenz:** WGS84 → ECEF → East-South-Up

## 📖 Dokumentation

Anleitungen, Referenzen und Hintergründe liegen unter [`docs/`](docs/README.md) —
nach dem [Diátaxis](https://diataxis.fr/)-Rahmen geordnet (Tutorial / How-to /
Reference / Explanation). Guter Einstieg: das Tutorial
[Erste Fahrt durch Wiesbaden](docs/tutorials/erste-fahrt.md).

## Herkunft & Aufteilung

Dieses Projekt wurde ursprünglich in Claude Code unter
`C:\Users\ssonn\aivideo\WiesbadenReal` begonnen und wird dort **weiterhin von
Claude fortgesetzt** (Limit-Resets: 16.08.).

Dieser Ordner ist der **eigenständige, abgesicherte Freebuff-Pfad** desselben
Projekts: eine isolierte Arbeitskopie mit eigenem Git-Verlauf, damit beide
Pfade unabhängig voneinander weiterarbeiten können.

- Ursprung (Claude): `C:\Users\ssonn\aivideo\WiesbadenReal`
- Freebuff-Pfad (hier): `/d/freebuff_city_wi/WiesbadenReal`

## Stand der Implementierung

**Phase 1 – GIS-Pipeline** (Spezifikation Abschnitt 15):

| Modul | Status |
|---|---|
| `GeoCoordinateConverter` (WGS84 ↔ UE) | ✅ fertig |
| `OSMTypes` / `OSMDataParser` (OSM XML/JSON/Overpass) | ✅ fertig |
| `PolygonUtils` (2D-Geometrie, Triangulierung) | ✅ fertig |
| `RoadTypeLibrary` / `RoadNetworkTypes` | ✅ fertig |
| `RoadNetworkGenerator` (Straßen, Kreuzungen, Spuren) | ✅ fertig |
| `BuildingGenerator` (Gebäude, Dächer, Multipolygone) | ✅ fertig |
| `HeightmapImporter` (DEM-Import, ASCII-Grid + SRTM .hgt) | ✅ fertig |
| `TerrainGenerator` (Landscape-Heightmaps + Einebnung) | ✅ fertig |
| `WiesbadenWorldBuilder` (Editor-Workflow, orchestriert alles) | ✅ fertig |
| `WiesbadenTrafficSignCatalog` (amtl. Verkehrszeichen + OSM-Parser) | ✅ fertig |
| `WiesbadenRoadMarkings` (RMS/StVO-Maße für Markierung & Ausstattung) | ✅ fertig |
| `RoadFurnitureGenerator` (Pass: Schilder, Leitpfosten, Haltlinien) | ✅ fertig |

**Phase 1 (GIS-Pipeline) ist damit vollständig und über einen Editor-Actor bedienbar.**

**Noch offen:** Phasen 6–12 (Pedestrians, Player, Wanted-Level, UI, Audio,
Optimierung) sowie die DEM-Verdrahtung im Editor; die sichtbare Darstellung
der Verkehrs-Simulation (Fahrzeug-Meshes auf den Sim-Positionen) ist ein
Folgeschritt.

**Umgesetzt ueber Phase 1 hinaus:** Laufzeit-Core (GameInstance/GameMode/
WorldSubsystem + World Partition), fliegbarer Helikopter, generische
Fahrzeug-Kamera, Rotor-Physik, Bodenfahrzeug, Wetter-System + Niagara-FX,
Regionen + regionen-abhaengige Assets sowie die datenreine Verkehrs-KI
(siehe unten).

### Nachtrag 19.08.2026 — Gelaende-Korrektur und Spielerfahrzeug

| Modul | Status |
|---|---|
| `HeightmapImporter::ParseSrtmTileOrigin` (Georeferenz aus Dateiname) | ✅ korrigiert |
| Terrain-Crop auf die OSM-Ausdehnung (Aufloesung 27,6 m → 7,8 m/Quad) | ✅ wirksam |
| `AWiesbadenWorldBuilder::BuildLandscapeImportMaps` (Landscape-Import) | ✅ korrigiert |
| `WiesbadenCarLightsComponent` (Fahr-/Brems-/Rueckfahrlicht, Blinker) | ✅ fertig |
| `WiesbadenEngineAudio` + `WiesbadenCarAudioComponent` (Motorklang) | ✅ fertig |
| `WiesbadenCarSpawn` (Startadresse → naechste befahrbare Spur) | ✅ fertig |
| VW Kaefer 1969 als Platzhalter (Spieler + Verkehr) | ✅ eingebunden |

**Gelaende:** Die SRTM-Kachel wurde zuvor 1 Grad zu weit suedlich verortet — die
Stadt stand auf fremdem Relief. Nach der Korrektur liegt Platter Strasse 144 bei
230 m ueber NN, was der tatsaechlichen Hanglage am Taunus entspricht. Der
Stadt-Build liefert seither 121.721 Strassensegmente und 20.213 Kreuzungen
(zuvor 108.073 / 14.164).

**Spielerfahrzeug:** Start an Platter Strasse 144, auf die naechstgelegene
befahrbare Spur gesetzt (32 m Luftlinie) und in Fahrtrichtung ausgerichtet.
Bedienung: `W`/`A`/`S`/`D`, `Leertaste` Handbremse, `R` Rueckwaertsgang,
`L` Fahrlicht (Aus → Stand → Abblend → Fern), `Q`/`E` Blinker, `H` Warnblinker.

**Einschraenkung:** Die Raeder des Kaefer-Platzhalters drehen und lenken nicht.
Das Quellmodell ist nach Materialien gruppiert, nicht nach Bauteilen; es gibt
keine Rad-Objekte. Die Rad-Komponenten in `AWiesbadenCar` bleiben erhalten und
werden von `UpdateWheels()` weiter gefuehrt — ein separates Radmesh muss nur
zugewiesen werden.

## Verzeichnisstruktur

```
WiesbadenReal/
  WiesbadenReal.uproject
  Source/
    WiesbadenReal.Target.cs
    WiesbadenRealEditor.Target.cs
    WiesbadenReal/
      WiesbadenReal.Build.cs
      GIS/                 # Phasen 1–3
```

## Editor-Workflow (AWiesbadenWorldBuilder)

1. In der UE-Level-View einen **WiesbadenWorldBuilder** platzieren.
2. Im Details-Panel `OsmFilePath` (`.osm`/`.xml`/`.json`) und optional
   `DemFilePath` (`.asc`/`.hgt`) setzen, Materialien zuweisen.
3. Button **„Build City“** klicken — die Pipeline läuft einmal durch:
   OSM parsen → Georeferenzierung → DEM importieren → Straßen → Gebäude →
   Landscape (+ Einebnung unter Straßen/Gebäuden).
4. Ergebnis als Procedural-Meshes (Straßen, Gebäude) und einer echten
   `ALandscape` im Viewport (alternativ Vorschau-Mesh via
   `bCreateLandscapeActor = false`); Reports (Zähler, Dauer) unter
   „GIS|Ergebnis“. „Clear Generated Geometry“ entfernt alles inkl. Landscape.

Hinweis: Die Generierung läuft asynchron auf einem Worker-Thread; im Editor
zeigt ein Fortschrittsdialog (mit Abbrechen) den Stand, der Editor bleibt
bedienbar. Nur das finale Erzeugen der Meshes/Landscape passiert im
Game-Thread.

## Laufzeit-Core (GameInstance / GameMode / WorldSubsystem)

Das Core-Modul laedt die Stadt zur Laufzeit und streamt sie per World Partition:

| Datei | Rolle |
|---|---|
| `Core/WiesbadenCityData.h` | Session-weite Stadt-Daten (OSM, DEM, Netz, Meshes, Reports) |
| `Core/WiesbadenGameInstance` | Konfiguration (Config) + asynchroner Laufzeit-Build der Pipeline |
| `Core/WiesbadenGameMode` | Spielfluss: Init anstossen, Status an Blueprints (OnCityStatus) |
| `World/WiesbadenCitySubsystem` | Orchestrierung je Welt: Daten holen, Stadt spawnen, Streaming-Zustand |
| `World/WiesbadenCityActor` | Procedural-Mesh-Host der Stadt (Strassen, Gebaeude, Terrain) |
| `World/WiesbadenStreamingSource` | World-Partition-Streaming-Quelle, folgt dem Player-Pawn |

**Ablauf (OnWorldBeginPlay):** 1) World Partition pruefen → Streaming-Quelle
registrieren. 2) Daten aus dem GameInstance holen bzw. Laufzeit-Build
anstoessen (`bGenerateAtRuntime`). 3) Stadt-Actor spawnen, Meshes anwenden.

**Zwei Betriebsmodi:**
- **Laufzeit-Build** (`bGenerateAtRuntime = true`, Standard): Beim Spielstart
  laeuft die komplette GIS-Pipeline auf einem Worker-Thread; erst das Spawnen
  der Meshes passiert im Game-Thread. Ideal fuer PIE/Entwicklung.
- **Produktion** (`bGenerateAtRuntime = false`): Die Stadt liegt als gebackene
  World-Partition-Map im Level; World Partition streamt die Zellen automatisch
  um die Streaming-Quelle (Player). Es wird nichts generiert.

**Setup (einmalig):** In den Project Settings unter *Maps & Modes* den
`WiesbadenGameInstance` als Game Instance Class und den `WiesbadenGameMode`
als Default GameMode setzen. Alle Parameter (OSM/DEM-Pfade, Materialien als
Soft-Refs, `StreamingRadiusMeters`) stehen in den Project Settings bzw.
`DefaultGame.ini` unter `[/Script/WiesbadenReal.WiesbadenGameInstance]`.

**Ehrliche Hinweise:** Laufzeit erzeugte Geometrie landet im persistenten Teil
einer partitionierten Welt (nicht zellenweise gestreamt) - das Zellen-Streaming
wirkt auf die gebackenen Inhalte. Eine echte `ALandscape` laesst sich nur im
Editor erzeugen (Import-API ist editor-only); im Packaged Game zeigt der
Terrain-Kanal das Vorschau-Mesh. Streaming-Zustand und Stadt-Status sind ueber
`IsCityStreamingComplete()` / `GetCityStatus()` und die BP-Ereignisse
erreichbar.

## Fliegbarer Helikopter (Fahrzeug-Prototyp)

`Vehicles/WiesbadenHelicopter` ist ein fliegbarer `APawn` mit Platzhalter-
Geometrie (Engine-Basis-Shapes) und adressiert die drei gemeldeten Punkte:

| Punkt | Umsetzung |
|---|---|
| Kamera dreht nicht mit | Kamera-Boom ist Kind der Heli-Wurzel und erbt Pitch/Yaw/Roll (`USpringArmComponent`) |
| Cockpit-Ansicht | Zweite `UCameraComponent` an einem Cockpit-Socket; Umschaltung per Taste |
| Rotoren zu weit hinten | Hauptrotor auf dem Mast ueber dem Schwerpunkt, Heckrotor am Ende des Heckauslegers |
| Kampfheli (Ka-52-Stil) | `bCoaxialRotors` aktiv: gegenlaeufiger Doppelrotor (zweites Platzhalter-Blatt-Mesh rotiert entgegengesetzt), flinkere Physik (hoehere Momenten-Autoritaet, weniger Daempfung, vmax 85 m/s) |

**Steuerung (zero-config, Tasten werden gepollt):**
W/S = Pitch, A/D = Roll, Q/E = Yaw, Space/Shift = steigen, Ctrl = sinken,
G = Triebwerk an/aus (Autorotation testbar). Die **Kamera ist fest im
Follow-Modus direkt hinter der Flugmaschine** (`bLockFollowMode` an der
Fahrzeug-Kamera) - kein Umschalten auf Orbit/Cockpit, keine Orbit-Schwenkung.

**Hinweise:** Die Rotor-Physik kommt aus `FWiesbadenRotorPhysics` (Governor,
Lift, Collective, Zyklik, Heckrotor, Autorotation); der Heli integriert nur
Kraft/Drehmoment. Bodenkontakt ueber einen Down-Raycast. Zum Fliegen den Pawn
in ein Level setzen - `AutoPossessPlayer = Player0` uebernimmt ihn beim Start
automatisch. Alle Parameter liegen editierbar in den Kategorien
`Wiesbaden|Heli|...` bzw. `Wiesbaden|Heli|Rotor`.

## Generische Fahrzeug-Kamera

`Vehicles/WiesbadenVehicleCameraComponent` ist eine wiederverwendbare Kamera-
Komponente fuer beliebige Fahrzeuge/Pawns. Sie erzeugt ihr Rig selbst in
BeginPlay (SpringArm + Third-Person-Kamera + Cockpit-Socket/-Kamera) und bietet
Follow, Orbit (Pfeiltasten) und Cockpit mit Tastenumschaltung (`ToggleKey`,
Default `C`). Der Helikopter nutzt sie, statt eigene Kamera-Logik zu duplizieren.

Einsatz: Komponente an einen Pawn haengen, `CockpitOffset`/Armlaenge/etc. in der
Kategorie `Wiesbaden|Kamera` anpassen - fertig. Voraussetzung: der Owner sollte
ein `APawn` sein, der von einem `APlayerController` besessen wird.

## Bodenfahrzeug (PKW-Prototyp)

`Vehicles/WiesbadenCar` ist ein fahrbarer `APawn` mit Platzhalter-Geometrie
(Engine-Basis-Shapes) und denselben Konventionen wie der Helikopter
(zero-config-Input-Polling, `AutoPossessPlayer = Player0`, generische
Fahrzeug-Kamera mit C-Umschaltung).

**Steuerung:** W/S = Gas/Bremse, A/D = Lenken, Space = Handbremse,
R = Rueckwaertsgang, C = Kamera (Follow -> Orbit -> Cockpit).

Die **Physik** kommt aus dem reinen, deterministischen Modul
`Vehicles/WiesbadenVehiclePhysics` (`FWiesbadenVehiclePhysics`, analog zur
Rotor-Physik und damit in Automation-Tests pruefbar):

- **Motor** – Drehmomentkurve ueber der Drehzahl (T_max aus der Leistung,
  Abfall zum Leerlauf und zur Drehzahlgrenze).
- **Automatik-Getriebe** – Gangwechsel ueber Drehzahlschwellen, Drehzahl aus
  Radumfang und Uebersetzung; Rueckwaertsgang nur im Stand.
- **Laengsdynamik** – Antriebskraft (begrenzt durch das Traktionslimit
  mu*Gewicht) minus Rollreibung, Luftwiderstand und Bremse.
- **Querdynamik** – kinematisches Bicycle-Modell (yaw = v/L * tan(delta)),
  begrenzt durch das Seitenkraftlimit (Untersteuern bei hohem Tempo).

Der Pawn integriert nur Geschwindigkeit -> Position und Gierrate ->
Ausrichtung, haelt den Bodenkontakt per weichem Down-Raycast und animiert
Raeder (Drehung + Vorderrad-Lenkeinschlag). Alle Parameter (Leistung,
Gangverhaeltnisse, Reibwerte, Radstand) liegen editierbar unter
`Wiesbaden|Fahrzeug|...`.

## Rotor-Physik

`Vehicles/WiesbadenRotorPhysics` ist ein reines, deterministisches
`FWiesbadenRotorPhysics`-Modul fuer Drehfluegler (kein Welt-/Actor-Zugriff,
daher in Automation-Tests pruefbar). Es modelliert:

- **Governor** haelt die Rotordrehzahl nahe der Soll-Drehzahl, begrenzt durch die Wellenleistung.
- **Lift** = Faktor * omega^2 * CollectivePitch (Blattanstellwinkel).
- **Zyklik** neigt die Rotorscheibe -> horizontale Kraft + Pitch/Roll-Moment.
- **Heckrotor** kompensiert das Reaktionsmoment und liefert die Yaw-Kontrolle (Pedal).
- **Koaxial-Rotoren** (`bCoaxialRotors`, Ka-52-Stil): gegenlaeufiger
  Doppelrotor verdoppelt den Auftrieb, hebt das Reaktionsmoment gegenseitig
  auf (kein Heckrotor) und steuert Yaw per direktem Pedal-Moment
  (differentielle Blattverstellung).
- **Blattspitzenverlust** (Retreating Blade Stall): Ab ~75 % der
  Hoechstgeschwindigkeit (`MaxForwardSpeedMetersPerS`) bricht der Auftrieb der
  ruecklaufenden Blaetter ein und sinkt bis vmax auf ~45 %% - begrenzt die
  Geschwindigkeit realistisch statt mit einem harten Clamp.
- **Autorotation**: Bei Triebwerksausfall und Sinkflug treibt der aufsteigende
  Luftstrom den Rotor an und erhaelt Drehzahl/Auftrieb. `EngineRpm`
  (Triebwerksdrehzahl = Rotordrehzahl * `EngineToMainRotorRatio`, 0 bei
  Triebwerk aus) speist Audio/HUD.

Ausgabe pro Tick: `Force` (N) und `Torque` (N*m) im Fahrzeug-Lokalkoordinatensystem.

## Flugsound (Helikopter)

`Vehicles/WiesbadenHelicopterAudioComponent` erzeugt den Flugsound aus dem
Flugzustand - zwei Betriebsarten:

- **Prozeduraler Fallback** (Standard, keine Assets noetig):
  `FWiesbadenHelicopterAudioModel` (rein + deterministisch) erzeugt
  int16-PCM - Rotor-Rauschen durch einen Tiefpass (Grenzfrequenz folgt
  Drehzahl/Blattlast), "Wop-Wop"-Amplitudenmodulation mit der
  Blattpassfrequenz (Blattzahl * U/min), plus Motor-Ton (Drehzahl * 8
  Zylinder) bei laufendem Triebwerk. Die Samples werden in einen
  `USoundWaveProcedural` gepusht (Queue-Limit gegen Pufferdrift).
- **Asset-basiert**: `RotorSound`/`EngineSound` (USoundWave) mit
  Pitch/Volume aus Drehzahl und Blattlast (Rotor: Pitch ~ RPM/420, Volume ~
  Last; Motor: Pitch ~ RPM/3000).

Der Heli-Pawn speist die Komponente pro Tick (Rotor-Drehzahl, Collective,
Triebwerksdrehzahl an/aus, Fahrtgeschwindigkeit); bei Triebwerksausfall ist
nur noch das Rotor-Geraeusch zu hoeren (Autorotation).

**Kampfheli-Charakter** (Referenz: Mi-35/28/Ka-52/Mi-8 - Hover & Departure):
der Heli nutzt `BladeSlapDepth` (0.55 statt 0.38) und `RotorCutoffBaseHz`
(110 statt 180) - ein schwerer, tiefer Rotorschlag statt zivilem Surren;
`BladeCount = 3` (Ka-52: 3 Blaetter je Rotor).

## Verkehrszeichen & Straßenausstattung

Die Datenmodelle fuer amtliche Beschilderung und Markierung sind verankert:

- `GIS/WiesbadenTrafficSignCatalog` - kuratierter VzKat-Katalog (Gefahr-/
  Vorschrift-/Richt-/Zusatzzeichen, Verkehrseinrichtungen) + Parser fuer OSM
  `traffic_sign=*` (`DE:206`, `DE:274-50`, `DE:274[30]`, mehrere via `;`).
  Die Eintraege liegen als JSON unter `Content/Config/TrafficSignCatalog.json`
  (mit `aliases`-Feld fuer OSM-Kurzformen wie `325` -> `325.1`, optional
  `noTexture`) und werden zur Laufzeit geladen - Erweiterungen ohne
  Neukompilierung. Beim Editor-Start prueft `ValidateTexturesAtStartup` jede
  Id gegen die Textur-Datei (`Sign_<Id>.png`) und warnt fehlende Zeichen.
  Der Katalog ist ein mutables Registry: `UWiesbadenTrafficSignLibrary`
  erweitert ihn aus Blueprints (Add/Remove/Reload), und der `WiesbadenWorldBuilder`
  hat eine `ReloadTrafficSignCatalog`-CallInEditor-Kachel (Hot-Reload der JSON).
- `GIS/RoadTypeLibrary` - Strassenklassen-Registry (RASt 06/RAA): Spurbreite,
  Spuren/Richtung, Regeltempo, Gehweg, Prioritaet, Verkehrsdichte. Die Werte
  liegen als JSON unter `Content/Config/WiesbadenRoadTypes.json` (gemeinsamer
  Config-Pfad wie der Schilderkatalog, `WiesbadenConfigPaths`); fehlt die
  Datei, gelten die Code-Defaults.
- `GIS/BuildingGenerator` - Gebaeude-Fassaden mit Materialvarianten (Putz,
  Backstein, Sandstein, Glas, Beton, Fachwerk). Ein Per-Adress-Override
  (`BuildingSettings.FacadeOverrideAddresses` + `AddressFacadeMaterials`,
  z. B. "Mainzer Strasse 129") gibt einzelnen Gebaeuden einen eigenen
  Mesh-Abschnitt mit eigenem Fassaden-Material, ohne die Varianten-Pipeline
  anzufassen. `PutzFassade_*` (CC0-Putzfassade) liegt bereits in
  `Content/Textures/Facades/`.
- `GIS/WiesbadenRoadMarkings` - StVO/RMS-Maße (Zebra 50/50 cm, Leitpfosten
  50 m, Haltlinie, Linienbreiten, Schildhoehen).
- `GIS/RoadFurnitureGenerator` - Ausstattungs-Pass: platziert Schilder
  (Kreuzungs-Kontrolle, Tempolimits, explizite OSM-`traffic_sign`-Tags),
  Leitpfosten (beidseitig, 50-m-Raster) und Halt-/Wartelinien als
  Platzierungsdaten entlang des Straßennetzes.
- `AWiesbadenWorldBuilder` konsumiert den Pass direkt im `BuildCity`-Lauf
  (`bGenerateFurniture`) und verdrahtet die Schild-Id mit dem Textur-Lookup:
  `ResolveSignTexture(Id)` laedt `Sign_<Id>.png` aus `SignTextureFolder`
  (Default `/Game/Textures/TrafficSigns/`); `ResolveSignMaterial(Id)` erzeugt
  daraus ein Material-Instanz-Dynamic ueber `SignMaterial`.
- `GIS/CityPrompt` - regelbasierter **City-Prompt-Parser** (WorldClaw-artig
  "Prompt -> Spezifikation -> Welt", aber ohne LLM/Cloud): eine Textzeile wie
  "dichte Gruenderzeit-Innenstadt mit Marktkirche, wolkig" wird deterministisch
  in eine `FCityPromptSpec` uebersetzt - Bebauungsdichte (0..1),
  Fassadenstil-Gewichte (Varianten 0-5), erkannte Landmarken (kanonische
  Namen) und eine Wetterlage. Die Pipeline wendet an: Dichte ->
  `MinFootprintAreaSqm` der Gebaeude (dicht = auch kleine Gebaeude);
  Stil-Gewichte + Landmarken -> `FacadeOverrideKey` je Gebaeude (Prioritaet:
  Adress-Override > Landmarke `PromptLandmark:<Name>` > Stil
  `PromptStyle:<Name>`, Stil deterministisch gewichtet ueber den OSM-Id-Hash,
  Landmarken matchen name=* case-/sz/umlaut-insensitiv mit Wortgrenzen). Die
  Varianten-Pipeline (`SelectMaterialVariant` + Section-Gruppierung) bleibt
  unangetastet; die Render-Seite mappt die Keys ueber `PromptFacadeMaterials`
  (WorldBuilder/CityActor, analog `AddressFacadeMaterials`) auf Materialien.
  Weitere Regeln: Tageszeit ("abends" -> 19 Uhr etc., `TimeOfDayHours`, -1 =
  nicht gesetzt) wird beim Stadt-Spawn ins Wetter-System uebernommen
  (`SetTimeOfDay`); Gebaeudehoehen ("hoch" 1.4, "Hochhaus" 2.0) ->
  `BuildingSettings.HeightScale`; Verkehrsdichte ("viel verkehr" 0.85,
  "stau" 0.95, 0..1) wird in `FWiesbadenCityData::TrafficSettings`
  uebernommen und steuert die Spawn-Rate der Verkehrs-Simulation (s. u.). Die
  Spec liegt in `FWiesbadenCityData::CityPromptSpec` (HUD/Diagnose). Eingabe:
  `CityPrompt` am `WiesbadenWorldBuilder` bzw. als Config im
  `WiesbadenGameInstance`. Deutsch + Englisch, Umlaute normalisiert
  (ae/oe/ue/ss); Stil-Namen zentral in `CityPromptParser::GetFacadeStyleNames`.
- `World/WiesbadenWeatherSystem` - datenreine **Wetter-Zustandsmaschine**:
  konsumiert `ECityWeatherPreset` aus dem City-Prompt, blendet Wetterwechsel
  sanft ueber `TransitionSeconds` (Intensitaeten Rain/Fog/CloudCover mischen
  sich), schreitet die Tageszeit fort (`HoursPerRealSecond`, SPEC 11:
  1 Real-Stunde = 24 Ingame-Stunden) und leitet Sonnenstand, Nacht-Flag und
  Umgebungslicht daraus ab. Laueft pro Welt im `UWiesbadenCitySubsystem`
  (Tick + Uebernahme der Prompt-Wetterlage beim Stadt-Spawn); Blueprint:
  `GetWeatherState()`/`SetWeatherTarget()`. Rendering (Sky, Licht, Partikel)
  bleibt dem Aufrufer.
- `World/WiesbadenWeatherFX` - **Wetter-Rendering** (Niagara):
  `FWiesbadenWeatherFXParams::FromWeatherState` leitet deterministisch aus dem
  Wetter-Zustand die FX-Parameter ab (Regen-/Schnee-SpawnRate, Nebel,
  Bewoelkung, Blitz-Intervall nur im Gewitter, Wind, Sonnenlicht-Farbe +
  -Intensitaet); die `UWiesbadenWeatherFXComponent` am CityActor spawnt/
  steuert die Effekte ueber User-Parameter (`SetVariableFloat`/
  `SetVariableLinearColor`, Vertrag in der Klasse dokumentiert) und treibt die
  **DirectionalLight** der Szene (Farbe + Intensitaet; nachts 0, klar bewolkt
  gedaempft) - per UPROPERTY `SunLight` zuweisbar oder automatisch die erste
  `ADirectionalLight` des Levels. Assets werden im Details-Panel der
  Komponente gesetzt (NS_/NE_-Assets mit exponierten User-Parametern). Der
  Vertrag steht maschinenlesbar in `Content/Config/WeatherFXCatalog.json`
  (fuenf Assets: Emitter/Module/Renderer + User-Parameter mit Typ/Default/-
  Clamp + Fixed Bounds + kanonische `path`-Pfade); `ValidateSystem` prueft
  zugewiesene Systeme nach jedem Spawn und warnt bei fehlenden Parametern
  (kein stummer Vertragsbruch). **Auto-Zuweisung:** nicht manuell gesetzte
  Effekte laedt die Komponente in BeginPlay aus den `path`-Pfaden
  (`/Game/Niagara/NS_Weather<Name>`) - sobald die Assets nach Content/Niagara
  gebaut sind, laeuft das Wetter ohne Details-Panel-Konfiguration.
  Manueller Editor-Bau je Asset: `Content/Config/WeatherFXAssetBuildGuide.md`
  (Schritt-fuer-Schritt aus dem Katalog); Konformitaets-Check headless:
  `node Tools/verify_weatherfx_catalog.mjs` (69 Checks gegen C++-Vertrag +
  Engine-Modul-Inventar), im Editor: `WiesbadenReal.Weather.FXCatalogModules`.
- `GIS/WiesbadenTrafficSimulation` - **datenreine Verkehrs-KI** auf dem
  Fahrspur-Graphen: `FWiesbadenTrafficSimulation` (USTRUCT, kein Welt-Zugriff)
  laesst Fahrzeuge dem Graph folgen (Spur-Mittellinie -> Kreuzungs-Verbindung
  `FLaneConnection::ConnectionPath` -> Folgespur; an Kreuzungen waehlt ein
  Fahrzeug deterministisch per FNV-1a-Hash aus Fahrzeug-Id + Knoten-Id eine
  erlaubte Verbindung, Sackgassen entfernen das Fahrzeug). **TrafficDensity
  aus dem City-Prompt steuert die Spawn-Rate linear** (Rate =
  `MaxSpawnRatePerSecond` * Dichte; Round-Robin ueber alle befahrbaren
  Spuren, blockierter Spur-Anfang verschiebt den Spawn). Kopf-zu-Schwanz:
  der Folger wird so begrenzt, dass die `MinGapCm`-Luecke zum Vordermann
  nie unterschritten wird - hohe Dichte erzeugt natuerlich Stau.
  Deterministisch (Seed), Report (`FWiesbadenTrafficReport`: aktive
  Fahrzeuge, Mittelgeschwindigkeit, km-Werte). Initialisiert und getickt vom
  `UWiesbadenCitySubsystem` (beim Stadt-Spawn auf dem finalen
  Datencontainer); Blueprint: `GetTrafficReport()`/`GetTrafficVehicles()`.
- `GIS/WiesbadenRegionAssets` - **regionen-abhaengige Assets** (WorldClaw-
  Schritt 3: Objekte logisch platzieren): `UWiesbadenRegionAssetGenerator`
  platziert deterministisch (Seed = FNV-1a-Hash aus Region-Name + Kategorie)
  Baeume nur in Gruen-Regionen, Ufer-Objekte am Rand von Wasser-Regionen und
  Industrie-Objekte nur in Industrie-/Gewerbe-Regionen (Gitter + Jitter im
  Polygon, Punkt-in-Polygon-Test). Rein datenrein: `FRegionAssetLayout`
  (Position/Rotation/Skala je Asset). Spawner: `URegionAssetSpawnerComponent`
  (am CityActor) rendert Baeume als HISM, Ufer/Industrie als ISM. Pass in
  `BuildCityData` (Stufe RegionAssets, 66 %) zwischen Regions und Buildings.
- `GIS/WiesbadenRegion` - **Stadt-Regionen** (WorldClaw-Schritt 3: Welt in
  Regionen unterteilen, Objekte logisch platzieren): `UWiesbadenRegionGenerator`
  erzeugt aus geschlossenen OSM-Flaechen (landuse/natural/leisure/waterway)
  Regionen mit Typ (Wasser/Gruen/Wohnen/Gewerbe/Industrie), Name und
  Polygon-Geometrie. Paperkonform (arXiv 2608.05248 §2.3) laeuft der
  Regions-Pass in `BuildCityData` **vor** der Gebaeude-Erzeugung: Der
  BuildingGenerator erhaelt die Regionen-Karte (`FBuildingGenerationSettings::RegionMap`)
  und ordnet jedes Gebaeude beim Bauen seiner Region zu (`RegionType`/
  `RegionName` am `FGeneratedBuilding` - separate Instanzen, manuell
  feinjustierbar); Wasser-Gebaeude werden bei `bRemoveWaterBuildings`
  verworfen, **bevor** Mesh entsteht (kein Haus im See), plus regionales
  Hoehen-/Fassaden-Feintuning (`GetRegionalHeightScale`: Industrie 1.1 /
  Gruen 0.85; `GetRegionalFacadeKey`: `Region:Industrie`/`Region:Gewerbe`).
  Prioritaet bei Ueberlappung: Wasser > Gruen > Wohnen > Gewerbe > Industrie;
  Flecken unter `MinRegionAreaSqm` (500 m^2) werden verworfen. Nicht-fataler
  Pass (Stufe Regions, 62 %) vor Buildings (68 %); Ergebnis in
  `FWiesbadenCityData::Regions`/`RegionReport`. Die Zuordnung der Gebaeude zu
  ihren Regionen geschieht ausschliesslich beim Bauen im `UBuildingGenerator`
  (ueber `RegionMap`) - kein Nach-Pass.
- `GIS/WiesbadenCityPipeline` - gemeinsame daten-reine `BuildCityData()`:
  Editor (`AWiesbadenWorldBuilder`) und Runtime (`WiesbadenGameInstance`)
  rufen dieselbe Kette auf (OSM/DEM -> Strassen/Gebaeude/Terrain ->
  Ausstattung); damit entstehen Schilder/Leitpfosten auch zur Laufzeit
  (`bGenerateFurniture`, Config im GameInstance).
- `World/RoadFurnitureSpawnerComponent` - visueller Ausstattungs-Spawner:
  rendert `FRoadFurnitureLayout` als InstancedStaticMesh - Schildermast +
  Tafel (Tafel-Material je Zeichen ueber `WiesbadenSignAssets::CreateMaterial`
  = `ResolveSignMaterial`, ein ISM je eindeutigem Zeichen), Leitpfosten
  (Pfosten + Reflektor) und Halt-/Wartelinien. Haengt im Editor am
  `WiesbadenWorldBuilder`, zur Laufzeit am `WiesbadenCityActor`; der
  `SignMaterial`-Vertrag steht in [`ASSETS.md`](ASSETS.md).

Die **amtlichen Verkehrszeichen-Grafiken sind bereits heruntergeladen** und
liegen als PNG unter `WiesbadenReal/Content/Textures/TrafficSigns/`
(`Sign_<VzKat>.png`, inkl. Tempolimit-Serie 274-/278-x) - die
`ResolveSignTexture`/`ResolveSignMaterial`-Lookups des `WiesbadenWorldBuilder`
zielen genau darauf. Fassaden-/Dachtexturen und 3D-Straßenausstattung sind
weiterhin Drop-in; alles ist in [`ASSETS.md`](ASSETS.md) mit Quellen, Lizenzen
und Import-Schritten dokumentiert.

## Automation-Tests

Unter `Source/WiesbadenReal/Tests/` liegen editorfaehige Automation-Tests fuer
die datenverarbeitenden Kernmodule sowie das Rotor-Physik-Modul (ohne geladenes
Level ausfuehrbar):

| Datei | Abgedeckt |
|---|---|
| `GeoCoordinateConverterTest.cpp` | ECEF/ESU-Roundtrip, Achsenkonvention, Metrik |
| `OSMDataParserTest.cpp` | XML/JSON-Parsing, Fehlerfaelle, Tag-Interpretation |
| `HeightmapImporterTest.cpp` | Raster/Bilinear, ASCII-Grid, SRTM-Rohdaten, Sampler |
| `GeneratorTest.cpp` | Straßen/Kreuzungen, Gebaeude, Terrain-Tile, Heightmap, Fassaden-/Landmarken-Overrides (`Buildings.PromptOverrides`) |
| `RotorPhysicsTest.cpp` | Rotor: Schwebeflug (Lift ~ Gewicht), Autorotation, Zyklik-/Heckrotor-Vorzeichen |
| `VehiclePhysicsTest.cpp` | Fahrzeug: Stand/Gang, Beschleunigung+vmax, Bremsen, Lenkung/Seitenkraftlimit, Rueckwaerts, Traktionslimit |
| `HelicopterAudioTest.cpp` | Flugsound: Blattpass-/Filterfrequenzen, Determinismus (Seed), int16-Bereich, Lautstaerke (Blattlast, Triebwerk an/aus) |
| `TrafficSignCatalogTest.cpp` | Verkehrszeichen: OSM-Parsing, Tempolimits, Katalog-Lookup |
| `CityPromptTest.cpp` | City-Prompt: Dichte/Stile/Landmarken/Wetter (deutsch+englisch), Umlaute, Defaults, Determinsmus, Tageszeit/Verkehr/Hoehen (`ExtendedRules`) |
| `RegionGeneratorTest.cpp` | Regionen: Tag-Klassifikation, Erzeugung (Bounds/Flaeche/Fleckenfilter), Punkt-Klassifikation, regionale Hoehen-/Fassaden-Regeln + Generator-Integration (kein Haus im See im Mesh) |
| `WeatherSystemTest.cpp` | Wetter: sanfter Uebergang (Blend/Intensitaeten), Tageszeit (1 Realstunde = 24 Ingame-Stunden), Sonnenstand/Nacht/Licht, CityPrompt-Integration |
| `WeatherFXTest.cpp` | Wetter-FX: Param-Ableitung (Wetterlage -> Effekte), Uebergangs-Blend, Tageslicht-Farbtemperatur, Sonnen-Intensitaet (Tag/Nacht, Bewoelkung), Determinsmus + Vertrags-Validierung (FindMissingParameters) + Asset-Pfade (kanonische `path`-Felder) + Editor: Katalog-Modul-Referenzen gegen UE-5.8-Engine-Content |
| `RegionAssetTest.cpp` | Regionen-Assets: Kategorie-Zuordnung (Gruen->Baeume, Wasser->Ufer, Industrie/Gewerbe->Industrie), Platzierung im Polygon, Determinsmus, deaktivierte Kategorien |
| `RoadFurnitureGeneratorTest.cpp` | Ausstattungs-Pass: Schilder, Leitpfosten, Haltlinien |
| `TrafficSimulationTest.cpp` | Verkehrs-KI: Dichte->Spawn-Rate (0/2/7/10, Monotonie), Graphfolge (Spur->Verbindung->Folgespur), Sackgassen-Entfernung, Kopf-zu-Schwanz (MinGap, kein Ueberholen), Determinsmus, Report |

Ausfuehrung im Editor (Session Frontend → Automation) oder per Commandlet:

```
UnrealEditor-Cmd.exe WiesbadenReal.uproject -ExecCmds="Automation RunTests WiesbadenReal; Quit" -unattended -nop4 -nullrhi
```

Die Tests sind `Shipping`-Builds nicht beigemischt (Build.cs); alle
Generierungs-Aufrufe laufen mit `nullptr`-Hoehenmodell auf Z=0 bzw. mit
synthetischen Mini-DEMs, das Rotor-Physik-Modul ist rein mathematisch -
alles deterministisch und ohne externe Datensaetze.

Die vollständige Spezifikation liegt in [`SPEC.md`](SPEC.md).

## Hinweis zu Datenquellen

Für den Import werden echte Daten benötigt (noch nicht eingecheckt):

- OSM-Extrakt: Overpass-API (Query siehe `UOSMDataParser::BuildOverpassQuery`)
- DEM: Copernicus DEM / SRTM, per `gdal_translate -of AAIGrid` in das
  ESRI-ASCII-Grid-Format konvertiert (wird von `UHeightmapImporter` gelesen)
