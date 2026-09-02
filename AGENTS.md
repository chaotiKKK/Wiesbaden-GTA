# AGENTS.md — Projekt-Wissen für künftige Sessions

Nicht-offensichtliche Fakten, die sich nicht aus Code/Doku rekonstruieren lassen.

## Umgebung & Build

- **Neuer Rechner (ab 2026-09-01, Benutzer HP):** einzige Arbeitskopie unter `C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal`, Engine auf der Platte unter `C:\freebuff\WiesbadenReal_Sicherung\UE_5.8` (NICHT Program Files) - alle `.cmd`/`Tools` zeigen dorthin. Build HIER moeglich (VS 2022 + MSVC 14.44 + Win11-SDK 26100). Die alten `ssonn`/`aivideo`/Program-Files-Pfade und BEIDE Worktrees (`D:\freebuff_city_wi`, `C:\Users\ssonn\aivideo\WiesbadenReal`) existieren hier NICHT; `verify-worktree-sync.mjs` ist damit gegenstandslos.
- git-Repo vorhanden (Branch `main`); `.gitignore` haelt `Content/__ExternalActors__` (26 GB gebackene Karte), `Data/Raw`, UE-Build-Ausgaben und `*.log` draussen.
- `WiesbadenReal.Build.cs` setzt **`bUseUnity = false`**: gleichnamige anonyme-Namespace-Helfer in mehreren `.cpp` (`FLatLon`, `MakeLane`, `WriteTempText`, `Dt`, `NextNoise`, `SamplesPerPush`, `BytesPerSample`) kollidieren, sobald der Unity-Build TUs zusammenfasst; neue Quelldateien verschieben die Chunk-Grenzen und decken latente Kollisionen auf. Non-Unity kompiliert sauber (Projekt IWYU-tauglich) - nicht ohne Dedup dieser Helfer wieder einschalten.
- In diesem Freebuff-Worktree (`D:\freebuff_city_wi`) ist **kein UE-Build möglich**: nur Quellen, keine Binaries/Intermediate/.sln. Code wird statisch geprüft, nicht kompiliert.
- Erst-Build/Tests laufen extern über `UnrealEditor-Cmd.exe WiesbadenReal.uproject -ExecCmds="Automation RunTests WiesbadenReal.GIS"`.
- UE 5.8 unter `C:\Program Files\Epic Games\UE_5.8` — kritische APIs direkt gegen die dortigen Header verifizieren.
- Zweiter, **gebauter** Worktree (Claude-Original, nicht anfassen): `C:\Users\ssonn\aivideo\WiesbadenReal`.
- Greps über den ganzen Engine-Baum laufen in Timeouts — nur einzelne Header-Dateien greppen (oder `rg`).

## Konventionen

- Code strikt ASCII; Umlaute nur in bestehenden UPROPERTY-Kategorie-Strings (`GIS|Straßen`, `GIS|Gebäude`). Neue Strings: „Strassen", „Gebaude", „Gefaelle".
- Antworten und Code-Kommentare auf Deutsch (Nutzer kommuniziert Deutsch).

## Architektur-Entscheidungen

