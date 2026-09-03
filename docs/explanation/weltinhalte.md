# Erklaerung: Weltinhalte - Beschilderung, Ausstattung, Wetter und Verkehr

Dieses Dokument erklaert die GIS-gestuetzten Inhalts- und Ausstattungs-Systeme,
mit denen aus OSM-Daten eine belebte Stadt wird: amtliche Beschilderung,
Strassenklassen, Gebaeude-Fassaden, Fahrbahnmarkierung und -ausstattung, der
regelbasierte City-Prompt, das Wetter-System samt Rendering, die Verkehrs-KI und
die Stadt-Regionen. Es war frueher der (weit ueber Verkehrszeichen hinausreichende)
Abschnitt "Verkehrszeichen & Strassenausstattung" der Wurzel-README.

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