- Editor-Pipeline (`AWiesbadenWorldBuilder::BuildCity`) ist asynchron (ThreadPool + `std::atomic`-Fortschritts-Kontext); Mesh-/Landscape-Erzeugung bleibt nach `bDone` auf dem Game-Thread.
- Gemeinsame, daten-reine Pipeline `WiesbadenCityPipeline::BuildCityData()` (GIS/WiesbadenCityPipeline) liefert OSM/DEM → Straßen/Gebäude/Terrain/**Furniture** für Editor **und** Runtime (`WiesbadenGameInstance::RunCityLoadPipeline` ruft nur noch diese Funktion). `FBuildInput`/`FBuildTools` sind die Eingaben.
- `FWiesbadenCityData` ist der session-weite Datencontainer (vom GameInstance gehalten).
- BuildCity-MoveTemp-Effekt: Nach erfolgreichem Build sind `Context->Data.RoadNetwork/Buildings/FurnitureLayout` LEER (per MoveTemp in Actor-Member uebernommen) - Code nach dem Build muss die Member lesen; `Regions`/`RegionAssetReport` werden nie gemovt und bleiben im Context. Der Kontext `FWiesbadenBuildContext` ist cpp-lokal in WiesbadenWorldBuilder.cpp (kein Header-Typ).
- Build-Zusammenfassung zentral in `GIS/WiesbadenBuildSummary` (datenrein): CSV-Historie `Saved/BuildHistory/CityBuilds.csv`, Einzeiler `FormatBuildSummaryLine` (identisch Editor/Runtime), Details-Panel-`LastBuild*` (WorldBuilder) und `GetLastBuild*()` (GameInstance, Blueprint) - Editor schreibt via `WriteBuildSummaryToCsv` (ALLE 4 BuildCity-Ausgaenge), Runtime im RunCityLoadPipeline-Block. Die `GetLastBuild*()`-Getter existieren auch an GameMode + CitySubsystem (delegieren null-sicher an den GameInstance - Level-Blueprints erreichen sie ohne GI-Zugriff); die CDO-Defaults sind in `Core.RuntimeConfig` getestet (Delegations-Getter per `GetDefault<...>` ohne Welt testbar).
- Map-Bake-Workflow: Nach `BuildCity` setzt der WorldBuilder `bCityBaked` (nicht transient, ueberlebt Map-Save); `SaveCityAsMap()` (CallInEditor) speichert das aktuelle Level via `UEditorLoadingAndSavingUtils::SaveMap` unter `MapAssetPath` (Default `/Game/Maps/WiesbadenCity`) und schreibt `GameDefaultMap`/`EditorStartupMap` in die `DefaultEngine.ini` - NICHT per `GConfig->SetString`+`Flush(GEngineIni)`: das no-opt in UE 5.8 still, wenn `FindBranch` den Branch unter dem vollen Pfad nicht findet (Log behauptet Erfolg, Datei bleibt unangetastet; UE-konform ist `UGameMapsSettings`-CDO+`SaveConfig()`, das den Missing-Branch-Fall explizit behandelt). Stattdessen schreibt `SaveCityAsMap` die Datei direkt per datenreinem `ApplyDefaultMapToIniText` (GIS/WiesbadenBuildSummary, Text->Text, Test `Core.BuildSummaryCsv`) + `FFileHelper::SaveStringToFile` (ForceUTF8WithoutBOM, kein BOM wie die Quelldatei) mit Rueck-Lesen-Verifikation. Fallstricke dabei: `TrimStartAndEnd()` ist [[nodiscard]] UND mutiert nicht (Copy) - in-place ist `TrimStartAndEndInline()`; `TArray::Insert` ist [[nodiscard]] (C4834) - `EmplaceAt` nutzen. `bAutoSaveCityAsMap` (EditAnywhere, Default false) ruft `SaveCityAsMap` direkt nach erfolgreichem BuildCity auf - Achtung: wird NACH `bBuildInProgress=false` aufgerufen (der SaveCityAsMap-Guard wuerde sonst greifen). Save-Ergebnis-Feedback im Details-Panel: `bAutoSaveSucceeded` (VisibleAnywhere, nicht transient) + `LastError` werden bei JEDEM SaveCityAsMap-Aufruf gesetzt (auch bei fruehen Abbruechen wie laufendem Build/fehlendem World - dort fehlte `LastError` frueher), `ResetResults` setzt beides zurueck. `SaveCityAsMap` verifiziert danach die WP-Speicherung via `VerifyWorldPartitionSave` (statisch/datenrein, Test `Core.MapBake`): bIsPartitioned (`UWorld::IsPartitionedWorld`) + Map-Datei (`FPackageName::DoesPackageExist`) + External-Actor-Packages auf der Platte (`HasExternalActorPackages`, zaehlt `__ExternalActors__/<MapOrdner>/*.uasset`) - NICHT `UWorldPartition::IsStreamingEnabled()`: das ist im selben Prozess nach `CreateOrRepairWorldPartition` immer false (frisches WP-Objekt nie initialisiert), der verlaessliche Indikator ist der Platten-Check. WICHTIG (UE-5.8-Quelle verifiziert): Der klassische Spatial-Hash schreibt SEHR WOHL separate Zell-Packages unter `Content/__ExternalActors__/Maps/<Map>` (1 Package je Chunk-Actor, umap schrumpft von GB auf ~10 KB) - ABER nur, wenn der volle Converter-Flow lief (s. Zeile 27), sonst bleiben die Actors in der umap und die Stadt ist im Spiel unsichtbar (0 ExternalActors). Das CitySubsystem prueft in `InitializeCity` ZUERST `HasBakedCityInLevel()` (TActorIterator auf `AWiesbadenWorldBuilder::bCityBaked`) - eine gebackene Stadt verhindert den Laufzeit-Build komplett (keine Doppel-Geometrie, keine Minuten Wartezeit beim Play), aber der gebackene Pfad startet TROTZDEM die Verkehrs-Simulation: `FindBakedCityBuilder()` liefert den Actor, dessen nicht-transiente `RoadNetwork`/`TrafficSettings` (UPROPERTYs, in der Map serialisiert) gehen in `TrafficSimulation.Initialize` - der WorldBuilder hat ein eigenes `TrafficSettings`-Property (EditAnywhere, wird ins Pipeline-Input durchgereicht), weil es im gebackenen Pfad kein `FWiesbadenCityData` gibt. Die reine Entscheidung steckt in `UWiesbadenCitySubsystem::ShouldRunRuntimeBuild` (statisch, Test `Core.MapBake`). WICHTIG: `SaveMap` auf ein unbenanntes (Untitled-)Level funktioniert (SaveAs-Fall); `bCityBaked` wird in `ResetResults` zurueckgesetzt. `EngineUtils.h` (TActorIterator) liegt direkt in Engine/Public, nicht unter `Engine/`-Pfad.
- Landscape-Import (UE-Quelle verifiziert, LandscapeEdit.cpp:3123-3231): `ALandscapeProxy::Import` verlangt `InImportHeightData.Num() == InImportMaterialLayerInfos.Num()` UND sucht beide Maps per `FindChecked(FGuid())` - der Schluessel MUSS der Default-Guid `FGuid()` sein. Zwei getrennte Crash-Modi: (1) leere `MaterialLayerInfos`-Map (Groessenpruefung), (2) beide Maps mit `FGuid::NewGuid()` geschluesselt (Groessen passen, aber `FindChecked(FGuid())` findet nichts - sieht wie eine gueltige Korrektur von (1) aus). Beide faenge der Test `GIS.LandscapeImport`; der datenreine Helfer `AWiesbadenWorldBuilder::BuildLandscapeImportMaps` (static, public) erzeugt die Maps korrekt mit `FGuid()` + leerer `TArray<FLandscapeImportLayerInfo>`.
- World-Partition-Chunking (gebackene Map): `bGenerateCityChunks` (Default true) am WorldBuilder teilt Strassen-/Gebaeude-Meshes nach Dreieck-Schwerpunkt auf ein Grid (`CityChunkSizeMeters`, Default 500) - `FWiesbadenCityChunking::BuildChunks` (GIS/WiesbadenCityChunking, datenrein, Test `Core.CityChunking`) remappt die Vertex-Indizes pro Zelle (geteilte Kanten bleiben konsistent); je Zelle spawnt `SpawnCityChunks` einen `AWiesbadenCityChunk` (World/WiesbadenCityChunk, 2 ProceduralMesh-Komponenten, nicht transient -> wird mit der Map serialisiert). Die monolithischen WorldBuilder-Komponenten bleiben dabei LEER (sonst Doppel-Geometrie). `EnsureWorldPartition` (WorldBuilder, wird in `BuildCity` VOR `SpawnCityChunks` UND in `SaveCityAsMap` aufgerufen) bildet den KOMPLETTEN "Enable World Partition"-Flow nach (`FWorldPartitionConverter::Convert`, UE-Quelle WorldPartitionConverter.cpp): (1) `PersistentLevel->ConvertAllActorsToPackaging(true)` (setzt bUseExternalActors + Reduced-Schema, NUR wenn `IsUsingExternalActors()` noch false), (2) `CreateOrRepairWorldPartition(WorldSettings)`, (3) `WorldPartition->bEnableStreaming = true` (bStreamingWasEnabled setzt Initialize selbst), (4) `WorldPartition->Initialize(World, Identity)` wenn nicht initialisiert + `WorldPartitionChangedEvent.Broadcast(World)`. Ohne die Schritte 1+4 werden neu gespawnte Chunk-Actors beim SaveMap NICHT externalisiert (0 External-Actor-Packages, 8-GB-umap, Stadt unsichtbar im Spiel) - nur `CreateOrRepairWorldPartition` reicht NICHT (war der Fehler in mehreren Rebuilds). WICHTIG: `Initialize` registriert den ActorDescContainer, der ueber `FCoreUObjectDelegates::OnObjectPreSave` bei JEDER Actor-Speicherung den ActorDesc erzeugt (`Actor->CreateActorDesc()`, ActorDescContainer.cpp:561) - deshalb externalisiert der Save erst nach Initialize. `IWorldPartitionEditorModule::Get()` laedt das Modul on-demand, auch im Commandlet sicher. Danach markiert er WorldBuilder + Landscape als always-loaded (`SetIsSpatiallyLoaded(false)`), damit `FindBakedCityBuilder()`/Traffic auch bei entladenen Zellen funktioniert - die Chunk-Actors streamen ueber die bestehende StreamingSource (AWiesbadenStreamingSource). MapCheck-Fehler "Nicht-raeumlich geladener Actor referenziert raeumlich geladene" (1984x) sind erwartbar: der WorldBuilder haelt das `CityChunks`-UPROPERTY-Array mit harten Referenzen auf die spatial-geladenen Chunks - das zwingt deren Nachladen (Stadt sichtbar), macht aber das Zellen-Streaming ineffizient (alle Chunks laden mit).
- WorldClaw-Dreischritt als Architektur-Leitlinie: Planung = `CityPrompt` (Prompt->Spec), Assets = OSM/DEM-Pipeline, **Regionen = `UWiesbadenRegionGenerator`** (GIS/WiesbadenRegion, WorldClaw-Schritt 3): OSM-Flaechen (landuse/natural/leisure/waterway) -> Regionen (Wasser/Gruen/Wohnen/Gewerbe/Industrie). Paperkonform (arXiv 2608.05248 §2.3): Regionen-Pass laeuft in `BuildCityData` VOR der Gebaeude-Erzeugung (Stufen 62%/68%); der BuildingGenerator bekommt die `RegionMap` ueber `FBuildingGenerationSettings` und entscheidet pro Gebaeude BEIM BAUEN (nicht per Nach-Pass): Wasser-Gebaeude werden bei `bRemoveWaterBuildings` verworfen, BEVOR Mesh entsteht (kein Haus im See - auch nicht im Mesh), plus regionale Hoehen-/Fassaden-Faktoren (`GetRegionalHeightScale`: Industrie 1.1/Gruen 0.85; `GetRegionalFacadeKey`: `Region:Industrie`/`Region:Gewerbe`). Die Region-Zuordnung lebt AUSSCHLIESSLICH im BuildingGenerator - `AssignRegionsToBuildings` wurde entfernt (war toter Nach-Pass-Code, der nur Tests bediente). Prioritaet Wasser > Gruen > Residential > Commercial > Industrial (Uferpark!). `MinRegionAreaSqm`=500 filtert Flecken; Gebaeude bleiben separate Instanzen (RegionType/RegionName). RegionGenerator ist optional in `FBuildTools` - MUSS in WorldBuilder UND GameInstance gesetzt werden (beide NewObject + UPROPERTY gerootet).
- Gebaeude-Fassaden: Mesh-Sections sind nach `MaterialVariant` (0-5: Putz/Backstein/Sandstein/Glas/Beton/Fachwerk) gruppiert; die Varianten-Pipeline ist bewusst UNANGETASTET. Per-Adress-Override (bewusst getrennt): `BuildingSettings.FacadeOverrideAddresses` (Worker) + `AddressFacadeMaterials` (Adresse → Material, Editor/CityActor) — der Generator gibt dem Gebaeude nur einen eigenen Abschnitt (`FBuildingMeshSection::FacadeOverrideKey`), `ResolveBuildingMaterial(Channel, Key)` waehlt direkt das Material. `PutzFassade_*` (ambientCG „Plaster005“, CC0) liegt in `Content/Textures/Facades/`. Der Adress-Abgleich ist lowercase- plus sz/umlaut-insensitiv (`NormalizeAddressForMatch`: ß→ss, ä/ö/ü→ae/oe/ue), damit OSM-„Straße“ die ASCII-Config „Strasse“ trifft; rein datenbasiert, Test `Buildings.AddressNormalization`.

## City-Prompt

- `GIS/CityPrompt` parst eine Textzeile (deutsch/englisch, ae/oe/ue/ss-Normalisierung wie `NormalizeAddressForMatch`) regelbasiert in `FCityPromptSpec` (Dichte 0..1, Fassadenstil-Gewichte 0-5, Landmarken, Wetter) - WorldClaw-artig "Prompt -> Spezifikation", aber ohne LLM. Wortgrenzen-Check: flektierte Formen ("lockere", "gruener") muessen als eigene Keywords gefuehrt werden. Die Pipeline wendet Dichte (MinFootprintAreaSqm) UND Stil-/Landmarken-Overrides an: `FacadeOverrideKey` je Gebaeude mit Prioritaet Adress-Override > Landmarke (`PromptLandmark:<Name>`, matcht name=* wie `NormalizeAddressForMatch` + Wortgrenzen, setzt auch `bIsLandmark`) > Stil (`PromptStyle:<Name>`, deterministisch gewichtete Auswahl per FNV-1a-Hash der OSM-Id, sortierte Varianten wegen TMap-Unsortiertheit). Die Varianten-Pipeline ist unangetastet; die Render-Seite loest die Keys ueber `PromptFacadeMaterials` auf (analog `AddressFacadeMaterials`). Stil-Namen zentral in `CityPromptParser::GetFacadeStyleNames()` (Key-Drift vermeiden!). DisplayName bei Gleichstand: kleinste Variantennummer.
- City-Prompt erkennt auch Tageszeit (`TimeOfDayHours`, -1 = nicht gesetzt; nachts 0/abends 19/morgens 8 Uhr), Verkehrsdichte (`TrafficDensity` 0..1 -> Spawn-Rate der Verkehrs-Simulation, s. u.) und Gebaeudehoehen (`BuildingHeightScale` 0.6/1.0/1.4/2.0 -> `BuildingSettings.HeightScale` im Generator). Tageszeit wird beim Stadt-Spawn ins Wetter-System uebernommen (`SetTimeOfDay` im CitySubsystem). Wortgrenzen erzwingen Flektions-Eintraege: "leere"/"leeren" fuer "leer", "hochhaeusern" fuer "hochhaus" (node-Port deckte beide Luecken auf).
- Node-Port `Tools/verify_cityprompt.mjs` bildet `CityPromptParser::Parse` 1:1 ab (Fixture-Suite + C++-Inventar-Abgleich, `--parse` fuer Headless-Experimente). Regel-Aenderungen im C++-Parser MUSSEN den Port mitziehen: Der Inventar-Abgleich extrahiert die `TEXT("...")`-Literale direkt aus `GIS/CityPrompt.cpp` und laeuft als Drift-Guard in `Tools/verify-worktree-sync.mjs` (blockiert auch `--sync`). Das Regel-Inventar lebt zentral in `RULES` (einmal definiert, von Parse() und Drift-Check genutzt). Die Prompt->Spec-Erwartungen liegen in der gemeinsamen `Content/Config/CityPromptSpecFixture.json`, die der C++-Test `CityPrompt.SpecFixture` UND der Port konsumieren - bidirektionaler Abgleich: eine Regel-Aenderung muss in BEIDEN gruen laufen (Fixture pflegen statt Erwartungen in C++/Node duplizieren).
- `World/WiesbadenWeatherSystem` (datenreine USTRUCT-Zustandsmaschine, pro Welt im `UWiesbadenCitySubsystem`; dort `Weather.Tick(DeltaTime)` + `SetTargetWeather(Data.CityPromptSpec.Weather)` beim Stadt-Spawn, Blueprint `GetWeatherState()`/`SetWeatherTarget()`): Wetterlage aus `ECityWeatherPreset`, Blend ueber `TransitionSeconds`, Tageszeit `HoursPerRealSecond` = 24/3600 (1 Realstunde = 24 Ingame-Stunden). WICHTIG: `Tick` clamp't grosse Deltas auf 10 s (Schutz vor Zeitspruengen) — Tests muessen in <=10s-Schritten fahren; `SetTargetWeather` muss `LastState.Blend01` sofort mitziehen, sonst liest `GetState()` den alten Blend.
- Wetter-Rendering: `World/WiesbadenWeatherFX` — `FWiesbadenWeatherFXParams::FromWeatherState` (datenrein, deterministisch) leitet aus dem State die FX-Parameter ab; die `UWiesbadenWeatherFXComponent` am CityActor spawnt/steuert Niagara-Effekte ausschliesslich ueber **User**-Parameter (`SetVariableFloat`/`SetVariableLinearColor`) — Namen muessen exakt den exponierten User-Parametern der NS_/NE_-Assets entsprechen (RainSpawnRate/SnowSpawnRate/FogDensity/CloudOpacity/LightningInterval/WindSpeed/SunLightColor). `Niagara` ist in `WiesbadenReal.Build.cs` gelistet. Schnee nutzt Rain-Intensitaet als Dichte-Stellvertreter (eigenes Effekt-System, langsamere Rate); Blitze nur bei Thunderstorm, Intervall deterministisch aus Tageszeit. Die Komponente treibt zusaetzlich die **DirectionalLight** der Szene (`SunLight`-UPROPERTY oder automatisch erste `ADirectionalLight` via TActorIterator in BeginPlay): `SunIntensity` = 3.14 * Tageslicht * (1 - 0.6*Bewoelkung) - nachts 0, Gewitter daemmerig. Vertrags-Validierung: `Content/Config/WeatherFXCatalog.json` spezifiziert die 5 Assets (Emitter/Module/Renderer + User-Parameter mit Typ/Default/Clamp, praefixfreie Namen); `UWiesbadenWeatherFXComponent::ValidateSystem` prueft zugewiesene Systeme nach jedem Spawn (`GetExposedParameters().GetParameters()` gegen die Katalog-Vertraege, `User.`-Praefix wird fuer den Vergleich entfernt) und warnt bei fehlenden Parametern - kein stummer Vertragsbruch. NS_-Assets sind Binaerdateien und muessen im Editor gebaut werden; die JSON ist die editorfaehige Blaupause. WICHTIG (UE-Quelle verifiziert): `Activate(true)` auf einem bereits aktiven System setzt es zurueck (`EResetMode::ResetSystem` in `ActivateInternal`, Early-out nur bei `bReset==false`) - die FX-Komponente aktiviert deshalb nur beim Uebergang inaktiv->aktiv (`IsActive()`-Guard), nie pro Tick. Katalog spezifiziert je Asset explizite **Fixed Bounds** (`bounds`-Feld) - CPUSim-Emitter ohne Bounds werden bei Distanz-Culling weggeculled (Wolken-Schale, Nebel, Regenbox); `WarnMissingFixedBounds` warnt EINMALIG je System, wenn laut Katalog Bounds noetig, aber keine nutzbaren gesetzt sind (`HasUsableFixedBounds`: FBox muss gueltig UND Extent>0 sein - die 5.8-`FBox`/`TBox` ist bei nie gesetzten Bounds invalid bzw. Null-Box; `GetFixedBounds()` an `UNiagaraSystem`). **Auto-Zuweisung:** nicht manuell gesetzte Effekt-Systeme laedt die Komponente in BeginPlay aus den kanonischen Pfaden `GetDefaultAssetPath` (`/Game/Niagara/NS_Weather<Name>`, identisch zum `path`-Feld je Asset im Katalog) - sobald die Assets nach Content/Niagara gebaut sind, laeuft das Wetter ohne Details-Panel-Konfiguration. Programmatisches NS_-Authoring ist in 5.8 unzuverlaessig (kein `FNiagaraStackGraphUtilities::AddModuleToStack` mehr - Modul-Verkabelung waere blinde Graph-Chirurgie); der Editor-Test `FXCatalogModules` verifiziert die Katalog-Modul-Referenzen gegen das Engine-Content - alle Namen sind real (SpawnRate/SpawnBurst_Instantaneous/AddVelocity/GravityForce/Drag/**Color** (statt SetOpacity, Alpha=Opacity)/**ParticleState** (statt SetLifeTime)/RibbonWidth); der Blitz-Intervall wird per Emitter-Loop (Lifetime=User.LightningInterval) + SpawnBurst_Instantaneous geloest. Headless-Vertragscheck: `node Tools/verify_weatherfx_catalog.mjs` (69 Checks: Katalog userParameters/path gegen den C++-Vertrag, Modulnamen gegen das UE-5.8-Modul-Inventar, bounds-Ausdehnung) - der Editor-Test FXCatalogModules ist das Editor-Pendant; Bau-Anleitung fuer den manuellen Editor-Schritt: `Content/Config/WeatherFXAssetBuildGuide.md` (je Asset: Emitter/Module mit Engine-Pfaden, User-Parameter, Fixed Bounds, Zielpfad).
- Regionen-Assets: `GIS/WiesbadenRegionAssets` — `UWiesbadenRegionAssetGenerator::Generate` (datenrein, deterministisch; Seed = FNV-1a-Hash von `Region.Name + ":" + Kategorie`) platziert Baeume nur in Green-, Ufer-Objekte nur in Water- und Industrie-Objekte nur in Industrial/Commercial-Regionen (Gitter + Jitter, Punkt-in-Polygon). Pass in `BuildCityData` zwischen Regions (62%) und Buildings (68%) bei 66% — braucht `OutData.RegionReport.bSuccess`. Spawner `URegionAssetSpawnerComponent` (CityActor): Baeume als **HISM**, Ufer/Industrie als ISM, je ein StaticMesh/Material pro Kategorie im Details-Panel; ohne zugewiesene Meshes wird nur geloggt. `bGenerateRegionAssets`/`RegionAssetSettings` existieren an WorldBuilder UND GameInstance. **Fallstrick (14.719 Gamethread-Hitches je Lauf, meist Frame 0):** die varianten Baum/Busch-HISM entstehen in `MakeInstanceComponent` per `NewObject(Owner, Name)` OHNE `RF_Transient` - trotz Header-Kommentar "Instanz-Komponente ist transient". Damit werden sie beim Backen in die Karte serialisiert; `BeginPlay` legt sie mit denselben Namen erneut an, `NewObject` auf belegten Namen zwingt den Spiel-Thread auf die Render-Aufraeumung des Alten zu warten ("waiting for resource cleanup ... Fix the higher level code"). Fix (Commit 1193364): vorhandene Komponente per `StaticFindObjectFast` WIEDERVERWENDEN - funktioniert mit der bereits gebackenen Karte ohne Neubau.

## Verkehrs-Simulation

- `GIS/WiesbadenTrafficSimulation` (`FWiesbadenTrafficSimulation`, USTRUCT, datenrein/deterministisch): Fahrzeuge folgen dem Spur-Graph von `FRoadNetwork` (Spur-Centerline -> `FLaneConnection::ConnectionPath` -> Folgespur). **LaneId == Index in `Network->Lanes`** (wie `FRoadNetwork::GetLane`) - Tests bauen Netze deshalb per Index. An Kreuzungen waehlt ein Fahrzeug deterministisch per FNV-1a-Hash(FahrzeugId, KnotenId) unter den nicht-restricted Nachfolgern; Sackgassen entfernen es. Spawn-Rate = `MaxSpawnRatePerSecond` * `TrafficDensity` (Round-Robin ueber befahrbare Spuren; blockierter Spur-Anfang < MinGap verschiebt den Spawn). Kopf-zu-Schwanz je Bahn (Sortierung absteigend nach Distanz, bei Gleichstand nach FahrzeugId - totale Ordnung fuer Determinismus). WICHTIG: `Initialize` haelt einen `const FRoadNetwork*` - die Pipeline konfiguriert nur `FWiesbadenCityData::TrafficSettings` (Dichte aus Prompt), initialisieren/ticken darf nur das `UWiesbadenCitySubsystem` beim Stadt-Spawn auf dem finalen GameInstance-Container (Move wuerde den Zeiger stale machen). Blueprint: `GetTrafficReport()`/`GetTrafficVehicles()`. Node-Port (`verify-traffic-sim.js`) und C++-Test `TrafficSimulationTest.cpp` teilen dieselben Erwartungen (2/7/10-Spawns, MinGap-Kette 300/900 cm/s, keine UE_Logs im Sim-Modul).

## Flug & Audio

- Heli-Rotor-Physik (`FWiesbadenRotorPhysics`): `bCoaxialRotors` = gegenlaeufiger Doppelrotor (ka-52-Stil, verdoppelter Auftrieb, kein Heckrotor, Pedal = direktes Yaw-Moment); `MaxForwardSpeedMetersPerS` + `RetreatingBladeStallStartFrac` modellieren den Blattspitzenverlust (LiftScale -> 0.45 an vmax) statt hartem Speed-Clamp. `EngineRpm = MainRotorRpm * EngineToMainRotorRatio` (0 bei Triebwerk aus) speist Audio/HUD.
- Flugsound: `UWiesbadenHelicopterAudioComponent` spielt Assets (RotorSound/EngineSound) mit RPM-/Last-Parametern ODER den prozeduralen Fallback `FWiesbadenHelicopterAudioModel` (deterministischer xorshift-PRNG, Seed-Parameter -> Automation-/node-tests): Rotor = Rauschen durch One-Pole-Tiefpass (Cutoff steigt mit RPM+Collective) + Wop-Wop-AM mit Blattpassfrequenz, Motor = Ton RPM/60*8 Zylinder. Samples als int16-PCM in `USoundWaveProcedural::QueueAudio` (vorher `GetAvailableAudioByteCount()` gegen Pufferdrift pruefen); `USoundWaveProcedural::NumSamplesToGeneratePerCallback` ist protected - nicht setzbar. `BladeSlapDepth` (AM-Tiefe) + `RotorCutoffBaseHz` (Basis-Cutoff) erzeugen den Kampfheli-Charakter; der Heli nutzt Koaxial-Konfiguration (bCoaxialRotors=true, zweiter gegenlaeufiger Rotor, BladeCount=3).

- **`FMath::FInterpTo(x, 0, dt, Speed)` gibt bei `Speed<=0` SOFORT das Ziel (0) zurueck** (UE-Quelle). Die Heli-Ratendaempfung setzte `Speed = RateAssist*Neutral`, `Neutral=0` bei vollem Ausschlag -> Gier-/Nick-/Rollrate wurde JEDES Bild auf 0 gerissen (Giermoment war korrekt 360k N*m, nur die Rate genullt; Fehlerbild "Heli giert nicht" trotz richtiger Autoritaet). Fix: Daempfung nur bei `Neutral>epsilon`. Danach `CoaxialYawAuthority` 60000->16000 (sonst >400 Grad/s statt ~30-80).
- Externe Steuerung: `AWiesbadenHelicopter::SetExternalControl(FWiesbadenHeliControl)`/`ClearExternalControl` ist der saubere Eingang (KI/Zwischensequenz/Replay/Test), wirkt ueber die echte Rotorphysik; `ReadInput` wendet ihn nur an, enthaelt sonst NULL Test-Code. Test-Choreografie (Gierprobe/Flugprofil) liegt in `UWiesbadenVehicleTestHarness` (UActorComponent), das die Dev-Befehle zur Laufzeit auf dem Heli anlegen - im normalen Spiel existiert es nicht.
- Dieselbe Naht am Fahrzeug: `AWiesbadenCar::SetExternalControl(FWiesbadenCarControl)`/`ClearExternalControl` (Throttle/Brake/Steering/bHandbrake/bReverse). `ReadInput` prueft `bExternalControlActive` GANZ oben und umgeht dann die Tastenabfrage (glaettet die externen Werte per FInterpTo wie eine echte Eingabe), `ApplyVehiclePhysics` zieht die Handbremse ebenfalls aus dem Steuerwert. `UWiesbadenVehicleTestHarness` traegt jetzt Heli- UND Fahrzeugprofile (`Heli()`/`Car()`, getrennte Tick-Zweige); das Fahrprofil (`StartDriveProfile`) faehrt Vollgas geradeaus, dann Lenk-Sweep und misst die Kursaenderung wrap-sicher gegen den Startkurs (`FMath::FindDeltaAngleDegrees`). So beweist der Standard-Kaefer Laengsdynamik (0->~60 km/h, Gaenge 1->3) und Lenkung ohne Tastatur.
- Helikopter-Autopilot `UWiesbadenHelicopterAutopilot` (UActorComponent, UNABHAENGIG vom Test-Harness, echte Spiel-KI): `FlyTo(WorldTarget)`/`HoldPosition()`/`Disengage()`, treibt den Heli per SetExternalControl. Kaskadierte P-Regler: Horizontalfehler->Ziel-Geschwindigkeit (gekappt)->Nick/Roll im Rumpf-Frame (nahe am Ziel geht die Ziel-Geschwindigkeit gegen 0 -> bremst = Position halten, EIN Gesetz fuer Anflug+Schweben); Hoehenfehler->Ziel-Steigrate->Kollektiv. **Nicht-offensichtlich:** Vorwaertsflug kippt den Rotor und KLAUT Vertikalschub -> der Heli sackt trotz vollem Kollektiv ab; reine Rueckfuehrung kommt zu spaet. Fix = Auftriebs-VORSTEUERUNG `Collective += TiltLiftCompensation*(|Pitch|+|Roll|)`. **LOAD-BEARING (Audit-Fund):** der Heli bewegt sich KINEMATISCH (`AddActorWorldOffset`, keine Physik/MovementComponent/ComponentVelocity) -> `AActor::GetVelocity()` liefert **0**. Fuer jede Regelung/KI MUSS `GetVelocityMetersPerSecond()` genutzt werden (liefert das interne `Velocity`-Member in m/s), sonst ist die gesamte Geschwindigkeits-Daempfung wirkungslos und die Beruhigung passiert nur durch Rotor-Drag. **Zwei Folge-Fallen der ECHTEN Daempfung:** (a) der Rumpf darf sich nur WEIT weg (`FaceTargetMinDistanceMeters` ~60 m) zum Ziel giern - naeher dran wuerde die aktive Bremse im drehenden Frame tangential wirken -> Umkreisen; (b) der Rotor hat eine Anfahr-Totzone: sehr kleine Nick-Befehle bewegen ihn nicht -> ohne `MinApproachSpeed`-Untergrenze (~2 m/s bis zum Ankunftsradius) bleibt er mit stationaerem Fehler kurz vorm Ziel stehen. Sluggisher Plant -> niedrige Geschwindigkeit (MaxApproachSpeed ~5, ApproachGain ~0.05) noetig, sonst ueberschiesst er (Bremsautoritaet gering). Verifiziert: Ankunft <8 m, Hoehe +-1 m, konvergiert ohne Umkreisen. `GetAltitudeMeters()` (Raycast) fuer Regelung meiden (verrauscht).
- Kollektiv-Kennlinie am Heli auf `MaxCollectivePitchDeg=6`/`MinCollectivePitchDeg=3` gezogen (Modul-Default 2..14 gab bei vollem Hebel ~4x Gewicht -> >20 m/s Steigen); der Schwebepitch stellt sich per `ComputeHoverPitchDeg` selbst ein (lift=weight, ~4 Grad). `GetAltitudeMeters` ist ein Abwaerts-Raycast -> trifft Dachfirste, die Vario-/Hoehen-Anzeige rauscht entsprechend.

## Dev-Befehle & In-Game-Verifikation

- **`-ExecCmds` erreicht NUR die `ULocalPlayer::Exec`-Kette** (Engine/Private/Player.cpp, UE-Quelle): PlayerInput -> PlayerController (`ExecActor`) -> Pawn -> HUD -> GameMode -> CheatManager -> GameState -> CameraManager. NICHT die GameInstance/deren Subsysteme (die erreicht nur die In-Game-Konsole ueber `UGameViewportClient::Exec`). Deshalb liegen die Dev-Execs auf `AWiesbadenPlayerController` (in `WiesbadenGameMode` via `PlayerControllerClass` gesetzt), NICHT in einem Subsystem - ein `UGameInstanceSubsystem`-Exec feuerte per `-ExecCmds` nie.
- **`-ExecCmds` trennt Befehle per KOMMA, nicht Semikolon.** Semikolon macht alles zu EINEM Befehl (der erste schluckt den Rest als Argumente).
- Dev-Execs (AWiesbadenPlayerController): `WbTeleport <0-2>`, `WbResetVehicle`, `WbTraffic <0/1>`, `WbCam <0-2>`, `WbHeli`, `WbNudge <nick> <roll>`, `WbHeliYaw <s>`, `WbHeliFly <s>`, `WbDrive <s>` (Fahrprofil am besessenen Fahrzeug), `WbHeliGoto <dx> <dy> <dz>` (Autopilot fliegt <dx,dy,dz> m relativ und haelt), `WbHeliHover` (Autopilot haelt Position), `WbHeliOff` (Autopilot aus -> Steuerung zurueck an Tastatur, mitten im Flug loesbar). Datenreine Kernlogik (Teleportziele/Aufrichten) in `FWiesbadenDevActions` - teilt sich mit dem Pause-Menue (Test `WiesbadenReal.Dev.Actions`). `GetOrAddHarness(AActor*)` legt den Test-Harness on-demand auf Heli ODER Fahrzeug an; `GetOrAddAutopilot` analog fuer den Autopiloten. Autopilot-Nachweis im Log: `WbDev Autopilot t=N: Abstand X m ... Modus Anflug/Halten` + `Wegpunkt erreicht`.
- **Tastatur-Injektion (keybd_event/SendInput) erreicht das D3D-Spielfenster NICHT.** Verifikation laeuft ueber `-ExecCmds` + Log + `CopyFromScreen`-Screenshots (nur bei Fenster-Vordergrund; PrintWindow ist auf D3D schwarz). CopyFromScreen faengt bei aktiver Desktop-Nutzung leicht Fremdfenster ein.
- Cockpit-Innensicht: keine 3D-Innenraeume modelliert. `UWiesbadenVehicleCameraComponent::AddCockpitHiddenMesh` blendet die eigene Aussenhaut fuer den Fahrer aus (`bOwnerNoSee`, nur seine Sicht), das HUD zeichnet die Instrumententafel. Cockpit-Kamera-Versatz je Fahrzeug im Konstruktor (Default `CockpitOffset(95,0,140)` + Kamera bei `(0,0,110)` ergab Z~250 = schwebte ueber dem Wagen).
- Rauchtest `Tools/smoke_test.cmd` (+ `.ps1`): ZWEI kurze Editorsitzungen (Helfer `Invoke-Session`), feuert Dev-Execs, wertet aus dem Log Bestanden/Durchgefallen (Exit 0/1). 6 Pruefungen: Fahren (WbDrive: Tempo>20 km/h + Kursaenderung>15 Grad am Standard-Kaefer), Materialien, Teleport, ResetVehicle, HeliFly, HeliYaw. Zwei Sitzungen, weil Fahrzeug und Heli sich die Besitzung teilen (der Kaefer muss fuer WbDrive besessen bleiben, WbHeli entlaedt ihn). World-Partition-Eigenheiten: (a) beim Umherfliegen haengt das Spiel streckenweise ("Gamethread hitch waiting for resource cleanup"), also NICHT auf Demo-Ende/Fahrende warten, sondern auf GENUG Log-Messpunkte bzw. die Material-Bilanz (feuert 8 s nach dem Laden); (b) seit dem HISM-Overwrite-Fix laden zwei Sitzungen hintereinander wieder zuverlaessig.
- **Headless-FPS ist doch aus dem Log messbar:** der 8-s-Diagnoseblock (`UWiesbadenCitySubsystem`, feuert ungated `GeometryReportDelay>=8`) loggt `Bildzeit ueber N Bilder: Mittel X ms (Y Bilder/s) ...` PLUS den Strang-Split `Straenge im Mittel: Spiel X ms, Renderer Y ms, Grafikkarte Z ms`. (Frueher gesehene "28/144 FPS" waren das ANDERE Projekt "Wiesbaden Survivors" im Hintergrund, NICHT dieses Spiel.)
- **Der Engpass ist der SPIEL-STRANG, NICHT die GPU** (fruehere GPU-These war falsch; auch der Chunk-Code-Kommentar sagte es schon). Gemessen am Boden im Stand: **Spiel ~110-160 ms, Renderer ~11 ms, Grafikkarte ~7-16 ms** (~10 FPS). Nanite/LODs/Sichtweite braeuchten hier GAR NICHTS - die GPU langweilt. Der Diagnoseblock loggt jetzt zusaetzlich ein **Last-Inventar** (`LogGeometryBalance`): typ. **~660 Chunk-Actors, ~19.700 Primitive-Komponenten (~11.500 beweglich, ~4.900 mit Kollision), ~10.000 Instanz-Komponenten mit ~1,07 Mio. Instanzen** - alles gleichzeitig resident. Die Kosten haengen an dieser VERWALTETEN Menge (Sichtbarkeits-/Bounds-/HISM-Cluster-Cull je Bild), nicht an der sichtbaren.
- **Sackgasse (verifiziert):** Streaming-Radius am Boden verkleinern (`AWiesbadenStreamingSource`, adaptiv nach Hoehe) bringt NICHTS - die 664 Chunks bleiben trotz 2000-m-Radius resident. `MarkAlwaysLoaded` steht nur auf Sky/Sun/Fog, die Chunks sind also nicht always-loaded; trotzdem streamt WP sie nicht aus. Ursache liegt in der WP-Streaming-Granularitaet/Chunk-Einrichtung (Runtime-Grid-Zellgroesse vs. Stadt), nicht am Radius. **Echter Hebel:** WP-Runtime-Grid feiner konfigurieren ODER Chunks/Foliage tatsaechlich streambar backen (Editor + Re-Bake), sodass am Boden nur die Nachbarschaft resident ist; DANN wuerde ein hoehenadaptiver Streaming-Radius (Boden klein, Luft gross fuers Helikopter-Panorama) die Bildrate vervielfachen.
- **Fahrzeug-Architektur:** Standardauto ist `AWiesbadenCar` - KEINE reine Kinematik, sondern das Modul `FWiesbadenVehiclePhysics` (Motor, Automatikgetriebe, Radkraefte, Lenkung), vom Pawn integriert (Geschwindigkeit->Position, Gierrate->Ausrichtung, Bodenkontakt per Raycast). Es beschleunigt real (0->~60 km/h, Gaenge) und lenkt - der ausgelieferte Kaefer. `AWiesbadenChaosCar` (echte Chaos-Physik) nur mit `-WbChaosCar` und hat noch den Chassis-Kollisions-Bug (sitzt auf dem Bauch, faehrt daher nicht vorwaerts; Fix nur im UE-Editor am PhysicsAsset, `SkeletalBodySetups` sind nicht per Python skriptbar). Der HUD-Tacho zeichnet nur fuer `AWiesbadenCar`. Deshalb weist der Rauchtest die Fahrphysik am Standardauto (WbDrive) nach, nicht am belly-gebugten ChaosCar.

## Verkehrszeichen & Assets

- Verkehrszeichen-Katalog liegt als JSON unter `Content/Config/TrafficSignCatalog.json` (`aliases`-Feld für OSM-Kurz-/Alt-Formen); Fallback bei fehlender Datei: nur 274/278. Textur-Name ersetzt `.`→`-` (`Sign_325-1.png` für `325.1`): UE-Asset-Namen dürfen keinen Punkt enthalten (Punkt trennt Package von Objekt). Namens-/Auflösungs-Helfer liegen zentral in `WiesbadenSignAssets` (BuildTextureName/ResolveTexture/CreateMaterial) — `AWiesbadenWorldBuilder::ResolveSignTexture/ResolveSignMaterial` und der Spawner delegieren dorthin.
- Der Katalog ist ein **mutables, gelocktes Registry** (`FCriticalSection`); `GetCatalog()` liefert eine Kopie (Snapshot), `ParseOsmTag` holt sie einmal je Aufruf. Blueprints erreichen es über `UWiesbadenTrafficSignLibrary` (Add/Remove/Get/Find/Reload); die `ReloadTrafficSignCatalog`-CallInEditor-Kachel am WorldBuilder ist der Hot-Reload der JSON.
- Beide Datenkataloge (Verkehrszeichen + `WiesbadenRoadTypes.json`) liegen als lose Dateien unter `Content/Config/`; den gemeinsamen Pfad liefert `WiesbadenConfigPaths::ConfigFile` (GIS/WiesbadenConfigPaths.h). `URoadTypeLibrary::LoadFromJsonFile` greift bei fehlender Datei auf die Code-Defaults (RASt 06/RAA) zurück.
- Node-Port `Tools/verify_roadtypes.mjs` bildet `URoadTypeLibrary::LoadFromJsonFile`/`ApplyBuiltInDefaults` 1:1 ab (RASt-06/RAA-Tabelle + JSON-Feld-Guards mit denselben Guard-Bedingungen, `--parse [pfad]` fuer schnelle Katalog-Tests). Inventar-Drift-Guard gegen `RoadTypeLibrary.cpp` (Add()-Tabelle, Oberflaechen-Ueberrides, JSON-Feldnamen) wie beim CityPrompt-Port - eine Aenderung im C++ ohne Port-Update faellt auf (Exit 1).
- Visueller Ausstattungs-Spawner `URoadFurnitureSpawnerComponent` (Schilder-Mast+Tafel, Leitpfosten, Halt-/Wartelinien) als ISM: ein Draw-Call je eindeutigem Zeichen (per-Sign-MID), nicht der fruehere Atlas-Ansatz (SignAtlasBaker wurde entfernt). Haengt im Editor am WorldBuilder, zur Laufzeit am CityActor.
- Katalog-Textur-Validierung läuft beim Editor-Start über `FWiesbadenRealModule::StartupModule` (Game-Modul, `#if WITH_EDITOR`) → `ValidateTexturesAtStartup`; `speedLimit`-Basen (274/278) und `noTexture`-Einträge (600) werden übersprungen.
- Schilder-Grafiken stammen von Wikimedia Commons; die API rate-limitt aggressiv (HTTP 429) → Downloads mit Retry/Backoff + User-Agent.
- BASt liefert nur JPG ohne Transparenz (ungeeignet für runde Schilder); `600` (Absperrpfosten) hat kein amtliches Flach-SVG.
- Schild-Texturen-Import: PNGs sind Quellen; im GEBAUTEN Worktree sind die 66 Zeichen als `.uasset` nach `/Game/Textures/TrafficSigns/Sign_<Id>` importiert (`ResolveTexture` findet sie, Schilder zeigen echte Texturen). Der Freebuff-Worktree haelt nur die PNG-Quellen (importierte `.uasset` sind NUR-GEBAUT wie die gebackene Map). Nach-Import neuer PNGs: `UnrealEditor-Cmd.exe <proj> -run=pythonscript -script=...` mit `AssetImportTask` (`unreal.AssetToolsHelpers.get_asset_tools()`), Wiederverwendbares Skript `.freebuff/tmp/import_signs.py`. FALLSTRICK: Python-`print()`/`unreal.log()` werden im Cmd-Stream NICHT erfasst (Log zeigt nur Interchange-Zeilen) - Verifikationsergebnis in eine Datei schreiben und danach lesen.

## Datenquellen (offline beschafft, gitignored)

- OSM: `WiesbadenReal/Data/Raw/OSM/wiesbaden.osm.json` (seit 2026-08-18: 162,6 MB, Overpass-JSON, pretty-printed; 1,35 Mio. elements / 1,115 Mio. Nodes geparst; BBox 49.995,8.08,50.16,8.42; enthaelt 484 Elemente „Mainzer Straße“ inkl. Hausnummer 129 als Way 153554408). Holen: `Tools/overpass_fetch.mjs` (node, Retry/Backoff, exakt `BuildOverpassQuery`). WICHTIG - Query-Fallstrick: KEINE `relation["type"="route"]`-Abfrage erlaubt! Der Recurse `>;` zieht die GESAMTEN Mitglieds-Geometrien der Linien in die Antwort - eine Bus-/Zuglinie, die Wiesbaden beruehrt, bringt ihre komplette Strecke (bis Barcelona) mit. Dadurch war der alte Datensatz „ganz Deutschland“ (Bounds Lat 41,4-54,3 / Lon 1,8-19,1), der Terrain-Crop griff nicht (DEM-Kachel kleiner als „OSM-Ausdehnung“) und die Stadt bekam eine volle 111-km-Landscape. Route-Relations werden von der Pipeline NICHT konsumiert (nur `type=restriction` fuer Abbiegeverbote) - beide Query-Quellen (OSMDataParser.cpp `BuildOverpassQuery` + Tool) muessen synchron bleiben. Daten-Bounds nach Fix: ~22 x 41 km (lange Ways kreuzen die BBox legitim); Terrain-Tile ist ein Quadrat ueber max(ExtentX, ExtentY) = ~31,9 km.
- Headless-Stadt-Rebuild: `UnrealEditor-Cmd.exe <proj> -run=pythonscript -script=<rebuild_city.py>` mit `load_level('/Game/Maps/WiesbadenCity')`, WorldBuilder finden, `set_editor_property('bAutoSaveCityAsMap', True)` + `TerrainSettings.GridSize`, dann `build_city()` SYNCHRON aufrufen (blockiert mit internem SlowTask-Pump bis fertig); Ergebnis in JSON-Datei schreiben (Python-print wird nicht erfasst). Die Map-Save-Phase (WP-Verteilung + External-Actor-Packages einzeln) dauert ~15-25 min. Fallstricke: `new_level('/Game/Maps/WiesbadenCity')` schlaegt still fehl, wenn die umap existiert (LevelEditorSubsystem Error „asset already exists") - fuer einen echten Clean-Rebuild die umap+ExternalActors+HLOD-Packages per Bash vorher verschieben/loeschen. Die Rebuild-Skripte muessen die Property-Namen exakt treffen (OsmFilePath/DemFilePath/bGenerateCityChunks/CityChunkSizeMeters). PYTHON IM GAME-KONTEXT: `-game -run=pythonscript` fuehrt das Skript trotzdem im EDITOR-Kontext des Commandlets aus (Python-Script-Commandlet) - `EditorActorSubsystem.get_all_level_actors()` zaehlt dort die spatial-geladenen Chunk-Zellen NICHT (keine StreamingSource aktiv, 0 Chunks), der Beweis fuer "Stadt sichtbar" muss ueber den echten Game-Lauf (`<proj> /Game/Maps/... -game`) + Saved-Log erfolgen (0 Failed-imports, CitySubsystem-Marker, ExternalActors-Zaehler auf der Platte).
- `GEngineIni` (und alle G*Ini) sind NUR Ini-NAMEN ("Engine"), KEINE Pfade - GConfig-APIs (SetString/Flush) loesen sie intern auf, FFileHelper NICHT. Direkter Dateizugriff auf die Projekt-DefaultEngine.ini: `FPaths::ProjectConfigDir() + TEXT("DefaultEngine.ini")`. Im GUI schrieb `LoadFileToString(*GEngineIni)` relativ zum CWD eine falsche Datei und die Rueck-Lese-Verifikation las dieselbe falsche Datei; im Cmd scheitert es laut ("konnte nicht gelesen werden: Engine").
- Terrain `GridSize` (Default 4033 in `FTerrainGenerationSettings`) ist die Landscape-Aufloesung: Der Crop schrumpft nur die FLAECHE (4033er-Grid bleibt 4096 Komponenten = PIE-Speicherkeule, 21-GB-OOM bei Play-Duplikation auf 16-GB-Rechner). Fuer PIE tragbar: `GridSize=2049` (1/4 Speicher, 15,6 m/Quad bei ~32-km-Tile); 1025 als Notbremse (31 m/Quad, blockig).
- DEM: SRTM-1 `.hgt` aus dem AWS-Open-Data-Bucket „Skadi“ (`https://elevation-tiles-prod.s3.amazonaws.com/skadi/N<lat>/<tile>.hgt.gz`) — einzige funktionierende .hgt-Quelle ohne API-Key. `kurviger.de`, `dds.cr.usgs.gov` und OpenTopography-ohne-Key sind tot. Kacheln `N50E008.hgt` + `N49E008.hgt` liegen in `Data/Raw/DEM/`; Importer liest .hgt direkt (3601²=1").

## Werkzeug-Fallstricke

- Worktree-Sync: `Tools/verify-worktree-sync.mjs` (node) vergleicht Source/Content/Tools byte-genau (SHA-1) zwischen diesem Worktree (`WiesbadenReal/…` + Root-`Tools/`) und dem gebauten (`C:\Users\ssonn\aivideo\WiesbadenReal\…`, Projekt = Root) und warnt bei Drift (Exit 1); `--sync` kopiert lokal → gebaut, `--built <pfad>` ueberschreibt das Ziel. Nur-gebaut-Dateien werden NIE geloescht. WICHTIG: Datei-Pfade in den Maps sind `{abs, sha1}`-Objekte, Vergleich nur ueber `sha1` (Pfade beider Bäume unterscheiden sich strukturell: Freebuff `WiesbadenReal/Source` ↔ gebaut `Source`; Tools ist die Vereinigung von Root-`Tools/` + `WiesbadenReal/Tools/`). Vor dem Vergleich laeuft der Drift-Guard `Tools/verify_cityprompt.mjs` (Port-Inventar vs. `GIS/CityPrompt.cpp`): Aendert der C++-Parser Regeln ohne parallelen Port-Update, bricht das Skript mit Exit 1 ab - auch bei `--sync` (blockiert, damit der veraltete Port nicht in den gebauten Worktree wandert).
- `python3` ist ein Windows-Store-Alias (funktioniert nicht). Auf dem neuen Rechner funktioniert aber `python` (echtes Python 3.14 unter `C:\Python314`); im alten Setup war `node` (v24) der einzige Ausweg. Fuer Skripte/JSON `node` oder jetzt `python`.
- Testlauf via `UnrealEditor-Cmd.exe` zuverlässig nur mit `-stdout` UND absolutem `-project=`-Pfad: ohne `-stdout` kann der Cmd in manchen Sessions stumm mit Exit 1 enden (leere Logs, kein Saved/Logs-Update), obwohl dieselbe Binary funktioniert; Ergebnis dann aus stdout greppen (`Test Completed. Result={...}`). Exit-Code ist AUCH mit `-stdout` unzuverlaessig (ein Lauf endete mit 255, obwohl alle 68 Tests Success waren - ein Test wurde uebersprungen) - gegen `grep -c "Result={Success}"` und die Registrierungszahl (aktuell 135 in Tests/, waechst mit neuen Features) pruefen, bei Abweichung neu laufen. WICHTIG: Seit die `DefaultEngine.ini` `GameDefaultMap=/Game/Maps/WiesbadenCity_Alkis4` setzt, laedt auch der Cmd zuerst die gebackene World-Partition-Map - das Log steht dabei mehrere Minuten auf `WorldPartition initialize started...` (CPU dreht, kein Test Started), dann laufen die Tests normal (aktuell 135/135); Abschluss-Marker im Log: `**** TEST COMPLETE. EXIT CODE: N ****`.
- `run_tests.cmd` ruft `Build.bat` OHNE `call` auf -> es BAUT nur und fuehrt NIE Tests aus (ohne `call` kehrt die Kontrolle nicht zurueck, `Build.bat`s `exit /b` beendet das ganze Skript; Log stoppt nach dem Build, kein `Saved/Logs`, kein Editor-Prozess). Fuer den Testschritt `run_tests_only.cmd` (Editor direkt) nehmen oder `call "...\Build.bat"` einfuegen.
- VS 2022 via winget: der mitgelieferte Bootstrapper ist veraltet -> Exit 5008 "Bootstrapper failed with known error" (das noetige Selbst-Update auf die Mindest-Installer-Version scheitert im `--quiet` still). Fix: frischen Bootstrapper von `https://aka.ms/vs/17/release/vs_community.exe` laden und mit `--add Microsoft.VisualStudio.Workload.NativeDesktop --add Microsoft.VisualStudio.Workload.NativeGame --includeRecommended --passive --norestart` starten.
- PowerShell-`Start-Process -ArgumentList` zerlegt `-ExecCmds="Automation RunTests ..."` an den Leerzeichen -> der Cmd haengt nach "Ready to start automation" stumm (CPU steigt, kein Test Started). Fix: Testlauf als `.bat`-Datei mit exakt quotiertem ExecCmds starten (`Start-Process -FilePath ...bat`). UE-Log-Zeitstempel sind UTC, `date` zeigt lokal (UTC+2) - beim Pollen nicht verwechseln, ein vermeintlich "2 h eingefrorener" Lauf war nur 6 min alt.
- `convert` im PATH ist der Windows-FAT-Konverter, **kein** ImageMagick — keine Bildkonvertierung verfügbar.
- `FFileHelper::SaveStringToFile` (5.8): `EEncodingOptions` ist ein **verschachtelter** Enum von FFileHelper (`FFileHelper::EEncodingOptions::ForceUTF8`), `EFileWrite` dagegen ein globaler Enum (`EFileWrite::FILEWRITE_Append`). Build-Historie der Stadt-Builds: `Saved/BuildHistory/CityBuilds.csv` (`GIS/WiesbadenBuildSummary`, Append thread-sicher via Mutex, Kopfzeile beim ersten Lauf); Auswertung: `node Tools/analyze_city_builds.mjs` (avg/median/min/max, `--trend <n>`).
- Node löst Git-Bash-`/tmp/...` nicht auf — absolute Windows-Pfade verwenden.
- `str_replace` kann Dateien in versteckten Verzeichnissen (`.freebuff/`) nicht editieren.
- `.freebuff/` ist die gitignorierte App-Datenbank; Scratch-Dateien dort nach Gebrauch löschen.

## Stille Fehler (Sitzung 19.08.2026)

Diese Klasse von Fehlern hat gemeinsam, dass **nichts abstürzt und nichts
offensichtlich falsch aussieht**. Sie sind nur durch Nachrechnen konkreter
Zahlen zu finden — jeder hat inzwischen einen Test, der genau darauf prüft.

- **SRTM-`.hgt`-Dateiname bezeichnet die SÜDWEST-Ecke**, nicht die Nordwest-Ecke.
  `N50E008.hgt` deckt 50–51 °N ab. Der Importer zog vorher 1° ab
  (`MinLatitude = North - 1.0`) und legte das gesamte Höhenmodell 111 km zu weit
  südlich — Wiesbaden stand auf fremdem Relief, technisch einwandfrei und völlig
  plausibel aussehend. Auffällig wurde es erst am Höhenmaximum des Rasters von
  891 m: der Große Feldberg (881 m) liegt bei 50,23 °N, südlich von 50 °N gibt es
  diese Höhe nicht. Verräterisch war zudem, dass die **Länge** schon immer korrekt
  als Südwest-Ecke behandelt wurde — nur die Breite wich ab.
  Test: `WiesbadenReal.GIS.HeightmapImporter.SrtmGeoreference`.

- **Terrain-Crop war verdrahtet, aber wirkungslos**, solange `CropBounds` nicht
  übergeben wurde (Default `nullptr`). Das Tile spannte die volle SRTM-Kachel
  (111 km) statt der Stadt → 27,6 m pro Quad. Die Einebnung unter Fahrbahnen war
  dadurch faktisch wirkungslos (0 Zellen). Mit Crop: 7,8 m pro Quad und 636.643
  eingeebnete Zellen. Kennzahl zur Kontrolle: `Landscape erzeugt: … cm/Quad`.

- **`ALandscapeProxy::Import` verlangt den Default-`FGuid()` als Schlüssel** für
  BEIDE Maps (`InImportHeightData`, `InImportMaterialLayerInfos`) und prüft
  vorher `check(Num() == Num())`. Die Engine liest ausschließlich mit
  `FindChecked(FGuid())`. Eine leere Layer-Map bricht an der Größenprüfung ab,
  ein `FGuid::NewGuid()` besteht diese und scheitert danach an `FindChecked` —
  die scheinbar naheliegende Korrektur des ersten Fehlers ist also der zweite.
  Test: `WiesbadenReal.GIS.LandscapeImport`.

- **Nächste-Straße-Suche muss HORIZONTAL messen.** Adresspunkte haben keine
  Höhe (`GeoToUnrealGround` liefert Z = 0), Fahrspuren liegen auf Terrainhöhe.
  Am Hang sind das in Wiesbaden über 100 m, die eine 3D-Messung vollständig
  dominieren: 31 m Luftlinie wurden als 117 m gemeldet. Auf einem Hang kann
  dadurch eine weiter entfernte Straße gewinnen, die zufällig auf ähnlicher
  Höhe liegt. Test: `WiesbadenReal.Vehicles.CarSpawn`.

## Werkzeug-Fallstricke (Ergänzungen)

- **Blender 5.2 hat den COLLADA-Importer entfernt** (`bpy.ops.wm.collada_import`
  existiert nicht mehr; verfügbar sind fbx/obj/ply/stl/usd/gltf/alembic). UE5
  importiert `.dae` ebenfalls nicht. Für Assimp-exportierte DAE liegt ein eigener
  Parser bereit (ElementTree + `from_pydata`), der sich bewusst auf `<polylist>`
  mit ausschließlich Dreiecken und `offset="0"` beschränkt und bei Abweichung
  **abbricht** statt zu raten.

- **`bpy.ops.object.origin_set` wird im `--background`-Modus ohne Fehlermeldung
  als CANCELLED verworfen** (fehlender UI-Kontext). Der Pivot bleibt still
  liegen, wo er war. Vertices stattdessen direkt verschieben
  (`for v in mesh.vertices: v.co -= pivot`). Gleiche Falle: `ob.bound_box` ist
  gecacht und meldet nach direkter Vertex-Manipulation die alten Werte —
  zum Nachmessen über die Vertices iterieren.

- **Blenders FBX-Export schreibt bereits Zentimeter**, UE liest FBX ebenfalls als
  Zentimeter. Ein `import_uniform_scale = 100` in der Annahme „Blender liefert
  Meter" ergibt ein 414 m langes Auto. Nach jedem Mesh-Import die Maße ausgeben
  (`mesh.get_bounds().box_extent`) — sonst fällt es erst im Level auf.

- **Git Bash schreibt Argumente um, die mit `/` beginnen** (MSYS-Pfadkonvertierung).
  Aus dem UE-Paketpfad `/Game/Maps/WiesbadenCity_HiRes` wurde
  `C:/Program Files/Git/Game/Maps/…`, und der Editor bot an, stattdessen die
  Default-Map zu laden. Entweder `MSYS_NO_PATHCONV=1` setzen oder über
  PowerShell `Start-Process` starten (dort greift die Konvertierung nicht —
  Vorsicht aber bei `-ExecCmds=` mit Leerzeichen, siehe oben).

- **`verify-worktree-sync.mjs --sync` kopiert D: → C:** („lokal → gebaut"). Steht
  der C:-Baum weiter vorn — etwa nach Korrekturen, die noch nicht zurück nach D:
  gewandert sind — überschreibt `--sync` genau diese Korrekturen. Nur-gebaut-
  Dateien bleiben zwar erhalten, geänderte werden aber ersetzt. Vor `--sync`
  immer die `VERSCHIEDEN`-Liste prüfen und im Zweifel die Richtung umkehren
  (`--built <pfad>`).

## Stille Fehler (Fortsetzung): unsichtbarer Verkehr

Dieselbe Klasse wie oben - kein Absturz, keine Warnung, das Log meldete sogar
Erfolg. Zwei Fehler uebereinander:

- **Kein Traeger fuer die Fahrzeuge.** Im gebackenen Pfad rief
  `InitializeCity()` `TrafficSimulation.Initialize()` auf und kehrte sofort
  zurueck - `SpawnCityActor` wurde nie erreicht. Der Tick zeichnet aber nur
  `if (CityActor)`. Die Simulation lief ueber 2.708 km und 111.262 Spuren,
  ohne dass ein einziges Fahrzeug gezeichnet wurde. Behoben ueber
  `SpawnTrafficHostActor()`: ein CityActor als reiner Wirt fuer den ISM-Pool,
  bewusst OHNE `ApplyCityData` (die Geometrie liegt gebacken im Level).

- **Verkehr ueber das ganze Netz verteilt.** `SpawnLaneIds` enthielt alle
  111.262 Spuren, gespawnt wurde reihum. Selbst bei den vollen 3.000
  Fahrzeugen waere das rund EIN Auto je Kilometer gewesen - in Sichtweite
  praktisch nie eines. Dazu eine Spawn-Rate von 1 Fahrzeug/s: bis zum Maximum
  haette es 50 Minuten gedauert. Umgebaut auf spielerzentriertes Spawnen
  (`SetObserverLocation`, `SpawnRadiusMeters` 600 m, `DespawnRadiusMeters`
  900 m, `TargetVehiclesInRadius` 220). Ergebnis: 110 Fahrzeuge simuliert,
  110 gezeichnet - vorher 5 und 0.

**Lehre:** "System initialisiert" ist kein Beleg dafuer, dass es etwas tut.
Die Verkehrsbilanz in `UWiesbadenCitySubsystem::Tick` meldet daher fuenf
Sekunden nach dem Anlaufen einmalig simulierte GEGEN gezeichnete Fahrzeuge -
und bei null gezeichneten eine Warnung mit den moeglichen Ursachen statt einer
Erfolgsmeldung. Fuer jedes System, das im Hintergrund rechnet, sollte es eine
solche Kennzahl geben.

**Beim Aendern der Spawn-Logik beachten:** Ohne gesetzten Beobachter gilt
weiterhin der Vertrag `Rate = MaxSpawnRatePerSecond * Dichte` - darauf stuetzt
sich `WiesbadenReal.Traffic.SpawnRate`. Ein Auffuell-Schub darf nur im
spielerzentrierten Pfad greifen; sonst ist das Defizit gegen `MaxVehicles`
immer riesig und hebelt die Ratensteuerung aus. Genau das hat der Test
gefangen.

## Verkehr: Kollision und Ruecksicht auf den Spieler

- **Kollision ueber einen Koerper-Pool, nicht ueber die Instanzen.** Die
  Verkehrsfahrzeuge sind ein InstancedStaticMesh ohne Kollision; der Pool wird
  jeden Tick neu aufgebaut, ein Kollisionsneuaufbau je Frame waere
  unbezahlbar. 24 unsichtbare Boxen folgen den naechsten Fahrzeugen im
  60-m-Umkreis (`UTrafficVehicleSpawnerComponent::SelectNearestVehicles`).
  Untereinander ueberlappen sie (im Stau wuerden sie sich sonst verklemmen),
  ueberzaehlige werden auf `NoCollision` gesetzt statt weggeschoben - ein
  vergessener Koerper waere eine unsichtbare Wand ohne auffindbare Ursache.
  `ClearVehicles()` raeumt sie mit ab, sonst bleiben sie beim Levelwechsel
  stehen.

- **Bremsmodell: `v = sqrt(2 * a * s)`, NICHT `(Gap - MinGap) / Dt`.**
  Die zweite Formel stammt aus der Folgeabstand-Berechnung und gilt fuer einen
  FAHRENDEN Vordermann. Bei einem stehenden Hindernis und 60 Hz liefert sie
  fuer 10 m Abstand ueber 5.500 cm/s - also gar keine Bremsung; der Verkehr
  faehrt bis zum letzten Frame voll drauf zu und steht dann schlagartig. Wer
  die Hindernis-Logik anfasst, muss beim Bremswegmodell bleiben.
  Der Test prueft daher MONOTONIE ("naeher heisst nie schneller") statt
  Einzelwerte - das faengt den Fehler auch dann, wenn ein einzelner Wert
  zufaellig passt.

- **Querpruefung ist nicht optional.** Ohne den Fahrschlauch-Test (160 cm
  halbe Breite) bremst der Gegenverkehr, sobald der Spieler ihm entgegenkommt,
  und die Strasse ist sofort verstopft.

- **Hoehe wird bewusst ignoriert** (horizontal gemessen). Ein Fahrzeug auf der
  Bruecke darueber laesst den Verkehr darunter faelschlich bremsen. Das ist
  der bewusst gewaehlte Preis dafuer, dass am Hang nicht faelschlich
  IGNORIERT wird: zu frueh bremsen faellt kaum auf, zu spaet bremsen ist ein
  Unfall. Ein Testfall haelt das fest, damit es niemand als Fehler
  "korrigiert".

## Stille Fehler (Fortsetzung): Stadt ohne Material und ohne Licht

Wieder dieselbe Klasse - kein Absturz, keine Warnung, und die Geometrie-Bilanz
meldete sogar Erfolg (`1984 Chunk-Actors geladen, 12743 Strassen- und 5825
Gebaeude-Abschnitte`). Trotzdem sah die Stadt im Spiel leer aus. Erst ein
Screenshot zeigte, was Logzeilen nicht hergaben: alles rendert im
Default-Schachbrett, in einer fast schwarzen Szene.

- **Material-Slots waren nie belegt.** `RoadMaterial`, `SidewalkMaterial`,
  `BuildingWallMaterial`, `BuildingRoofMaterial` und `TerrainMaterial` am
  WorldBuilder standen alle auf `nullptr`; im ganzen Projekt existierten nur
  die Kaefer-Materialien. `UProceduralMeshComponent` rendert ohne Material
  kommentarlos mit `WorldGridMaterial`. Behoben ueber
  `EnsureDefaultMaterials()` (wird am Anfang von `BuildCity` gerufen) plus
  `Tools/build_materials.py`, das die Assets unter `/Game/Materials/City`
  anlegt.

- **Zuweisung ist NUR beim Build moeglich.** Die Chunk-Actors speichern
  ausschliesslich ihre beiden `UProceduralMeshComponent` (Header pruefen!).
  `FRoadMeshSection::Channel` und `FBuildingMeshSection::MaterialVariant`
  sind keine `UPROPERTY` und ueberleben das Backen nicht. Nach dem Build
  laesst sich also nicht mehr feststellen, welcher Abschnitt Fahrbahn und
  welcher Gehweg ist - ein Nachziehen zur Laufzeit ist unmoeglich, die Stadt
  MUSS neu gebaut werden. (Ein `UPROPERTY` auf `Channel` hilft NICHT, solange
  der Chunk die Sections nicht speichert.)

- **Fassadenvarianten liefen ins Leere.** Der BuildingGenerator gruppiert die
  Sections bereits nach `MaterialVariant` (0-5 Putz/Backstein/Sandstein/Glas/
  Beton/Fachwerk), aber `ResolveBuildingMaterial` bekam die Variante gar nicht
  uebergeben - die ganze Stadt haette eine einzige Fassade bekommen. Die
  Signatur nimmt die Variante jetzt entgegen, `FacadeVariantMaterials` ordnet
  je Index ein Material zu.

- **Sonnen-Intensitaet war ein UE4-Altwert.** In `WiesbadenWeatherFX.cpp`
  stand `MaxSunIntensity = 3.14f` mit dem Vermerk "entspricht dem UE-Default".
  Das war Pi aus dem einheitenlosen UE4-Modell; in UE5 ist die Intensitaet
  einer DirectionalLight in **Lux**, eine neu platzierte Sonne hat dort 10 lx.
  Zusammen mit der zusaetzlich abgedunkelten Lichtfarbe blieb die Stadt
  praktisch schwarz. Die Lichtakteure selbst fehlten NICHT - DirectionalLight,
  SkyLight, SkyAtmosphere und Nebel waren alle in der Map vorhanden.

- **Neue Kennzahl: Material-Bilanz.** `UWiesbadenCitySubsystem::
  LogMaterialBalance()` zaehlt Mesh-Abschnitte ohne Material. Die
  Geometrie-Bilanz allein war irrefuehrend: sie meldete korrekt tausende
  Abschnitte, waehrend keiner davon ein Material hatte. Dazu
  `RequestDiagnosticScreenshot()` hinter dem Schalter `-WbScreenshot` - Logs
  allein haetten diesen Fehler nie aufgedeckt.

### Fallstricke der UE-Python-API (bei den Materialien aufgetreten)

- `MaterialExpressionNoise.Function` ist **protected** und von Python aus
  nicht setzbar (der Simplex-Default genuegt).
- `SkyLightComponent` hat **`intensity`**, nicht `intensity_scale`.
- Mobility sitzt an der **Root-Komponente**; `Actor.set_mobility()` existiert
  nicht.
- Normalmaps in OpenGL-Konvention (`*_NormalGL`) brauchen einen Gruen-Flip im
  Material (Maskieren, `OneMinus`, `AppendVector`), sonst kippt die
  Beleuchtungsrichtung der Fassaden.
- `-game` auf der bestehenden ALKIS-Map braucht ueber **3 Minuten** allein fuer
  `GenerateStreaming` - Zeitfenster fuer automatisierte Laeufe entsprechend
  grosszuegig waehlen.

## Stille Fehler (Fortsetzung): Strassen im Boden versenkt

Der hartnaeckigste Fall der Serie. Die Stadt war beleuchtet, alle 18.568
Mesh-Abschnitte hatten ein Material, 121.721 Strassensegmente waren erzeugt -
und zwischen den Baubloecken lag trotzdem nur Wiese. Kein Fehler, keine
Warnung, alle Kennzahlen gruen.

**Ursache: `UTerrainGenerator::FlattenUnderRoads` hob das Gelaende auf die
Fahrbahnoberflaeche statt darunter.** Der Kommentar im Code behauptete "Die
Mittellinie liegt bereits auf Terrainhoehe" - das stimmt nicht. Der
RoadNetworkGenerator setzt

```cpp
Point.Z = HeightSampler->SampleHeightCm(XY) + Settings.RoadSurfaceOffsetCm;
```

Die Mittellinie traegt den 8-cm-Versatz also SCHON. Wer sie unveraendert als
Gelaendehoehe schreibt, macht Gelaende und Fahrbahndecke koplanar. Dazu kam:
`RasterizeDisc` ueberschrieb bedingungslos, weshalb am Hang der zuletzt gemalte
Mittellinienpunkt gewann - der kann einen ganzen Einebnungsradius weiter oben
liegen.

Gemessen am gebauten Stand: **26 % aller Fahrbahn-Vertices lagen UNTER dem
Gelaende** (Verdeckung im Mittel 8,9 cm, maximal 49,7 cm); die uebrigen ragten
im Schnitt nur 13,4 cm heraus. Die Landscape-Dreiecke (7,81 m/Quad)
zerschnitten die Fahrbahn dadurch permanent. Hohe Gebaeude stiessen weiter
durch - genau deshalb sah die Luftaufnahme nach "Stadt ohne Strassen" aus.

Behoben durch zwei Aenderungen in `FlattenUnderRoads`:
- `RoadFlattenSinkCm` (Default 20 cm, Groessenordnung Bordsteinhoehe) wird von
  der Mittellinienhoehe abgezogen.
- Zielhoehen werden erst je Zelle als **Minimum** aller ueberdeckenden
  Strassenpunkte gesammelt und dann geschrieben (`ForEachDiscCell`), statt
  einander zu ueberschreiben.

Test `GIS.TerrainGenerator.RoadFlattenClearance` faehrt bewusst eine steigende
Strasse (dort schlagen beide Fehler zu) und verlangt, dass KEIN
Mittellinienpunkt unter dem Gelaende landet.

### Diagnose-Werkzeuge, die dafuer noetig waren

Logzeilen allein haetten diesen Fehler nie aufgedeckt - sie meldeten
durchgehend Erfolg. Neu im `UWiesbadenCitySubsystem`, alle hinter
`-WbScreenshot`:

- `RequestDiagnosticScreenshot()` - Bild nach `Saved/Diagnose`, danach beendet
  sich der Lauf selbst. Zusaetzlich `-WbAerial=<Meter>` fuer eine Kamera ueber
  dem Spieler: aus der Fahrerkamera ist "Strassennetz fehlt" nicht zu erkennen.
- `LogMaterialBalance()` - zaehlt Mesh-Abschnitte ohne Material.
- `LogHeightStackNearPlayer()` - Vertexzahl, Z-Bereich und Verdeckungsstatistik
  von Fahrbahn und Gebaeuden gegen die getroffene Gelaendehoehe.

**Fallstrick bei der Messung:** Die Chunk-Actors stehen ALLE im Weltursprung
(`SpawnActor(..., FTransform::Identity, ...)`), ihre Vertices tragen absolute
Weltkoordinaten. `GetActorLocation()` ist als Entfernungsmass damit wertlos -
es misst nur den Abstand zum Ursprung. Es zaehlen ausschliesslich die
Mesh-Bounds bzw. die Vertices selbst.

### Weitere Fallstricke dieser Sitzung

- `build_alkis.py` startet mit `new_level()`. Ein frisches Level hat KEINE
  Lichtakteure - die fertige Stadt rendert dann komplett schwarz. Deshalb legt
  `AWiesbadenWorldBuilder::EnsureLightingActors()` Sonne, Himmelslicht,
  Atmosphaere und Nebel jetzt selbst an (nicht raeumlich geladen, sonst
  streamt World Partition die Sonne weg). `Tools/ensure_lighting.py` macht
  dasselbe fuer bereits gebackene Maps.
- In UE 5.8 gibt es **kein** `bUsedWithLandscape` mehr (in `Material.h`
  nachgeprueft); `bUsedWithInstancedStaticMeshes` dagegen schon und es ist
  Pflicht.
- Testwerte NICHT aus der Implementierung abschreiben: `Weather.FXParams`
  wiederholte `3.14f` woertlich und lief bei der Lux-Umstellung auf. Der Wert
  kommt jetzt per `FWiesbadenWeatherFXParams::GetMaxSunIntensityLux()`.

## Nachtrag: die eigentliche Ursache der unsichtbaren Strassen

Der oben beschriebene Einebnungs-Fehler war echt und ist behoben - aber er war
NICHT der Grund, warum die Strassen unsichtbar blieben. Nach seiner Korrektur
sank die Verdeckung von 26 % auf 5 % und die Fahrbahn war trotzdem weiterhin
nicht zu sehen. Wer hier aufhoert, haelt eine halbe Korrektur fuer die Loesung.

**Die eigentliche Ursache: `RoadSurfaceOffsetCm = 8.0` ist zu klein fuer die
Aufloesung des Landscape.** Das Landscape hat 7,81 m je Quad und kann eine
6,5 m breite Fahrbahn-Rinne gar nicht abbilden; zwischen zwei Gitterpunkten
interpoliert es linear ueber die Fahrbahn hinweg. Auch ein perfekt eingeebneter
Korridor hilft nicht, wenn der Einebnungs-Kreis (Radius frueher nur 4,75 m)
streckenweise gar keinen Gitterpunkt trifft.

Gemessen an der Platter Strasse:

| Element | Z | sichtbar |
|---|---|---|
| Gelaende | 11300 | - |
| Fahrbahn (+8 cm) | 11308 | **nein** |
| Bordstein (+15 cm) | 11315 | **ja** |

Die Sichtbarkeitsschwelle lag also zwischen 8 und 15 cm. Geaendert:
`RoadSurfaceOffsetCm` 8 -> 30, `RoadFlattenMarginCm` 150 -> 400 (der Korridor
muss gross gegen die GITTERWEITE sein, nicht gegen die Fahrbahnbreite),
`RoadFlattenSinkCm` 20 -> 45. Der Test verlangt jetzt mindestens 20 cm Abstand
statt nur "nicht vergraben".

### Was bei der Diagnose Zeit gekostet hat

- **Farben im Bild interpretieren statt messen.** Gedeckte Gruen- und
  Grautoene liessen Gelaende, Fahrbahn und Fassade verwechselbar aussehen; aus
  dem Bild wurden mehrfach falsche Schluesse gezogen. Erst
  `Tools/debug_colorize.py` (jedes Material eine gesaettigte Leuchtfarbe,
  in place geaendert, damit die Chunk-Referenzen erhalten bleiben) machte in
  EINEM Screenshot sichtbar, was gezeichnet wird und was fehlt. Bei
  Sichtbarkeitsfragen gehoert dieses Werkzeug an den ANFANG, nicht ans Ende.
- **Datenpruefungen koennen alle gruen sein, waehrend nichts zu sehen ist.**
  Der Reihe nach geprueft und jeweils fuer unauffaellig befunden:
  Abschnittszahl, Vertexzahl, leere Abschnitte (0), Material-Zuweisung
  (6056 Abschnitte / 2,33 Mio Dreiecke auf M_WbRoad), `bSectionVisible` (0
  unsichtbar), versteckte Actors (0), Normalenrichtung, Dreiecks-Wicklung
  gegen die korrekt rendernden Gebaeude, Kantenlaengen (4,4 m im Mittel),
  entartete Dreiecke (0). Keine dieser Kennzahlen konnte den Fehler zeigen,
  weil er in der HOEHENDIFFERENZ zu einer anderen Oberflaeche lag.
- **`GetActorLocation()` der Chunks ist als Entfernungsmass unbrauchbar** (alle
  im Ursprung) - das hat eine ganze Messrunde entwertet.
- **Der Hoehen-Trace misst Kollision, nicht das Bild.** Procedural-Meshes haben
  wegen `bCreateCollision=False` gar keine Kollision. Ein Trace kann also nur
  das Landscape finden und beantwortet die Frage "was verdeckt hier was?" nur
  halb.

## Die tatsaechliche Ursache: falsche Dreiecks-Wicklung im Fahrbahn-Band

Auch der Nachtrag oben war noch nicht die Loesung. Nach Anheben der Fahrbahn
auf 30 cm sank die Verdeckung auf 1 % und die mittlere Bodenfreiheit stieg auf
57,7 cm - die Strasse blieb trotzdem unsichtbar. Erst der Test mit einem
BEIDSEITIGEN Fahrbahnmaterial liess sie schlagartig erscheinen.

**`FPolygonUtils::BuildRibbonMesh` wickelte die Dreiecke falsch herum.** Die
Vorderseite zeigte nach unten, Backface-Culling entfernte die Flaeche von oben.
Betroffen waren alle drei Nutzer der Funktion: Fahrbahn, Gehweg und
Fahrbahnmarkierungen. Bordsteine blieben sichtbar, weil sie als senkrechte
Flaechen entstehen - daher war im Bild eine Bordsteinlinie zu sehen, aber keine
Fahrbahn.

Der Fehler war deshalb so zaeh, weil JEDE datenseitige Pruefung sauber war:
Abschnittszahl, Vertexzahl, leere Abschnitte (0), Materialzuweisung (6056
Abschnitte / 2,33 Mio Dreiecke), `bSectionVisible` (0 unsichtbar), versteckte
Actors (0), Komponenten-Bounds (decken die Geometrie ab), Kantenlaengen (4,4 m
im Mittel), entartete Dreiecke (0) - und sogar die Vertex-Normalen: die stehen
fest auf `FVector::UpVector` und sehen damit vollkommen korrekt aus. **Fuer das
Culling zaehlt aber ausschliesslich die Reihenfolge der Indizes.**

### Handedness: die Falle, die eine Diagnoserunde verschenkt hat

Eine Zwischenpruefung verglich die geometrische Normale der Fahrbahn mit der
waagerechter Gebaeudeflaechen und meldete "gleiches Vorzeichen, Wicklung in
Ordnung". Zwei Fehler steckten darin:

1. Die Stichprobe traf Gebaeude-BODENflaechen, die selbst nach unten zeigen -
   verglichen wurde also falsch gegen falsch.
2. Als Formel diente `CrossProduct(B - A, C - A)`. **Unreal arbeitet
   linkshaendig; die Vorderseite entspricht `CrossProduct(C - A, B - A)`.**

Empirisch belegt (nicht angenommen): die alte Wicklung `(0,2,1)`/`(1,2,3)`
ergab nach `CrossProduct(B - A, C - A)` ein +Z - und war nachweislich von oben
weggecullt. Richtig ist `(0,1,2)`/`(1,3,2)`. Beachten: `GetLeftNormal` liefert
`(Direction.Y, -Direction.X)`, fuer eine Achse in +X also **-Y**; wer hier +Y
annimmt, dreht die Wicklung erneut falsch (genau das passierte beim ersten
Testentwurf).

Test `GIS.PolygonUtils.RibbonWinding` haelt das fest.

### Werkzeug der Wahl bei Sichtbarkeitsfragen

`Tools/debug_road_twosided.py` (Material beidseitig schalten) beantwortet die
Frage "wird die Flaeche gezeichnet oder weggecullt?" in EINEM Lauf und haette
den Fehler sofort gezeigt. Zusammen mit `Tools/debug_colorize.py` (jedes
Material eine Leuchtfarbe) gehoert es an den ANFANG einer Sichtbarkeitssuche -
beide aendern die Materialien IN PLACE, die gebackenen Chunk-Referenzen bleiben
erhalten, ein Neubau entfaellt.

### Auch die Daecher waren falsch gewickelt (Waende dagegen nicht)

Nach der Korrektur im Fahrbahn-Band waren die Strassen sichtbar, die Gebaeude
aber hohle Schalen ohne Dachflaeche. Derselbe Fehler an anderer Stelle.

Eingegrenzt wurde er, indem NUR `M_WbBuildingRoof` beidseitig geschaltet wurde:
das Bild wurde daraufhin praktisch deckungsgleich mit dem, in dem ALLE
Materialien beidseitig waren. Damit stand fest, dass die Waende korrekt sind
und ausschliesslich die Daecher gedreht werden muessen - ohne diesen Test haette
man beim pauschalen Umdrehen die funktionierenden Waende zerstoert.

Betroffen waren drei Stellen im `UBuildingGenerator`:
- Flachdach (Ohrenschnitt-Indizes) - Wicklung je Dreieck umgekehrt,
- Dach-Faecher ueber den Schwerpunkt (`TriangleBase`),
- Dachflaeche zwischen Traufe und First (`Base`).

Die Wand-Wicklung bei `Section.Triangles.Add(Base + 0/2/1)` (3 Tabs
Einrueckung, innerhalb der Lambda) bleibt bewusst UNVERAENDERT - sie sieht dem
Dach-Muster zum Verwechseln aehnlich und unterscheidet sich nur durch die
Einrueckung und die Vertexreihenfolge.

**Merksatz:** Gleiches Indexmuster heisst NICHT gleiche Orientierung - die
haengt an der Reihenfolge, in der die Vertices vorher abgelegt wurden. Deshalb
laesst sich Wicklung nur pro Erzeugungspfad beurteilen, nie global.

## Fassaden, HUD und Fussgaenger

### Fenster kommen aus dem Shader, nicht aus Geometrie

Die Gebaeude waren schmucklose Extrusionen. Ausmodellierte Fensterlaibungen
haetten bei 104.458 Gebaeuden die Dreieckszahl vervielfacht - stattdessen
erzeugt `add_facade_windows` (Tools/build_materials.py) Fenster, Gesimsband und
Erdgeschoss rein im Material.

Moeglich wird das durch die UV-Belegung des BuildingGenerators, die genau dafuer
gemacht ist: **U laeuft in METERN entlang der Wand, V in GESCHOSSEN**. `frac(V)`
ist damit die Position im Geschoss, `U / Achsabstand` die Position in der
Fensterachse. Kein zusaetzliches Dreieck - und weil nur Materialien betroffen
sind, ist KEIN Stadt-Neubau noetig.

Die Fensterkanten entstehen ohne If-Knoten:
`saturate((v - low) * k) * saturate((high - v) * k)`. Das ist billiger und
liefert eine leicht weiche Kante, die in der Ferne nicht flimmert.

### Materiallauf ohne Neubau: das Landscape verliert seine Referenz

`new_material()` loescht das Asset und legt es neu an. Die Procedural-Meshes der
Chunks finden es danach ueber den Pfad wieder - **das Landscape nicht**: seine
`landscape_material`-Referenz zeigt ins Leere und das gesamte Gelaende wird zum
Schachbrett. Vorher fiel das nie auf, weil nach jedem Materiallauf ohnehin ein
Neubau folgte. `reassign_landscape_material()` verknuepft es jetzt neu.

### Zwei Kopien derselben Lichtlogik

`build_materials.py` hatte eine eigene `ensure_lighting()` mit Himmelslicht 1.0
und lief zuletzt - damit hat es die in `Tools/ensure_lighting.py` gepflegten
Werte stillschweigend ueberschrieben. Dieselbe Doppelpflege wie beim Sonnenwert
im Test. Die Zustaendigkeit liegt jetzt AUSSCHLIESSLICH bei
`Tools/ensure_lighting.py`; `build_materials.py` fasst Licht nicht mehr an.

### Himmelslicht 6.0 statt 1.0

In einer Strassenschlucht liegen fast immer BEIDE sichtbaren Fassadenseiten im
Schatten und werden nur vom Himmelslicht aufgehellt. Mit dem UE-Default 1.0 und
einer Belichtung, die auf die sonnenbeschienene Flaeche eingestellt ist, liefen
sie gegen Schwarz. Der Blaustich der Schattenseiten ist dabei KEIN Fehler,
sondern Himmelslicht.

### HUD ohne Asset

`AWiesbadenVehicleHUD` zeichnet Tacho, Drehzahlband, Gang und Kontrollleuchten
rein per Canvas - bewusst kein UMG-Widget: so braucht es kein Asset, keine
Editor-Handarbeit, funktioniert sofort im gebackenen Spiel und laesst sich in
den automatisierten Screenshot-Laeufen mitpruefen. Registriert wird es im
GameMode-Konstruktor (`HUDClass`).

Die Rechenteile sind statisch und datenrein (Test `Vehicles.HUD`). Wichtig
dabei: der Drehzahlbalken bezieht sich auf die LEERLAUF-Drehzahl, nicht auf
null - sonst zeigt ein stehender Motor bereits ein Achtel Ausschlag.

### Fussgaenger

`FWiesbadenPedestrianSimulation` spiegelt bewusst den Aufbau der
Verkehrssimulation: datenreine statische Funktionen fuer alles Rechenbare,
spielerzentriertes Spawnen, Zeichnung ueber einen ISM-Pool am CityActor
(`UPedestrianSpawnerComponent`). Im Bestand: 29.749 Segmente mit Gehweg,
2.620 km Gehweg.

**Bewusste Grenze:** Es gibt KEINEN Fussgaengergraphen. Die Figuren laufen an
den Gehwegen der vorhandenen Strassensegmente entlang und kehren am Segmentende
um; Querungen und Knotenlogik fehlen. Ein echter Graph waere ein eigenes
Vorhaben - aus Fahrersicht ist der Unterschied auf dem Gehweg kaum auszumachen.
Die Figuren sind skalierte Engine-Zylinder: sichtbar ein Platzhalter, weil eine
Figur mit Schrittanimation ein Skelettmesh braucht, das das Projekt nicht hat.

Der Test `World.PedestrianSimulation` prueft vor allem, dass der seitliche
Versatz IMMER hinter der Bordsteinkante liegt - ein Vorzeichenfehler dort setzt
die Fussgaenger auf die Fahrbahn und faellt im Spiel kaum auf.

## Gebaeude-Kollision: der Spieler fuhr durch die Haeuser

Die Stadt-Meshes werden mit `bCreateCollision = false` gebacken -
Dreieckskollision fuer 5.825 Gebaeude-Abschnitte waere in Cook-Zeit wie
Speicher unbezahlbar. Die Folge fiel lange nicht auf, weil sie nur beim
tatsaechlichen Fahren stoert: `AWiesbadenCar` bewegt sich per
`AddActorWorldOffset(..., /*bSweep=*/true)`, sucht also durchaus Kollision -
es war nur keine da. Der Sweep fand ausschliesslich das Landscape, weshalb das
Fahrzeug korrekt dem Gelaende folgte und trotzdem durch jede Wand fuhr.

Geloest wie beim Verkehr, ueber einen Pool statt ueber Geometrie:
`UBuildingCollisionSpawnerComponent` haelt 96 `UBoxComponent` und setzt sie auf
die naechstgelegenen Gebaeude im Umkreis von 150 m. Die Daten dafuer liegen
bereits in der gebackenen Map - `FGeneratedBuilding::Bounds` ist ein
nicht-transientes UPROPERTY am WorldBuilder, und der Kommentar an der Struktur
nennt genau diesen Zweck ("wo sie gebraucht wird ... ueber Bounds und
SourceId"). Kein Neubau noetig, kein Cooking.

**Bewusste Naeherung:** Die Box ist achsparallel. Bei einem gedrehten Gebaeude
deckt sie etwas mehr ab als der Grundriss - das Fahrzeug haelt dann wenige
Dezimeter vor der Wand statt an ihr. Dem Hindurchfahren klar vorzuziehen, und
`FGeneratedBuilding` fuehrt den Umriss gar nicht mit (bewusst, siehe Kommentar
dort); eine gedrehte Box braeuchte erst einen Rebuild mit zusaetzlichem Feld.

**Fallstrick:** Ueberzaehlige Koerper MUESSEN abgeschaltet werden, wenn weniger
Gebaeude in Reichweite sind als der Pool gross ist. Ein vergessener Koerper
bleibt sonst als unsichtbare Wand im Gelaende stehen - der Klassiker bei
Pool-Recycling. Test `World.BuildingCollisionSelection` deckt die Auswahl ab
(horizontale Messung, ungueltige Grundrisse uebersprungen).

### Nachtrag: `SetIsSpatiallyLoaded` reisst den Build ab

`EnsureLightingActors()` markiert die Lichtakteure als nicht raeumlich geladen -
eine gestreamte Sonne waere je nach Spielerposition an oder aus. Beim
spaeter ergaenzten `APostProcessVolume` schlaegt das fehl:

```
Assertion failed: CanChangeIsSpatiallyLoadedFlag()
  AWiesbadenWorldBuilder::EnsureLightingActors()
  AWiesbadenWorldBuilder::BuildCity()
```

**Volumes sind Brushes und verbieten die Aenderung.** Die Assertion reisst den
kompletten Stadt-Build mit. Deshalb geht jeder Aufruf jetzt ueber
`MarkAlwaysLoaded()`, das vorher `CanChangeIsSpatiallyLoadedFlag()` prueft.

**Wie das durchrutschen konnte - und was daraus folgt:** Der abgestuerzte Build
lief im Hintergrund, und die PowerShell-Huelle meldete trotzdem "fertig", weil
sie den Exitcode nicht auswertete. Der Fehler fiel erst zwei Messungen spaeter
auf, als eine Kennzahl "0 Gebaeude" meldete und der **Zeitstempel der .umap**
zeigte, dass die Map seit Stunden unveraendert war.

Fuer automatisierte Builds gilt daher:

- `$LASTEXITCODE` auswerten, nicht auf die Ausgabe vertrauen.
- Den **Zeitstempel der erzeugten Map** vorher/nachher vergleichen - das ist der
  einzige Beleg, dass wirklich gebaut wurde.
- Bei "die Aenderung wirkt nicht": ZUERST pruefen, ob der Build ueberhaupt lief,
  bevor die Logik verdaechtigt wird. Ein Absturz im Hintergrund sieht in den
  Kennzahlen exakt aus wie ein wirkungsloser Codepfad.

## Overpass liefert Knoten DOPPELT - und das kostete alle Knoten-Tags

Die Stadt hatte keine einzige Ampel: `Netzstatistik: 20213 Kreuzungen
(0 Ampeln, 344 Kreisverkehre)` - obwohl die OSM-Quelle 2.310 Ampelknoten
enthaelt und die Overpass-Abfrage sie ausdruecklich anfordert.

**Ursache: `OutDataSet.Nodes.Add(Id, ...)` ueberschrieb getaggte Knoten.**
Overpass gibt denselben Knoten mehrfach aus - einmal aus der Knoten-Abfrage MIT
Tags, danach nochmals aus der Way-Rekursion OHNE. An der Wiesbaden-Datei
nachgezaehlt: **1.125.032 Knoten-Eintraege fuer 1.115.437 eindeutige Knoten**,
also 9.595 Duplikate. Der Ampelknoten 529593 kommt exakt zweimal vor.

Wirkung: Von 2.310 Ampelknoten kamen **drei** im Datensatz an. Betroffen waren
ALLE Knoten-Tags - Zebrastreifen, Stopp, Vorfahrt, Haltestellen,
Strassenlaternen. Damit fiel unbemerkt die gesamte Kreuzungssteuerung aus.

Behoben ueber `AddOrMergeNode()` (JSON- UND XML-Pfad): vorhandene Tags bleiben,
Tags beider Vorkommen werden vereint. Danach: **1.123 Ampelknoten** im
Datensatz, 128 davon auf Knoten mit mindestens zwei Strassen. Test
`GIS.OSMDataParser.DuplicateNodeTags` prueft beide Reihenfolgen und die
Vereinigung.

### Wie die Ursache eingekreist wurde

Wichtig war, Daten- und Codefrage sauber zu TRENNEN - sonst sucht man im
falschen Modul:

1. `Tools/check_traffic_signals.mjs` streamt die Rohdatei und zaehlt: 2.310
   Ampelknoten, 1.299 von >= 2 Strassen geteilt, 130 als Endpunkt von >= 3
   Strassen. **Die Daten waren also in Ordnung.**
2. Test `GIS.RoadNetwork.TrafficSignalControl` baut den Fall synthetisch nach
   und BESTEHT. **Der Generator war also auch in Ordnung.**
3. Test `GIS.RoadNetwork.RealOsmTrafficSignals` parst die ECHTE Datei und
   meldete "Ampelknoten im Datensatz: 3". Damit war der Parser ueberfuehrt.

Ohne Schritt 2 haette man den Kreuzungs-Code verdaechtigt, ohne Schritt 1 die
Datenquelle. Der Zaehler in Schritt 3 zeigte auf die eine Zeile.

### Und wieder: implementiert heisst nicht verbunden

Das Ampelsystem (`FWiesbadenTrafficLightSystem`) war vollstaendig, datenrein
und getestet; die Verkehrs-Simulation hielt einen Zeiger darauf bereit
(`SetTrafficLightSystem`) samt fertiger Haltelogik. **Nur rief die Methode
niemand auf** - der Zeiger blieb nullptr, und dann gilt jede Verbindung als
gruen. Dasselbe Muster wie beim unsichtbaren Verkehr und den unsichtbaren
Strassen.

Deshalb zaehlt `FWiesbadenTrafficSimulation::GetVehiclesHeldAtRed()` jetzt
mit, wie viele Fahrzeuge tatsaechlich an Rot stehen. "Das System laeuft"
beweist hier gar nichts.

**Merke:** Das Ampelsystem muss ein MEMBER des Subsystems sein - die Simulation
speichert nur einen Zeiger. Ein lokales System waere nach der Initialisierung
zerstoert.

## Die Messung selbst kann der Fehler sein

Fuenf Eingriffe hintereinander (Trim-Analyse, Wege-Einebnung, Korridor von 400
auf 900 cm, kachelbasierter Height-Sampler, vertauschte Einebnungsreihenfolge)
haben eine Kennzahl nicht bewegt: "31 % der Fahrbahn-Vertices liegen unter dem
Gelaende". Nach dem letzten Eingriff war das Ergebnis auf die Nachkommastelle
identisch mit dem davor - dieselbe schlimmste Position, dieselben Werte.

Diese Unbeweglichkeit war der eigentliche Befund. Sie hiess nicht "der Eingriff
hat nicht gewirkt", sondern "die Zahl haengt gar nicht an dem, was ich aendere".

Die Verdeckungsstatistik mass so: Abwaertstrace am Fahrbahn-Vertex gegen
`ECC_WorldStatic`, oberster Treffer = "Gelaendehoehe". Seit die Fahrbahnen
eigene Kollision haben (`bCreateRoadCollision`), ist der oberste Treffer an
einem Fahrbahn-Vertex aber die FAHRBAHN. Die Gegenprobe zaehlt es aus:

    Trace-Gegenprobe an denselben Vertices:
    89 x Fahrbahn, 45 x Gelaende, 0 x anderes, 0 x kein Treffer.

Gegen die echten Landscape-Daten gemessen (`GetHeightAtLocation(Complex)`):

    134 Fahrbahn-Vertices geprueft - 0 unter dem Gelaende (0 %),
    frei liegende im Mittel 91,6 cm ueber Grund.

Es gab nie eine Verdeckung. Es gab das Gegenteil: die Fahrbahn schwebte fast
einen Meter ueber dem Boden und riss an jeder Kante sichtbar auf - genau das,
was im Spielvideo als "zerissene Strassen" zu sehen war. Und die Gegenmassnahmen
gegen die Phantom-Verdeckung (Fahrbahnversatz 30 cm, Gelaende-Aushub 45 cm,
MINIMUM aller ueberdeckenden Strassenpunkte ueber einen 9-m-Radius) hatten diese
Luecke Schritt fuer Schritt selbst aufgerissen.

Daraus drei Regeln:

1. **Eine Kennzahl, die sich ueber mehrere wirksame Eingriffe nicht bewegt,
   misst nicht das, was sie behauptet.** Bevor der sechste Eingriff kommt, die
   Messmethode gegenpruefen.
2. **Traces messen die Welt, nicht eine Ebene davon.** Wer Gelaendehoehe meint,
   fragt die Landscape-Daten (`ALandscapeProxy::GetHeightAtLocation`), nicht den
   obersten Kollisionstreffer. Sobald eine zweite Geometrie Kollision bekommt,
   kippt jede trace-basierte Hoehenmessung stillschweigend.
3. **Schwellwerte brauchen zwei Seiten.** Der Test `RoadFlattenClearance`
   verlangte nur eine Mindesthoehe. Er blieb gruen, waehrend die Bodenfreiheit
   von 8 auf 91,6 cm wuchs. Eine einseitige Schranke deckt die Haelfte der
   moeglichen Fehler ab und suggeriert dabei volle Abdeckung.

## "Abgesetzt" ist nicht "vorhanden"

Der Helikopter meldete in jedem Lauf `Helikopter abgesetzt: 12 m neben dem
Fahrzeug bei (-122108, -120494, 11345)`. Er war trotzdem nie zu finden.

`ApplyGroundConstraint` hob ihn nur an, WENN der Abwaertstrace etwas traf. Er
entsteht aber in Frame 0, bevor World Partition Gelaende und Strassen gestreamt
hat: Trace ins Leere, Schwerkraft laeuft weiter, und als der Boden Sekunden
spaeter erschien, lag er bereits darunter. Von dort trifft ein Abwaertstrace
erst recht nichts mehr.

Kein Treffer heisst **Boden unbekannt**, nicht **Boden abwesend**. Beim Streamen
darf daraus kein freier Fall folgen. Der Konstraint traced jetzt zusaetzlich
nach OBEN (steckt der Helikopter unter der Welt, wird er auf die Oberflaeche
zurueckgesetzt) und klemmt die Sinkgeschwindigkeit, solange in keiner Richtung
Geometrie gefunden wird.

Allgemein: Eine Spawn-Meldung belegt das Absetzen, nicht den Verbleib. Wo ein
Objekt gefunden werden SOLL, gehoert sein Zustand in die Diagnose - hier seine
Hoehe ueber Grund und die Entfernung zum Spieler.

## Kollisionskoerper in echten Massen

Das Spielerfahrzeug trug eine Kugel mit 220 cm Radius als Kollisionskoerper -
4,4 m Durchmesser fuer einen VW Kaefer von 4,08 m Laenge und 1,55 m Breite. Es
stiess damit rund anderthalb Meter vor jeder Wand an und rollte an Kanten auf
der Kugelrundung auf. Ersetzt durch eine Box mit den tatsaechlichen Massen
(204/78/75 cm Halbmasse).

Ebenso fehlte die Bodenausrichtung vollstaendig: gedreht wurde nur um die
Welt-Z-Achse. Der Wagen blieb an jeder Steigung waagerecht, fuhr deshalb
waagerecht in den Anstieg hinein und wurde anschliessend von der Hoehenkorrektur
ruckartig nachgezogen. Die Karosserie legt sich jetzt per
`FRotationMatrix::MakeFromZX(Flaechennormale, Fahrtrichtung)` an den Untergrund;
damit zeigt die Vorwaertsachse die Steigung hinauf und die Bewegung folgt der
Strasse.

## Der Kommentar, der eine Annahme fuer ein Ergebnis ausgab

Im Strassennetz stand an der Stelle, an der ein Segment verworfen wird:

    // Segment ist kuerzer als die Kreuzungsflaechen an seinen Enden. Die
    // Kreuzungsflaechen schliessen die Luecke; ein Fahrbahnstueck wird dort
    // nicht erzeugt.

Der zweite Satz ist keine Beobachtung, sondern eine Hoffnung. Jede
Kreuzungsflaeche ist die konvexe Huelle IHRER eigenen Arme und reicht genau so
weit wie IHRE Kuerzung. Liegen zwei Kreuzungen 30 m auseinander und kuerzt jede
12 m, decken beide zusammen 24 m ab - dazwischen bleiben 6 m blanker Boden.

Im Spiel sah das aus wie eine abgerissene Strasse: Fahrbahn UND beide Gehwege
enden in einer sauberen Querkante, dahinter Wiese. Der Nutzer hat es ueber vier
Aufnahmen hinweg gemeldet ("straßen schlimm", "immer noch straßenschäden"),
waehrend die Suche bei Hoehen und Materialien lag.

Zwei Dinge haben das Auffinden verzoegert:

1. Der Fall wurde nur auf `Verbose` protokolliert und war damit in keinem
   normalen Lauf zu sehen. Im echten Wiesbaden trifft er 9.544 Segmente.
2. Der Kommentar las sich wie ein Befund. Wer ihn liest, hakt die Stelle ab.

**Regel: Ein Kommentar, der eine Wirkung behauptet, braucht eine Messung oder
einen Test - sonst gehoert der Konjunktiv hinein.** Wo Geometrie
stillschweigend entfaellt, gehoert der Zaehler auf `Log`, nicht auf `Verbose`.

Behoben wird nicht mehr durch Verwerfen, sondern durch anteiliges Zuruecknehmen
beider Kuerzungen, bis ein Reststueck bleibt. Die Fahrbahn ragt dann etwas in
die Kreuzung hinein - gleiches Material, gleiche Hoehe, im Bild nicht zu
unterscheiden. **Eine Ueberlappung ist harmlos, eine Luecke nicht.** Test:
`GIS.RoadNetworkGenerator.ShortSegmentSurvivesTrim`, mit Gegenprobe, dass lange
Arme weiterhin gekuerzt werden.

## Bounding-Box-Mitte ist nicht der Drehpunkt

Beim Einbau der Ka-52-Modelle lagen die Bounds-Mittelpunkte der Rotoren nicht
im Ursprung (oberer Rotor y = 26,6 cm). Der erste Reflex war, den Versatz
herauszurechnen - genau falsch: Ein DREIBLATTROTOR ist um seine Drehachse nicht
bounding-box-symmetrisch. Eine Zentrierung auf die Box haette ihn von der Achse
weggeschoben, auf der er kreisen soll.

Ebenso wurde die Nasenrichtung nicht geraten, sondern an den Vertices
ausgezaehlt: Y-Spanne -60,0 bis +40,6 cm (Pivot am Rotormast, nicht mittig), im
aeusseren Fuenftel des langen Endes ein Querschnitt von 21,4 x 32,9 cm gegen
13,1 x 28,5 cm am kurzen. Hinten Leitwerk und Hoehenflosse, vorn die schmale
Kanzel - Nase = +Y.

## Was am Licht NICHT geholfen hat

Zum milchigen Gesamteindruck wurden drei Aenderungen versucht und je am Bild
geprueft:

- Himmelslicht 6,0 -> 2,0 -> 3,5 (gegen Sonne 10,0): WIRKT sichtbar.
- Gelaende-Albedo von 0,035/0,060/0,022 auf 0,085/0,135/0,050 (echtes Gras
  liegt bei 15 bis 25 Prozent, die Werte waren viermal zu dunkel): physikalisch
  richtig, am Bild KEIN Unterschied - die Auto-Belichtung gleicht es aus.
- Lumen ausdruecklich eingeschaltet: KEIN Unterschied, war offenbar schon aktiv.

Die dunklen Schattenfassaden sind damit weiterhin unerklaert. Die Putztextur
ist mit gemessen 0,382 linearer Albedo hell, liegt also nicht daran. Wichtig:
Das steht so auch in den Kommentaren - keine der drei Stellen behauptet eine
Wirkung, die nicht am Bild nachgewiesen ist.

## Transient heisst: ueberlebt das Speichern NICHT

In der gebackenen Stadt stand kein einziges Schild, kein Leitpfosten, keine
Markierung und keine Laterne. Das Build-Log meldete trotzdem jedes Mal:

    Ausstattungs-Spawner: 50875 Schilder, 177812 Leitpfosten, 21 Markierungen.

Diese Zeile beschreibt die EDITOR-Sitzung, in der gebaut wurde - nicht die
gespeicherte Karte. Die ISM-Komponenten des Spawners sind
`UPROPERTY(Transient)`; damit wird die Komponente nicht serialisiert, und ihre
Instanzdaten sind beim Laden weg. Gemessen in der fertigen Map:

    Laternen beim Start: 0 Standorte geladen, 0 Mast-Instanzen.

Daneben stand im Code die Begruendung:

    // Beim Bauen erzeugt, werden die Instanzen mit der Map gespeichert -
    // genau wie die Strassenausstattung.

Dieselbe Annahme traegt die 1,53 Millionen Baeume (`TreeInstances` ist ebenfalls
Transient, und `RegionAssetLayout` ist nicht einmal ein Member). Die Referenz
"genau wie die Strassenausstattung" war also selbst schon falsch - ein Irrtum,
der sich fortgepflanzt hat.

Die LAYOUT-Daten lagen dagegen vollstaendig vor; aus der Map ausgelesen:
50.875 Schilder, 177.812 Leitpfosten, 3.332 Laternen, 121.721 Segmente. Es
fehlte allein der Aufruf, der daraus wieder Instanzen macht. Der WorldBuilder
hat dafuer jetzt ein `BeginPlay` (Aufbau von 300.000 Instanzen: rund 0,5 s).

Regeln daraus:

1. **Ein Zaehler aus dem Build-Log beweist nichts ueber die gebackene Karte.**
   Was zaehlt, ist eine Messung im geladenen Spiel.
2. **Transient und "wird mit der Map gespeichert" schliessen sich aus.** Wo ein
   Kommentar das Gegenteil behauptet, gehoert der Marker geprueft.
3. Serialisiert werden die DATEN (Layout), aufgebaut werden die INSTANZEN beim
   Start. Das spart Platz und ist die Absicht hinter Transient - sie muss nur
   auch ausgefuehrt werden.

## Wenn die Daten fehlen, nicht der Code

Nach dem Aufbau der Laternen war es nachts weiter dunkel. Die Messung:

    Laternen: naechste 915 m entfernt, 48 Leuchten aktiv von 3332 Standorten.

OSM kennt fuer ganz Wiesbaden 3.332 Leuchten - ein Bruchteil des Bestands.
Ohne diese Zahl liessen sich drei voellig verschiedene Ursachen nicht
unterscheiden, die sich im Bild gleich anfuehlen: keine Laternen in den Daten,
Standorte beim Backen verloren, oder Leuchten am falschen Ort. Alle drei sind
in dieser Sitzung tatsaechlich aufgetreten.

Deutsche Ortsstrassen sind nach DIN 13201 durchgaengig beleuchtet; fehlende
Leuchten werden daher ergaenzt (alle 30 m, Seiten wechselnd, hinter dem
Bordstein) - aber NUR, wo keine kartierte in der Naehe steht. 67.542 ergaenzt,
70.874 gesamt, naechste Leuchte danach 10 m statt 915 m.

## Die Kreuzungsflaeche traf die Fahrbahnenden nicht

Der Nutzer meldete ueber mehrere Aufnahmen hinweg "Verbindungsprobleme" an den
Strassen. Drei Anlaeufe zuvor hatten Hoehen (Kreuzungsnaht) und Laengen
(Kuerzungs-Luecken) korrigiert - beides echte Fehler, aber nicht dieser.

Gemessen wurde erst beim vierten Anlauf das Naheliegende: Liegt das Ende jedes
Fahrbahnbands ueberhaupt INNERHALB der Kreuzungsflaeche?

    4754 Bandenden geprueft:
      3480 in der Flaeche
      1274 AUSSERHALB (27 %), groesste Luecke 523 cm

Die Flaeche entstand aus `Mitte + Auswaertsrichtung * Kuerzungslaenge` - also
unter der Annahme, jede Strasse verlasse die Kreuzung GERADLINIG entlang ihrer
Anfangstangente. Bei gekruemmten Zufahrten endet das Band seitlich versetzt.

Der Umriss wird jetzt aus den TATSAECHLICHEN Bandenden und deren dortiger
Richtung gebildet, in einem Durchgang NACH Kuerzung und Projektion. Damit kann
die Luecke konstruktionsbedingt nicht mehr entstehen; gemessen 0,0 cm.

Test `GIS.RoadNetworkGenerator.IntersectionCoversRibbonEnds` faehrt eine krumme
Zufahrt und prueft gegen, dass sie wirklich gekruemmt ist - sonst beruehrte er
den Fehlerfall gar nicht.

**Regel: Wenn zwei Flaechen aneinanderstossen sollen, ist die Frage nicht, ob
beide richtig BERECHNET sind, sondern ob die eine die andere geometrisch
ERREICHT.** Das ist eine andere Messung.

### Noch offen: Strassen ohne gemeinsamen Knoten

Eine zweite Ursache ist beziffert, aber nicht behoben: Von 4.000 freien
Segmentenden liegen 1.175 innerhalb von 15 m an einem FREMDEN Segmentende
(Median 7,8 m, Minimum 53 cm). Das sind Strassen, die sich beruehren muessten,
in den OSM-Daten aber keinen gemeinsamen Knoten teilen - sie enden im Gras.
Ob das die im Spiel sichtbaren Luecken erklaert, ist noch NICHT nachgewiesen.

### Dritter Fall derselben Ursache: die Baeume

`RegionAssetLayout` war nicht einmal ein Member des WorldBuilders - die Liste
existierte nur waehrend des Builds im Arbeitsspeicher, und die
Instanz-Komponenten (`TreeInstances` und Geschwister) sind ebenfalls
`Transient`. Von 1.532.254 erzeugten Baeumen stand keiner in der gebackenen
Stadt.

Jetzt gilt dasselbe Muster wie bei Ausstattung und Laternen: Daten
serialisieren, Instanzen beim Start aufbauen. Gemessene Kosten:

- Aufbau von 1.779.137 Instanzen (Baeume, Ufer-, Industrieobjekte): 3,7 s
- Groesster External-Actor: 538 MB -> 960 MB

Damit ist dieselbe Ursache dreimal in einer Sitzung aufgetreten. Jedes Mal
meldete das Build-Log korrekte Zahlen - die eine Editor-Sitzung beschrieben,
nicht die Karte.

## "Zerissene Strassen" waren Streaming, nicht Geometrie

Der Nutzer meldete ueber sieben Aufnahmen hinweg zerrissene Strassen. Drei
echte Geometriefehler wurden dabei gefunden und behoben (Kuerzungs-Luecken,
Kreuzungsflaeche verfehlt die Bandenden, Keile an Knick-Knoten). Der EINDRUCK
blieb trotzdem - und hatte eine vierte, ganz andere Ursache.

Aus 400 m Hoehe ist das Netz lueckenlos. Aus dem Helikopter in groesserer Hoehe
nicht. Der Unterschied:

- Baeume, Schilder, Leitpfosten und Laternen haengen am WorldBuilder. Der ist
  IMMER GELADEN, also stehen sie bis zum Horizont.
- Strassen und Gebaeude liegen in 1.984 gestreamten Chunk-Actors. Geladen sind
  davon rund 1.390.

In der Ferne stehen also Baeume und Schilder auf blanker Wiese, waehrend die
Strasse fehlt. Das sieht aus wie zerrissene Geometrie, ist aber vollstaendige
Geometrie, die nicht geladen ist.

**Regel: Bevor fehlende Geometrie repariert wird, pruefen, ob sie ueberhaupt
geladen ist.** Der Unterschied ist am Bild nicht zu sehen, in der Zahl der
geladenen Chunk-Actors sofort.

Die Ladereichweite ist jetzt einstellbar
(`WorldPartitionLoadingRangeMeters`, Standard 2.500 m, zur Laufzeit ueber
`-WbLoadRange=<Meter>`). Gesetzt wird sie ueber einen Konsolen-BEFEHL, nicht
ueber eine Variable:

    wp.Runtime.OverrideRuntimeSpatialHashLoadingRange -grid=0 -range=<cm>

`FindConsoleVariable` findet ihn nicht - der erste Versuch lief still ins Leere.

**HLOD ist hier keine Option.** Der HLOD-Builder verarbeitet ausschliesslich
`UStaticMeshComponent` (siehe Engine/HLODBuilder.cpp); die Stadtgeometrie
besteht aus `UProceduralMeshComponent`. Fuer echte Fernsicht muesste die
Pipeline auf StaticMeshes umgestellt werden.

## Schilder auf einem geraden Strahl

Die Schilder standen reihenweise in Wiesen und Vorgaerten. Der Grund war
derselbe Denkfehler wie bei der Kreuzungsflaeche, zweimal in einer Zeile:

    Base = Kreuzungsmitte
         + Auswaertsrichtung * (RadiusCm + Backset)
         + Rechts * (HalbeBreite + Seitenversatz);

1. Es unterstellt eine GERADLINIGE Zufahrt. Bei einer Kurve marschieren die
   Schilder schnurgerade weiter, waehrend die Strasse abbiegt.
2. `RadiusCm` ist das MAXIMUM ueber alle Arme. An einer Kreuzung mit einer
   breiten Strasse werden auch die Schilder der schmalen Arme entsprechend weit
   hinausgeschoben, teils 20 m.

Sie sitzen jetzt am tatsaechlichen Bandende, seitlich neben der EIGENEN
Fahrbahn.

Wichtig fuer die Fehlersuche: Diese falsch stehenden Schilder wurden zunaechst
als BELEG dafuer gelesen, dass dort Fahrbahn fehle. Das war falsch - die
Strasse lag daneben. Ein Fehler haette so beinahe doppelt gezaehlt.

## Ka-52-Flugphysik: alles oder nichts

Die Flugphysik trug die Werte eines Ultraleicht-Hubschraubers (1.000 kg,
5,5 m Rotor, 420 U/min, 250 kW). Das Modell ist ein Ka-52: 9,8 t, 14,5 m
Rotordurchmesser, 350 U/min, 3.600 kW.

Solche Beiwerte lassen sich NICHT einzeln anpassen. Masse, Auftriebs- und
Widerstandsbeiwert, Rotortraegheit, Reglerverstaerkung, Motorleistung,
Traegheitsmomente und alle Steuermomente haengen zusammen; wird eines
verstellt, hebt die Maschine nicht mehr ab oder laesst sich nicht steuern.

Beim Umstellen wurde `AutorotationGain` uebersehen: Der Blattwiderstand wuchs
um das Elffache, das antreibende Moment nicht. Die Autorotation hielt danach
nur noch 124 statt 350 U/min - ein Triebwerksausfall waere nicht mehr
beherrschbar gewesen. Der Test `RotorPhysics.Autorotation` hat es gefunden.

**Und die Tests selbst waren Teil des Problems:** Drei von ihnen schrieben die
alten Auslegungswerte als feste Zahlen fest (Drehzahlband 350..430 fuer 420
U/min, Pruefgeschwindigkeiten 60 und 75 m/s fuer vmax 75). Sie schlugen fehl,
obwohl das Modell korrekt rechnete - sie prueften den alten Auslegungspunkt.
Alle drei leiten ihre Grenzen jetzt aus den Einstellungen ab. Dieselbe Falle
gab es schon bei der Taglaenge und bei der Sonnenstaerke.

## Der Mittelwert, der die Stadt verdeckt hat

Die Verdeckungsstatistik meldete ueber viele Laeufe hinweg "mittlere
Bodenfreiheit 20 cm" - und der Nutzer sah trotzdem eine schwebende, an den
Kanten abgeschnittene Strasse. Beide hatten recht.

Die Stichprobe lag 200 m um den Startpunkt, und der liegt in der EBENE.
Ueber 3 km gemessen sieht dieselbe Groesse so aus:

    Bodenfreiheit (1800 Werte):
      Median          15 cm
      90 % unter      37 cm
      99 % unter    1324 cm
      groesster Wert 2795 cm

Neun Zehntel der Fahrbahn liegen sauber auf. Das oberste Prozent steht bis zu
28 m frei. Ein Mittelwert von 20 cm kann aus lauter 20 cm bestehen - oder aus
95 Prozent null und ein paar Metern. Im Bild ist der Unterschied sofort zu
sehen, in der Kennzahl nicht.

**Zwei Regeln:**

1. **Eine Stichprobe um den Spieler misst die Gegend um den Spieler.** Liegt der
   Startpunkt in der Ebene, kann sie ueber Hanglagen nichts aussagen. Der
   Radius gehoert zur Aussage dazu.
2. **Fuer Fehler, die als AUSREISSER auftreten, ist der Mittelwert das falsche
   Mass.** Perzentile und Maximum zeigen sie, das Mittel verdeckt sie.

Ursache ist geometrisch: Das Gelaende hat 7,81 m Rasterweite, die Fahrbahn ist
rund 7 m breit. Am Hang traegt die Einebnung nur ein bis zwei Rasterpunkte;
daneben faellt der Boden weg. Behoben durch eine Boeschung von der aeusseren
Fahrbahnkante hinunter aufs Gelaende (ab 25 cm Hoehendifferenz).

Kosten: 3.787 Abschnitte mit 3,5 Mio. Dreiecken - mehr als die Fahrbahn selbst
(949.000). `EmbankmentMinHeightCm` ist die Stellschraube, falls das zu teuer
wird.

**NICHT geloest sind Bruecken.** Die 28-Meter-Werte sind Brueckenbauwerke; dort
waere eine durchgehende Erdwand falsch. Die Boeschung ist auf 4 m begrenzt
(`EmbankmentMaxHeightCm`), darueber bleibt die Kante frei. Bruecken brauchen
ein eigenes Bauwerk mit Pfeilern.

## Ein Log, das nur die Schleife belegt

Die Strassenlaternen leuchteten sichtbar nicht, waehrend das Protokoll
"48 Leuchten aktiv, naechste 10 m" meldete. Beides stimmte. Die Meldung belegte
nur, dass die Zuweisungsschleife gelaufen war - nicht, dass Licht ankommt.

Erst der erweiterte Log beantwortete die Frage:

    Erste Leuchte: (-121343, -119520, 12006), sichtbar ja, 6000 cd,
    Radius 1800 cm, angehaengt ja. Bezugspunkt (-120760, -118808, 11668).

Position, Sichtbarkeit, Staerke, Reichweite, Anhaengung - damit war klar: die
Leuchte war da und eingeschaltet, nur zu schwach. Ein Punktlicht verteilt seine
Leistung auf die ganze Kugel, ein Scheinwerfer buendelt sie; dieselbe Zahl
bedeutet fuer beide etwas voellig anderes. 6.000 ergaben in 7 m Masthoehe rund
10 Lux.

**Regel: Ein Zaehler belegt, dass Code gelaufen ist. Fuer die Frage, ob er
GEWIRKT hat, braucht es den Zustand des Ergebnisses.**

## Der Einwand, den ich selbst formuliert und weggeschoben habe

Die Gehwegflaeche einer Kreuzung wurde auf Bordsteinhoehe gehoben, weil der
Gehweg sonst an jeder Ecke 14 cm abriss. Im selben Kommentar stand:

    // Die Flaeche fuellt die Ecken zwischen den Armen; die Fahrbahnplatte
    // liegt tiefer und wird von ihr nicht verdeckt, weil sie kleiner ist.

Der Nachsatz ist genau verkehrt. Die Gehwegflaeche ist GROESSER und liegt
HOEHER - sie deckt die Fahrbahn damit vollstaendig zu. An allen 13.643
Kreuzungen mit Gehweg lag danach Beton ueber dem Asphalt.

Das Bemerkenswerte ist nicht der Fehler, sondern sein Ablauf: Der Einwand war
erkannt ("eine echte Ringflaeche waere sauberer") und wurde mit einem Satz
weggeschoben, der bei einer Sekunde Nachdenken nicht traegt.

**Regel: Ein Kommentar, der einen erkannten Einwand entkraeftet, ist eine
Behauptung wie jede andere - er braucht einen Test.** Der Test hier ist
trivial und haette den Fehler sofort gefunden: Kein Gehweg-Dreieck darf die
Kreuzungsmitte ueberdecken (`GIS.RoadNetworkGenerator.JunctionSidewalkRing`,
mit Gegenprobe, dass ueberhaupt Gehweg entsteht).

Gebaut wird jetzt ein Ring zwischen aeusserem und innerem Umriss: zu jedem
Punkt des aeusseren der naechstgelegene innere, dazwischen ein Band. Beide
Umrisse stammen aus denselben Armenden und sind konvex, deshalb passt die
Zuordnung ohne Triangulierung mit Loch.

## Leistungsmessung im unfokussierten Fenster ist keine

Auf die Meldung "es ruckelt" wurden nacheinander Zeichenaufrufe, Instanzen,
Lumen, Gelaende und Strassenkollision als Ursache geprueft - durch Ausblenden
und Vergleichen der Bildzeit. Drei voellig verschiedene Konfigurationen (eine
davon ohne Gelaende, Strassen, Gebaeude UND Ausstattung) lieferten:

    Spiel 142,6 ms, Renderer 34,6 ms, Subsystem 4,3 ms

Auf die Nachkommastelle identisch. Drei verschiedene Welten koennen nicht
dieselbe Zeit brauchen - der Diagnoselauf laeuft in einem Fenster OHNE FOKUS,
und Unreal drosselt solche Fenster.

**Der Headless-Diagnoselauf kann Leistung grundsaetzlich nicht messen.**
Belastbar sind nur Zahlen aus einer echten Spielsitzung; die Messung laeuft
deshalb dort alle 15 Sekunden mit (Bildzeit, Aussetzer, Aufteilung auf Spiel-
und Renderer-Strang, Anteil des Subsystems).

Was daraus gesichert ist: Renderer 33 ms, Subsystem 4,5 ms, Spiel-Strang
137 ms. Die Last liegt auf dem Spiel-Strang ausserhalb des eigenen Codes.
Welche Engine-Arbeit das ist, ist NICHT geklaert.


## "Strassensegmente fehlen" hiess: sie waren begraben

Auf die Meldung fehlender Strassenstuecke wurde zuerst die Geometrie geprueft -
und zwar durch Zaehlen statt durch Schauen. Ergebnis:

    Alle 121721 Segmente haben eine Fahrbahnflaeche.

Es fehlte nichts. Eine Strasse unter dem Gelaende sieht aber genauso aus wie
eine fehlende. Die Messung nach dem Einebnen ergab:

    73180 von 1972278 ebenerdigen Punkten liegen UNTER dem Gelaende (3,7 %),
    schlimmster Fall 1352 cm.

Dreizehn Meter sind kein Interpolationsfehler. Zwei Ursachen:

**1. Die Einebnung lief ueber ALLE Segmente, auch Bruecken und Tunnel.** Eine
Bruecke 13 m ueber Grund hat das Gelaende auf Brueckenhoehe gezogen - und die
ebenerdige Strasse, die darunter durchfuehrt, im Erdreich begraben. Ein Tunnel
hat umgekehrt einen Graben ausgehoben. Eine Bruecke steht auf Pfeilern, ein
Tunnel liegt unter dem Berg; beide haben mit der Gelaendehoehe an ihrer Stelle
nichts zu tun. Nach dem Ausschluss: 64694 Punkte, schlimmster Fall 238 cm.

**2. Je Zelle gewinnt der naechstgelegene Strassenpunkt.** Laufen zwei Strassen
mit Hoehenunterschied dicht nebeneinander - Rampe neben ebenerdiger Strasse,
Serpentine am Hang -, setzt die hoehere die Zelle und begraebt die tiefere.
Die Gegenmassnahme ist eine zweite, schmale Runde: Wo wirklich Asphalt liegt,
darf das Gelaende NIE ueber der tiefsten dort verlaufenden Fahrbahn liegen.
Ausserhalb bleibt der naechstgelegene Punkt massgeblich, damit die Boeschung
der Strasse folgt.

**Der Deckel-Radius muss mindestens die halbe Zelldiagonale betragen.** Die
erste Fassung nahm halbe Fahrbahnbreite plus 1 m - bei schmalen Wegen 250 cm -
und aenderte am schlimmsten Fall gar nichts (238 cm, unveraendert). Grund: Die
Gelaendehoehe unter einem Punkt entsteht durch bilineare Interpolation aus VIER
Zellen, von denen eine bis zu CellSize * sqrt(2) / 2 entfernt liegt - bei
781 cm Rasterweite also 553 cm. Eine nicht erfasste hohe Ecke zieht die
interpolierte Flaeche wieder ueber die Fahrbahn.

Mit dem korrigierten Radius:

    2645 von 1972278 Punkten unter dem Gelaende (0,1 %), schlimmster Fall 209 cm
    Gegenprobe: 1969633 Punkte ueber Grund, im Mittel 29,3 cm, hoechstens 364 cm

Die Gegenprobe gehoert zwingend dazu. 29,3 cm ist der Sollwert (20 cm Versatz
plus 14 cm Absenkung); ohne sie waere nicht zu unterscheiden, ob die
Verdeckung behoben oder nur gegen schwebende Fahrbahnen eingetauscht wurde -
genau dieser Fehler ist hier schon einmal passiert.

Merksatz: **Vor der Suche nach fehlender Geometrie messen, ob die vorhandene
sichtbar ist.** Die Zaehlung "alle Segmente haben eine Flaeche" war richtig und
hat trotzdem in die Irre gefuehrt.

## Die Abstandsregel endete an der Kantengrenze

"Die KI-Autos verhaeddern sich" war woertlich zu nehmen. Die Kopf-zu-Schwanz-
Regel gruppierte die Fahrzeuge nach Spur BZW. Verbindung und begrenzte nur
innerhalb einer Gruppe. Ein Fahrzeug am Spurende sah damit nicht, dass auf der
Kreuzungsverbindung davor bereits jemand stand: Es fuhr hinein, und erst im
naechsten Tick - jetzt in derselben Gruppe, mit bereits negativem Abstand -
wurde es auf 0 geklemmt. Sichtbar als ineinander steckende Fahrzeuge an jeder
Kreuzung.

Die Regel braucht deshalb einen zweiten Durchgang ueber die Bahngrenze hinweg:
Wer naeher am Bahnende ist als Mindestluecke plus Bremsweg, schaut auf das
hinterste Fahrzeug der Folgebahn.

## Beschleunigung begrenzen, Bremsen nicht

Die Verkehrssimulation setzte die Geschwindigkeit in einem Tick auf den
Zielwert - ein Fahrzeug sprang zwischen Stillstand und Vollgas. Daraus
entstanden Bremswellen, die rueckwaerts durch die Kolonne liefen.

Die naheliegende Abhilfe - Beschleunigung UND Bremsung deckeln - ist falsch.
Die Abstandsregel loest exakt auf: Sie setzt genau die Geschwindigkeit, bei der
die Mindestluecke im naechsten Schritt noch eingehalten ist. Ein gedeckeltes
Bremsen heisst, dass ein Fahrzeug diese Geschwindigkeit nicht erreicht - und
auffaehrt. Der bestehende Kopf-zu-Schwanz-Test verlangt schon 1100 cm/s^2.

Begrenzt wird deshalb nur das Beschleunigen. Genau dort lag die sichtbare
Sprunghaftigkeit ohnehin.

## Der Lenkbefehl darf nie direkt ins Giermodell

`ComputeYawRate(Input.Steering)` nahm die rohe Eingabe: Ein Tastendruck
bedeutete Volleinschlag im selben Bild. Kein Fahrzeug auf Raedern verhaelt sich
so; es war der groesste Anteil an dem Eindruck "Fahrphysik unrealistisch".

Der Einschlag ist ein ZUSTAND, der der Eingabe mit begrenzter Geschwindigkeit
folgt (2,5 je Sekunde, also 0,8 s von Anschlag zu Anschlag).

Zwei Folgefehler dabei:

**`FMath::Sign(0.0f)` ist 0** und weicht damit von jedem Ziel ab. Die Pruefung
"Vorzeichen verschieden also Rueckstellung" hat das Einlenken aus der
Geradeausstellung als Rueckstellung eingestuft und mit der schnelleren
Ruecklaufrate ausgefuehrt - der Fehler, der behoben werden sollte, blieb damit
teilweise bestehen. Nur ein Test mit einer konkreten Zahl (0,25 nach 0,1 s) hat
das gezeigt; ein Test auf "wird groesser" waere gruen geblieben.

**Der bestehende Lenktest hielt die Traegheitsfreiheit fest.** Er lenkte einen
10-ms-Tick lang und erwartete die volle Gierrate. Solche Tests muessen beim
Beheben des Fehlers mitgeaendert werden - sonst zwingen sie den Fehler zurueck.

## Reifen haben EIN Kraftbudget

Bremskraft und Querkraft waren unabhaengig: 0,7 g bremsen plus 0,75 g Querkraft
sind 1,03 g, mehr als der Reifen uebertragen kann. Unter Vollbremsung liess
sich genauso scharf einlenken wie ohne - der haeufigste Grund, aus dem sich ein
Fahrmodell wie auf Schienen anfuehlt. Der Reibungskreis koppelt beides:

    verfuegbar_quer = mu*g * sqrt(1 - (laengs / (mu*g))^2)


## Kreuzungsinneres wird von der Fahrbahn-Einebnung nicht erfasst

"Strassen haben immer noch Gruenflaechen, besonders an Kreuzungen." Die
Ursache steht als Konstruktionsmerkmal im Code: FlattenUnderRoads ebnet
entlang von TrimmedCenterline ein - und getrimmt heisst genau, dass das
Kreuzungsinnere ausgespart ist. Die Baender enden am Kreuzungsrand, dazwischen
liegt die Kreuzungsplatte, und unter ihr wurde nie abgesenkt. Gemessen:

    Kreuzungsflaechen: 74312 Zellen unter 20213 Kreuzungen abgesenkt.

Das Absenken selbst ist heikler als es klingt. Vier Fassungen, alle gemessen:

| Fassung                            | Platten vergraben | groesster Freiraum |
|------------------------------------|-------------------|--------------------|
| tiefste Ecke, volle Kreisscheibe    | 59  (0,05 %)      | 2751 cm            |
| tiefste Ecke, auf Umriss begrenzt   | 188 (0,16 %)      | 1558 cm            |
| naechstgelegene Ecke                | 1583 (1,4 %)      | 1499 cm            |
| Minimum der Ecken in der Umgebung   | 458 (0,4 %)       | 453 cm             |

Was daraus zu lernen ist:

**Intersection.RadiusCm ist der Abstand des ENTFERNTESTEN Arms.** Eine
Kreisscheibe damit ist bei asymmetrischen Kreuzungen weit groesser als die
Platte und reisst das Gelaende unter Nachbarstrassen weg - eine Strasse
schwebte 27 m ueber dem Boden. Massgeblich ist der Umriss.

**"Naechstgelegene Ecke" reproduziert den Interpolationsfehler eine Ebene
hoeher.** Die Platte ist eine Flaeche zwischen den Ecken; eine Treppe aus
Eckhoehen hebt die bilinear interpolierte Gelaendeflaeche ueber die
benachbarte, tiefere Ecke. Das Minimum ueber die Umgebung ist bei kleinen
Kreuzungen flach (kein Interpolationsfehler moeglich) und folgt bei grossen
dem Gefaelle.

**Kreuzungen an Bruecken und Tunneln (168 von 20213) bleiben ausgespart.** Ihre
Ecken liegen bis zu 15 m auseinander - jede gemeinsame Hoehe ist dort falsch.

## Ein Temporary als Netz haengt den Zeiger in die Luft

Elf Testaufrufe machten `Sim.Initialize(MakeNetwork(), ...)`. Initialize merkt
sich nur einen Zeiger; das Temporary stirbt am Ende des Ausdrucks. Das lief
jahrelang unauffaellig durch, weil der freigegebene Speicher unangetastet
blieb. Eine zusaetzliche Allokation in Initialize (die Nachbarspur-Tabelle) hat
ihn neu belegt - und die Simulation stuerzte mit ungueltigen Spur-Indizes an
einer Stelle ab, die mit der Ursache nichts zu tun hatte.

Die Rvalue-Ueberladung ist deshalb `= delete`, bei Verkehr, Ampeln UND
Fussgaengern. Der Compiler faengt das jetzt ab, statt es dem Zufall zu
ueberlassen.

**Merksatz: Wenn ein Absturz an einer Stelle auftritt, die der letzte Eingriff
nicht beruehrt hat, ist die Lebensdauer eines Verweises der erste Verdacht -
nicht die abstuerzende Zeile.**


## Sichtbarkeit ist keine Eigenschaft der Daten

Die Kreuzungen waren im Spiel nicht zu sehen. Geprueft wurde: Dreieckszahl
(74.687 vorhanden), Hoehenlage (-46 bis 50.375 cm, wie die Fahrbahn),
Material (aufgeloest wie erwartet), Chunk-Zuordnung (SplitSection ordnet jedes
Dreieck ueber seinen Schwerpunkt genau einer Zelle zu), Verdeckung durch das
Gelaende (Platten lagen darueber). ALLES davon war in Ordnung.

Der Fehler war die UMLAUFRICHTUNG. Die Normalen standen fest auf UpVector -
das steuert aber nur die Beleuchtung. Die Rueckseitenentfernung richtet sich
nach der Reihenfolge der Indizes, und die kam aus der konvexen Huelle:

    Kreuzungsplatten: 78158 Dreiecke mussten umgedreht werden.

Das sind praktisch alle. Sechs Runden Datenpruefung haben nichts gefunden,
weil Sichtbarkeit in den Daten nicht steht.

**Das Werkzeug, das die Frage in drei Minuten beantwortet hat:**
`-WbHideLandscape` plus Blick von oben. Ist an der Stelle HIMMEL zu sehen,
fehlt die Flaeche oder ist rueckseitig zugewandt. Ist Gelaende zu sehen, liegt
sie darunter. Zwei voellig verschiedene Ursachen, im normalen Bild nicht zu
unterscheiden.

Dazu `-WbTour=N`: Kamera an jede Kreuzung, senkrecht von oben plus vier
Himmelsrichtungen. Aus EINEM Blickwinkel ist eine Kreuzung nicht zu beurteilen.

## GetLeftNormal liefert (Y, -X) - also RECHTS

`FPolygonUtils::GetLeftNormal(D)` gibt `(D.Y, -D.X)` zurueck. Das ist eine
Drehung um MINUS 90 Grad; der Vektor heisst "links" und zeigt auf die
mathematisch rechte Seite. Eine Konvention aus dem linkshaendigen
Koordinatensystem.

Daran ist der Kreuzungsumriss gescheitert: Nach Richtung sortiert und "erst
rechte, dann linke Torecke" gereiht entstand ein STERN mit Zacken statt einer
Flaeche. Die Flaechenformel meldete durch die Vorzeichen-Aufhebung 31 statt
45 Quadratmeter - das sah nach "etwas kleiner" aus, nicht nach "kaputt". Erst
das Bild hat es gezeigt.

**Konsequenz: Seitenzuordnungen nicht herleiten, sondern ueber den ABSTAND
entscheiden.** Der Anschluss von Band, Gehweg und Bordstein an die
Kreuzungstore waehlt jetzt die naeher liegende Ecke und misst den Versatz mit
(0,23 cm im Mittel ueber 314.403 Randpunkte). Ein Wert in Fahrbahnbreite
wuerde vertauschte Seiten sofort verraten.

## Der Rueckschnitt behandelte den harmlosesten Fall als teuersten

Die Trimmweite ist `halbe Breite des anderen Arms / |sin(Winkel)|`. Bei einer
geradeaus DURCHLAUFENDEN Strasse ist der Winkel zum Gegenarm 180 Grad und der
Sinus null. Ein Divisor-Minimum von 0,35 fing die Division ab - und machte
daraus das 2,86-fache der halben Breite:

    Trimmweite 884 cm, Plattenabdeckung 42 %

Fast neun Meter Rueckschnitt an jedem Arm jeder der 20.213 Kreuzungen. Dabei
braucht eine durchlaufende Strasse gar keinen: Die Baender liegen auf einer
Linie und setzen einander fort. Nach der Korrektur 414 cm und 82 %.

## Zaehler, die nach dem Zaehlen zurueckgesetzt werden

"Anschluss an Kreuzungstore: 0 von 0 Bandenden" hiess nicht "nichts
angeschlossen", sondern "nicht gemessen" - der Zaehler wurde NACH der Schleife
zurueckgesetzt, die ihn hochzaehlt.

Ebenso falsch war der selbst aufgestellte Massstab "die Zahl der umgedrehten
Dreiecke muss 0 oder 100 Prozent sein". Da jedes Dreieck einzeln geprueft und
notfalls gedreht wird, ist das Ergebnis in jedem Fall richtig; die Zahl
beschreibt nur die Eingabe.

**Vor dem Deuten einer Zahl pruefen, was sie ueberhaupt misst.**

## Was die Fuge zwischen Band und Platte angeht: es gab sie nicht

Die Vermutung war, dass Fahrbahnband und Kreuzungsplatte ihre Randpunkte
getrennt berechnen und deshalb um Zentimeter auseinanderliegen. Gemessen nach
dem Anschluss ueber gemeinsame Punkte:

    Versatz beim Anschluss: im Mittel 0,00 cm (124.800 Randpunkte)

BuildRibbonMesh versetzt die Mittellinie mit derselben Endtangente, die auch
der Kreuzungsarm benutzt - beide kamen von jeher auf denselben Punkt. Der
Tor-Ansatz bleibt trotzdem richtig, weil er die Fugenfreiheit GARANTIERT statt
sie dem Zufall zu ueberlassen, und weil er die verschluckten Ecken der
konvexen Huelle ueberhaupt erst sichtbar gemacht hat. Aber er hat nichts
repariert, was kaputt war.

## Boeschungen gehoerten in den Kanal "Fahrbahn"

Zwei Fehler auf einmal:

**Die Verdeckungsstatistik lief ueber alle Abschnitte eines Chunks** -
einschliesslich der Boeschungen, die bauartbedingt bis auf Gelaendehoehe
hinunterlaufen. Ihre unteren Vertices zaehlten ausnahmslos als "verdeckte
Fahrbahn". Gemeldet wurden 8 bis 14 Prozent, bereinigt sind es 4.

**Die Kollisionsabschaltung erkannte Boeschungen an "Kanal Fahrbahn PLUS
Oberflaeche Erde"** - und erfasste damit auch echte unbefestigte Strassen. Auf
einem Feldweg fiel man durch den Boden.

Boeschungen haben jetzt einen eigenen Kanal. Ihnen dabei das GELAENDE-Material
zu geben war ein Rueckschritt: 2,4 Millionen Dreiecke entlang jeder
Strassenkante, grasfarben - genau die "Gruenflaechen auf der Fahrbahn", die
abgestellt werden sollten. Sie behalten das unbefestigte Material.

## UProceduralMeshComponent kocht Kollision synchron

Vorgabe ist `bUseAsyncCooking = false`. Bei 1.394 Chunk-Actors, die World
Partition beim Fahren nachlaedt, blockiert jeder Chunk den Spiel-Strang. Das
Ladeprotokoll zeigt es als Zeilenflut:

    LogChaos: Input trimesh contains N bad triangles

Jede Zeile ist ein Kochvorgang. Passend dazu die Messung: Spiel-Strang 137 ms,
Renderer 33 ms, 22 bis 24 Aussetzer ueber 50 ms je 31 Bilder - die Last liegt
nicht beim Zeichnen, und es ist keine durchgaengige Last, sondern Nachladen.

`bUseAsyncCooking = true` ist gesetzt, aber NICHT verifiziert: Der Messlauf
erreichte die Spielphase nicht, weil die Karte ueber vier Minuten laedt.

## Das Ruckeln kam aus den Shadern, nicht aus dem Spiel

Vier Anlaeufe gingen daneben, weil ich jedes Mal geraten habe, wo die Zeit
bleibt: Kollisionskochen, Streaming, 10.381 Zeichenaufrufe, meine eigenen
Simulationen. Erledigt hat es erst eine Messreihe, in der jeweils GENAU EIN
Ding weggenommen wurde:

| Lauf | Bildzeit | Spiel-Strang | Renderer |
|---|---|---|---|
| alles an | 146 ms | 20-26 ms | 142-146 ms |
| ohne Strassen + Gebaeude | 163 ms | 19 ms | 160-162 ms |
| ohne Gelaende | 138-185 ms | 201-223 ms | **7,6-7,9 ms** |
| alle Materialien einfarbig | 70-99 ms | 7,6-10,2 ms | **8,8 ms** |

Drei Dinge stehen da drin, die man nur so sieht:

1. **Die Stadt auszublenden machte es LANGSAMER** (146 -> 163 ms). Weniger
   Geometrie ist nicht automatisch schneller: Ohne Haeuser verdeckt nichts
   mehr das Gelaende, und die Verdeckungspruefung verliert genau das, was sie
   sonst einspart. Ein Test, der die Erwartung umkehrt, ist trotzdem ein
   Ergebnis.
2. **Der Renderer faellt in beiden entlastenden Laeufen auf dieselben ~8 ms.**
   Zweimal derselbe Boden aus zwei verschiedenen Richtungen - das ist die
   Grundlast, alles darueber ist das Gelaendematerial.
3. **Einfarbig macht auch den Spiel-Strang schnell** (26 -> 7,6 ms). Wer nur
   auf den Renderer geschaut haette, haette das uebersehen; der Spiel-Strang
   wartet auf den Renderer.

Ursache: `MaterialExpressionNoise` mit `levels = 3` in siebzehn Materialien -
dreioktaviges Simplex-Rauschen, ausgewertet JE BILDPUNKT. Das Gelaende fuellt
fast den ganzen Schirm.

Behoben, indem das Rauschen einmal in eine kachelbare Textur gebacken wird
(`Tools/make_noise_texture.py`, Details in ASSETS.md). Gleiche Optik, einmal
vorberechnet.

**Die Lehre ist nicht "Shader sind teuer".** Sie ist: Solange die Messung nur
eine Gesamtzahl liefert, ist jede Ursachenaussage geraten. Erst das
Wegnehmen einzelner Anteile - und zwar auch solcher, die man fuer unschuldig
haelt - macht aus der Zahl eine Aussage. Vorher habe ich viermal etwas
"optimiert", das nie das Problem war.

### Zwei Fallen bei genau dieser Messung

* **Alle frueheren Zahlen lagen in der Ladephase.** Die Karte laedt ueber drei
  Minuten. Ein Messlauf muss laenger sein als das, und ausgewertet werden
  duerfen nur die letzten Fenster.
* **`-ExecCmds` verliert gegen `DefaultEngine.ini`.** Der Versuch, Lumen zur
  Laufzeit abzuschalten, hat beide Laeufe identisch gemessen, weil die Zeile
  in der ini gewonnen hat. Zwei Laeufe mit exakt gleichem Ergebnis sind ein
  Warnsignal, kein Beleg fuer Unwichtigkeit.

## Drei Zaehler, die Wartezeit mitzaehlen - und mich dreimal getaeuscht haben

In einer einzigen Sitzung bin ich dreimal auf dieselbe Falle hereingefallen:
Ein Zaehler heisst nach einem Strang, misst aber dessen Frame-Dauer
EINSCHLIESSLICH der Zeit, in der er auf jemand anderen wartet.

| Zaehler | heisst | misst tatsaechlich |
|---|---|---|
| `GRenderThreadTime` | Render-Strang | Bildzeit, wenn er auf die Karte wartet |
| `RHIGetGPUFrameCycles` | Grafikkarte | Spanne ueber das Bild inkl. Leerlauf |
| `GGameThreadTime` | Spiel-Strang | Bildzeit, wenn er auf den Renderer wartet |

Jedes Mal lief es gleich ab: Der Zaehler lag dicht an der Bildzeit, ich las
das als "hier ist der Engpass" - und genau diese Uebereinstimmung war das
Warnsignal, nicht der Beweis. Ein Strang, der die Bildzeit exakt trifft,
wartet vermutlich darauf.

**Was WIRKLICH misst:**

* `stat gpu` - fortlaufend gemittelte Arbeit der Karte je Durchgang, mit
  eigenen Spalten fuer Busy, Wait und Idle. Die Summe geht zur Bildzeit auf.
* `stat game` mit `stats.MaxPerGroup 45` - `World Tick Time` ist die echte
  Arbeit des Spiel-Strangs. Als der Zaehler 43 ms meldete, waren es 8,95 ms.
* `ProfileGPU` ist NICHT verlaesslich: Es misst EIN Bild. Ohne Baeume meldete
  es 13,6 ms, waehrend der laufende Betrieb 53 ms brauchte.

## Eine feste Aussetzer-Schranke wird wertlos, wenn das Mittel sie erreicht

Der Zaehler "Bilder ueber 50 ms" meldete bei 46 ms Mittel **0** Aussetzer und
bei 51 ms Mittel **168** - ohne dass sich am Ruckelverhalten etwas geaendert
haette. Fast jedes Bild lag knapp darueber. Ich habe diesen Sprung erst fuer
eine Verschlechterung gehalten.

Als Ruckeln nimmt man ein Bild wahr, das aus der Reihe faellt, nicht eines,
das eine feste Zahl ueberschreitet. Der Zaehler vergleicht deshalb gegen das
laufende Mittel des Fensters (Faktor 2). Beide Werte werden ausgegeben, damit
aeltere Messungen vergleichbar bleiben.

Der neue Zaehler trennt sofort zwei Dinge, die die alte Schranke vermischt
hat: "durchgehend langsam" (105 ms Mittel, 143 Ueberschreitungen, **0**
Ausreisser) gegen "ruckelt" (34 ms Mittel, **36** Ausreisser).

## Overpass liefert auch WAYS doppelt - und die zweite Kopie hat keine Tags

Es gibt in dieser Datei seit langem einen Abschnitt darueber, dass Overpass
KNOTEN doppelt liefert und ein blindes `TMap::Add` die getaggte Kopie
ueberschreibt; damals kostete das alle 2.310 Ampeln. Die Lehre wurde als
Einzelfall abgelegt, nicht als Fehlerklasse - und niemand hat gefragt, ob
dasselbe fuer Ways gilt.

Es gilt. An der Wiesbaden-Datei:

    alle Way-Elemente  225.479
    verschiedene Ids   223.434     -> 2.045 Doppelte
    davon mit tagfreier Kopie HINTEN: 2.045 (alle)

Ein Way, der Mitglied einer Relation ist, kommt zweimal: einmal vollstaendig
getaggt, danach als blosses Geruest aus Id und Knotenliste. Die tagfreie Kopie
gewinnt, `highway=*` verschwindet, und der Way ist ab da keine Strasse.

**Auswirkung:** 1.990 fehlende befahrbare Wege, 118,6 km - darunter Stuecke
der Rheinallee (2,29 km), der Mainzer Strasse (1,77 km), der Platter Strasse
(1,07 km), des Konrad-Adenauer-Rings und des Kaiser-Friedrich-Rings.

**Gefunden ueber einen einzigen Satz:** "Platter Strasse stadtauswaerts fehlt
in Spielwelt UND Minikarte". Das UND war der Hinweis - die Minikarte zeichnet
aus dem Netz, nicht aus der Geometrie, also fehlte es schon in den Daten.

Behoben mit `AddOrMergeWay` (beide Einlesewege, JSON und XML) plus zwei
Regressionstests, einer je Reihenfolge des Duplikats.

### Das Werkzeug dazu

`-WbDumpStreets` schreibt jedes Segment mit seiner OSM-Way-Id;
`Tools/audit_streets.mjs` haelt das gegen die Quelldaten. Verglichen wird ueber
die **Id**, nicht ueber den Namen: "Platter Strasse" steht an 60 Wegen,
darunter Wirtschafts- und Fusswege, und ein fehlender Fahrstreifen ginge im
Namensvergleich unter.

**Falle dabei:** `FFileHelper` waehlt die Kodierung nach Inhalt und schreibt
UTF-16, sobald ein Umlaut vorkommt - deutsche Strassennamen sind voll davon.
Als UTF-8 gelesen ergab die Datei EINE Zeile, und der Bericht meldete 39.863
fehlende Wege und 5.272 km. Ein Werkzeug, das bei unlesbarer Eingabe "alles
kaputt" meldet statt "Eingabe unlesbar", ist gefaehrlicher als keins. Jetzt:
`ForceUTF8WithoutBOM` beim Schreiben, BOM-Erkennung und eine Abbruchschwelle
beim Lesen.

## Instanz-Kosten haengen an der VERWALTETEN Menge, nicht an der sichtbaren

Alle 1.532.254 Baeume lagen in EINER Instanz-Komponente am WorldBuilder -
einem Actor, den World Partition nie streamt. Gemessen an einer Stelle, an der
ausser einer Handvoll winziger Kegel am Horizont kein Baum im Bild stand:

    1.532.254 Instanzen   82 ms Bildzeit
      153.226 Instanzen   64 ms
            0 Instanzen   54 ms

Das Bild von der Messstelle war der entscheidende Hinweis: Vier sichtbare
Kegel koennen keine 27 ms Zeichenzeit kosten. Damit schied Ueberdeckung aus,
und es blieb die Verwaltung.

Eine Sichtweitenbegrenzung haette daran NICHTS geaendert - die Kosten
entstehen auch fuer Instanzen, die nie gezeichnet werden. Geholfen hat nur,
sie auf die Zellen zu verteilen (`FCityChunkMesh::RegionAssets`).

## Detailstufen ersetzen keine Sichtweite

Mit echten Baummodellen (34.000 bis 356.000 Dreiecke, Blaetter als Geometrie)
stieg die GPU-Zeit trotz vier Detailstufen von 35 auf 102 ms:

    Zustand                        GPU     Basepass   Prepass   Schatten
    Kegel                        35 ms       3,7       2,4        7,0
    echte Baeume ohne Sichtweite 102,5 ms   34,8      32,0       16,3
    echte Baeume mit Sichtweite   44,5 ms    6,8       5,2       13,4

Der Sprung bei Basepass UND Prepass war die Diagnose: Der Tiefendurchgang hat
den billigstmoeglichen Pixelschritt. Springt ausgerechnet der von 2,4 auf
32 ms, liegt es an roher Dreiecksmenge - nicht an Material, nicht an Schatten.

Auch die groebste Stufe hat noch 900 bis 10.600 Dreiecke, und Wiesbaden liegt
an den Taunushaengen: Von der Innenstadt sieht man kilometerweit Wald.

## Die Editor-Skriptbibliothek meldet Fehlschlag als Rueckgabewert

`EditorStaticMeshLibrary.set_lods` gibt im Kommandozeilenlauf (`-run=pythonscript`)
**-1** zurueck und tut nichts - sie prueft intern, ob sie "im Editor und nicht
im Spiel" laeuft. Keine Ausnahme, keine Fehlermeldung. Das Skript meldete
danach seelenruhig "12 von 12 Pflanzen importiert", und alle hatten eine
Detailstufe.

Solche Aufrufe gehoeren in den VOLLEN Editor:

    UnrealEditor.exe <uproject> -ExecCmds="py <skript>" -unattended -nosplash

`-ExecCmds` trennt mit KOMMA, nicht mit Semikolon - ein angehaengtes
"; Quit" landet im Python-Aufruf und ergibt einen Syntaxfehler. Das Skript
beendet den Editor deshalb selbst.

## Blenders `Image.pixels` liefert ROHWERTE, keine Helligkeiten

Fuer die Beine des Spielermodells sollte die Hosenfarbe aus dem Fotoscan
selbst kommen, damit der Uebergang an der Huefte nicht auffaellt. Also den
Bildpunktdurchschnitt am Hosensaum ausgelesen: `(0.1382, 0.1249, 0.1112)`.
Diesen Wert als Grundfarbe eingesetzt - und die Figur trug eine HELLGRAUE
Hose neben einem fast schwarzen Mantel.

Bei einem 8-Bit-Bild gibt `Image.pixels` den Byte-Wert geteilt durch 255
zurueck, OHNE Farbverwaltung. Das ist der sRGB-kodierte Wert, nicht die
Rechenhelligkeit. Grundfarben in Blender sind aber linear. 0,138 als sRGB
gelesen sind linear 0,017 - ein Faktor 8. Die Hose war rund dreimal zu hell
im fertigen Bild.

Die Umrechnung gehoert dazwischen:

    def srgb_to_linear(c):
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4

Gegenprobe ist billig: die gebackene PNG-Datei wieder auslesen. Steht dort
0,19 fuer eine Grundfarbe, die 0,03 sein sollte, ist der Fehler genau hier.

## Blenders X wird Unreals X - eine Figur muss nach +X schauen

Der Scan schaut in Blender nach -Y. Ueblicherweise heisst es, Blenders -Y
werde beim FBX-Weg zu Unreals +X; das stimmt hier nicht. Der Import meldete:

    Groesse 41.3 x 70.3 x 177.7 cm

41,3 cm ist die Schulterbreite, 70,3 cm die Tiefe mit der Kettensaege. In
Unreal lag die Breite auf X - die Figur stand quer zur Laufrichtung und waere
seitwaerts durch die Stadt gelaufen.

Blenders X landet auf Unreals X. Wer will, dass die Figur nach vorn schaut,
dreht sie in Blender auf +X:

    me.transform(Matrix.Rotation(math.radians(90.0), 4, 'Z') @ shift)

Die Ausmasse aus dem Import sind die Probe: in Blickrichtung muss die
GROESSERE Zahl stehen, wenn die Figur etwas vor sich her traegt.

## Ein Fotoscan bringt seinen Schatten in der Textur mit

Neben den Scan gesetzte, rechnerisch saubere Geometrie sieht angeleuchtet
aus. Der Mantel des Scans ist dunkel, weil er beim Fotografieren im Schatten
lag - diese Verdunklung steckt in der Farbtextur, nicht in der Beleuchtung.
Die neu gebauten Hosenbeine hatten sie nicht, und an der Huefte klaffte ein
Helligkeitssprung, obwohl beide Farben gemessen zusammenpassten.

Abhilfe: die Eigenverschattung mitbacken - ein `ShaderNodeAmbientOcclusion`
in die Grundfarbe multipliziert, `only_local = False`, damit der Torso beim
Backen seinen Schatten auf Huefte und Schritt wirft.

Das gilt allgemein fuer Scans: ihre Textur ist keine reine Albedo. Wer
danebenbaut, muss die fehlende Verschattung nachliefern.

## Fotoscans kommen entlang der UV-Naehte aufgetrennt an

Der Scan meldete 2 853 lose Teile, 77 133 offene Randkanten und 3 302
Randschleifen. Das las sich wie ein zerfetztes Netz, an dem nichts zu retten
ist. Tatsaechlich war es EINE Flaeche, nur an jeder UV-Naht in getrennte Ecken
zerlegt - der uebliche Zustand nach einem GLB-Export.

    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=0.0001)

Danach: 21 Teile, davon eines mit 196 493 Ecken und 20 Krumen aus dem
Fotorauschen, 1 393 Randkanten - der Huftschnitt. Erst verschweisst laesst
sich weich schattieren oder reduzieren; vorher sieht jede Glaettung
facettiert aus, weil die Ecken sich ihre Normalen nicht teilen.

## Ein falscher Abtasttyp hat die ganze Stadt grau gemacht

Die Spielerfigur kam im ersten Spiellauf grau an - kein Gesicht, keine Jacke,
nur Standardgrau. Modell, Texturen und Materialzuweisung waren nachweislich in
Ordnung: der Import hatte alle drei Schlitze protokolliert. Im Spielprotokoll
stand die Ursache in einer einzigen Zeile, mehrere tausend Zeilen tief:

    LogMaterial: Warning: M_WbFigur.uasset: Failed to compile Material for
    platform PCD3D_SM6, Default Material will be used in game.
        (Node TextureSampleParameter2D) Sampler type is Normal,
            should be Color for /Engine/EngineResources/DefaultTexture
        (Node TextureSampleParameter2D) Sampler type is Linear Grayscale,
            should be Color for /Engine/EngineResources/WhiteSquareTexture

Unreal verlangt, dass der ABTASTTYP eines Textureingangs zur KOMPRESSION der
dort liegenden Textur passt - auch bei der ERSATZTEXTUR, die nur dann sichtbar
wird, wenn eine Materialinstanz den Parameter nicht ueberschreibt. Passt es
nicht, wird das Material NICHT uebersetzt und die Engine zeichnet stumm das
Standardmaterial. Kein Fehler, keine rote Meldung, nichts im Editor.

Die Prueflinge waren nicht offensichtlich: `WhiteSquareTexture` klingt neutral,
ist aber ein FARBBILD mit Farbraum-Korrektur. Und wo gar keine Ersatztextur
gesetzt ist, steht `DefaultTexture` - ebenfalls ein Farbbild.

Dieselbe Sache traf gleichzeitig drei Ecken des Projekts:

| Material | Stelle |
|---|---|
| `M_WbFigur` | Normale und Rauheit ohne passende Ersatztextur |
| `M_WbBuildingWall`, `M_WbFacade_Putz/Beton/Sandstein/Backstein` | Metallic-Karten als `TC_MASKS` importiert, aber `LINEAR_GRAYSCALE` abgetastet |
| `M_VWBeetle_Master` | vier Eingaenge ohne passende Ersatztextur (Asset aus dem Kaefer-Paket, von keinem Werkzeug erzeugt) |

**Folge fuer die Leistungsmessung:** Die 50,1 ms aus der Messreihe nach dem
SM6-Wechsel wurden auf dem STANDARDMATERIAL gemessen, nicht auf den echten
Texturen. Als Grundlinie sind sie wertlos.

Abhilfe im Projekt: `Tools/Blender/make_default_textures.py` erzeugt drei
eigene Ersatztexturen (`T_WbVorgabe_Weiss`, `_Maske`, `_Normal`), deren
Kompression der Import selbst setzt. Bei ihnen ist nichts zu raten.

**Prueffrage nach jeder Materialaenderung:**

    findstr /C:"Failed to compile Material" Saved\Logs\WiesbadenReal.log

Ist die Ausgabe leer, stimmen die Abtasttypen. Sonst steht in der Zeile
darunter genau, welcher Eingang welche Kompression erwartet.

## Die Fassadentexturen heissen anders, als sie aussehen

Nachdem die Materialien wieder uebersetzten, war jedes Haus in Wiesbaden
Spiegelglas. Das lag nicht am Material, sondern an der Textur dahinter:

| Datei | Was wirklich drin ist | Wohin sie verdrahtet war |
|---|---|---|
| `Facade_Wohnhaus` | Glas-Vorhangfassade eines Buerohochhauses | `M_WbFacade_Putz` (Putz!) |
| `Facade_Buerohaus` | Backsteinblock mit Fenstern | `M_WbFacade_Beton` |
| `Facade_Altbau` | nachts beleuchtetes Hochhaus, sonst schwarz | `M_WbFacade_Backstein` |
| `Facade_Nachkrieg` | Backsteinhochhaus mit Fenstern | `M_WbFacade_Sandstein` |

Alle vier sind moderne Hochhaeuser. Fuer eine verputzte Wiesbadener
Gruenderzeitfassade ist KEINE davon brauchbar - der Name im Ordner sagt, wozu
sie gedacht war, nicht, was sie zeigt. `Tools/collect_materials.py` hat sie
nach Verwendungszweck benannt, ohne den Inhalt anzusehen.

Vor dem Verdrahten einer gesammelten Textur: ansehen. Ein Blick auf das
verkleinerte Farbbild kostet Sekunden und haette hier vier Fehlgriffe
verhindert.

## Drei Python-Skripte in einem ExecCmds - das zweite fror ein

`-ExecCmds="py a.py, py b.py, py c.py"` hat am 28.08. nach dem ersten Glied
angehalten: `import_materials.py` lief durch (letzter Marker 12:47), danach
stand der Editor VIER STUNDEN bei Frame 0 - kein Absturz, kein Fehler, das
Fenster meldete "Responding: False" bei 70 MB Speicher. `build_materials.py`
und das dritte Skript liefen nie.

Seither gilt: EIN Skript je Editorstart. `run_materials.cmd`
(UnrealEditor-Cmd, -run=pythonscript) und die Einzelstarter (voller Editor,
ein `py`-Befehl, Skript beendet den Editor selbst per `quit_editor()`) sind
beide erprobt; nur die Kette ist es nicht.

Erkennungszeichen fuer den naechsten haengenden Lauf: Im Log nur noch
`LogDerivedDataCache ... Maintenance` im Stundentakt, Frame-Zaehler bleibt
`[  0]`, und `Get-Process UnrealEditor | Select Responding` sagt False.

Zweites Erkennungszeichen, andere Ursache: Im Kommandozeilenlauf schrieb
`unreal.log()` die Marker ins PROJEKTLOG (Saved/Logs/WiesbadenReal.log),
nicht in die umgeleitete Standardausgabe. Ein leeres materials.log heisst
also nicht, dass nichts geschah - erst beide Stellen pruefen, dann urteilen.

## chk_seite war die ganze Zeit die RECHTE Seite

Eine Stunde Fehlersuche an der Tuer-Plakette, die "verschwunden" war -
dabei zeigte das Kontrollbild namens `chk_seite` von Anfang an die RECHTE
Fahrzeugseite (Kamera bei -Y), und die rechte Plakette war absichtlich
entfernt. Die linke Tuer, auf der die Plakette laengst sass, war auf keinem
der drei Kontrollbilder zu sehen.

Regel: Kontrollbilder beschriften, WOHIN sie schauen, nicht wie sie heissen.
Ein Name wie `seite` ist eine Behauptung ohne Richtung - `links`/`rechts`
haetten den Irrweg verhindert.

## Der Kachelversatz wurde zweimal abgezogen - und die Haelfte funktionierte

Beim Herbie-Backen verschiebt main() die UDIM-UVs vor dem Backen in die
Grundkachel. Der Plakettenrasterer zog denselben Versatz DANACH noch einmal
ab. Ergebnis: Kachel-0-Inseln (Versatz 0) funktionierten, alle anderen
landeten links ausserhalb des Bildes - "Tuer geht, Haube geht nicht", was
nach allem Moeglichen aussah, nur nicht nach einem Versatzfehler.

Wenn ein ortsabhaengiger Fehler GENAU entlang einer Datenstruktur-Grenze
verlaeuft (hier: je UV-Kachel), zuerst nach doppelt angewandten
Transformationen suchen. Der Zaehlertrick, der ihn fand: je Ablehnungsgrund
einen Zaehler ("box_x: 96105, ok: 590") - der Knotengraph davor bot keine
einzige ablesbare Zwischenzahl.

## Blender-Knotengraphen sind nicht debugbar - rastern statt raten

Die 53-Plaketten liefen erst ueber Cycles-Materialknoten (Projektion,
Baender, Masken als Node-Geflecht). Als sie verschwanden, gab es NICHTS zum
Nachsehen: kein print, kein Zwischenwert, nur ein schwarzes Backergebnis.
Drei blinde Reparaturversuche spaeter wurde derselbe Algorithmus in ~80
Zeilen Python/numpy neu geschrieben (Dreieckstest, Barycentrik, Alpha-Mix) -
mit Treffer- und Pixelzaehlern, und der eigentliche Fehler (doppelter
Kachelversatz) fiel beim ERSTEN Lauf auf.

Faustegel: Logik mit mehr als zwei Bedingungen gehoert nicht in einen
Knotengraphen, wenn es einen gleichwertigen Python-Weg gibt.

## Dieselbe Falle zum zweiten Mal: die 20-Bilder-Drossel

Auf "wie ist die Bildrate?" habe ich heute drei Diagnoselaeufe gemacht und
gemeldet: 9-10 Bilder/s im Streaming, 19 Bilder/s in der geladenen Stadt,
"Bildrate ist das dringendste Problem". Anschliessend die Fassaden-Shader
per Differenzmessung geprueft:

    Fassaden-Shader:    52,1 ms   Grafikkarte 49,8 ms
    Fassaden einfarbig: 50,8 ms   Grafikkarte 49,1 ms

Kein Unterschied - und die Grafikkarte lag in JEDER Messung des Tages bei
49-51 ms. 50,0 ms sind exakt 20 Bilder/s. Das ist kein Messergebnis, das ist
ein Deckel.

Unreal drosselt Fenster OHNE FOKUS auf 20 Bilder/s, und jeder
`-unattended -windowed`-Lauf hat keinen Fokus. Der Abschnitt
"Leistungsmessung im unfokussierten Fenster ist keine" steht seit Wochen
weiter oben in dieser Datei - von mir geschrieben, nach genau demselben
Irrweg.

**Erkennungszeichen, an dem es sofort auffaellt:** wenn eine Kennzahl ueber
voellig verschiedene Konfigurationen hinweg IDENTISCH bleibt, misst man nicht
die Konfiguration. Beim ersten Mal waren es "142,6 ms in drei verschiedenen
Welten", diesmal "49-51 ms mit und ohne Fassaden-Shader". Diese Probe kostet
nichts und haette beide Male eine Stunde gespart: zwei Laeufe vergleichen,
die sich stark unterscheiden MUESSEN.

Vor jeder Leistungsaussage also: eine Konfiguration messen, von der man
weiss, dass sie deutlich schneller sein muss. Bewegt sich die Zahl nicht,
ist die Messung ungueltig - egal wie plausibel sie aussieht.

## get_editor_property liefert bei STRUKTUREN einen Verweis, keine Kopie

Der Stadtneubau starb nach 13 Sekunden mit EXCEPTION_ACCESS_VIOLATION,
gelesene Adresse `0x...fff8`, im Callstack `Copy<FBuildingGenerationSettings>`
aus dem PythonScriptPlugin.

`Tools/build_city.py` machte dreierlei in dieser Reihenfolge:

    values[name] = source_builder.get_editor_property(name)   # 1. lesen
    LES.new_level(TARGET)                                     # 2. Level wechseln
    builder.set_editor_property(name, values[name])           # 3. schreiben

Schritt 2 zerstoert den Quell-Actor. Bei einfachen Werten - Zahlen,
Wahrheitswerten, Zeichenketten - ist das folgenlos, denn Python haelt eigene
Objekte. Bei STRUKTUREN haelt Python einen Verweis in den Speicher des
besitzenden Objekts. Nach dem Levelwechsel zeigt er ins Freigegebene.

Deshalb ist der Fehler jahrelang nicht aufgefallen: er trat erst auf, als
eine der kopierten Strukturen eine TMap enthielt
(`FBuildingGenerationSettings::PromptFacadeVariantWeights`). Flache Strukturen
ueberstehen den Griff ins tote Objekt oft unbemerkt.

**Abhilfe:** ein Objekt anlegen, das keinem Level gehoert, und die Werte
dorthin kopieren, SOLANGE die Quelle lebt:

    holder = unreal.new_object(unreal.WiesbadenWorldBuilder)
    holder.set_editor_property(name, source.get_editor_property(name))
    LES.new_level(TARGET)
    builder.set_editor_property(name, holder.get_editor_property(name))

**Was NICHT geht:** `copy.deepcopy` auf Unreal-Strukturen. Das meldet sauber
`TypeError: cannot pickle`. Mein erster Test dafuer war damit untauglich und
hat die Frage nicht beantwortet, sondern nur so ausgesehen.

**Probe fuer den naechsten langen Lauf:** eine Kopie des Skripts, die
unmittelbar vor dem teuren Aufruf mit `raise SystemExit(0)` anhaelt. Der
Trockenlauf kostet 30 Sekunden und durchlaeuft dieselbe Absturzstelle wie der
40-Minuten-Lauf.

## 16 Bit Heightmap: das Gelaende wird oben still abgeschnitten

Unreal speichert Landscape-Hoehen als `uint16`. Die Umrechnung lautet

```
worldZ = (value - 32768) / 128 * ZScale
```

Daraus folgt eine harte Obergrenze:

```
maxHoehe = ZScale * 32767 / 128
```

Bei der Projektvorgabe `ZScale = 100` waren das **25.599,2 cm**. Bezugshoehe
117 m, also alles ueber **373 m ueber Null**. Der Taunuskamm liegt darueber -
die Hohe Wurzel misst 618 m. Er wurde zu einer waagerechten Platte.

**Wie es auffiel:** Der Nutzer meldete "auf der Platter Strasse Richtung
Taunusstein sitzt die Fahrbahn nicht im Terrain, sondern in der Luft". Die
Messung ueber alle 22.227 Kreuzungen ergab 1.612 Faelle mit ueber 10 m
Abweichung - und an **jeder einzelnen** meldete das Gelaende denselben Wert
25.599 cm.

**Die Erkennungsregel:** Ein Messwert, der an weit auseinanderliegenden Orten
exakt gleich ist, misst nicht den Ort, sondern einen Anschlag. (Verwandt mit
der 20-Bilder-Drossel weiter oben: dort war es eine Bildrate, die sich ueber
voellig verschiedene Einstellungen nicht bewegte.)

Die Strassen waren die ganze Zeit richtig - sie nehmen ihre Hoehe direkt aus
dem Hoehenmodell. Nur das Gelaende unter ihnen war gekappt.

**Behoben** in `WiesbadenWorldBuilder::CreateLandscapeFromTile`: die ZScale
wird jetzt aus dem groessten vorkommenden Betrag berechnet und notfalls
angehoben, statt still zu klemmen. Vorgabe von 100 auf 300 erhoeht. Der Preis
ist Stufenhoehe `ZScale/128` = 2,3 cm - das Quellmaterial SRTM liefert ganze
Meter, also kein Verlust. Festgehalten in
`WiesbadenReal.GIS.TerrainGenerator.Hoehenbereich`.

## `new_level()` schlaegt still fehl - und misst dann die alte Karte

`LevelEditorSubsystem.new_level(pfad)` gibt bei bereits vorhandenem Asset
`False` zurueck und schreibt

```
LevelEditorSubsystem: Error: NewLevel. Failed to validate the destination.
An asset already exists at this location.
```

ins Protokoll - **ohne Ausnahme**. Wird der Rueckgabewert nicht geprueft,
laeuft alles Folgende in der noch geladenen Karte weiter.

Genau das ist passiert: Der Hoehenpruefer baute in die Spielkarte statt ins
Schmierlevel, mass gegen den dort gebackenen alten Landscape und lieferte
zweimal hintereinander **byteweise identische** Zahlen - die Reparatur war
laengst eingebaut, wurde aber nie gemessen.

**Die Erkennungsregel:** Zwei Durchlaeufe mit geaenderter Ursache, die exakt
dasselbe Ergebnis liefern, haben nicht dasselbe gemessen wie behauptet.

`Tools/check_heights.py` loescht das Schmierlevel jetzt vorher, prueft den
Rueckgabewert, prueft die tatsaechlich geladene Karte und bricht ab, wenn
noch ein Landscape in der Welt liegt. `FWiesbadenHeightAudit` meldet
zusaetzlich, gegen wie viele Gelaendeflaechen gemessen wurde.

- **Der WorldBuilder in der Spielkarte traegt noch absolute `ssonn`-Datenpfade** (`C:/Users/ssonn/aivideo/.../wiesbaden.osm.json`); `build_city()` scheitert damit nach 0,1 s mit "Datei nicht gefunden" und der Audit misst ein leeres Netz. `check_heights.py` biegt osm/alkis/dem/road-config vor dem Bau auf `Paths.ProjectDir()` um (Commit 7644dc6). Dieselbe stale-Pfad-Falle betrifft alle Tools, die den Karten-WorldBuilder nutzen.
- **Der Audit misst `Intersection.Location.Z` (= `SampleHeightCm(Knoten)+20`), NICHT die sichtbare Kreuzungsplatte** (deren Mitte = `Centroid.Z` der Arm-Tore). Die ~14 Ausreisser >2 m sind geneigte Platten an Steilhaengen (Aartal/Taunus); Mittel 35,9 cm ist top. **Sackgasse:** `Location.Z` auf den Plattenschwerpunkt zu setzen REGRESSIERT massiv (Bruecken/Tunnel-Arme mit Layer-Offset 550 mitteln zu Muell -> 17 Punkte >10 m). Echter Fix = Kreuzungen geometrisch flach graden + Zufahrten rampen (nur mit Sichtpruefung).

## Zwei Fallen des Bash-Werkzeugs unter Windows

**Hintergrundbefehle starten im Projektstamm**, nicht im zuletzt per `cd`
gesetzten Verzeichnis. `cmd //c run_tests.cmd` meldete
`ist entweder falsch geschrieben oder konnte nicht gefunden werden` - und der
Rahmen meldete trotzdem **Exit 0**, weil das nachgestellte `echo` erfolgreich
war. Immer absolute Pfade verwenden und die Ausgabedatei lesen.

**Backslashes ueberleben ein Heredoc nicht.** `\n` in einem
`cat > x.py <<'PY'`-Block kam als `\n` an, das Python dann als Zeilenumbruch
las - der Anker traf nie. Loesung: Backslashes aus Zeichencodes bauen
(`BS = chr(92)`) statt sie zu schreiben.
