# AGENTS.md — Projekt-Wissen für künftige Sessions

Nicht-offensichtliche Fakten, die sich nicht aus Code/Doku rekonstruieren lassen.

## Umgebung & Build

- **Neuer Rechner (ab 2026-09-01, Benutzer HP):** einzige Arbeitskopie unter `C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal`; verbindliche Engine ist die Launcher-Installation **5.8.2 (CL 56702186)** unter `C:\Program Files\Epic Games\UE_5.8` - alle `.cmd`/`Tools`/Doku zeigen dorthin. Die Plattenkopie `C:\freebuff\WiesbadenReal_Sicherung\UE_5.8` (5.8.1, CL 56057345) bleibt nur als Rueckfall (dann Rebuild noetig, da `Intermediate`/`Binaries` gegen 5.8.2 gebaut sind). Build HIER moeglich (VS 2022 + MSVC 14.44 + Win11-SDK 26100). Die alten `ssonn`/`aivideo`-Pfade und BEIDE Worktrees (`D:\freebuff_city_wi`, `C:\Users\ssonn\aivideo\WiesbadenReal`) existieren hier NICHT; `verify-worktree-sync.mjs` ist damit gegenstandslos.
- **FALLE: die beiden UE-5.8-Baeme niemals mischen (Symptom 2026-09-17).** Baut man den Editor mit der Plattenkopie (`C:\freebuff\...\UE_5.8`, 5.8.1) statt mit der Launcher-Installation (5.8.2), scheitert JEDE Uebersetzungseinheit mit Typneudefinitionen in Engine-Headern: `GenericPlatform.h error C2953 "SelectIntPointerType" ... bereits definiert`, `error C2011 "FGenericPlatformTypes"`, `fatal error C1189: #error: PLATFORM_32BITS should not be defined`. Die `note: Siehe Deklaration`-Zeilen nennen dann `C:\Program Files\Epic Games\UE_5.8\...` als ERSTE Deklaration - das ist der Fingerzeig. Ursache ist NICHT der Code, nicht UBA und nicht die Toolchain: das **gemeinsame PCH liegt im Projekt-`Intermediate`** und traegt die Headerpfade der Engine, mit der es erzeugt wurde; der andere Baum parst dieselben (bytegleichen) Header ein zweites Mal. Weder `-NoUBA`, noch das Loeschen der Projekt-PCHs hilft dauerhaft - nur derselbe Baum. Praktisch: `Tools/build_gate1.cmd` ruft die installierte Engine; bei einem Baumwechsel vorher `WiesbadenReal\Intermediate\Build\...\*.pch` loeschen. PCH-Wiederverwendung ist danach stabil (inkrementeller Lauf: 0 Aktionen, `Result: Succeeded`).
- git-Repo vorhanden (Branch `main`); `.gitignore` haelt `Content/__ExternalActors__` (26 GB gebackene Karte), `Data/Raw`, UE-Build-Ausgaben und `*.log` draussen.
- **Speicher/Pagefile (am Rechner verifiziert 2026-09-25):** 31,0 GiB RAM (33.307.574.272 B), `AutomaticManagedPagefile=False`, feste `C:\pagefile.sys` mit 98.304 MB (96 GiB) Initial **und** Maximum. Genau das ermoeglicht den Voll-Bake im Editor (Speicherspitze ~19 GB; der Commandlet-Weg starb an 42 GiB virtuell). Merksatz dazu: eine Pagefile-Aenderung wirkt erst NACH einem Neustart - im Thread-Export steht deshalb noch "die 96-GiB-Datei laeuft noch nicht, aktuell 64-GB-Stand"; das ist ueberholt. Vor groesseren Laeufen nicht aufraeumen (kein Smaller-Memory-Tuning), die 96 GiB sind der Grund, dass der Bake ueberhaupt durchlaeuft.
- **Vor jedem Editor-/Commandlet-Lauf die Prozessreste raeumen (Messung 14.09.2026):** `zenserver` + alle `UnrealEditor*` beenden, 3 s warten, dann starten. Back-to-back-Betrieb blaeht die Ladezeit auf ~8 min auf; aus dem sauberen Zustand war derselbe Start nach **~94 s** durch. Eine "haengende" Ladezeit ist also fast immer ein Restprozess und kein Projektproblem - und die 94 s sind die Vergleichsgroesse, mit der man einen echten Regressionsverdacht erst ausschliesst.
- `WiesbadenReal.Build.cs` setzt **`bUseUnity = false`**: gleichnamige anonyme-Namespace-Helfer in mehreren `.cpp` (`FLatLon`, `MakeLane`, `WriteTempText`, `Dt`, `NextNoise`, `SamplesPerPush`, `BytesPerSample`) kollidieren, sobald der Unity-Build TUs zusammenfasst; neue Quelldateien verschieben die Chunk-Grenzen und decken latente Kollisionen auf. Non-Unity kompiliert sauber (Projekt IWYU-tauglich) - nicht ohne Dedup dieser Helfer wieder einschalten.
- **Warnungen sind FEHLER** (u. a. C4458 Variablen-Shadowing bricht den Build ab). Falle: `AGameModeBase::GameState` ist ein Member (der AGameStateBase*-Actor) - eine lokale `GameState` in einer GameMode-Methode verdeckt ihn -> Build-Abbruch. Lokale anders benennen (`GS`).
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

- **Grounding: es gibt KEINEN Punkt-zu-Punkt-Wegfinder (am Code geprueft 2026-09-25).** Vorhanden ist nur der Spur-Graph `FRoadNetwork::GetSuccessors`/`LaneSuccessors` (`GIS/RoadNetworkTypes.h`) fuer die Fahrzeugbewegung. `FWiesbadenTrafficSimulation::FindPathCrossing`/`FindPathProximity` sind **Konflikt-Geometrie** (Ueberschneidung zweier Fahrwege), KEIN Routing - der Name taeuscht. Kein OpenSet, kein CameFrom/Reconstruct, kein A*, kein GPS-Routing im ganzen Baum. Eine notierte "GPS-A*-Idee" ist Absicht, kein Code: jede neue Routen-Aufgabe (Bus ueber seine Halte, Polizei-Verfolgung, Lieferroute) beginnt mit einem Graphen-Aufbau plus Kostenfunktion, nicht mit dem "Einschalten" von etwas Vorhandenem.

## Flug & Audio

- Heli-Rotor-Physik (`FWiesbadenRotorPhysics`): `bCoaxialRotors` = gegenlaeufiger Doppelrotor (ka-52-Stil, verdoppelter Auftrieb, kein Heckrotor, Pedal = direktes Yaw-Moment); `MaxForwardSpeedMetersPerS` + `RetreatingBladeStallStartFrac` modellieren den Blattspitzenverlust (LiftScale -> 0.45 an vmax) statt hartem Speed-Clamp. `EngineRpm = MainRotorRpm * EngineToMainRotorRatio` (0 bei Triebwerk aus) speist Audio/HUD.
- Flugsound: `UWiesbadenHelicopterAudioComponent` spielt Assets (RotorSound/EngineSound) mit RPM-/Last-Parametern ODER den prozeduralen Fallback `FWiesbadenHelicopterAudioModel` (deterministischer xorshift-PRNG, Seed-Parameter -> Automation-/node-tests): Rotor = Rauschen durch One-Pole-Tiefpass (Cutoff steigt mit RPM+Collective) + Wop-Wop-AM mit Blattpassfrequenz, Motor = Ton RPM/60*8 Zylinder. Samples als int16-PCM in `USoundWaveProcedural::QueueAudio` (vorher `GetAvailableAudioByteCount()` gegen Pufferdrift pruefen); `USoundWaveProcedural::NumSamplesToGeneratePerCallback` ist protected - nicht setzbar. `BladeSlapDepth` (AM-Tiefe) + `RotorCutoffBaseHz` (Basis-Cutoff) erzeugen den Kampfheli-Charakter; der Heli nutzt Koaxial-Konfiguration (bCoaxialRotors=true, zweiter gegenlaeufiger Rotor, BladeCount=3).
- **Ka-52-Modell (Neubau 2026-09-17, Import `/Game/Vehicles/Ka52`):** `AWiesbadenHelicopter` bindet die drei Meshes `Fuselage`/`Rotor_Upper`/`Rotor_Lower` ueber `ConstructorHelpers` (Pfade muessen exakt `<Paket>.<Objekt>` sein, sonst faellt der Actor still auf den Wuerfel-Rueckfall zurueck). Das Modell ist FERTIG skaliert und traegt seine Weltlage EINGEBAKEN: Rotor-Achse in Mesh-XY fast bei (0,0) - der gemessene Drehpunkt der Scheiben liegt bei (-0,19;+2,59) bzw. (+4,92;-2,06) cm und wird von `ComputeRotorMountOffset` auf (0,0) gelegt (siehe Ka-52-Abschnitt am Dateiende); Boden bei z=0, Rumpf 0..295 cm, Naben z 495 / 376,5 cm. Deshalb gibt es keine Median-Offsets mehr: der Hub-`USceneComponent` traegt nur die Nabenhoehe (495 / 376.5), das Blatt-Mesh darunter exakt `-HubHeight` - dann rotiert die Geometrie geometrisch um die Mastachse, ohne dass ein Offset nachgefuehrt werden muss. `TailBoomMesh`/`TailFinMesh`/`TailRotorBlade` sind beim echten Mesh unsichtbar (Heck und Stummelfluegel stecken im Rumpf-Mesh). Import + Materialkette: `Tools/import_ka52.cmd` (FBX, Nanite, keine Import-Bones) und `Tools/fix_ka52_materials.cmd` (4 PBR-Texturen -> `M_Ka52PBR` -> ALLE Material-Slots der drei Meshes; der FBX-Import legt sonst leere Tripo-Restmaterialien an - der Rumpf hat 10 Slots). **FALLE (2026-09-17):** `AssetTools.create_asset` legt ein Material nur IM SPEICHER an - ohne zusaetzliches `EditorAssetLibrary.save_loaded_asset(mat)` wird `M_Ka52PBR.uasset` NIE geschrieben, waehrend die Meshes sehr wohl gespeichert werden und danach auf ein nicht existierendes Asset zeigen (grauer Rumpf, Texturen ungenutzt). Der Commandlet-Log meldete trotzdem "M_Ka52PBR verdrahtet" und Erfolg. Beleg war nur der Plattencheck (`ls Content/Vehicles/Ka52/M_Ka52PBR.uasset`). Verifikation: `Tools/verify_ka52.cmd` (Meshes/Bounds in /Game) und `Tools/verify_ka52_actor.cmd` (CDO des Actors - welche Meshes an welchen Komponenten haengen, Nabenhoehen, XY-Abstand Blatt<->Mastachse = 0; Ergebnis `Saved/Diagnose/ka52_actor.txt`, weil Python-prints im Commandlet-Log verschwinden koennen). Dauerhaft abgesichert im Automation-Test `WiesbadenReal.Vehicles.HelicopterModell` (`Tests/HelicopterModelTest.cpp`, Editor-Kontext): Mesh-Namen an den Komponenten (faengt ConstructorHelpers-Tippfehler, die still auf den Wuerfel-Rueckfall gehen), Materialname je Slot, Rumpfmasse 14,1 x 8,7 x 2,95 m, Sohle auf 0 sowie Blatt-z = -Nabenhoehe und XY-Abstand 0 zur Mastachse.

- **`FMath::FInterpTo(x, 0, dt, Speed)` gibt bei `Speed<=0` SOFORT das Ziel (0) zurueck** (UE-Quelle). Die Heli-Ratendaempfung setzte `Speed = RateAssist*Neutral`, `Neutral=0` bei vollem Ausschlag -> Gier-/Nick-/Rollrate wurde JEDES Bild auf 0 gerissen (Giermoment war korrekt 360k N*m, nur die Rate genullt; Fehlerbild "Heli giert nicht" trotz richtiger Autoritaet). Fix: Daempfung nur bei `Neutral>epsilon`. Danach `CoaxialYawAuthority` 60000->16000 (sonst >400 Grad/s statt ~30-80).
- Externe Steuerung: `AWiesbadenHelicopter::SetExternalControl(FWiesbadenHeliControl)`/`ClearExternalControl` ist der saubere Eingang (KI/Zwischensequenz/Replay/Test), wirkt ueber die echte Rotorphysik; `ReadInput` wendet ihn nur an, enthaelt sonst NULL Test-Code. Test-Choreografie (Gierprobe/Flugprofil) liegt in `UWiesbadenVehicleTestHarness` (UActorComponent), das die Dev-Befehle zur Laufzeit auf dem Heli anlegen - im normalen Spiel existiert es nicht.
- Dieselbe Naht am Fahrzeug: `AWiesbadenCar::SetExternalControl(FWiesbadenCarControl)`/`ClearExternalControl` (Throttle/Brake/Steering/bHandbrake/bReverse). `ReadInput` prueft `bExternalControlActive` GANZ oben und umgeht dann die Tastenabfrage (glaettet die externen Werte per FInterpTo wie eine echte Eingabe), `ApplyVehiclePhysics` zieht die Handbremse ebenfalls aus dem Steuerwert. `UWiesbadenVehicleTestHarness` traegt jetzt Heli- UND Fahrzeugprofile (`Heli()`/`Car()`, getrennte Tick-Zweige); das Fahrprofil (`StartDriveProfile`) faehrt Vollgas geradeaus, dann Lenk-Sweep und misst die Kursaenderung wrap-sicher gegen den Startkurs (`FMath::FindDeltaAngleDegrees`). So beweist der Standard-Kaefer Laengsdynamik (0->~60 km/h, Gaenge 1->3) und Lenkung ohne Tastatur.
- Helikopter-Autopilot `UWiesbadenHelicopterAutopilot` (UActorComponent, UNABHAENGIG vom Test-Harness, echte Spiel-KI): `FlyTo(WorldTarget)`/`HoldPosition()`/`Disengage()`, treibt den Heli per SetExternalControl. Kaskadierte P-Regler: Horizontalfehler->Ziel-Geschwindigkeit (gekappt)->Nick/Roll im Rumpf-Frame (nahe am Ziel geht die Ziel-Geschwindigkeit gegen 0 -> bremst = Position halten, EIN Gesetz fuer Anflug+Schweben); Hoehenfehler->Ziel-Steigrate->Kollektiv. **Nicht-offensichtlich:** Vorwaertsflug kippt den Rotor und KLAUT Vertikalschub -> der Heli sackt trotz vollem Kollektiv ab; reine Rueckfuehrung kommt zu spaet. Fix = Auftriebs-VORSTEUERUNG `Collective += TiltLiftCompensation*(|Pitch|+|Roll|)`. **LOAD-BEARING (Audit-Fund):** der Heli bewegt sich KINEMATISCH (`AddActorWorldOffset`, keine Physik/MovementComponent/ComponentVelocity) -> `AActor::GetVelocity()` liefert **0**. Fuer jede Regelung/KI MUSS `GetVelocityMetersPerSecond()` genutzt werden (liefert das interne `Velocity`-Member in m/s), sonst ist die gesamte Geschwindigkeits-Daempfung wirkungslos und die Beruhigung passiert nur durch Rotor-Drag. **Zwei Folge-Fallen der ECHTEN Daempfung:** (a) der Rumpf darf sich nur WEIT weg (`FaceTargetMinDistanceMeters` ~60 m) zum Ziel giern - naeher dran wuerde die aktive Bremse im drehenden Frame tangential wirken -> Umkreisen; (b) der Rotor hat eine Anfahr-Totzone: sehr kleine Nick-Befehle bewegen ihn nicht -> ohne Gegenmassnahme bleibt er mit stationaerem Fehler kurz vorm Ziel stehen. **Update (ebab1d4):** die alte `MinApproachSpeed`-Untergrenze (~2 m/s bis zum Ankunftsradius) LOESTE das nur scheinbar - sie liess den Heli beim kleinsten Abdriften mit vollem Mindesttempo zurueckkicken -> ~15 m Halte-Grenzzyklus (Playtest-Messung, NICHT die frueher behaupteten <8 m). Jetzt: dreizoniges Profil (Ankunftsradius=0 / Settle-Band ohne Boden / Transit mit Boden, `SettleRadiusMeters` ~30) PLUS Integralterm auf dem Positionsfehler NUR im Settle-Band, mit Anti-Windup (bedingte Integration `IntegralFreezeSpeed`, laedt nur bei Stillstand + Leck `IntegralLeakTau`). Halte-Fehler dadurch Mittel ~13.4 -> ~6.8 m (meist im 8-m-Radius). NAIVES PI ohne Anti-Windup schwang mit ~37 m (Windup) - der Freeze/Leck ist load-bearing. Sluggisher Plant -> niedrige Geschwindigkeit (MaxApproachSpeed ~5, ApproachGain ~0.05) noetig, sonst ueberschiesst er (Bremsautoritaet gering). **Sackgasse (nicht erneut versuchen):** das lineare ApproachGain durch ein Bremsweg-Ankunftsgesetz (sqrt(2*a*d)) zu ersetzen wurde getestet und VERWORFEN - die Bremsautoritaet nahe Schweben ist zu schwach (~0.04 m/s2) und die Geschwindigkeit laeuft dem Befehl nach, das sqrt-Gesetz ueberschoss 16-52 m statt <8 m. Der Plant ist die Grenze, nicht das Regelgesetz; ein echter Fix braeuchte Geschwindigkeits-Vorsteuerung oder noch langsameres MaxApproachSpeed. Verifiziert: Ankunft <8 m, Hoehe +-1 m, konvergiert ohne Umkreisen. `GetAltitudeMeters()` (Raycast) fuer Regelung meiden (verrauscht).
- Kollektiv-Kennlinie am Heli auf `MaxCollectivePitchDeg=6`/`MinCollectivePitchDeg=3` gezogen (Modul-Default 2..14 gab bei vollem Hebel ~4x Gewicht -> >20 m/s Steigen); der Schwebepitch stellt sich per `ComputeHoverPitchDeg` selbst ein (lift=weight, ~4 Grad). `GetAltitudeMeters` ist ein Abwaerts-Raycast -> trifft Dachfirste, die Vario-/Hoehen-Anzeige rauscht entsprechend.

## Dev-Befehle & In-Game-Verifikation

- **`-ExecCmds` erreicht NUR die `ULocalPlayer::Exec`-Kette** (Engine/Private/Player.cpp, UE-Quelle): PlayerInput -> PlayerController (`ExecActor`) -> Pawn -> HUD -> GameMode -> CheatManager -> GameState -> CameraManager. NICHT die GameInstance/deren Subsysteme (die erreicht nur die In-Game-Konsole ueber `UGameViewportClient::Exec`). Deshalb liegen die Dev-Execs auf `AWiesbadenPlayerController` (in `WiesbadenGameMode` via `PlayerControllerClass` gesetzt), NICHT in einem Subsystem - ein `UGameInstanceSubsystem`-Exec feuerte per `-ExecCmds` nie.
- **`-ExecCmds` trennt Befehle per KOMMA, nicht Semikolon.** Semikolon macht alles zu EINEM Befehl (der erste schluckt den Rest als Argumente).
- Dev-Execs (AWiesbadenPlayerController): `WbTeleport <0-2>`, `WbResetVehicle`, `WbTraffic <0/1>`, `WbCam <0-2>`, `WbHeli`, `WbNudge <nick> <roll>`, `WbHeliYaw <s>`, `WbHeliFly <s>`, `WbDrive <s>` (Fahrprofil am besessenen Fahrzeug), `WbHeliGoto <dx> <dy> <dz>` (Autopilot fliegt <dx,dy,dz> m relativ und haelt), `WbHeliHover` (Autopilot haelt Position), `WbHeliOff` (Autopilot aus -> Steuerung zurueck an Tastatur, mitten im Flug loesbar). Datenreine Kernlogik (Teleportziele/Aufrichten) in `FWiesbadenDevActions` - teilt sich mit dem Pause-Menue (Test `WiesbadenReal.Dev.Actions`). `GetOrAddHarness(AActor*)` legt den Test-Harness on-demand auf Heli ODER Fahrzeug an; `GetOrAddAutopilot` analog fuer den Autopiloten. Autopilot-Nachweis im Log: `WbDev Autopilot t=N: Abstand X m ... Modus Anflug/Halten` + `Wegpunkt erreicht`. **Log-Kategorien nicht verwechseln:** die WbDev-Befehls-Echos laufen unter `LogWbCore`, die Autopilot-Fortschrittszeilen unter `LogWbVehicles` (anderes Cpp) - Grep entsprechend waehlen. **Doku-Kopplung:** `docs/reference/wbdev-konsolenbefehle.md` ist per `Tools/check_wbdev_docs.ps1` (erste, editor-unabhaengige Pruefung im Rauchtest) an den Code gekoppelt - wer einen Exec-Befehl umbenennt/entfernt oder eine `UE_LOG("WbDev:...")`-Zeile umformuliert, MUSS die Referenz mitziehen, sonst faellt der Rauchtest durch (Formatplatzhalter werden beim Vergleich ignoriert, nur der feste Wortlaut zaehlt).
- **Tastatur-Injektion (keybd_event/SendInput) erreicht das D3D-Spielfenster NICHT.** Verifikation laeuft ueber `-ExecCmds` + Log + `CopyFromScreen`-Screenshots (nur bei Fenster-Vordergrund; PrintWindow ist auf D3D schwarz). CopyFromScreen faengt bei aktiver Desktop-Nutzung leicht Fremdfenster ein.
- **VISUELLE Verifikation headless: UEs eigener `HighResShot`, NICHT OS-Capture.** `-ExecCmds="HighResShot 1600x900"` in einer `-game`-Fenstersitzung schreibt eine voll gerenderte PNG nach `Saved/Screenshots/WindowsEditor/HighresScreenshot0000N.png` (~1.5 MB, per Read-Tool lesbar) - funktioniert, obwohl OS-Fenster-Capture (CopyFromScreen/PrintWindow) auf D3D schwarz ist. Der Shot per `-ExecCmds` feuert spaet genug und erwischt die geladene Szene. Damit sind Materialien/Beleuchtung/HUD headless pruefbar (frueher faelschlich fuer unmoeglich gehalten).
- **`-ExecCmds` erreicht AUCH global registrierte Konsolenbefehle** (`IConsoleManager::RegisterConsoleCommand`), egal wo registriert - auch aus einem `UGameInstanceSubsystem` (z. B. `Wb.Buy`/`Wb.Store`/`Wb.Guthaben` des StoreSubsystems liefen so). Das WIDERSPRICHT nicht der Exec-Ketten-Regel oben: registrierte Konsolenbefehle sind ein eigener Pfad, NUR UFUNCTION-`exec` an Subsystemen bleibt unerreichbar. Semikolon-Fallstrick beachten (Komma-getrennt).
- **Projekt-Screenshot `-WbShot=<sek>`** (schreibt `Saved/Diagnose/Messstelle00000.png`) rendert nur zuverlaessig, wenn per **`.cmd`-Datei** gestartet (Muster `fps_alkis10.cmd`); ein direkter PowerShell-Launch `& $ue ... > log` ergab LEEREN Log + KEINEN Shot. `-WbShowMap` (+ `-WbMapZoom=N`) erzwingt die offene Weltkarte fuer Karten-Screenshots (headless kein Tasten-Input). Alternative zu HighResShot.
- Cockpit-Innensicht: keine 3D-Innenraeume modelliert. `UWiesbadenVehicleCameraComponent::AddCockpitHiddenMesh` blendet die eigene Aussenhaut fuer den Fahrer aus (`bOwnerNoSee`, nur seine Sicht), das HUD zeichnet die Instrumententafel. Cockpit-Kamera-Versatz je Fahrzeug im Konstruktor (Default `CockpitOffset(95,0,140)` + Kamera bei `(0,0,110)` ergab Z~250 = schwebte ueber dem Wagen).
- Rauchtest `Tools/smoke_test.cmd` (+ `.ps1`): ZWEI kurze Editorsitzungen (Helfer `Invoke-Session`), feuert Dev-Execs, wertet aus dem Log Bestanden/Durchgefallen (Exit 0/1). 7 Pruefungen: Fahren (WbDrive: Tempo>20 km/h + Kursaenderung>15 Grad am Standard-Kaefer), Materialien, Perf-Regression (Spiel-Strang-Zeit + Last-Inventar aus dem 8-s-Diagnoseblock gegen tunebare Schranken -`$MaxSpielMs`/`$MaxPrimComponents`/`$MaxInstances`-, bewusst ueber der Ist-Last, faellt nur bei Verschlechterung; nach WP-Fix enger ziehen), Teleport, ResetVehicle, HeliFly, HeliYaw. Zwei Sitzungen, weil Fahrzeug und Heli sich die Besitzung teilen (der Kaefer muss fuer WbDrive besessen bleiben, WbHeli entlaedt ihn). World-Partition-Eigenheiten: (a) beim Umherfliegen haengt das Spiel streckenweise ("Gamethread hitch waiting for resource cleanup"), also NICHT auf Demo-Ende/Fahrende warten, sondern auf GENUG Log-Messpunkte bzw. die Material-Bilanz (feuert 8 s nach dem Laden); (b) seit dem HISM-Overwrite-Fix laden zwei Sitzungen hintereinander wieder zuverlaessig.
- **Headless-FPS ist doch aus dem Log messbar:** der 8-s-Diagnoseblock (`UWiesbadenCitySubsystem`, feuert ungated `GeometryReportDelay>=8`) loggt `Bildzeit ueber N Bilder: Mittel X ms (Y Bilder/s) ...` PLUS den Strang-Split `Straenge im Mittel: Spiel X ms, Renderer Y ms, Grafikkarte Z ms`. (Frueher gesehene "28/144 FPS" waren das ANDERE Projekt "Wiesbaden Survivors" im Hintergrund, NICHT dieses Spiel.)
- **Der Engpass ist der SPIEL-STRANG, NICHT die GPU** (GPU langweilt bei ~3-8 ms; Nanite/LODs/Sichtweite braeuchten GAR NICHTS). Der Diagnoseblock loggt ein **Last-Inventar** (`LogGeometryBalance`): Primitive-Komponenten, Kollisionskoerper, Instanz-Komponenten + Instanzen gesamt.
- **KORREKTUR (A/B systematisch bewiesen): die ~6-10 FPS sind WORLD-PARTITION-STREAMING-HITCHES, NICHT residente Foliage-Last.** Der Streaming-Radius (`-WbRadius=N`-Diagnoseschalter in `AWiesbadenStreamingSource::BeginPlay`, ueberschreibt den in der Karte serialisierten Wert) gegen die Bildzeit gemessen: 6000 m -> 28 Chunks / 578k Instanzen / **27 Bilder in 8 s, Mittel 155 ms (6 FPS), 17 Aussetzer**; 800 m -> 16 Chunks / **560k Instanzen (fast gleich!)** / **306 Bilder, Mittel 13 ms (76 FPS), 7 Aussetzer**. Die Instanzenzahl aendert sich kaum (578k->560k), die Bildrate springt von 6 auf 76 FPS. Also: 560k Foliage-Instanzen resident kosten im STATIONAEREN Zustand ~nichts (76 FPS); die hohe Mittelbildzeit bei grossem Radius sind 100-400-ms-Hitches, weil mehr Chunks zum Messzeitpunkt noch einstreamen (Kollisions-Cook + HISM-Aufbau je Chunk). **Falscher Hebel:** Foliage-Dichte senken (Instanzen sind nicht das Problem). **Richtiger Hebel:** Streaming-Churn senken - kleiner Boden-Radius (~800 m; hoehenadaptiv, in der Luft gross fuer den Heli-Blick). Reduziert die Zahl gleichzeitig einstreamender Chunks -> weniger Hitches -> ~76 FPS. **GELANDET (1a8f34c):** der hoehenadaptive Radius ist umgesetzt - `GroundRadiusMeters` (900) am Boden, `StreamingRadiusMeters` (6000) ab Reiseflughoehe, smoothstep ueber die Baender `AdaptiveStart/FullAltitudeMeters` (60-350 m), Hoehe per Abwaerts-Trace vom Pawn. Boden-Last dadurch ~13-14 ms / 8832 Komponenten / 565k Instanzen (statt ~110-163 ms / 19700 / 1.07 Mio). `-WbRadius=N` erzwingt weiter FESTEN Radius (adaptiv aus) fuer die A/B-Diagnose. Perf-Regression-Schranken im Rauchtest koennten jetzt enger gezogen werden.
- **WURZEL des Streaming-Problems (headless bewiesen, Diagnose im 8-s-Block):** Die 664 Chunks bleiben trotz 2000-m-Radius ALLE resident, weil sie am **Ursprung (0,0,0) gespawnt** sind (`SpawnCityChunks`, `FTransform::Identity`) und ihre Vertizes in WELT-Koordinaten tragen. Dadurch spannen die Actor-Bounds jedes Chunks ~**3x4 km vom Ursprung BIS zur Geometrie** (Beispiel: Lage (0,0,0), Bounds-Mitte (-150k,-200k), Ausdehnung 3002x4001 m; 602 von 664 geladenen Chunks liegen >2 km vom Spieler). World Partition kann diese riesigen, sich am Ursprung ueberlappenden Actors NICHT raeumlich trennen -> Block-Laden. Deshalb ist der hoehenadaptive Radius wirkungslos UND feinere Grid-Zellen/HLOD allein bringen nichts (alles downstream der kaputten Bounds). **PRAEZISE Ursache (Komponenten-Diagnose):** NICHT die echte Geometrie spannt die Bounds - RoadMesh und die bestueckten Foliage-Varianten (`Trees_01..06`/`Bushes_01..06`) sitzen eng an der Zelle (~500 m). Die Spannweite kommt von **LEEREN Komponenten mit Punkt-Bounds bei (0,0,0)**: dem leeren `BuildingMesh` (Chunks ohne Gebaeude) und den drei ungenutzten Basis-HISMs `Trees`/`Waterfront`/`Industrial` (0 Instanzen; die echte Vegetation steckt in den Varianten). `GetComponentsBoundingBox` vereinigt diese Ursprungs-Punkte mit der Zell-Geometrie -> 3x4 km. **Minimaler Fix (kein Vertex-Neuauthoring):** leere Basis-HISMs/leeren BuildingMesh gar nicht erst erzeugen bzw. an der Zelle verankern / von den Streaming-Bounds ausschliessen (`bUseAttachParentBound`), dann Re-Bake (WP-Zellzuordnung ist gebacken). Danach (2) Runtime-Grid-Zellgroesse ~Chunkgroesse + adaptiver Radius reaktivieren; (3) HLOD-Proxies fuer den Luftblick. Nachweis: `Streaming-Diagnose: ... jenseits 2 km` muss von 602 -> ~0 fallen (die Komponenten-Diagnose feuert nur fuer Chunks >1000 m Bounds, also nach dem Fix still).
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

- **GitHub-Release-Download:** `gh release download` kann bei grossen Assets mit Exit 0 eine verkuerzte Datei hinterlassen; vor dem Entpacken immer SHA-256 und ZIP pruefen. Der HTTPS-Fallback braucht bei privaten Releases `GH_TOKEN`/`GITHUB_TOKEN` oder den Wert aus `gh auth token`.
- Worktree-Sync: `Tools/verify-worktree-sync.mjs` (node) vergleicht Source/Content/Tools byte-genau (SHA-1) zwischen diesem Worktree (`WiesbadenReal/…` + Root-`Tools/`) und dem gebauten (`C:\Users\ssonn\aivideo\WiesbadenReal\…`, Projekt = Root) und warnt bei Drift (Exit 1); `--sync` kopiert lokal → gebaut, `--built <pfad>` ueberschreibt das Ziel. Nur-gebaut-Dateien werden NIE geloescht. WICHTIG: Datei-Pfade in den Maps sind `{abs, sha1}`-Objekte, Vergleich nur ueber `sha1` (Pfade beider Bäume unterscheiden sich strukturell: Freebuff `WiesbadenReal/Source` ↔ gebaut `Source`; Tools ist die Vereinigung von Root-`Tools/` + `WiesbadenReal/Tools/`). Vor dem Vergleich laeuft der Drift-Guard `Tools/verify_cityprompt.mjs` (Port-Inventar vs. `GIS/CityPrompt.cpp`): Aendert der C++-Parser Regeln ohne parallelen Port-Update, bricht das Skript mit Exit 1 ab - auch bei `--sync` (blockiert, damit der veraltete Port nicht in den gebauten Worktree wandert).
- `python3` ist ein Windows-Store-Alias (funktioniert nicht). Auf dem neuen Rechner funktioniert aber `python` (echtes Python 3.14 unter `C:\Python314`); im alten Setup war `node` (v24) der einzige Ausweg. Fuer Skripte/JSON `node` oder jetzt `python`.
- Testlauf via `UnrealEditor-Cmd.exe` zuverlässig nur mit `-stdout` UND absolutem `-project=`-Pfad: ohne `-stdout` kann der Cmd in manchen Sessions stumm mit Exit 1 enden (leere Logs, kein Saved/Logs-Update), obwohl dieselbe Binary funktioniert; Ergebnis dann aus stdout greppen (`Test Completed. Result={...}`). Exit-Code ist AUCH mit `-stdout` unzuverlaessig (ein Lauf endete mit 255, obwohl alle 68 Tests Success waren - ein Test wurde uebersprungen) - gegen `grep -c "Result={Success}"` und die Registrierungszahl (aktuell 135 in Tests/, waechst mit neuen Features) pruefen, bei Abweichung neu laufen. WICHTIG: Seit die `DefaultEngine.ini` `GameDefaultMap=/Game/Maps/WiesbadenCity_Alkis4` setzt, laedt auch der Cmd zuerst die gebackene World-Partition-Map - das Log steht dabei mehrere Minuten auf `WorldPartition initialize started...` (CPU dreht, kein Test Started), dann laufen die Tests normal (aktuell 135/135); Abschluss-Marker im Log: `**** TEST COMPLETE. EXIT CODE: N ****`.
- `run_tests.cmd`/`build_only.cmd` rufen `Build.bat` jetzt per `call` auf (GEFIXT 2026-09-10): vorher OHNE `call` -> `Build.bat`s `exit /b` beendete das ganze Skript, es BAUTE nur und fuehrte NIE Tests aus (Log stoppte nach dem Build, kein `Saved/Logs`-Update, kein Editor-Prozess). Faellt das Muster erneut irgendwo auf: `call "...\Build.bat"` voranstellen, sonst laeuft der Testschritt nie.
- Editor headless detached: `Start-Process` mit EINZELNEN Argumenten zerlegt `-ExecCmds="Automation RunTests X; Quit"` falsch (Leerzeichen/`;` -> der Editor laedt "RunTests" als Map statt Tests zu fahren: hohe CPU, kein Testlauf, Saved-Log bleibt alt). Zuverlaessig: `System.Diagnostics.ProcessStartInfo` mit `Arguments` als EINEM exakt gequoteten String (Vorlage: `C:\freebuff\WiesbadenReal_Sicherung\.freebuff\launch_tests.ps1`).
- `CreateDefaultSubobject` in einem Actor-Konstruktor laeuft im Automation-Kommandlet-Kontext (`-ExecCmds=Automation RunTests`) in die Assertion `Element type 'Components' has not been registered!` (TypedElementRegistry) -> im Test keinen Actor per `NewObject` erzeugen, dessen Konstruktor Komponenten anlegt; Komponenten dort nullptr lassen und im Test null-sicher pruefen.
- **Release-Pipeline `Tools/build_release.cmd`** (lokal, kein GitHub Actions - UE-Build braucht die Engine): fail-fast Qualitaets-Gates VOR der Paketierung, kein Gate ueberspringbar. Reihenfolge (billig->teuer, shift-left): Gate 1 Kompilieren (`Build.bat WiesbadenRealEditor`) -> Gate 2 Unit-Tests (Automation, blockt bei Fail>0 / fehlendem `TEST COMPLETE. EXIT CODE: 0`, KEINE feste Zahl hartkodiert) -> Gate 3 Rauchtest (`smoke_test.ps1` Exit 0) -> nur bei GRUEN: `package_game.cmd` (BuildCookRun, Stunden) -> Desktop-Verknuepfung `Wiesbaden Real (Paket).lnk` per WScript.Shell auf `Saved/Package/Windows/WiesbadenReal.exe`. `-GatesOnly` laeuft nur Gates 1-3 (~10-15 min, vor dem Commit) und ueberspringt NUR das Paket, kein Qualitaets-Gate. **Rollback:** vor dem Ueberschreiben sichert die Pipeline das bisherige Paket nach `Saved/Package_previous`; `build_release.cmd -Rollback` tauscht in Sekunden aktuell<->vorher (Verknuepfung folgt, erneutes -Rollback rollt wieder vor) - macht eine rote Release umkehrbar. WICHTIG: Die Pipeline parkt die fremde `RailTransportTest.cpp` NICHT - solange die Rail-Arbeit nicht kompiliert, blockt sie korrekt an Gate 1 (das ist gewollt, kein Bug). **Falle:** PowerShell `-File script.ps1 -TippfehlerFlag` schluckt unbekannte Flags STILL und laeuft mit Vorgabewerten weiter - ein `-GateOnly` statt `-GatesOnly` loeste so den vollen, stundenlangen Release-Build aus. `[CmdletBinding()]` ueber dem `param()`-Block zwingt sofortige Abweisung ("kein Parameter ... entspricht", Exit vor Gate 1).
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

## Laufzeit-Diagnose-Warnungen koennen Fehlalarme sein - Gesamtbilanz zaehlt

Die Selbstdiagnosen im Log (Gebaeude-Kollision, Fussgaenger, Ampeln) feuern teils
false-positive; eine EINZELNE Warnung nicht fixen, erst die Session-Gesamtbilanz
pruefen. Beispiel: "Gebaeude-Kollision ... faehrt dort weiter hindurch" entsteht,
wenn der waagerechte Diagnose-Trace am HANG ansteigendes Gelaende (Landscape)
trifft, bevor er die Box erreicht - ueber die Session 58x "wirksam ... blockiert"
(Ueberdeckung gedreht 1.05x) gegen 1x Warnung = gesund. Fussgaenger "KEINER
gezeichnet" war ein Startup-Transient (danach animiert + "Ueberfahren: N
Fussgaenger"). Grep "wirksam" vs. Warnung, sonst repariert man Nicht-Bugs.

## Ampel-Kopplung "0 an Rot" war KEIN Bug - nur Ampel-Sparsamkeit (AUFGEKLAERT, 284daab)

Das gemeldete "1073 Ampeln, 0 Fahrzeuge an Rot / Kopplung greift nicht" war ein
DIAGNOSE-Fehlalarm, kein Kopplungs-Defekt. Instrumentierung der Stopp-Schleife
(Zaehlung StopZone -> signalisiert -> rot) zeigte: von 4-6 Fahrzeugen in der
Stopp-Zone erreicht fast KEINES eine signalisierte Verbindung (signalisiert~0),
weil nur ~1073 der ~20213 Kreuzungen Ampeln sind (~5%) - der Verkehr um den Spieler
quert fast nur ampellose Knoten. Die Kopplung ist korrekt (Unit-Test
`Traffic.RedLightStop`). Defekt war die Diagnose, die geometrische Ampel-Naehe mit
Versagen verwechselte. Fix: Sim summiert `LifetimeVehiclesApproachingSignal`/
`-HeldAtRed` (via `TrafficLights::IsConnectionControlled`); CitySubsystem urteilt auf
Fahr-Evidenz statt Geometrie (held>0 -> wirksam; wenige Anfahrten -> kein Fehler;
>=20 Anfahrten ohne Halten -> echter Defekt). "Ampeln 0.0 ms" bleibt ERWARTET
(Phase faul in IsConnectionGreen). MERKE (drittes Mal diese Sitzung): eine einzelne
Selbstdiagnose-Warnung erst gegen die Fahr-/Gesamtbilanz pruefen, bevor man sie glaubt.

## Werkzeug-Fallstricke (Ergänzung 11.09.2026): Zeilenenden bei Skript-Edits

Die UE-Quellen (`Source/**`, `AGENTS.md`) sind CRLF; einzelne Zeilen darin nicht
(fremde `str_replace`-Edits hinterlassen LF-only-Zeilen). Fallstricke:

- `Path.read_text()`/`write_text()` benutzen Universal-Newlines: beim Schreiben
  wird daraus LF -> git meldet die GANZE Datei als geändert (statt ~250 waren
  2060 Zeilen im Diff). In Skripten deshalb `read_bytes`/`write_bytes` (oder
  `newline=""` beim Öffnen) verwenden und das EOL des Ziels vorher messen.
- Unfall-Reparatur: `data.replace(b"\r\n", b"\n").replace(b"\n", b"\r\n")`
  (Vorlage: `.freebuff/fix_crlf.py`). Danach MUSS `git diff --numstat` wieder in
  der Größenordnung der echten Änderung liegen, sonst ist WIP unsichtbar zermahlen.
- `str_replace` trifft CRLF-Dateien nur, wenn der Suchstring die `\r` enthält.
- Verifikations-Muster fürs Umbenennen/Extrahieren ohne Compiler: Build über
  `cmd //c <absoluter Pfad>\build_only.cmd` (~25 s inkrementell), danach die volle
  Suite headless über `.freebuff/launch_tests.ps1` + Zählung der
  `Test Completed. Result={Success}`-Zeilen im Saved-Log (grün = 195).

## GameMode-Tick: Tastenflanke nur EINMAL pro Frame auswerten (11.09.2026)

`AWiesbadenGameMode::Tick` fragte `WasInputKeyJustPressed(InteractKey)` ZWEIMAL im
selben Frame ab (einmal in einem verworfenen Zweig, einmal im gewerteten). Die
Abfrage gilt fuer den ganzen Frame -> ein einziger Tastendruck loeste zwei Aktionen
aus; zusaetzlich blieb der Flankenmerker `bEntryKeyHeld` nach einer NPC-Interaktion
stehen und wechselte einen Frame spaeter doch noch ins Fahrzeug (Geister-Einstieg).
Regel: Tastenzustand einmal am Tick-Anfang lesen, in einer Lokalen halten, an alle
Zweige weitergeben; wer den Merker setzt, muss ihn im selben Tick konsumieren.

Die Auswahlregel "naechster Haendler in Reichweite" liegt jetzt als reine Funktion
`SelectMerchantInReach(...)` (static, ohne Welt/Pawn) bereit und ist damit ohne
`UWorld` testbar (Tests/StoreMerchantInReachTest.cpp, Muster wie
PickupSpawnerTest). Such-/Auswahlregeln von Seiteneffekten trennen: dann ist die
Regel ein billiger Unit-Test statt eines Integrationstests.

Testerwartung: volle Suite jetzt 197 Success (vorher 194/195), 0 Fail, EXIT CODE 0.

## Testnamen duerfen kein Praefix eines anderen Testnamens sein (11.09.2026)

Der Kommandozeilen-Runner sammelt nur BLATTKNOTEN des Testbaums
(`FAutomationReport::GetEnabledTestNames` sammelt nur Knoten mit
`ChildReports.Num() == 0`). Ein Testname, unter dem weitere Tests haengen, wird
zum Zwischenknoten und laeuft STILL nie - ohne Fehler, ohne Warnung, und die
Suite meldet weiter "0 Fail". Genau das war der Fall:

- `WiesbadenReal.Vehicles.HUD` (Eltern von ...HUD.ControlLegend, ...HUD.MapDistance)
  -> jetzt `WiesbadenReal.Vehicles.HUD.Instruments`
- `WiesbadenReal.Vehicles.CarLights` (Eltern von ...CarLights.AutomaticHeadlights)
  -> jetzt `WiesbadenReal.Vehicles.CarLights.Signals`

Damit liefen zwei Tests nie mit: 197 definiert, aber nur 195 ausgefuehrt (und
die Zahlen "194/195 gruen" der letzten Sitzungen waren entsprechend blind).
Invariante ab jetzt: definierte Testnamen == "Test Completed"-Zeilen im Log.
Pruefbefehl fuer beide Seiten:

    find Source -name "*.cpp" | xargs awk '/IMPLEMENT_(SIMPLE|COMPLEX)_AUTOMATION_TEST/{l=1;next} l==1 && match($0, /"[A-Za-z0-9_.]+"/) {print substr($0,RSTART+1,RLENGTH-2); l=0}' | sort -u | wc -l
    # und: grep -c "Test Completed" Saved/Logs/WiesbadenReal.log

Praefix-Kollisionen findet man am schnellsten, wenn man die Namen sammelt und
paarweise prueft, ob ein Name ein Praefix (mit Punkt) eines anderen ist.

## HUD-Haendlerhinweis: Actor und Spielerstandort, nicht Ursprung (11.09.2026)

`ResolveMerchantCue` konnte nie konkret werden:

- `CachedFootMerchant` (TWeakObjectPtr) wurde nie zugewiesen - der Suchlauf
  speicherte nur Entfernungen (`NearestOf` gab ein `double` zurueck).
- `DescribeNearestMerchantInReach` nahm einen Skalar `PlayerCm` und las ihn als
  X-Koordinate (`FVector(PlayerCm, 0, 0)`): gemessen wurde ab dem WELTURSPRUNG.
  Im Wiesbadener Massstab lag damit jeder Haendler ausserhalb der Reichweite,
  der Hinweis fiel dauerhaft auf "nah ran und F" zurueck.

Fix: der Suchlauf merkt Entfernung UND Actor (lokales `FNearest`), der Cue misst
ab dem Spielerstandort (`TryGetPlayerPlanarPos`) und benutzt die EIGENE
`InteractRangeCm` des Haendlers - dieselbe Regel wie
`AWiesbadenGameMode::PickMerchantInReach`, damit Hinweis und Interaktion
dieselbe Reichweite haben (ein Cue auf etwas, das F nicht erreicht, waere
schlimmer als keiner).

MERKE: der alte Test war gruen, weil er Haendler UND Spieler im Ursprung
annahm (`NewObject` ohne Wurzel + `PlayerCm = 0.0`) - er spiegelte den Fehler,
statt ihn zu finden. Eine Regression mit weit entferntem Spielerstandort steht
jetzt daneben.

## Build/Test-Fallstricke: DLL-Sperre, const-Outer, -ExecCmds-Quoting (17.09.2026)

**"Editor geschlossen" heisst nicht "baubar".** Der Link brach mit
`LNK1104 ... UnrealEditor-WiesbadenReal.dll kann nicht geoeffnet werden` und
davor `Link [x64] ... Exited with error code 9001. This action will retry without
UBA` ab - die UBA-Wendung verleitet zur falschen Faehrte (UBA war unschuldig,
auch der NoUBA-Lauf scheiterte genauso). Wahre Ursache: eine noch laufende
SPIELSITZUNG `UnrealEditor.exe <Projekt> -game /Game/Maps/...` (Elternprozess
explorer.exe), die das Modul-DLL geladen haelt. Die Kompilierung war laengst
gruen - nur der Link scheitert. Vor jedem Build pruefen:
`Get-CimInstance Win32_Process | Where-Object {$_.Name -match "Unreal"}`;
eine -game-Sitzung ist kein Rest des Editors und ueberlebt dessen Schliessen.

**`FindObject<T>()` verlangt in UE 5.8 einen NICHT-const `UObject*` als Outer**
(`UObjectGlobals.h`: `T* FindObject(UObject* Outer, FStringView Name, ...)`;
der `const UObject*`-Overload existiert nicht mehr). Ein `GetDefault<T>()`-Zeiger
ergibt in JEDER Zeile `error C2672: keine uebereinstimmende ueberladene Funktion`.
In Tests `GetMutableDefault<T>()` nehmen (nur lesen) - im Heli-Modelltest so geloest.

**`-ExecCmds` verliert seine Anfuehrungszeichen ueber `Start-Process -ArgumentList`.**
Aus `-ExecCmds="Automation RunTests X; Quit"` wird dann
`-ExecCmds=Automation RunTests WiesbadenReal.Vehicles; Quit -unattended ...`:
die Engine fuehrt NUR `Automation` aus, `RunTests` laeuft nie, `Quit` kommt nie -
der Editor idlet endlos und STILL (Log steht nach dem Asset-Registry-Scan still,
CPU ~40 %, kein `Cmd: Automation RunTests`-Eintrag). Beweis steht im Log unter
`LogInit: Command Line:` - fehlende Quotes dort. Immer ueber ein .cmd mit
`%1`-Argumenten starten (Quotes fest im Skript):
`Tools\run_automation_test.cmd <Filter> <Logname>` - Standard
`WiesbadenReal.Vehicles.HelicopterModell` -> `Saved\Logs\wb_test_heli.log`.
Gueltiger Lauf = Zeile `Display: Found N automation tests based on '<Filter>'`
UND am Ende `**** TEST COMPLETE. EXIT CODE: 0 ****`; fehlt die erste, lief nichts.

Stand 17.09.2026: Gate 1 gruen (`Result: Succeeded`, inkrementell ~3 s),
`WiesbadenReal.Vehicles.HelicopterModell` gruen und die ganze Fahrzeug-Batterie
`WiesbadenReal.Vehicles` 40/40 Success, 0 Fail, EXIT CODE 0. Der Modelltest
haelt die Ka-52-Bindung fest (Mesh-Namen, Material je Slot, Rumpfmasse,
`Blatt-z = -Nabenhoehe`, XY-Abstand 0 zur Mastachse). Einschraenkung: fehlt das
Mesh (frischer Checkout ohne `Tools\import_ka52.cmd`), meldet er nur einen
Hinweis statt Fehler. Seit 18.09.2026 schreibt er seine GEMESSENEN Ist-Werte als
EINE Zeile ins Log (`grep HelicopterModell-Ist`, Praefix `LogTemp: Display:`):
Mesh-Name je Komponente, Material je Slot als `Slots=10 [M_Ka52PBR x10]` (ein
leerer Slot faellt so auch bei gruenem Lauf auf), Rumpfmasse + Sohle,
Nabenhoehe/Blatt-z/XY-Offset je Rotor - ein gruener Lauf ist damit ohne
Quervergleich lesbar. Der Mesh-fehlt-Fall steht als
`=OHNE_MESH ... (ohne importiertes Mesh - nur Hinweise)` da.
**FALLE:** `AddInfo` erreicht den kopflosen `Automation RunTests`-Lauf NICHT
(`BeginEvents:`/`EndEvents:` bleiben leer; in einem alten Mehrfach-Lauf tauchten
AddInfo-Zeilen noch auf) - Zusammenfassungen per `UE_LOG` schreiben.

## Bus-Mitfahrt: der Anker trug die MESH-Rotation - die Kamera stand neben dem Bus (17.09.2026)

Gemeldet war: "als Fahrgast im Bus sieht man nichts und es scheint, als wird man mit
dem Bus in die Luft teleportiert". Zwei Ursachen, beide in `WiesbadenBusRoute`:

1. **Der Mitfahr-Anker bekam die Komponenten-Rotation, nicht die Fahrtrichtung.**
   `RideAnchor->SetWorldLocationAndRotation(loc, Bus->GetComponentQuat())` uebernimmt
   `Dir*MeshOrient` - MeshOrient ist die glTF-Korrektur des Imports (Yaw -90). Im
   Anker zeigte damit +X nach LINKS statt nach vorn (die Wagenlaengsachse liegt auf
   +Y). Die Fahrgast-Offsets (X = nach vorn) zeigten 90 Grad quer: Die Cockpit-Kamera
   sass rund 2-3 m NEBEN dem Bus in der Luft und flog mit - genau das gemeldete
   "in die Luft teleportiert". Fix: `Car->GetComponentQuat() * MeshOrient.Quaternion().Inverse()`
   an BEIDEN Stellen (Einsteigen + Tick), damit Anker +X = Fahrtrichtung, +Y = rechts,
   +Z = oben - dasselbe Bezugssystem wie `WiesbadenBusInterior`. Merksatz: bei
   glTF-Importen NIE die rohe Komponentenrotation als Fahrzeug-Bezugssystem nehmen.
   **Nachweis im Log** (nicht im Bild): `Bus-Mitfahrt: ... Anker-Yaw 94.4, Blick-Yaw
   94.4 (Versatz 0.0); Auge im Wagen X=172 (vorn) Y=73 (rechts) Z=130`. Der Versatz
   muss ~0 sein, und das Auge muss vorne rechts sitzen - vertauschte Achsen zeigen
   sich als (73, 220).

2. **Der Bus hat keinen Innenraum - und die Kamera sass zusaetzlich in der Spielfigur.**
   `SM_Bus` ist ein reines AUSSEN-Modell (Tripo/OBJ, `convert_bus.py`); in der
   geschlossenen Huelle sieht man von innen nichts (Rueckseiten) und von der Stadt nur
   zufaellig. Die uebrigen Fahrzeuge loesen das mit `AddCockpitHiddenMesh` (eigene Haut
   fuer den Fahrer aus) + HUD-Instrumente; ein Bus braucht dagegen SICHTBARE Waende.
   Deshalb: `World/WiesbadenBusInterior` (datenrein, Test `WiesbadenReal.Traffic.BusInterior`)
   baut den Innenraum als Kaesten in Wagenkoordinaten; `AWiesbadenBusRoute` legt ihn als
   `UProceduralMeshComponent` an den Anker (folgt dem Bus, nur waehrend der Mitfahrt
   sichtbar) und blendet Haut + Zielschilder fuer den Fahrgast aus. Fenster bleiben
   ABSICHTLICH offen (keine Scheiben) - sonst sieht der Fahrgast die Stadt nicht.
   Die aufrecht stehende Spielfigur fuellt sonst das Bild, weil die Fahrgastkamera auf
   SITZHOEHE (1,30 m) mitten in ihrem Brustkorb sitzt: beim Einsteigen
   `Pawn->SetActorHiddenInGame(true)`, beim Aussteigen wieder false, und die
   ausgeblendeten Meshes mit `RemoveCockpitHiddenMesh` zurueckholen (ohne diesen
   Gegenpart bliebe der Bus dauerhaft durchsichtig).

- **Beleg statt Vermutung:** `shot_busmitfahrt.cmd <Karte> <ParkStop> <Shot s> <Ride s> <Quit s>`
  startet `-WbZuFuss=5 -WbBusRide=<s>` (Entwicklungshilfe, vorbildlich `-WbMitfahr`:
  steigt selbst in einen gerade haltenden Bus ein, weil Tastendruecke in automatischen
  Laeufen nicht ankommen) und legt `Saved\Diagnose\Messstelle00000.png` ab. Der
  Fahrgast-Abschnitt der .cmd MUSS ueber die .cmd-Datei laufen (leerer Log sonst, siehe
  `-WbShot`-Eintrag oben).
- **Nebenbefund:** `-WbBusParkStop` kehrte frueher SOFORT aus dem Tick zurueck - damit
  liefen Anker, Mitfahrt und Dev-Einsteigen im Parkbetrieb nicht, an einem geparkten Bus
  war kein Einstieg moeglich. Jetzt laeuft nur die Fahrplan-/Verteilungsschleife nicht.
- **Messwerte des Meshes sind wichtiger als der Konverter-Kommentar:** `convert_bus.py`
  setzt den Ursprung auf die Bounds-MITTE, im Import liegt die Box aber bei Min.Z=0,00 /
  Max.Z=224,77 - die Unterkante also auf dem Pivot (`MeshBottomCm = 0`). Der Wagen ist
  nur 2,25 m hoch, deshalb die Sitz-Augenhoehe (1,30 m), nicht die Stehhoehe.
- **Falle beim Bauen:** ein eigener `struct FBox` kollidiert mit Unreals `FBox`
  (`TBox<double>`) - hier heisst der Kasten deshalb `FPart`.
- **Offen:** der Diagnose-Strahl nach unten trifft die Fahrbahn 1,96 m unter dem Auge
  (also ~0,7 m unter dem Bus-Ursprung). Auf einer Bruecke/unter einer zweiten Strasse
  ist das richtig; auf gerader Strasse waere der Bus angehoben. Nicht geprueft.
## Heli-Paar am Start: neuer Ka-52 als Spielerheli, altes Modell als Standstueck (17.09.2026)

Zwei Actors, zwei Modelle - nicht derselbe Actor mit Schalter:

- **`AWiesbadenHelicopter` = das Ka-52-Mesh** (`/Game/Vehicles/Ka52`, eigene PBR-Materialien,
  `HasImportedModel()`), der einzige bespielbare Heli. Der GameMode lackiert ihn NICHT mit
  der alten Zell-Tarnung - sonst saehe der Neubau aus wie das Standstueck daneben.
- **`AWiesbadenLegacyHelicopter` = das frueher benutzte Landmarken-Modell** (SM_HeliBody +
  zwei Rotoren, `M_WbHelicopter` + `M_HeliRotorBase`), `bCanEverTick=false`, Rumpf auf
  `BlockAll`: es steht, faellt nicht, wird nicht besessen. Ein eigener Actor statt eines
  "geparkten" Spielerhelis, weil dieser Schwerkraft, Rotordrehzahl und Eingabe mitbringt.

**Massstab, Mastachse und Nabenhoehen des alten Modells sind MESSUNGEN** (Modell 100,7 cm
lang -> Faktor 14,5 auf 14,6 m; Mast bei (1|-5) im Rumpf; Rotornabe im Rotormesh bei
(0|33); Naben 345/300 cm). Ohne die beiden Gedaechtnis-Offsets kreisen die Rotoren neben
dem Mast. Die Unterkante der Geometrie ist der RUMPFBODEN (Bounds z 0..17,1 cm) - der
Actor-Ursprung darf also auf die Aufstandsflaeche, der Boden-Trace kommt mit +5 cm aus.
Nicht "vorsichtshalber" noch tiefer setzen.

**Standabstand kommt aus der Geometrie, nicht aus einer festen Zahl:**
`ComputeHelicopterStandDistanceCm` = max(halbe Rotordurchmesser + 5 m, halbe Rumpflaengen
+ 2 m, 8 m). Aktuell gemessen: Scheiben 13,7 m (Ka-52) / 12,6 m (alt), Rumpfe 14,06 / 14,6 m
-> 18,15 m. Die Rotorbedingung ist die scharfe: die Scheiben liegen nur ~30 cm uebereinander
(Ka-52 unten 3,77 m gegen das alte Modell oben 3,45 m), ein zu kleiner Abstand zeigt
ineinander stechende Rotoren; die Laengenbedingung greift nur, wenn die Netze fehlen.
**Gemessen wird vom SPIELERHELI aus**, nicht vom Auto: der erste Entwurf setzte 12 m + Abstand
vor das Auto und rueckte den Alt-Heli damit um den Seitenversatz des Helis (6 m) aus der
Flucht - die beiden standen schraeg zueinander, und der Abstand war die Diagonale darueber.

**Beleg:** `shot_heli_paar.cmd [Karte] [Shot s] [Quit s] [Pose-Schalter...]` faehrt die
neueste Karte mit `-WbZuFuss=4` (Kamera auf Augenhoehe, sonst verdeckt die Fahrzeughoehe die
Kufen) und legt `Saved/Diagnose/Messstelle00000.png` ab. Im Bild ist die gruen-schwarze
Maschine mit den ZWEI uebereinanderliegenden Rotoren der Spielerheli (Ka-52-PBR), der
blaugraue mit Heckrotor das Standstueck - nicht vertauschen. Die Zahlen stehen in
`Saved/Logs/WiesbadenReal.log`:
`Helikopter abgesetzt: 12 m neben dem Fahrzeug bei (...)`, danach
`Alter Helikopter steht 18.2 m vor dem Spielerheli bei (...) - Rotorkreise 13.7 / 12.6 m, Rumpf 14.6 m`
(Differenz der beiden Punkte nachrechnen: sie MUSS der Standabstand sein).
Tests: `WiesbadenReal.Vehicles.LegacyHelicopterModell` und `.HeliStandAbstand`
(`Tools/run_automation_test.cmd WiesbadenReal.Vehicles <Logname>`).

**Achtung Hangar-Tor:** der Ka-52 ist standardmaessig nur mit gekauftem Heli-Hangar
einsteigbar (`FWiesbadenStore::MayEnterHelicopter`); ein automatischer Lauf, der einsteigen
will, muss das DEBUG-Makro `WbDev_AllowHelicopterWithoutHangar` definieren.

### Werkzeug-Fallen, die dabei Zeit gekostet haben
- In einer .cmd ist `%10` NICHT das zehnte Argument, sondern `%1` gefolgt von einer Null:
  `set REST=%4 %5 ... %10` schob den Kartennamen ein zweites Mal in die Zeile, sichtbar als
  `WiesbadenCity_Alkis150` in `LogCsvProfiler: Metadata set : commandline=`. Zusatzschalter
  in einer `shift`-Schleife einsammeln.
- Aus Bash heraus findet `cmd //c "script.cmd"` eine Datei im AKTUELLEN Verzeichnis nicht
  ("... ist entweder falsch geschrieben oder konnte nicht gefunden werden"). `.\script.cmd`
  schreiben oder einen Pfad mit Verzeichnisanteil nehmen (`Tools\x.cmd`).
- `Tools\run_automation_test.cmd` kann SOFORT abbrechen mit "Der Prozess kann nicht auf die
  Datei zugreifen, da sie von einem anderen Prozess verwendet wird" - ohne Log, ohne .out,
  auch mit frischem Lognamen, und obwohl kein Unreal-Prozess mehr laeuft (ein Handle auf die
  ABSLOG-/Umleitungsdatei ist offenbar noch nicht freigegeben). Es sieht aus, als haette der
  Test nicht gelaufen: die mtime des Logs gegen die Uhrzeit pruefen, statt dem fehlenden
  Fehlerbericht zu glauben. Verlaesslicher Ausweg: die Engine DIREKT aus Bash starten -
  `("/c/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" <uproject>
  -ExecCmds="Automation RunTests <Filter>; Quit" -unattended -nop4 -nullrhi -NoSound
  -ABSLOG=<absoluter Logpfad> > <out> 2>&1 &)` - das lief in derselben Lage sofort.
- `LogWbVehicles`/`LogWbStreaming` gehen NICHT in die `-stdout`-Umleitung der Shot-.cmd,
  sondern nach `Saved/Logs/WiesbadenReal.log`.

## Zwei Buslinien im Dauerbetrieb: feste Wagen-Nummern, 10 min Wende, Plattformen sind kein Fahrweg (17.09.2026)

Gemeldet war: "busse sollen feste id_s haben und durchgehen ihre strecken fahren,
mit kurzer pause an den beiden endpunkten von 10min. wenn die strecke nach mainz
noch teilweise fehlt dann baue sie. ausserdem linie 3 hinzufuegen."

- **Feste Wagen-Nummern sind jetzt ueber beide Linien eindeutig.** `BuildFleet` vergab
  je Linie 1..N - Linie 6 und Linie 3 hatten damit beide einen "Wagen 2", und beide
  schreiben in DASSELBE Log. `AWiesbadenBusRoute::FirstVehicleNumber()` setzt das
  Hundertfache der Liniennummer davor: Linie 6 -> **601..606**, Linie 3 -> **301..303**
  (`FServiceConfig::FirstVehicleId`). Die Zuordnung Slot k <-> Wagen Fleet[k] bleibt
  unveraendert: derselbe Bus bleibt derselbe, Wendezeit inklusive.
- **Wendezeit kommt aus der LINIENDATEI** (`terminus_dwell_seconds`, 600 s): Linie 3
  braucht dafuer KEINEN Eintrag in `WiesbadenGameMode.cpp` ausser der Datei selbst -
  Takt (`headway_seconds` aus dem OSM-`interval`), Wendezeit und Zielschilder stehen
  in `Data/Raw/Bus/line<ref>.json`, der GameMode listet nur, WELCHE Linien fahren.
  Gemessen im Lauf: Linie 6 Umlauf 104 min / 17 min Abstand -> 6 Wagen, Linie 3
  Umlauf 61 min / 20 min Abstand -> 3 Wagen, beide "Wendezeit 10 min je Ende".
- **Der Pool ist im Dauerbetrieb GENAU die Flotte.** Die Eigenschaft `NumBuses=12`
  liess vorher 12 Bus-Meshes bauen, von denen nur 6 fuhren (der Rest wurde jeden Tick
  `HideBusSlot`). Jetzt `N = Fleet.Num()`, nur `-WbBusCount=<n>` hebt den Pool bewusst an.
- **Plattformen sind MITGLIEDER der OSM-Busrelation, aber kein Fahrweg.** `Tools/build_bus_line.py`
  nahm alle Way-Mitglieder: die geschlossene `highway=platform`-Flaeche "Hauptbahnhof West /
  Mainzer Taubertsbergbad" wurde damit als Schleife IN die Strecke gespleisst (rund 20 Punkte,
  Linie 6 fuhr am Mainzer Hauptbahnhof um den Steg) und "Landtag" als 25-m-Querung ueber den
  Steig; die Strecke war dadurch 290 m zu lang. Jetzt werden Mitglieder mit Rolle `platform`
  bzw. `highway/public_transport/railway=platform` uebersprungen und im Bericht als
  "Steig-/Bahnsteigflaechen sind kein Fahrweg" ausgewiesen. Ergebnis: **294/294 Wege der
  Relation verbaut, 19,58 km, 40 Halte, 0 Luecken** - die Strecke nach Mainz fehlt NICHT,
  der "nicht angeschlossene Weg" im alten Bericht war genau diese Plattform.
- **Doppelte Knoten werden entfernt.** Zwei aufeinanderfolgende OSM-Wege enden und beginnen
  am selben Knoten - das sind 293 Stellen bei Linie 6 und 148 bei Linie 3. Ungefiltert stehen
  dort Nulllaengen-Segmente in `WorldPath`/`ArcCm` (Richtung unbestimmt, Bogenlaenge steht
  still). `SamplePolyline` faengt das mit `Max(SegLen, 1.0)` ab, aber die Linie selbst soll
  keine Nullsegmente fuehren: entdoppelt wird auf der gerundeten Koordinate (7 Stellen).
- **Beleg, nicht Behauptung:** `-WbBusLog` schreibt alle 2 s je Linie "Linie <ref>: Dienstzeit
  ..., n Wagen im Umlauf (Umlaufminuten)" und je Wagen "Bus k: X= Y= Z= Bogen <m> VERWEILT|
  faehrt SICHTBAR" - daran ist das Durchfahren UND die Wendezeit ablesbar (ein Wagen an
  Bogen 0 m mit Zustand VERWEILT ist die laufende Wendezeit am Startpunkt). `shot_buslinien.cmd`
  faehrt zusaetzlich eine Posenserie ab (`Saved/Diagnose/poses_buslinien/linien.txt`,
  Format Hoehe_m, AtX_cm, AtY_cm, Yaw, Pitch) und legt `Saved/Diagnose/WbSeries_000..003.png`
  ab: Nordfriedhof (Start beider Linien), Welfenstrasse (gemeinsamer Korridor), Kastel/
  Brueckenkopf, Endstation Wildpark. Die Strassen existieren bis Mainz-Gonsenheim (Bild 4
  zeigt Fahrbahn, Laternen, Gehwege) - dort ist nur die Bebauung duenn.
- **Posendatei-Koordinaten kommen aus dem Log**, nicht aus der Bogenlaenge: die Halte-Zeilen
  "Bus-Halte n: Bogen <m> -> Welt X= <cm> Y= <cm>" stehen mit `-WbBusLog` in
  `Saved/Logs/WiesbadenReal.log`.

### Bildkontrolle im Preview-Tab: EIN Server, Verzeichnis als Argument
Es gibt genau einen statischen Server im Umfeld - `Audioaufzeichnungen/_analyse/range_server.py`,
mit Range-Unterstuetzung (die stdlib `python -m http.server` ignoriert
Range-Header auf Python 3.14: immer 200 mit ganzer Datei, Chrome springt beim Suchen still
auf 0 zurueck). Er nimmt jetzt ein Verzeichnis:

    python _analyse/range_server.py [DIR] [PORT]      # DIR default Audioaufzeichnungen
    python _analyse/range_server.py 8791              # alte Form: nur Port (unveraendert)
    python _analyse/range_server.py ../WiesbadenReal/Saved/Diagnose 8791

Ein blosses Zahlargument bleibt der Port, und die erste Zeile im Log nennt den absoluten
Pfad - ein 404 ist damit als "falscher Ordner" erkennbar, nicht als Serverfehler. Die
frueher hier liegende Kopie `Tools/serve_dir.py` ist GELOESCHT (zwei Server mit derselben
Range-Logik hiessen zwei Stellen fuer jeden Fix). Ein Vorschau-Register mit `htmlPath`
rendert ausserdem NUR die HTML-Datei: Nachbar-Bilder im selben Ordner bleiben kaputt (leere
Bildrahmen) - man braucht den laufenden Server (`url` + `pid`, pid aus `netstat -ano | grep
:8791`). 15-MB-HighRes-PNGs vorher auf ~1800 px verkleinern, sonst bleibt der Preview-Tab
schwarz ("produced no frames").

### Bash/cwd-Falle, die diesmal Zeit gekostet hat
`MSYS2_ARG_CONV_EXCL='*' cmd //c ".\script.cmd"` startet eine INTERAKTIVE cmd (kein Lauf):
die Variable schaltet genau die Umschreibung ab, die `//c` zu `/c` macht. Richtig ist
`cmd //c ".\script.cmd"` OHNE die Variable. Und umgekehrt: die Engine-Zeile direkt aus Bash
BRAUCHT `MSYS2_ARG_CONV_EXCL='*'`, sonst wird `/Game/Maps/<Karte>` zu
`C:/Program Files/Git/Game/Maps/<Karte>` und der Lauf endet nach 2 s ohne Karte
(im Log sichtbar als `LogInit: Command Line:` mit dem Git-Pfad).
**Symptom, das in die Irre fuehrt (18.09.2026):** der Prozess endet dabei NICHT, sondern
stellt ein MODALES Fenster `Message` - "The map specified on the commandline ... could not be
found. Would you like to load the default map instead?" - und wartet auf einen Klick. Von aussen
sieht das wie ein haengender Editor aus: ~5 GB RAM, Fenster `Message`, und die mit `>` umgeleitete
Logdatei bleibt 0 Byte, weil der Lauf den Weltstart nie erreicht. Also: haengender UnrealEditor
mit leerem Log = erst die Kommandozeile im Projektlog (`LogInit: Command Line:`) ansehen, ob ein
Git-Pfad darin steht - und die Leiche mit `Stop-Process` wegraeumen, sonst blockiert sie den
naechsten Lauf (auch die Default-Karte wird dann nicht geladen).

## Gebackene Karte: KEINE Fahrbahn-Kollision - Boden kommt aus dem Strassennetz
- Der vertikale Boden-Trace traf in JEDER Probe beider Buslinien (132/132) nur das
  `Landscape`, nie einen Fahrbahn-Mesh. Die `RoadCollisionStaticMesh`-Komponenten STEHEN
  aber in den Chunk-Paketen (2010 Pakete von Alkis15) und die Meshes liegen als
  `Content/Generated/Chunks/SM_RoadCol_*` (1985 Stueck, 17.09. 13:59) auf der Platte -
  der Treffer fehlt also trotzdem. `LogWbStreaming: Hoehen-Stapel: Gelaende bei Z = ...`
  (0,5 Hz, Spielerposition) zeigt denselben Befund: auch der SPIELER steht auf dem
  Gelaende, das laut Baubuch unter dem Belag liegt (Fahrbahnversatz 30 cm, Einebnung
  nochmals 45 cm; die Gebaeude-Diagnose nennt es "Fahrzeug-Band Boden+35..185 cm").
- Messung je 25 m Fahrtweg: `-WbBusGroundAudit` schreibt
  `Saved\Diagnose\bus_ground_audit_line<ref>.txt` (getroffene Flaeche + Name, was darunter
  liegt, und den Abstand zur naechsten SPUR des Strassennetzes). Ergebnis Linie 6:
  Unterkante im Mittel 37 cm UNTER der Fahrbahn, an der Rheinbruecke 5,65 m (dort ist der
  Boden unter der Bruecke das Flussbett). JE LINIE eine eigene Datei - sonst ueberschreibt
  der zweite Bus-Actor die erste Messung.
- Fix im Bus-Actor: `BuildLaneIndex()`/`RoadSurfaceZ()` - aus `AWiesbadenWorldBuilder::
  RoadNetwork.Lanes` (nicht-transiente UPROPERTY der gebackenen Karte, `TActorIterator`;
  `FindBakedCityBuilder()` ist PRIVATE!) ein 200-m-Zellen-Gitter ueber alle Spur-
  stuetzpunkte, Spurhoehe entlang des Segments interpoliert, Spur innerhalb 9 m gewinnt
  gegen den Trace. Dieselbe Sollbahn faehrt die Verkehrs-Simulation - Bus und Auto stehen
  damit gleich hoch. `RoadSurfaceOffsetCm = 20` im Generator, real war der Abstand groesser
  (Einebnung) - deshalb NICHT mit einer Konstante rechnen, sondern das Netz fragen.
- NACHTRAG (Beleglauf ueber die GANZE Linie, 1111 Proben, Bogen 14..19570 m): die
  Fahrbahn-Kollision IST wirksam - aber nur, solange ihr Chunk gestreamt ist. 99 Proben
  trafen `RoadCollisionStaticMesh` (Spielernaehe), 1012 Proben nur das `Landscape`. Das
  Landscape ist EIN Actor und deshalb immer geladen, die Chunk-Pakete der Stadt nicht -
  entfernte Busse stehen ohne Chunk da. Darum darf man den Trace NIE als Fahrbahn lesen.
  Wo beides da war, stimmt das Strassennetz mit dem sichtbaren Belag ueberein: Abstand
  Belag <-> Spur im Mittel 1 cm, max 32 cm (99 Proben) - die Spurhoehe ist also die
  Fahrbahnoberkante, nicht eine Naeherung.
- Gemessen ueber die ganze Linie 6: Unterkante vorher im Mittel 52 cm UNTER der Fahrbahn,
  min 598 cm (Rheinbruecke, dort ist der Boden unter dem Deck das Flussbett), max +58 cm.
  Nach dem Fix: 1111 von 1111 Proben auf dem Strassennetz aufgesetzt.
- Dasselbe gilt fuer die Posen-Kamera: sie ankert ihre Hoehe am BODENTRACE, an der
  Rheinbruecke also 5,65 m unter dem Deck (Posenhoehe entsprechend gross).

## Kompletter Umlauf als Beleg: 2-s-Protokoll eines Wagens, Spielzeit laeuft 1:1
- `-WbBusLogWagon=<Wagennummer>` (z. B. 601) protokolliert GENAU diesen Wagen alle 2 s:
  `Umlauf Wagen 601 (6): t=973 s Bogen 8447.7 von 19588.3 m | Unterkante 20.77 m |
  Grundlage Fahrbahn aus dem Strassennetz (Gelaende-Trace 20.67 m, Abweichung -10 cm) |
  faehrt | Richtung hin | Mitfahrt nein` - Bogenlaenge, Bodenhoehe (BEIDE Bezuege),
  Zustand samt Wendezeit (`VERWEILT noch n s von 600 s`, an den Enden zusaetzlich
  `(Endpunkt fern)`/`(Endpunkt Start)`), Richtung und Mitfahrt in EINER Zeile. Eigener
  2-s-Takt, damit `-WbBusLogWagon` ohne `-WbBusLog` reicht.
- Der Umlauf der Linie 6 dauert **6215 s** (beide Richtungen + 2x 600 s Wende) - ein
  kompletter Umlauf braucht also ~104 min LAUFZEIT. Die Spielzeit laeuft dabei 1:1 zur
  Wanduhr (gemessen: t=2 s um 17:18:50, t=973 s um 17:35:01) - es gibt keine Beschleunigung,
  also `-WbQuitAfter` entsprechend gross setzen (6500) und abwarten.
- Slot 0 startet an der ersten HALTESTELLE (Bogen 2592 m), nicht am Endpunkt: die Flotte
  ist ueber den Umlauf gestaffelt. Ein Umlauf eines Wagens ist deshalb erst nach der
  vollen Umlaufzeit wieder an derselben Stelle - und die Wendezeit am Startende (Bogen 0)
  laeuft MITTEN in diesem Fenster ab, nicht am Anfang.
- `Tools\run_bus_umlauf.cmd [Karte] [QuitAfter]` ist der Beleglauf dafuer: Protokoll +
  Kurzzeilen aller Wagen + Boden-Audit + Mitfahrt (`-WbBusRide=900 -WbBusRideExit=1300`)
  + Bild aus dem fahrenden Bus (`-WbShot=1000`).
- Mitfahrt belegt das FAHREN nicht ueber den Zustand, sondern ueber die Augenkoordinaten:
  wechseln sie zwischen zwei `Mitfahrt Linie <ref> Wagen <nr>`-Zeilen, sitzt der Fahrgast
  in einem fahrenden Bus (an der Wendezeit stehen sie still).

## Ferne Wendezeit belegen: Dienstuhr verschieben statt 100 Minuten warten
- `-WbBusClock=<Sekunden>` addiert einen Versatz auf `ServiceSeconds`. Mit 2500 steht
  Wagen 601 die ganze Aufnahme ueber am ZWEITEN Endpunkt (Linie 6: Bogen 19588 m, Halt 39
  Mainz-Gonsenheim Wildpark) - sonst sieht man nur die Wendezeit am Startende.
- `FBusState` traegt `DwellRemainingSeconds` UND `DwellTotalSeconds`. Erst damit ist eine
  Probe MITTEN in der Wendezeit beweisfaehig: `VERWEILT noch 267 s von 600 s (Endpunkt)`.
  Die Logzeile nennt jetzt `Linie <ref> Wagen <nummer>` statt des Pool-Slots - bei zwei
  Linien im selben Log war "Bus 2" nicht mehr zuordenbar (Wagen 302 oder 602?).
- `-WbBusRideExit=<Sekunden>` steigt nach so vielen Sekunden Mitfahrt von selbst wieder
  aus (ohne das laesst sich der Ausstieg in einem automatischen Lauf nicht belegen) und
  protokolliert, dass Figur sichtbar und Innenraum ausgeblendet zurueckkommen.

## Liniendaten und Zielfilme muessen GETRACKT werden
- `.gitignore` schliesst `Data/Raw/` aus; `line6.json` war per `git add -f` ausgenommen,
  `line3.json`/`announce_line3.json`/`announce_line6.json` und die sechs
  `Content/Vehicles/Bus/Blind/*L3*`-Assets fehlten komplett - nach einem frischen Checkout
  gab es Linie 3 also nicht (und die Ansagen von Linie 6 auch nicht).
- REGEL statt `-f`: ein ausgeschlossenes ELTERNVERZEICHNIS laesst sich nicht wieder
  einschliessen. Darum in `.gitignore` `Data/Raw/*` (nicht `Data/Raw/`) + `!Data/Raw/Bus/`
  + `Data/Raw/Bus/*` + `!Data/Raw/Bus/*.json`. Kontrolle: `git check-ignore -v
  Data/Raw/Bus/line3.json` muss LEER bleiben, `Data/Raw/Bus/bus.glb` muss die Regel nennen.
- Ohne `line3_schedule.json` ist Linie 3 im Fahrplan-Modus (`-WbBusSchedule`) leer; der
  GameMode setzt fuer sie bewusst `ScheduleFile = ""`. Fuer einen echten Linie-3-Fahrplan
  fehlen die Soll-Abfahrtszeiten.

## Welt-Hoehenbezug (gegen den "Mainz ist 60 m zu tief"-Verdacht)
- UE-Z = DEM - **75 m** (`VerticalReferenceMeters = 75`, `Config/DefaultGame.ini` laedt
  `Data/Raw/DEM/N50E008.hgt`). Gegen die SRTM-Kachel geprueft: 350 Proben noerdlich 50 N
  liegen im Median 0,1 m (max 0,5 m) daneben - das Gelaende FOLGT der Kachel exakt.
  "Mainz hat nur 20-54 m" ist also der Bezug, kein Fehler.
- ABER: das Gebiet suedlich 50 Grad Nord ist der GEKLEMMTE Suedrand von N50E008
  (`SampleBilinearGeo` klemmt U/V auf das Raster). 44 Proben dort treffen die geklemmte
  Kante auf 0,0 m genau, waehrend die echte `N49E008.hgt` 6-20 m hoeher liegt. Wer
  Mainz-Gonsenheim genau haben will, muss die zweite Kachel anbinden und neu backen -
  fuer die Busse ist es ohne Belang (Gelaende UND Strassen kommen aus derselben Quelle).

### Fahrbahnhoehe der Busse: Spurwahl im Fehlermass, NICHT im Grundriss
- `RoadSurfaceZ` nahm die Spur mit dem kleinsten Abstand IM GRUNDRISS. An Knoten liegen
  Spuren bis 9 m daneben auf ganz anderem Niveau (Rampe, Bruecke, Parallelfahrbahn):
  das Boden-Audit mass `Abstand Unterkante<->Fahrbahn max 4014 cm` und `Belag<->Spur
  max 528 cm` - der Bus stand dort bis 40 m neben der Fahrbahn. Sichtbar war das nur
  im 25-m-Probenraster der Slots, nicht im 2-s-Protokoll des einen beobachteten Wagens
  (dessen max war 82 cm) - wer eine Abweichung sucht, darf nicht nur EINE Quelle lesen.
- Jetzt entscheidet der GESAMTABSTAND `D2 + (SpurZ - Boden-Trace)^2`, und eine Spur mit
  mehr als `MaxLaneDeviationCm = 1000` cm Abweichung wird verworfen (dann steht der Bus
  auf dem Gelaende-Trace, ~40 cm daneben statt 40 m). Bruecken bleiben richtig: das
  Rheinbrueckendeck liegt 6,1 m ueber dem Ufer, also unter der Schwelle.
- Das Audit schreibt die fuenf schlimmsten Stellen mit Bogenlaenge, Weltkoordinaten,
  Wagennummer sowie Boden- UND Spurhoehe heraus (`NoteDeviation`) - Summenwerte allein
  nennen keine Stelle, und ohne Stelle ist nicht entscheidbar, ob die Spur oder der
  Boden falsch war. Nach dem Fix: max 72 cm (Linie 6) / 25 cm (Linie 3) neben der
  Fahrbahn, 0 Proben ohne brauchbare Spur, schlimmster Wert -612 cm = Rheinbruecke.

### Spiel-Fensterlauf vs. kopfloser Lauf (Messlaeufe)
- Ein `-game`-Fenster im HINTERGRUND wird gedrosselt: gemessen 56 s Spielzeit in 9 min
  Wanduhr (10x zu langsam); zwei Fensterlaeufe endeten ausserdem nach ~1 min mit
  `FPlatformMisc::RequestExit(..., UGameEngine::Tick.ViewportClosed)`. Fuer Messungen
  daher `Tools\run_bus_audit.cmd [Karte] [Spielsekunden] [Log]` (`-game -nullrhi
  -NoSound`, kein Fenster): ~87 % Geschwindigkeit, Traces/Streaming unveraendert.
- Start aus bash: `powershell -NoProfile -Command 'Start-Process -FilePath cmd.exe
  -ArgumentList "/c <cmd-Datei> <args>" -WindowStyle Hidden'` - ein `(cmd //c ... &)`
  erzeugte Laeufe, die der Fensterschliesser nach ~1 min beendete.

### Beleglauf-Werkzeuge (Fahrbahn, Wendezeit, Mitfahrt)
- `Tools\run_bus_ground.cmd [Karte] [QuitAfter]` - Audit ueber die ganze Linie +
  Dienstuhrversatz (`-WbBusGroundAudit -WbBusLog -WbBusClock=2500`).
- `Tools\run_bus_fahrbahn.cmd` - dasselbe plus vier Nahaufnahmen am fernen Endpunkt
  (`Saved\Diagnose\poses_busfahrbahn\fahrbahn.txt`). ACHTUNG: die Posen-Serie beendet den
  Lauf kurz nach den Bildern - fuer ein Audit mit vielen Proben OHNE Posen fahren.
- `Tools\run_bus_haltestelle.cmd <Halt>` - parkt je einen Wagen beider Richtungen an
  Halt N (`-WbBusParkStop=N`, Halt 7 = Stadtstrasse, Halt 20 = Rheinbrueckenkopf) und
  nimmt Bilder auf: der Beleg "Räder auf dem Belag".
- `Tools\run_bus_umlauf.cmd [Wagen] [Karte] [Spielsekunden]` - Umlauf-Protokoll
  EINES Wagens im 2-s-Takt (`-WbBusLogWagon=<Nr>`, z. B. 601). Ein voller Umlauf
  Linie 6 dauert 104 min; `-WbBusQuitAfter=7000` genuegt fuer Rueckkehr UND
  Wendezeit am Startende. Der Takt ist Spielzeit = Wanduhr (kopflos ~1:1).
- `Tools\run_bus_mitfahrt_wagen.cmd [Wagen] ...` - Mitfahrt auf einem BESTIMMTEN
  Wagen (`-WbBusRideWagon=<Nr>`): ohne diese Nummer nimmt die Dev-Mitfahrt den
  ersten HALTENDEN Bus, also einen beliebigen Wagen - ein Beleg "auf Wagen 601"
  war damit Glueckssache.
- `Tools\umlauf_beleg.py <Wagen> <Log> <Linie>` zieht den Beleg aus dem Log:
  Probentakt, Wendezeiten mit erster/mittlerer/letzter Zeile, Hoehen je Abschnitt
  (Grenze Wiesbaden/Mainz aus der Haltestelle 'Landtag' der Liniendatei, NICHT aus
  einer Skriptzahl - 'Kasteler Strasse' gibt es auch in Wiesbaden), Orte der
  groessten Abweichungen. Ergebnis `Saved\Diagnose\beleg_umlauf.txt`.

### Liniendatei: genau EIN Leser fuer Bus und Haltestellenmonitor
- `World/WiesbadenBusLineFile.{h,cpp}` ist der einzige Leser von `Data/Raw/Bus/line<ref>.json`
  und `line<ref>_schedule.json`. `ReadLine`/`ReadSchedule` liefern Bus-Actor UND
  Haltestellenmonitor DASSELBE Objekt (gleiche Adresse, nur lesen) statt je einer eigenen Kopie:
  vorher lagen in beiden Actors je ein LoadLine/LoadSchedule/BuildWorldPath (~320 Zeilen fast
  gleicher Code) - eine Aenderung musste an zwei Stellen nachgezogen werden, sonst zeigt der
  Monitor andere Zeiten an, als die Busse fahren.
- Cache-Schluessel = Dateiname + Aenderungszeit + Origin: im Editor geaenderte Datei wird neu
  gelesen, unveraenderte nicht. Beleg im Lauf (Log-Kategorie `LogWbBusLineFile`): "Liniendatei
  line6.json gelesen" steht GENAU EINMAL fuer die ganze Welt, danach "Linie 6: Route 19.59 km, 40
  Halte auf der Linie (Bogen 0..19588 m)" - dieselben Zahlen, die der Bus-Actor meldet.
- Rollen bleiben getrennt: `WiesbadenBusLine` rechnet datenrein (Zeit->Bogenlaenge), der Leser macht
  Datei + Geo->Welt, die Actors setzen nur noch um, was sie brauchen (Bus: Liniennummer, Takt,
  Wendezeit, Zielschilder; Monitor: Liniennummer, Ziel, Takt, Wendezeit, `monitor_stops`).
- Test `WiesbadenReal.Traffic.BusLineFile` prueft die ECHTEN Dateien: projizierte Laenge gegen den
  eigenen Pfad nachgerechnet (<1 %), Halte-Bogenlaengen aufsteigend und auf der Linie, Achslage
  (Sueden = +Y, Osten = +X), Cache-Identitaet (zweiter Aufruf = dasselbe Objekt) und fehlende
  Datei ohne Absturz. Bewusst OHNE festgeschriebene Haltezahl - die Linie waechst mit OSM.

### Fahrbahnhöhe: Spur schlaegt Trace, Bruecken sind die grossen Abweichungen
- Im 2-s-Protokoll steht in JEDER Probe beider Linien "Grundlage Fahrbahn aus dem
  Strassennetz" - der Gelaende-Trace ist nur noch Rueckfall. Die grossen
  Abweichungen sind deshalb kein Platzierungsfehler, sondern Bruecken: Linie 6
  Bogen 12,17-12,73 km = Rheinbruecke (Trace trifft 5,7-6,0 m unter dem Deck das
  Wasser, +529 cm an der Rampe, deren Deck ueber der Strasse liegt) und
  Bogen 14,14 km = hoeher liegende Mainzer Strecke. Median der Abweichung -17 cm,
  nur 39 von 1837 Proben weiter als 2 m - alle in diesem Brueckenband.
- Die Fahrbahn-Kollision der Stadt ist im kopflosen Lauf nur auf ~8 % (Linie 6)
  bzw. ~19 % (Linie 3) der Proben ueberhaupt gestreamt: der Rest trifft das
  Landscape UNTER dem Belag. Wo sie da ist, stimmen Belag und Spur auf 1-23 cm
  (Mittel 1 cm) - die Zahl der Treffer misst das Streaming, nicht den Einbau.

### Bus-Innenraum: Texturen je Flaechenart, UVs in Kachelweite
- "Im Bus sind keine Texturen" war KEIN Renderfehler: der Innenraum war nur
  vertexgefaerbt, die UVs je Flaeche fest (0,0)-(1,1). Jetzt traegt jeder Kasten eine
  Flaechenart (`WiesbadenBusInterior::ETile`: Boden/Sitz/Wand/Decke/Technik), je Art
  gibt es eine kachelnde 512er-Textur (`Tools/make_bus_interior_textures.py` ->
  `Content/Vehicles/Bus/Interior/Source`) und ein Material Textur x Vertexfarbe
  (`Tools/import_bus_interior.py`, Basiscolor gegen `MaterialExpressionMultiply`
  gegengeprueft, `ok=5/5`). Die FARBE bleibt die Vertexfarbe, die Textur ist GRAU -
  sonst braeuchte jede Farbe (ESWE-Gelb der Stangen, blauer Stoff) eine eigene Textur.
- Ein `FSurface` haelt Farbe UND Flaechenart in einer Zeile; das Mesh wird je Art in
  einen eigenen Abschnitt gelegt (`BuildSection`), Abschnittsnummer = `(int32)ETile` =
  Materialindex. `BuildMesh` gibt es nicht mehr.
- UVs = absolute Wagenkoordinate / `TexCmPerTile()` (200 cm), NICHT kastenrelativ:
  benachbarte Kaesten derselben Wand liegen damit im selben Raster und stossen nicht
  versetzt an. Muster, die das kacheln, brauchen Rasterweiten, die 512 ganzzahlig
  teilen (Fugen 128/256 px, Punkte 16 px, Wellen mit ganzzahliger Frequenz).
- `UTexture2D::GetSizeX()` liefert 0, solange die Textur nicht aufgebaut ist - im
  Laufzeit-Log `GetImportedSize()` nehmen. Die Zeile "(0x0)" sah wie ein kaputtes
  Asset aus, war aber nur zu frueh gemessen. Belegzeile jetzt:
  `Bus-Innenraum: 51 Kaesten, 1224 Ecken in 5 Abschnitten; ... ok T_WbBusIntBoden(512x512, UV -2.05..2.05/-0.64..0.64) ...`
  (UV 2,05 = 410 cm von der Wagenmitte = halbe Wagenlaenge, also echte Kacheln).
### Haltestellen-Saeulen: zwei je Halte, und Richtungen mischen sich
- "Es fehlen Anzeigetafeln auf der gegenueberliegenden Haltestelle" war kein fehlendes
  Asset: `monitor_stops` hat fuenf Halte, und je Halte stand EINE Saeule - auf der
  Bordsteinkante der HINFAHRT. Jetzt baut `BuildMonitorsForSide(bForward,...)` je Halte
  zwei (Seite = `bForward ? RightDir : -RightDir`, Panel blickt zur Strasse), und jede
  nennt Zieltext UND Durchfahrtszeit IHRER Richtung: `SecondsToStopOnLeg(...)` =
  Hinfahrtszeit, bzw. Hinfahrt + Wendezeit + Rueckfahrt (`WiesbadenBusLine.cpp`,
  dieselbe Phasenfolge wie `BuildPhases`). Die Gegenrichtung braucht den zweiten
  Endpunkt: das Feld `from` liegt jetzt als `FLineFile.Origin` im Leser.
- Belegzeile je Saeule (kopfloser Lauf, `Tools/run_bus_interior_proof.cmd`):
  `Saeule Hinfahrt an Halt 1 'Wolkenbruch' auf (-118138, -123791) - Durchfahrt Ziel
  Mainz Gonsenheim Wildpark nach 53 s` und `Saeule Gegenrichtung ... auf (-117711,
  -124651) - Ziel Wiesbaden Nordfriedhof nach 5554 s` (9,6 m Abstand = beide
  Strassenseiten, verschiedene Zeiten).
- FALLE beim Pruefen der Fahrtrichtung: die Zeile `Umlauf Wagen <id> (6): ...
  Richtung hin/zurueck` schreibt NUR der per `-WbBusLogWagon` gewaehlte Wagen - ein
  Auszaehlen dieser Zeilen je Zeitstempel ergibt deshalb scheinbar "alle fahren
  gleichzeitig in dieselbe Richtung" (\"0 gemischt\"), obwohl die Flotte
  (`PhaseSeconds = Cycle/Anzahl`) sauber gemischt faehrt. Richtig ist der Vergleich der
  `Linie <ref> Wagen <id>: ... Bogen <x> m`-Zeilen ALLER Wagen ueber zwei Zeitpunkte:
  damit stehen z.B. W601/605/606 auf "hin" und W602/603/604 auf "zurueck".
- `unreal.log("###MARKER###")` aus einem Import-Skript landet NICHT im per `> log`
  umgeleiteten cmd-Log, sondern als `LogPython:` in `Saved/Logs/WiesbadenReal.log` -
  bei der Suche nach Import-Markern dort nachsehen. `LogWbBus`-Zeilen eines
  `-game -nullrhi`-Laufs ebenso: im stdout-Redirect steht nichts, das Log liegt in
  `Saved/Logs/WiesbadenReal.log` (Kurzbeleg: `Tools/run_bus_interior_proof.cmd`).
### Bus-Aussenmaterial: „die Texturen sind weg" hiess: das Elternmaterial war Engines glTF-Default
- Befund (`Tools/inspect_bus_materials.py`): alle 41 Plaetze von `SM_Bus` sind
  `MaterialInstanceConstant`s und tragen ihre eigene `BaseColorTexture`, ihr Elternmaterial war
  aber `/InterchangeAssets/gltf/MaterialInstances/MI_Default_Opaque_DS` aus dem ENGINE-Inhalt -
  ohne Nanite-Flag. Auf einem Nanite-Mesh ersetzt Unreal so ein Material durch Grau: der Bus
  rendert texturiert aus der Ferne (Fallback-Mesh) und grau von Nahem, also genau dann, wenn man
  als Fahrgast an anderen Wagen vorbeischaut. Kein Renderfehler, kein Streaming-Problem.
- Fix `Tools/fix_bus_materials.cmd` (Skript `fix_bus_materials.py`): Projekt-Master
  `/Game/Vehicles/Bus/M_WbBusBody` mit `used_with_nanite=True` und den Parameter-Namen, die die
  Instanzen schon setzen (`BaseColorTexture`/`RoughnessFactor`/`MetallicFactor`), und alle 41
  Instanzen darauf umhaengen - die Texturen bleiben in den Instanzen.
- Beleg: `Tools/verify_bus_materials.cmd` -> `Saved/Diagnose/bus_material_check.txt`
  (41 x `/Game/Vehicles/Bus/M_WbBusBody`, Nanite-Flag True, 41/41 mit BaseColor) und die
  Laufzeitzeile des Actors `Bus-Aussenmaterial: 41 Plaetze, 41 mit BaseColor-Textur, 0 auf
  Engine-Inhalt; Basis /Game/Vehicles/Bus/M_WbBusBody`. Beide Zahlen sind die Gegenprobe
  zueinander: faellt eine Instanz auf Engine-Inhalt zurueck, warnt der Actor im Log.
- `unreal.log("###...")` aus einem `-run=pythonscript`-Commandlet landet WEDER im per `> log`
  umgeleiteten cmd-Strom NOCH dauerhaft sonst irgendwo: der Commandlet schreibt nach
  `Saved/Logs/WiesbadenReal.log`, das der naechste Spiel-Lauf ueberschreibt (`-stdout` hilft
  nicht). Belege aus Commandlets darum IMMER per `open(...,"w")` in eine eigene Datei unter
  `Saved/Diagnose/` schreiben - sonst ist der Beleg nach dem naechsten Lauf weg (genau das war
  bei `wb_fix_bus_materials.log` der Fall: 0 Treffer fuer `###WBBUSFIX###`).
- `Richtung hin/zurueck` steht jetzt auch in der All-Wagen-Zeile
  (`Linie <ref> Wagen <id>: ... faehrt | Richtung hin ...`) - ein 2-s-Tick mit `-WbBusLog`
  belegt damit in EINER Zeile, dass beide Richtungen gleichzeitig befahren werden
  (W601/605/606 hin, W602/603/604 zurueck), ohne zwei Zeitpunkte vergleichen zu muessen.
### Material-Usage-Flags: graue Objekte trotz Texturen (Ka52, Nerobergbahn)
- Dieselbe Ursache wie beim Bus, andere Objekte: `Material /Game/Vehicles/Ka52/M_Ka52PBR missing
  usage flag Nanite!` (der Hubschrauber rendert grau) und
  `.../Nerobergbahn/Materials/MI_Nb_NbSchiene missing usage flag InstancedStaticMeshes!`
  (Schiene/Zahnstange/Seilkanal/Rost/Holz/Schotter stecken in ISM-Komponenten).
  `Tools/fix_material_flags.cmd` -> `Saved/Diagnose/material_usage_flags.txt` setzt die Flags.
- Die Warnzeile erscheint NUR im Fenster-Lauf (der kopflose `-nullrhi`-Lauf rendert Ka52 und
  Nerobergbahn nicht) - die Liste der zu reparierenden Materialien darf also nicht allein aus
  einem kopflosen Log kommen (`KNOWN`-Liste im Skript).
- An einer `MaterialInstanceConstant` gibt es das ISM-/Nanite-Feld NICHT
  (`Failed to find property 'used_with_instanced_static_meshes'`) - das Flag sitzt am
  BASIsmaterial und wird geerbt; das Skript setzt es darum am `parent`.
- Ein `-run=pythonscript`-Commandlet ueberschreibt `Saved/Logs/WiesbadenReal.log` schon beim
  START mit seinem eigenen Log (ein Lauf gegen 00:44 loeschte die Warnzeilen des Spiel-Laufs von
  00:40, bevor das Skript sie lesen konnte). Gegenmittel: das Spiel-Log vorher wegkopieren
  (`material_flags_source.log`) UND den Commandlet mit `-ABSLOG=` auf eine eigene Datei legen.

## Flugpruefung in echter Spielsitzung: Rotor-RPM, Mastachse, Kamera (18.09.2026)

- **Werkzeug:** `Tools\flight_check.cmd "<ExecCmds>" <Name> [MinSeconds]` ->
  `Tools/flight_check.ps1`: startet EINE echte Spielsitzung (`-game -windowed -resx=1280`,
  Default-Map `WiesbadenCity_Alkis15`), wartet auf genug Messpunkte, beendet sie. Die Quotes um
  `-ExecCmds` MUSS das Skript setzen (in `Start-Process -ArgumentList` gehen sie sonst verloren,
  dann laeuft nur das erste Wort und der Editor idlet endlos - genau die Falle aus dem
  Automation-Test-Runner). Engine/Build-PAARUNG: Gate 1 baut mit der INSTALLIERTEN Engine
  (`C:\Program Files\Epic Games\UE_5.8`), die Sitzung startet darum DIESELBE `UnrealEditor.exe` -
  nicht die freebuff-Kopie (shared PCH, siehe `Tools\build_gate1.cmd`).
- **Messnaht:** `FWiesbadenHeliMastSample` + `IWiesbadenHeliControl::SampleRotorMast()`
  (`Vehicles/WiesbadenVehicleControl.h`, Implementierung im Heli) liefert die GEOMETRIE-Wahrheit
  (Naben-/Blatt-Drehpunkte, Drehlagen, Blattachsen), `UWiesbadenVehicleTestHarness` sammelt sie je
  Bild und loggt je Sekunde `WbDev Mast t=` und `WbDev Kamera t=` (beide `LogWbVehicles`, NICHT in
  der Doku-Drift-Pruefung - die sieht nur `WbDev:`-Literale des PlayerControllers).
- **Gemessen 18.09. (Sitzungen a4 / followbank / orbit4, Logs `Saved/Logs/wb_flight_*.log`):**
  RPM 327-341 -> Naben-Drehung gemessen +1983/-1983 Grad/s gegen Soll +1983 (99,9 %, gegenlaeufig),
  Naben 0.0/0.0 cm ab Mastachse, Blatt-Drehpunkte 0.0/0.0 cm ab Nabe, Stange 0.00 Grad,
  Blattachsen 0.00/0.00 Grad - in JEDER Lage (auch 35 Grad Roll). Kamera: Follow ~1500-1550 cm
  Abstand, Blick Nick konstant -10 Grad bei Rumpf-Nick -8..-36 Grad (Horizont ruhig, Roll des
  Blicks -0.0 Grad, Gierfehler 0.0 Grad); Cockpit 203 cm und Blick Nick = Rumpf-Nick (starr mit
  der Zelle); Orbit ~1450-1520 cm, Horizont ebenfalls ruhig.
- **FALLE Blattstern-Mitte:** der Anker der MESH-Bounding-Box (`Mesh->GetBoundingBox().GetCenter()`,
  mit der Komponententransformation gedreht) ist ein STARRER Punkt der Nabe und misst nur die
  AABB-Asymmetrie des Sterns - beim Ka-52-Rotor konstant 179.5/186.1 cm, ohne dass irgendetwas
  schief sitzt. Richtig ist der WELT-Mittelpunkt der Komponenten-Bounds (`Comp->Bounds.Origin`,
  die Engine zieht das AABB je Bild neu): der wandert mit der Drehlage, und sein MITTELWERT ueber
  eine Drehung ist die Sternmitte. Aufloesung ~ (0,25 * Rotorradius)/Bilder, hier 2-15 cm bei
  ~90 Bildern/s - ein echter Versatz (1,8-m-Klasse) waere unuebersehbar. Im RUMPF-Frame messen,
  nicht im mitdrehenden Naben-Frame; erst der Aufrufer mittelt (der Heli liefert Momentaufnahmen).
- **BEFUND Kamera-Ausleger im Steilbank (Wiederholbar 6/6 Sitzungen):** bei Roll ~ -33 Grad
  (Rollumkehr im nudge-bankierten Profil, immer bei t=7 s) kollabiert der Kamera-Abstand EINEN
  Messpunkt lang auf 96-99 cm (statt ~1500) - in Follow UND Orbit, nicht im Cockpit. Verdacht:
  Federarm-Kollisionstest gegen die eigene Zelle/die `CollisionSphere` (Radius 160 bei z=130), die
  beim Kreuzen der Rollachse in den Strahl kommt. Kein Mast-/Rotorfehler; Fix waere die eigene
  Zelle aus dem Kamera-Trace zu nehmen (bDoCollisionTest/Owner-Ignore) - NICHT blind die
  `CollisionSphere` verkleinern (sie traegt die Boden-/Kollisionslogik).
- **Reihenfolge der Sessions ist egal, aber:** `WbHeli` MUSS vor `WbCam`/`WbHeliFly` stehen (alles
  laeuft im selben Frame der Deferred-Exec-Kette durch); der Heli ist beim ersten Frame schon
  abgesetzt (GameMode-BeginPlay), `WbHeli` greift also sofort.

## Kompletter Stadt-Bake: Rezept, Dauer, und was er NICHT braucht (18.09.2026)

- **Rezept in EINER Datei:** `rebake_alkis16.cmd` (Muster fuer jeden Neubau; die alten
  `rebake_alkis10..13.cmd`/`rebake_lod2.cmd` sind nur noch Beispiele). Quelle ist die zuletzt
  gebackene Karte, Ziel eine NEUE (`WB_SOURCE_MAP`/`WB_TARGET_MAP`) - ein Bake ueberschreibt nie
  die gespielte Stadt. Dauer mit dem VOLLEN Editor: 920 s fuer 2010 Kacheln, 1,43 Mio. Region-Assets
  (Speicherspitze ~19 GB bei 31,7 GB Maschinen-RAM; der Commandlet-Weg starb an 42 GiB virtuell).
  Alle Datenquellen explizit per Env, weil `rebuild_city.py` sie set-and-verify prueft und bei
  einem nicht greifenden Override ABBRICHT: `WB_OSM_FILE` (Original + nachgeholte Wald-Relationen),
  `WB_ALKIS_FILE` (LoD2-Hoehen/Daecher), `WB_DEM_FILE` (DGM1; setzt `import_dem` mit auf True),
  `WB_USE_OSM_TREES=1`, `WB_MAX_SEGMENT_CM=220`.
- **DGM1: die Datei im Repo ist bereits auf WGS84 umprojiziert - die Rohdaten sind es nicht.** Verifiziert 2026-09-25: `Data/Raw/DEM/wiesbaden_dgm1.asc` traegt `xllcorner 8.1041334645`, `yllcorner 49.9908818744`, `cellsize 0.000096409474` (~10,7 m), 2925x1698 Zellen. Das sind **lon/lat-Grad**, keine UTM32-Meter. `FHeightmapRaster::SampleBilinearGeo(Longitude, Latitude, ...)` in `GIS/HeightmapImporter.cpp` tastet genau in diesem Geo-Raum ab, der Welt-Sampler rechnet ueber `FRasterHeightSampler` ebenfalls lon/lat - ein unprojiziertes DGM1 (UTM32, Zone 32N) passt also nicht in dieses Raster. Die Umprojektion steckt in `Tools/fetch_dgm1_asc.py` (TIF-Tie-Points 33922, Quelle 436000/5538000 -> 456000/5556000 Wiesbaden UTM32, geschrieben wird `xllcorner` in Grad). Wer die Datei ersetzt, muss diesen Schritt wiederholen - und die ~10,7-m-Rasterung bedenken: das ist NICHT die 1-m-DGM1-Aufloesung, auch wenn der Quelldatensatz so heisst.
- **Fertig erkennt man den Lauf NICHT am Prozess**, sondern an der neuen Zeile in
  `Saved/BuildHistory/CityBuilds.csv` (`MapPath`, `Result=ok`) und an
  `###WBSTADT### FERTIG - Karte ... liegt vor.` im `rebake_alkis16.log`; Ausgabe kommt gepuffert,
  die Datei kann minutenlang 0 Byte bleiben - am RAM-Wachstum des Editors weiterarbeiten.
- **Die Pipeline setzt die neue Karte SELBST als Default** (`Config/DefaultEngine.ini`:
  `GameDefaultMap` + `EditorStartupMap`) - die Aenderung steht danach im `git status` und gehoert
  mit in den Commit. Getrackt ist nur die `Content/Maps/*.umap` (~13 KB, Huelle + WorldBuilder);
  die ~2000 externen Actor-Pakete (`Content/__ExternalActors__`, 1,9 GB) und die 3431
  Chunk-Meshes (`Content/Generated/`) sind per .gitignore draussen - ein frischer Checkout braucht
  weiterhin einen Bake.
- **KEIN Anker-Pass nach einem frischen Bake:** `AnchorStreamingBounds()` laeuft im Build-Pfad
  jeder Kachel (`BakeToStaticMeshes`) und noch einmal in `BeginPlay`. Gegenprobe im Spiel ist die
  Streaming-Diagnose: `Diagnose: Spieler bei (...); geladene Chunks Distanz 60..1831 m, davon
  **0 jenseits 2 km**, 0 jenseits 4 km.` (`Tools/anchor_chunk_bounds.py` bleibt nur fuer Karten
  noetig, die VOR diesem Fix gebacken wurden.)
- **Zwei Fallen beim Belegbild einer neuen Karte:** `-unattended` startet keinen GameMode -
  dann fehlen die Laufzeit-Actors (keine `Liniendatei ... gelesen`-Zeile, keine Nerobergbahn) und
  es sieht aus, als haette die neue Karte keinen Inhalt; und `-WbScreenshot` feuert schon beim
  Weltstart, also VOR `Bringing World ... up for play` - das Luftbild ist schwarz (350 KB statt
  ~1,7 MB). Richtig ist `-WbShotWhenReady -WbCamHeight=<m>` (wartet auf `IsCityReady()`,
  Settle-Countdown, dann EIN HighResShot, danach Selbstende) - so macht es `shot_alkis16.cmd`.
  Gegenprobe, dass die Karte traegt: `Bringing World ...Alkis16 up for play`,
  `Liniendatei line3/line6.json gelesen`, `Strang): 9559 Primitive-Komponenten ... 573
  Instanz-Komponenten mit 716593 Instanzen`, 0 Zeilen `missing usage flag`.

## Wetter ohne Niagara: Post-Process-Overlay (18.09.2026)

- **Der Grund, warum nie etwas fiel, war eine Zeile:** `WiesbadenWeatherFX` haengte das
  Partikelsystem an `GetOwner()->GetRootComponent()` - Besitzer ist der GameMode, und der hat
  KEINE Wurzelkomponente. `if (!bWanted || !AttachRoot) return;` brach also in jedem Tick ab, ohne
  Fehler, ohne Log. Bei wurzellosen Besitzern (GameMode, GameState, Subsysteme) nicht anhaengen,
  sondern `SpawnSystemAtLocation` + je Tick `SetWorldLocation(ViewLocation)` (Kamera-Nachfuehrung);
  einmal je Sitzung protokollieren, WELCHER Weg genommen wurde
  (`Besitzer-Wurzel=KEINE (darum Kamera-Nachfuehrung)`) - sonst ist dieselbe Stille wieder moeglich.
- **Niagara ist aus Python NICHT baubar** (gemessen, `Saved/probe_niagara_templates.txt`): keine
  `NiagaraEditorLibrary`, die Factory erzeugt nur leere Systeme, `unreal.NiagaraSystem` zeigt weder
  Emitter noch `fixed_bounds` noch `exposed_parameters`. Post-Process-MATERIALIEN sind dagegen
  vollstaendig skriptbar (`Tools/add_weather_postprocess.py` baut `M_WbWeatherOverlay` komplett aus
  Python). Wer einen sichtbaren Effekt braucht und keine Handarbeit im Editor will: Post-Process
  nehmen. Die Handanleitung fuer den Niagara-Weg liegt in `docs/Wetter_Niagara_Anleitung.md`.
- **`SceneTexture` liefert float4.** `float4 + float3` ist ein Material-Compilerfehler, der NICHT
  auffaellt: UE faellt still auf das Standard-Post-Process zurueck und reicht die Szene unveraendert
  durch - es sieht exakt so aus, als taete der Effekt nichts. Auf RGB maskieren. Gefunden nur, indem
  eine Wegwerf-Variante gebaut wurde, die NUR das Muster ausgibt (ohne Szene): erst damit war
  "Effekt unsichtbar" von "Material tot" zu unterscheiden.
- **Bildschirmraum-Fallen:** Regenstreifen schmaler als ein Bildschirmpixel verschwinden komplett
  (auf 2-3 px verbreitern). Eine Schneeflocke, die in ZELLkoordinaten rund ist, wird zum Strich, wenn
  die Zelle 24x375 px misst - Zellen quadratisch rechnen. Danach blieb ein sichtbares Raster: dagegen
  Spaltenversatz per Hash, Helligkeitsstreuung und eine Scherung des Gitters.
- **Die 8-s-Wetterblende luegt Diagnosen an:** eine Pruefung, die beim ersten stabil AUSSEHENDEN
  Bild feuert, misst mitten in der Blende. N aufeinanderfolgende unveraenderte Bilder fordern.

## Messdisziplin: warum "der Verkehr wechselt NIE die Spur" falsch gemessen war (18.09.2026)

- **Ein Tick-Zaehler kann "nie" nicht von "selten" unterscheiden.** Die Diagnose druckte
  `LaneChangesThisTick`; bei 4 s Sperrzeit je Fahrzeug steht dort auch bei gesundem Ueberholen fast
  immer 0. Richtig: Lebenszeit-SUMME plus Ablehnungsgruende (ohne Nachbarspur / Luecke zu eng / ohne
  Gewinn) und die Seite der engen Luecke. Erst diese Aufschluesselung zeigte, dass 92 % der
  Ablehnungen an "Nebenspur vorn dicht" lagen - also am zu SPAETEN Ausloeser, nicht an der Regel.
- **Der Defekt sass in den VOREINSTELLUNGEN**, nicht in der Formel: `LaneChangeMinGapCm` 1400 cm
  gegen einen Folgeabstand `MinGapCm` von 700 cm - eine solche Luecke entsteht in einer Kolonne
  nirgends. Ein Test, der sich seine Settings selbst baut, haette die Formel mit gesunden Zahlen
  gefuettert und gruen gemeldet. `Traffic.SpurwechselLuecke` prueft darum Abschnitt 0 gegen ein
  DEFAULT-konstruiertes `FWiesbadenTrafficSettings` und enthaelt die Gegenprobe gegen die alten
  1400 cm. Regel: wenn der Fehler in Defaults steckt, muss der Test die Defaults anfassen.
- **A/B zweier Spiellaeufe ist wertlos, solange das Auto selbst faehrt.** Ich habe zwei Laeufe
  verglichen, um einen "Sprenkel" zu erklaeren - die Kamera stand schlicht woanders, es war eine
  andere Hausfassade. Vergleichsbilder nur mit festem Ort (`-WbGoto`) und stehendem Fahrzeug.
- Ergebnis der vier Fixes (Alkis16, 271 Fahrzeuge, 75-95 s): **53 -> 308 Spurwechsel**, Tempo und
  Stauanteil unveraendert (16 km/h, 38 % Steher). Der Rest ist ECHTER Stau, kein Regelfehler.

## Stau-Karte: was eine Messkarte erst brauchbar macht (18.09.2026)

`-WbStauKarte` schaltet `bCollectLaneFlow` ein und schreibt bei JEDER Diagnose (alle 15 s, nicht erst
am Ende) `Saved/Diagnose/staukarte.txt`; `python Tools/render_stau_karte.py` zeichnet daraus
`staukarte.png`. Messlauf: `-game -WbStauKarte -WbGoto=<Strasse> -WbQuitAfter=190`.

- **Farbe = Tempo GETEILT DURCH LIMIT**, nicht Tempo: 30 km/h sind in der Tempo-30-Zone freie Fahrt
  und auf der Hauptachse Stau. Eine Karte nach absolutem Tempo faerbt jede Wohnstrasse rot.
- **Punktgroesse = Zahl der Messwerte** (ein roter Punkt aus 30 Werten ist Zufall, einer aus 40.000
  ein Befund); Rangliste nach STRASSE gewichtet buendeln, sonst fuellt eine Achse die ganze Liste.
- **Ausschnitt aus den MESSWERTEN**, nicht aus dem Netz - gemessen wird nur um den Spieler, das
  6-km-Netz als Rahmen macht daraus einen Fleck in der Ecke. Fahrzeuge IN Kreuzungen (`!bOnLane`)
  zaehlen nicht mit, sonst faerbt jede Ampel ihre Kreuzung als Dauerstau.
- Die UTF-16-Falle von `FFileHelper::SaveStringToFile` (s. Abschnitt "Overpass liefert auch WAYS
  doppelt") hat hier ZUM ZWEITEN MAL zugeschlagen: deutsche Strassennamen entscheiden ueber die
  Kodierung derselben Datei. Jede neue Diagnose-Ausgabe mit Ortsnamen gleich mit
  `ForceUTF8WithoutBOM` schreiben, der Leser erkennt beides.
- Befund (Alkis16, 190 s): Bahnhofsplatz 32 % des Limits, Konrad-Adenauer-Ring 32 %, Am Landeshaus
  19 %, Kaiser-Friedrich-Ring 44 %. Der Startplatz des Spielers ist NICHT darunter - das geparkte
  Spielerauto erklaert den Stau nicht.

## Ausliefern in diesem Repo: es gibt KEINE CI (18.09.2026)

- `gh pr checks` meldet "no checks reported", `statusCheckRollup` ist leer - **nie behaupten, die
  Checks seien gruen.** Der Nachweis ist lokal: `build_only.cmd` plus volle Suite
  (`Automation RunTests WiesbadenReal`, Stand 18.09.2026: **235** `Result={Success}`), und zwar VOR
  dem Commit auf dem isolierten Stand, nicht auf dem Arbeitsbaum mit fremden Straengen darin.
- **Branch wechseln, obwohl die committete Datei lokal weiter modifiziert ist:** erst
  `git rev-parse <commit>^{tree}` gegen `origin/main^{tree}` pruefen - sind sie gleich, fasst
  `git checkout -B main origin/main` keine Datei an und die lokalen Aenderungen der anderen Straenge
  ueberleben unangetastet. Ohne diesen Vergleich ist jeder Checkout ein Risiko fuer fremde WIP.
- **Hartcodiertes CRLF in einem Python-Patch kippt eine LF-Datei komplett auf CRLF** (Diff zeigte
  2055/1838 statt 231/14). Vor jedem Commit `git diff --numstat` gegen
  `git diff --numstat --ignore-cr-at-eol -w` halten; weichen sie stark ab, ist es ein
  Zeilenenden-Unfall. Reparatur: Bytes LF-only neu schreiben, MD5 des normalisierten Inhalts
  vergleichen - der Code muss dabei Byte fuer Byte gleich bleiben.
- **Python-Heredocs und Backslashes:** ein Zeilenumbruch-Escape innerhalb von `TEXT("...")` wurde
  beim Patchen zum echten Umbruch -> `error C2001: Zeilenvorschub in Konstante`. Nicht raten,
  sondern den Backslash als `chr(92)` bauen.

## Probe-Bake Alkis17: gebacken heisst nicht live (18.09.2026)

- Ergaenzung zum Bake-Rezept weiter oben: `rebuild_city.py` verdrahtet die neue Karte SELBST als
  `GameDefaultMap`/`EditorStartupMap`. Das ist richtig fuer einen Bake, der live gehen soll - bei
  einem PROBE-Bake muss `Config/DefaultEngine.ini` zurueckgenommen werden, denn welche Karte gespielt
  wird, ist die Entscheidung des Nutzers. Nach JEDEM Bake `git status Config/` ansehen.
- Der Editor schreibt `Config/DefaultEngine.ini` als CRLF, obwohl HEAD LF ist: der Diff zeigt dann
  212 geaenderte Zeilen fuer zwei echte Werte.
- Abnahme Alkis17 (924 s): `0 von 29 Chunk-Actors ohne Render-Geometrie`, 1073 Ampeln, 121 Bilder/s,
  1,9 GB externe Actors (= Alkis16). Lokal als `27e0c9a` committet, **nicht** live geschaltet.

## Audio-Buesse: verdrahtet, aber zwei davon ohne Quelle (18.09.2026)

Das Mischpult hat 7 Buesse (`Master/Music/SFX/Ambience/UI/Voice/Vehicle`) mit dB-Reglern und Ducking
(-10 dB, 0,15 s / 0,4 s) - aber auf **`Music` und `Ambience` sendet nichts**; 19 der 27 Audio-Assets
sind Halteansagen. `Build.cs` bindet kein Audio-Modul ein, MetaSounds ist nicht aktiviert. Wer dort
etwas hoerbar machen will, faengt bei der Quelle an, nicht beim Regler.

## Umlauf nach Kreuzungsgroesse: Raster ODER Welle - die Entscheidung (18.09.2026)

Die Frage, an der die Arbeit monatelang lag: jede Kreuzung ihren genauen
Wunschumlauf geben (dann passt die Zeit zur Groesse) oder alle auf ein
gemeinsames Raster runden (dann gibt es eine gruene Welle)? **Entschieden fuer
das Raster** - und damit so, wie es real gemacht wird: *gemeinsamer Umlauf im
Zug, eigene Aufteilung je Knoten*. Eine Welle setzt gleichen Takt voraus; mit
38,4 s hier und 41,1 s dort laeuft der Versatz binnen weniger Umlaeufe davon.
Die Groesse wirkt deshalb in der Gruen-AUFTEILUNG, nicht im Takt.

- Die Groesse (`JunctionSize01`, 0..1) kommt aus der **breitesten Zufahrt**
  (nicht dem Mittel - eine Hauptstrasse mit einmuendenden Wohnstrassen ist eine
  grosse Kreuzung) und der Armzahl, Breite mit 75 % gewichtet. Ohne Armdaten
  (synthetische Testnetze) bewusst 0,5 statt einer erfundenen Groesse.
- **Die Falle beim Raster:** kaufmaennisch gerundet kann der Umlauf UNTER die
  Summe aus festen Zeiten und Mindestgruen fallen. Die Gruenzeit wird dann
  hochgezogen, und der tatsaechliche Umlauf liegt ZWISCHEN zwei Rasterstufen -
  genau die krumme Zahl, gegen die das Raster antritt. Solche Ampeln laufen der
  Welle unsichtbar davon, die Statistik zeigt nur den Mittelwert. Darum nimmt
  `QuantiseCycle` einen `RequiredCycleSeconds` entgegen und rundet dann AUF.
- Beim Pruefen des Rasters NICHT `Fmod(Umlauf, Raster) == 0` verlangen: die
  Phasendauern sind `float`, eine Summe trifft 70 s auch als 69,99999 - und
  `Fmod` liefert dann fast ein VOLLES Raster statt null. Beide Enden zaehlen.
- Beleg (Alkis16, Bahnhofsplatz, 230 s, je 15 Messpunkte, gleicher Ort/Seed):
  mit den groessenabhaengigen Programmen **35,6 % Steher und 17,3 km/h**, ohne
  sie 38,5 % und 16,5 km/h; mittlerer Umlauf 39 s (Spanne 30..70) statt 51 s
  fuer alle. Die Spanne gehoert in die Diagnose - ein Mittelwert allein sieht
  bei "alle gleich" genauso aus wie bei "30..70".

## Fahrzeuge steckten ineinander: die Abstandsregel endete an der Bahngrenze (18.09.2026)

Im Probespiel standen wartende Kaefer sichtbar zur Haelfte ineinander. Die
Ursache war NICHT die Folgeregel, sondern ihr Geltungsbereich.

- **Zuerst messen, dann bauen.** Die neue Diagnose (`Fahrzeuge ineinander: N
  Paare - X selbe Bahn, Y selbe Kreuzung, ...`, alle 15 s) hat die Frage in
  einem Lauf entschieden: **0 Paare auf derselben Bahn** - die Kopf-zu-Schwanz-
  Regel arbeitet fehlerfrei -, dafuer 42 Paare je Diagnose auf VERSCHIEDENEN
  Verbindungen desselben Knotens. `ApplyHeadway` laeuft je Korb
  (`VehiclesByLane` bzw. `VehiclesByConnection`); zwei Fahrzeuge in
  verschiedenen Koerben sehen einander nie. `ApplyCrossEdgeHeadway` schliesst
  nur die eigene Folgebahn an, nicht die kreuzende.
- **Ueberlappung braucht ein Rechteck, keinen Mittenabstand.** Nebeneinander auf
  der Nachbarspur sind 3 m voellig richtig, hintereinander sind 3 m eine
  Beruehrung. Darum Rechteck gegen Rechteck (Separating Axis Theorem ueber vier
  Achsen), Hoehe bewusst aussen vor.
- **Die Regel: wer in den Knoten will, braucht einen freien Kreuzungspunkt.**
  Konfliktpaare je Knoten einmal beim Initialisieren (199.867 Verbindungen,
  54.368 Knoten, 152.038 kreuzende Paare, ~90 ms - je Tick waeren es
  Zehntausende Strecken-Schnitte). Gleiche Quellspur = KEIN Konflikt (die
  faechern aus einer Kolonne auf), gleiche Zielspur = Konflikt bis zum Ende,
  sonst der geometrische Schnittpunkt.
- **Die drei Stufen und ihr Preis** (Bahnhofsplatz, je 200 s, Mittelwerte):

  | Stand | Steher | Tempo | Paare | davon Kreuzung |
  |---|---|---|---|---|
  | ohne Regel | 34,5 % | 17,6 km/h | 65,9 | 42,2 |
  | Verbindung ganz gesperrt | 47,3 % | 13,8 km/h | 23,9 | 5,6 |
  | nur bis zum Konfliktpunkt | 46,5 % | 14,1 km/h | 16,7 | 4,6 |
  | + Vorfahrt nach Strassenklasse | 46,2 % | 14,4 km/h | 17,8 | 3,6 |
  | + Blockierfreihaltung | 51,5 % | 12,7 km/h | 9,2 | **0,5** |

  Merksaetze daraus: (1) Wer seine ganze Verbindung sperrt, bis er sie
  verlassen hat, kostet 10 Prozentpunkte umsonst - der Konfliktpunkt genuegt.
  (2) OHNE Vorfahrt haelt auch die Hauptachse vor jeder Wohnstrassen-
  einmuendung. (3) Die Blockierfreihaltung ist der teuerste Schritt UND der
  einzige, der die Paare in der Kreuzung wirklich auf null bringt - darum
  abschaltbar (`bKeepJunctionsClear`), damit die Abwaegung nachrechenbar bleibt.
- **Der Ausgangswert 34,5 % ist kein Ziel.** Er stammt aus einer Welt, in der
  Fahrzeuge einander durchdringen - physikalisch unmoeglicher Verkehr. Am
  normalen Startplatz (nicht am schlimmsten Knoten) kostet die Regel 16-23 %
  Steher bei 18-19 km/h, und die Kreuzungspaare sind dort **null**.
- **Offen bleibt**: 3-11 Paare "sonstige" - Fahrzeuge auf verschiedenen SPUREN,
  die sich ueberlappen. Das ist Spur-Geometrie (zu eng gelegte Parallel- oder
  Gegenspuren), keine Kreuzungsfrage.

## Kartenvergleich: der ERSTE Lauf einer Karte misst den Cache, nicht die Karte (19.09.2026)

Alkis17 gegen Alkis16 verglichen, um die Stadt umzuschalten. Beinahe mit dem
falschen Ergebnis:

| Lauf | LoadMap | GPU-Timeouts |
|---|---|---|
| Alkis16 (1.) | 21,2 s | 0 |
| Alkis17 (1.) | 138,5 s | 1 |
| Alkis17 (2.) | 83,8 s | 3 |
| Alkis16 (direkt danach) | 21,1 s | 0 |
| **Alkis17 (3.)** | **21,2 s** | **0** |

Nach vier Laeufen sah es nach einem klaren Regressionsbefund aus - Alkis17
viermal so langsam, dazu GPU-Timeouts, und Alkis16 reproduzierte unmittelbar
danach seine 21 s. Genau das war die Falle: eine Karte, die noch nie gespielt
wurde, baut beim Laden ihre abgeleiteten Daten auf (Shader/PSO/DDC), und dabei
laufen GPU-Payloads in den Timeout. Die Zahlenreihe 138 -> 84 -> 21 s zeigt es;
**zwei Messpunkte haetten die falsche Entscheidung getragen.** Bei einem Trend
so lange messen, bis er flach ist.

Die uebrigen Kennzahlen waren von Anfang an deckungsgleich und damit der
belastbarere Teil des Vergleichs: 31 Chunk-Actors, 0 ohne Render-Geometrie,
1073 Ampeln, 8,6 ms Bildzeit, 1,9 GB externe Actors - und der Verkehr auf eine
Nachkommastelle identisch (10,4 % Steher, 21,0 km/h). Eine deterministische
Simulation auf demselben Strassennetz ist ein guter Gleichheitsbeweis fuer zwei
Bakes aus denselben Daten.

**Paket-/Fresh-Clone-Stand ist Alkis16**: `Config/DefaultEngine.ini`
(GameDefaultMap + EditorStartupMap) muss auf die einzige verifizierte
`city-content-alkis16`-Veröffentlichung zeigen. Alkis17 bleibt ein lokaler neuer
Bake und darf ohne passendes Release-Paket nicht zum Default werden. Die .ini ist
reines LF - beim Schreiben NICHT auf CRLF kippen, sonst entstehen Phantom-Zeilen.

## Die Spuren waren nicht zu eng - die Karosserie stand daneben (19.09.2026)

Nach dem Kreuzungsfix blieben 3-11 ineinander steckende Paare auf VERSCHIEDENEN
Spuren. Naheliegende Vermutung: Parallel- und Gegenspuren liegen zu dicht. Die
Messung sagt etwas anderes.

- **Die Diagnose mass die falsche Position.** Sie prueft(e) `Vehicle.Location` -
  die Sollposition auf der Bahn. Gezeichnet wird aber `BodyLocation`, die
  nachlaufende Karosserie (`PlaceTrafficVehicles` nimmt sie, sobald
  `bBodyInitialized`). Wer Ueberlappungen im BILD erklaeren will, muss die
  Karosserie messen. Seitdem trennt die Diagnose beides: "N nur Karosserie"
  zaehlt die Paare, deren Sollpositionen sauber auseinander liegen.
- **Ergebnis: 26 von 28,5 Paaren waren reine Karosserie-Ueberlappungen**, bei
  Seitenversaetzen bis 8 m. Die schmalste beteiligte Spur misst 275 cm bei
  154 cm Fahrzeugbreite - **die Spurbreite war nie das Problem**.
- **Warum es im Stand nie besser wird:** Das Einspurmodell bewegt die Karosserie
  mit `Step = Tempo * Dt`. Bei Tempo null bewegt sie sich GAR NICHT. Das
  Sicherheitsnetz zog nur den Ueberschuss ueber 400 cm ab - wer mit 3,9 m
  Versatz zum Stehen kam, stand dort fuer immer. Bei 45-60 % Stehern im
  Stadtzentrum ist das der Normalfall, nicht die Ausnahme.
- **Die 400 cm waren mit dem SPURWECHSEL begruendet** (die Sollbahn springt um
  eine Spurbreite, die Karosserie soll gemaechlich herueberziehen). Das gilt im
  Fahren, nicht im Stehen. Jetzt drei Grenzen: 60 cm im Stand (= (275-154)/2,
  das Auto bleibt in seiner Spur), 120 cm im Fahren, volle 400 cm nur waehrend
  der Spurwechsel-Sperrzeit (`LaneChangeCooldown > 0`).

  | Stand | Paare | davon nur Karosserie | Seitenversatz bis |
  |---|---|---|---|
  | Grenze fest 400 cm | 28,5 | 26,0 | 829 cm |
  | + Stand 60 cm | 13,3 | 8,9 | 832 cm |
  | + Fahrt 120 cm | 13,9 | 8,5 | 554 cm |

  "Selbe Bahn" faellt dabei von 7-13 auf 0: zwei Fahrzeuge mit 7 m Abstand auf
  der Bahn sahen vorher aus wie ineinander geschoben.
- **Was BLEIBT, ist ein Netzbefund, kein Spurbefund:** Von den restlichen Paaren
  liegen die meisten auf Spuren VERSCHIEDENER Abschnitte, mit Sollbahnen bis auf
  **23 cm** aneinander. Zwei Strassen des Netzes liegen dort uebereinander
  (doppelt erfasste Wege, Zubringer neben der Hauptfahrbahn). Das ruecken keine
  Spuren zurecht - das muesste die Netzerzeugung beim Bake aufloesen. Die
  Diagnose weist es getrennt aus ("Spurpaare: N selber Abschnitt, M
  verschiedene").

## Fluss am Bahnhofsplatz: fuenf Hebel gemessen, keiner hat ihn zurueckgeholt (19.09.2026)

Die Kreuzungsregel kostet am Bahnhofsplatz Fluss (51 % Steher gegen 34,5 % in
der Welt, in der Fahrzeuge einander noch durchdrangen). Der Versuch, ihn ohne
Aufweichen der Regel zurueckzuholen, ist GESCHEITERT - aber er hat die Ursache
eingegrenzt. Die Reihe ist hier festgehalten, damit sie niemand zweimal laeuft:

| Hebel | Steher | Tempo | Befund |
|---|---|---|---|
| Ausgangsstand (Regel scharf) | 51,5 % | 12,7 km/h | |
| frueheres Bremsen statt Halt an der Linie | 48-62 % | 9-12 km/h | kein Gewinn |
| Platz hinter der Kreuzung 700 -> 480 cm | 48-59 % | 10-12 km/h | kein Gewinn |
| ein Drittel weniger Fahrzeuge (`-WbVerkehr=0.65`) | 49-54 % | 12-14 km/h | kaum Gewinn |
| Ampelkreuzungen ganz ausnehmen | 39-49 % | 12-16 km/h | **Fluss da, aber 64-71 ineinander steckende Paare** |
| nur zurueckstehen, wo das Signalprogramm trennt | 50-57 % | 10-12 km/h | Ueberlappungen bleiben niedrig, Fluss nicht |

**Was die Reihe zeigt:**

- **Es ist keine einzelne zu strenge Teilregel.** Wer eine Sperre lockert,
  findet die Wartenden danach bei der naechsten wieder. Die Sperrgruende
  verteilen sich stabil auf "kein Platz dahinter" (14-23) und "Weg belegt"
  (24-29); Vorfahrt (1-3) und Linksabbieger (5-9 von ~50) spielen fast keine
  Rolle - **Abbiegespuren waeren hier also nicht der Hebel**.
- **Es ist auch nicht schlicht zu viel Verkehr.** Ein Drittel weniger Fahrzeuge
  brachte 54 -> 52 %. Der Platz ist nicht ueberfuellt, er ist verwickelt.
- **Der einzige grosse Gewinn kam vom Ausnehmen der Ampelkreuzungen - und er
  ist nicht zu haben.** Dort explodierten die Ueberlappungen auf 64-71 Paare,
  mehr als vor der ganzen Kreuzungsarbeit. Damit ist nebenbei belegt, dass das
  Signalprogramm die Stroeme NICHT sauber trennt: Verbindungen derselben
  Richtungsgruppe sind gleichzeitig frei und koennen trotzdem in dieselbe Spur
  einfaedeln. Wer den Fluss wirklich heben will, muss dort ansetzen - an
  besseren Freigabegruppen, nicht an der Konfliktregel.

**Behalten wurde, was fuer sich richtig ist, auch ohne Messgewinn:** das
Bremsprofil statt des harten Halts (ein Auto, das 3,5 m vor der Linie auf null
springt, sieht falsch aus), der Platzbedarf nach Fahrzeuglaenge statt
Folgeabstand, das Heckmass ab Fahrzeugmitte statt voller Laenge, und das
Zuruecktreten dort, wo verschiedene Richtungsgruppen ohnehin nie zugleich frei
sind. Neu als Werkzeug: `-WbVerkehr=<Faktor>` (Dichte im Messlauf uebersteuern)
und die Aufschluesselung der Sperrgruende in der 15-s-Diagnose.

## Wo die Kreuzungsregel wirklich wirkt: zwei Karten statt einer (19.09.2026)

Frage war, wo die Kreuzungskonflikte den Verkehr veraendert haben. Eine Karte
allein beantwortet das nicht - sie zeigt, wo es steht, nicht was eine Aenderung
bewirkt hat. Gebraucht wird ein A/B auf DERSELBEN Karte im SELBEN Build.

- **Dafuer der Schalter `-WbOhneKreuzungsregel`** (`bJunctionConflicts`). Gegen
  die alte Karte von gestern zu vergleichen waere unsauber gewesen: dort waren
  auch Kartenversion (Alkis16), Signalprogramme und Karosserie-Modell anders.
  Beide Laeufe: `-WbStauKarte -WbGoto=Bahnhofsplatz -WbQuitAfter=190`.
- **Werkzeuge:** `Tools/vergleich_staukarten.py <vorher> <nachher>` (je Strasse,
  nach Messwerten gewichtet, mit Mindestzahl gegen Rauschen) und
  `Tools/render_stau_karte.py --diff <vorher> <nachher> [bild]` - eine
  DIFFERENZkarte. Zwei Karten nebeneinander zu legen beantwortet die Frage
  nicht: zwischen 1.400 Punkten findet das Auge den Unterschied nicht.

**Befund (59 Strassen mit je ueber 400 Messwerten):** Gesamt 48,4 % -> 36,8 %
des Limits. Die Wirkung ist NICHT gleichmaessig, sondern liegt fast vollstaendig
auf den Hauptachsen um den Hauptbahnhof:

| Strasse | ohne Regel | mit Regel | Messwerte |
|---|---|---|---|
| Gustav-Stresemann-Ring | 44,5 % | 27,8 % | 1,34 Mio. |
| Bahnhofsplatz | 26,5 % | 15,3 % | 1,01 Mio. |
| Mainzer Strasse | 48,3 % | 35,6 % | 721 k |
| Kaiser-Friedrich-Ring | 46,0 % | 38,5 % | 686 k |
| Gartenfeldstrasse | 58,2 % | 28,6 % | 32 k |

Das **Wohnstrassennetz bleibt unveraendert** - in der Differenzkarte ist es
durchgehend grau. Verbessert hat sich nichts ueber der Rauschgrenze (die
groessten "Gewinne" haben 600-700 Messwerte).

**Merksatz:** Die Regel kostet dort, wo viele Wege sich kreuzen - an den grossen
Ringknoten. Wer den Preis senken will, muss an diesen wenigen Knoten ansetzen
(Signalprogramm, Abfluss), nicht an der Regel: sie ruehrt 95 % des Netzes nicht
an.

## Der Kartenname steht nur noch an EINER Stelle (19.09.2026)

`GameDefaultMap` in `Config/DefaultEngine.ini` ist die Quelle. Alles andere
liest sie:

* `Tools/karte.cmd` -> setzt `%WB_MAP%` und `%WB_MAP_PFAD%` (Batch)
* `Tools/karte.py`  -> `standard_karte()` / `standard_karte_pfad()`
* `Tools/karte.ps1` -> gibt den Kurznamen aus (PowerShell)

**Warum das noetig war - der Zustand davor:** 14 Skripte starteten
`/Game/Maps/WiesbadenCity_Alkis` - eine Karte, die es seit Alkis2 nicht mehr
gibt. Der Start endete im englischen Engine-Dialog "could not be found" und
wartete dort auf einen Klick. Weitere rund 20 Foto-, Mess- und
Diagnose-Skripte zeigten auf Alkis3, Alkis4 oder Alkis15, waehrend die Stadt
Alkis17 war. **Ein Messlauf auf einer toten Karte sieht aus wie ein Messlauf.**
Auch die Desktop-Verknuepfung entstand per Vorgabe auf Alkis15, und
`kopiere_auf_stick.ps1` sicherte "die aktuelle Stadt" = Alkis4.

**Umgestellt:** 44 `.cmd` (Launcher, alle `diag_*`, `perf_*`, `shot_*`,
`Tools/run_bus_*`), 9 Python-Werkzeuge (Vorgabe jetzt `standard_karte_pfad()`,
`WB_*`-Umgebungsvariablen behalten Vorrang) und 4 PowerShell-Skripte
(`make_play_shortcut.ps1`, `kopiere_auf_stick.ps1`, `flight_check.ps1`,
`durchfall_regression.ps1`).

**Bewusst NICHT umgestellt** (der Name ist dort die Aussage): die
`rebake_*.cmd`/`rebuild_*.cmd` - ein Bake schreibt GENAU eine neue Karte -,
`shot_alkis16.cmd`, `fps_alkis10.cmd`, sowie `AGENTS.md`, `NEUER_PC.md` und
`docs/superpowers/plans/*` als Geschichte.

**Zwei Nebenbefunde beim Umstellen:**

* `rebuild_city.py` hatte eine VORGABE fuer `WB_TARGET_MAP` (Alkis4). Ein
  vergessenes Env haette damit die gespielte Stadt ueberschrieben. Jetzt ohne
  Vorgabe: fehlt die Zielkarte, bricht der Lauf mit klarer Meldung ab.
* Eine `.lnk`-Verknuepfung kann nichts "lesen" - sie speichert eine feste
  Befehlszeile. Das Beste ist, dass ihr ERZEUGER die Quelle liest: seitdem
  genuegt nach einem Bake `powershell -File Tools/make_play_shortcut.ps1`
  ohne Argument.

**Die Wache haelt es sauber:** `python Tools/pruefe_kartenname.py` (0 = sauber,
1 = Fundstellen mit Datei und Zeile). Das Muster trifft nur KONKRETE Namen -
Platzhalter wie `WiesbadenCity_AlkisNN` in Beispielen bleiben erlaubt.

## Kartenversionen aufgeraeumt: 48,5 -> 3,7 GB (19.09.2026)

Auf der Platte lagen **13 gebackene Kartenversionen mit 48,5 GB externer
Actors**. Geblieben sind zwei.

**Behalten - mit Begruendung, nicht aus Gewohnheit:**

* **Alkis16** (1,84 GB) - der verifizierte Paket-/Fresh-Clone-Stand (`GameDefaultMap`).
* **Alkis17** (1,84 GB) - lokaler neuer Bake und Rueckfall, nicht im verifizierten
  Release-Paket:
  `rebake_alkis17.cmd` backt aus Alkis16. Ohne sie gaebe es keine Vorlage fuer
  den naechsten Neubau. Beide im Spiel nachgeprueft: je 31 Chunk-Actors, **0
  ohne Render-Geometrie**.

**Geloescht (44,8 GB):** Alkis2 (leere Huelle), Alkis3 und Alkis9Proc (je
12,9 GB, die alten Voll-/Prozedural-Bakes), Alkis4 und Alkis9SM (je ~4 GB),
Alkis7, Alkis8 (Nanite-Defekt, stuerzte beim Laden ab), **Alkis10 bis Alkis13**
(je 1,39 GB - die dokumentierten LEEREN Fehlbakes) und Alkis15 (vom 16er und
17er abgeloest).

**Mit weg, weil sie nur auf geloeschte Karten zeigten:** `rebake_alkis10..13`,
`rebake_lod2`, `rebake_lod2_dgm1`, `rebake_nodgm`, `rebuild_baumfrei`,
`fps_alkis10`. Ein Skript, dessen Karte es nicht mehr gibt, ist keine
Dokumentation - es ist eine Falle, die im englischen "could not be
found"-Dialog endet (genau die 14 Faelle, die einen Tag vorher aufgeraeumt
wurden).

**Was das Loeschen kostet, ehrlich:** Die `.umap`-Huelle (13 KB) holt `git` aus
der Geschichte zurueck. Die NUTZLAST nicht - sie ist gitignored und nur durch
einen Neubau wiederherstellbar (~15 min ueber das Bake-Rezept). Bei den leeren
Fehlbakes ist das kein Verlust, bei den alten Voll-Bakes eine bewusste
Entscheidung: zwei geprueft gleichwertige Staende genuegen als Rueckfall.

**Merksatz fuers naechste Mal:** Die Zahl der Karten waechst mit jedem Bake um
1,4 bis 13 GB. Wer nach einem Bake nicht aufraeumt, hat nach zehn Bakes ein
halbes Terabyte - und Skripte, die auf sechs verschiedene Staende zeigen.

## Sylvia-GLB: Import, Pose und Runtime-Smoke (19.09.2026)

- **Importstruktur:** `sylvia.glb` wird in UE 5.8 als 15 einzelne SkeletalMeshes unter `/Game/Assets/People/Sylvia/sylvia/SkeletalMeshes/tripo_part_0..14` importiert, mit einem gemeinsamen Skeleton und `animations=0`. Runtime und Asset-Test muessen alle 15 Teile abdecken; `/Game/Assets/People/Sylvia/SK_Sylvia` ist nur ein altes Duplikat.
- **Knochennamen:** Interchange normalisiert `mixamorig:*` zu `mixamorig_*`; die Colon-Namen liefern bei `SetBoneRotationByName` keinen Treffer. In UE 5.8 ist `EBoneSpaces::LocalSpace` auskommentiert, daher sind ComponentSpace-Rotationen absolut - vor Pose-Aenderungen echte Bone-Namen und ein Bild pruefen.
- **Finder in Schleifen:** `ConstructorHelpers::FObjectFinder` fuer die 15 Teile darf nicht `static` im Loop sein; sonst cached der Finder den ersten Mesh und alle Komponenten sehen still dasselbe Asset.
- **World-Space-Widget:** Eine `UWidgetComponent` spannt die lokale Y/Z-Ebene auf; grosse `DrawSize`-Werte brauchen eine bewusste Komponentenskalierung. Schwarzer/gespiegelter Text trotz laufendem `NativePaint` bedeutet meist Rueckseite/Facing, nicht fehlenden Widget-Inhalt.
- **Importskript:** `-ExecCmds="py Tools/import_sylvia.py"` wird aus dem Engine-CWD als Python-Name fehlinterpretiert. Funktionierend ist `py exec(open(unreal.Paths.project_dir() + 'Tools/import_sylvia.py').read())`; das Skript beendet den Editor selbst, kein zusaetzliches `Quit` anhaengen.
- **Runtime-Smoke:** UE startet mit Engine-CWD, daher `-WbShotPoseFile` immer absolut angeben. Deterministischer Lauf: installierte UE 5.8 mit absolutem `.uproject`, `-WbTime=13 -WbWeather=Clear -WbShotWhenReady -WbShotPoseFile=<absolut>`, danach `Saved/Diagnose/WbSeries_000.png` pruefen; PowerShell `Start-Process -Wait` verhindert den irrefuehrenden leeren Ruecklauf von direktem `&`.
- **Sylvia-Gate:** `cmd /c Tools\build_gate1.cmd` baut den Modulstand; der fokussierte Cmd-Test ist `Automation RunTests WiesbadenReal.NPC.Sylvia.SceneAssets; Quit`. Als gruen zaehlt der Success-Marker im Log, nicht die fruehen allgemeinen Engine-Condition-Warnungen.
- **Validierungsgrenze:** `WiesbadenReal.NPC.Sylvia.SceneAssets` prueft Pfade, Teile, Niagara, Text und Koordinaten, aber keine Pose. Der letzte visuelle Lauf belegte Spane und Gedankenblase, nicht eine koharente stehende GLB-Figur; absolute Beinrotationen zerlegten das Modell und sind keine Loesung.

## Strassenmoebel: zwei Annahmen, die der Test widerlegt hat (20.09.2026)

**Die OSM-Datei enthielt die Daten nicht.** Das Spec ging davon aus, die acht
Moebel-Kategorien stuenden in `wiesbaden.osm.forest.json`. Tatsaechlich: 1.761
Baenke - und sonst so gut wie nichts (117 Poller, 6 Hydranten, je EINER
waste_basket/recycling/vending_machine/picnic_table, KEIN Briefkasten). Die
Overpass-Abfrage in `OSMDataParser.cpp` holt Strassen, Gebaeude und Ampeln; alles
andere war nie dabei. Vor jedem "wir werten Tag X aus" also im JSON zaehlen -
und dabei aufpassen: die Datei ist EINE Zeile, `grep -c` liefert darum immer 1.
Richtig ist `grep -o '"bench"' datei | wc -l`. Nachgezogen wird wie beim Wald
(`Tools/fetch_street_furniture.py` -> `wiesbaden.osm.moebel.json`, Original
bleibt); beim Mischen vorhandene Knoten ERGAENZEN, nie ein zweites Mal
anhaengen - der C++-Parser haelt Knoten in einer TMap, der zweite Eintrag
ueberschreibt den ersten und verbeult jeden Way, der ihn benutzt.

**`FWiesbadenRoadClearance` taugt nicht fuer Geometrie.** Es beantwortet
"blockiert ja/nein". Wer daraus die Richtung zur Strasse abtastet (16
Richtungen, wachsender Radius), liegt systematisch daneben: gemessen 27 Grad
bei einer Bank, die der Strasse den Ruecken kehren soll, und ein Objekt mitten
auf der Fahrbahn findet in 1,5 m keinen Gehweg und faellt weg. Fuer Richtung,
Abstand und Breite den Lotfusspunkt auf die Segmente rechnen (Gitter wie in
`FNearestWayIndex`, RoadFurnitureGenerator.cpp) - eine Antwort, die Abstand,
Fahrbahnbreite, Gehwegbreite, Laengsrichtung und Normale traegt.



## Zeilenenden: erledigt durch .gitattributes (20.09.2026)

**Die Warnungen weiter oben sind GEGENSTANDSLOS** - die Abschnitte
"Werkzeug-Fallstricke: Zeilenenden bei Skript-Edits" (11.09.), "Hartcodiertes
CRLF in einem Python-Patch" und "Der Editor schreibt DefaultEngine.ini als
CRLF" (18.09.). Sie beschreiben Handarbeit gegen ein Problem, das es nicht mehr
gibt. Wer sie liest, soll hier weiterlesen statt dort anzufangen.

Das Repo hat jetzt eine `.gitattributes` mit `* text=auto eol=lf`. Git
normalisiert beim EINCHECKEN auf LF, also ist gleichgueltig, was ein Werkzeug
auf die Platte schreibt: derselbe Text ergibt denselben Blob. Damit ist der
Churn nicht leichter zu reparieren, sondern unmoeglich.

Konkret heisst das fuer die Arbeit an dieser Datei:

- `write_text()`, `newline='
'`, ein Editor, ein fremder Agent - alles egal.
  Nachgewiesen: AGENTS.md einmal komplett auf LF und einmal komplett auf CRLF
  umgeschrieben, `git diff` blieb beide Male LEER.
- Die alte Regel "nur binaer ANHAENGEN, nie neu schreiben" ist damit hinfaellig.
- Was NICHT weggeht: eine Datei kann in sich gemischt sein, wenn ein Werkzeug
  einzelne Zeilen anders schreibt als den Rest. Das ist kein Git-Problem mehr,
  aber unschoen - `Tools/test_zeilenenden.py` meldet es.

**Ausnahmen in der Regel, beide mit Grund:**

- `*.cmd` / `*.bat` liegen auf der Platte als CRLF. cmd.exe springt bei `goto`
  ueber Byte-Versaetze und kann in reinen LF-Dateien mitten in einer Zeile
  landen. Im Baum nutzt genau eine der 111 .cmd-Dateien Sprungmarken
  (`shot_heli_paar.cmd`), aber CRLF ist fuer Windows-Stapeldateien ohnehin das
  richtige Format. Der Index bleibt auch dort LF.
- `*.uasset`, `*.umap`, Bilder, Modelle, Ton sind ausdruecklich `binary`. Git
  erkennt sie an ihren Null-Bytes auch selbst - aber eine zerstoerte .uasset
  faellt erst auf, wenn der Editor sie nicht mehr laedt. Das darf nicht an
  einer Heuristik haengen. **Wer eine neue Binaer-Endung einbringt, traegt sie
  ein**; `Tools/test_zeilenenden.py` faellt sonst.

**Die Einmal-Normalisierung** (Commit-Nachricht hat die Zahlen): 87 Dateien im
Index, davon mit `--ignore-cr-at-eol` nur `.gitattributes` selbst uebrig -
die anderen 86 waren reine Zeilenenden, 0 Binaerdateien betroffen. Der Baum war
vorher uneinheitlich (31 .cpp CRLF gegen 152 LF) und SIEBEN Dateien waren in
sich gemischt, AGENTS.md mit 3078 CRLF plus 64 blanken LF die schlimmste.

**Eine Falle beim Umstellen, fuer den naechsten, der so etwas macht:** nachdem
193 Dateien in einer Sekunde neu geschrieben waren, meldete `git status` alle
193 als geaendert, waehrend `git diff` leer war und `git hash-object` denselben
Hash lieferte wie der Index. Das ist der Stat-Cache (Racy-Git: Datei-mtime
nicht aelter als der Index, Groesse durch CRLF anders), NICHT ein
fehlgeschlagener Filter. `git update-index --refresh` half nicht, ein
`git add` der Pfade schon - Inhalt identisch, nur die Stat-Information wird
erneuert; dass `git diff --cached` danach leer blieb, ist zugleich der Beweis,
dass wirklich keine Datei inhaltlich abwich. Nicht in Panik `git reset --hard`
hinterherwerfen.


## Ausliefern bei fremder Arbeit im Baum: Tools/ausliefern.py (20.09.2026)

In diesem Baum arbeiten mehrere Agenten und der Nutzer gleichzeitig. `git add -A`
ist deshalb verboten, und `git add <pfade>` hat eine Falle, die an diesem Tag
zugeschnappt ist.

**Die Falle, nachgemessen:** `git add` mit mehreren Pfaden ist ALLES-ODER-NICHTS.
Trifft EIN Pfad auf nichts, bricht es mit `fatal: pathspec ... did not match any
files` ab (Exitcode 128) und staged auch die ANDEREN Pfade nicht. Zwei Faelle,
nur einer ist gefaehrlich:

- Datei nur im ARBEITSBAUM geloescht, im Index noch da: `git add` geht durch und
  nimmt die Loeschung mit. Harmlos.
- Pfad in Index UND Arbeitsbaum weg (nach `git rm`): Abbruch, nichts gestaged.

Am 20.09. war es der zweite Fall: nach `git rm Tools/enable_nanite_chunks.py`
scheiterte `git add` auf genau diesem Pfad, und der folgende Commit enthielt
nur die Loeschung - acht Aenderungen fehlten. Nichts hat gewarnt; der Commit
sah erfolgreich aus. Aufgefallen ist es nur, weil hinterher jemand
`git show --stat` gelesen hat.

**Die Abhilfe:**

    python Tools/ausliefern.py zeigen  <pfade...>   # was geht mit, was bleibt
    python Tools/ausliefern.py commit -F nachricht.txt <pfade...>
    python Tools/ausliefern.py pruefen
    python Tools/ausliefern.py push

Es committet mit `git commit --only -- <pfade>` (nimmt Loeschungen mit, geht am
Index fremder Dateien vorbei) und vergleicht danach die Dateiliste des Commits
mit der gewollten. Weicht sie ab, bricht es ab, statt einen halben Commit
stehen zu lassen. Die gewollten Pfade landen im Merkbuch
`.git/ausliefern-journal.json`; `push` prueft damit JEDEN unveroeffentlichten
Commit und verweigert, wenn Fremdes darin liegt (`--trotzdem` hebt das auf).

Nach `--amend` oder einem Rebase stimmen die Commit-Nummern nicht mehr - solche
Commits gelten als UNGEPRUEFT und werden gemeldet, nicht durchgewunken.

Selbsttest: `python -m unittest discover -s Tools -p "test_ausliefern.py"`
(15 Tests, jeder auf einem echten Wegwerf-Repo; einer haelt das Verhalten von
`git add` selbst fest - faellt er, hat git sich geaendert).


## Ampel-Freigabegruppen: der Achsenfehler und sein Preis (20.09.2026)

**Der Fehler:** `AxisForBearing` teilte nach FAHRTRICHTUNG (`Peilung < 180`).
Die Peilung kommt aber aus `Atan2(dY, dX)` - 0 Grad ist Ostfahrt, 90 Grad
Nordfahrt. Beide lagen unter 180 und damit in DERSELBEN Freigabegruppe: an
jeder der 1073 signalisierten Kreuzungen bekamen zwei zueinander senkrechte
Richtungen gleichzeitig Gruen. Zugleich lagen die beiden Richtungen DERSELBEN
Strasse (0 und 180 Grad) in verschiedenen Gruppen und bekamen nie zusammen
Gruen. Ein Test hielt das fest ("Peilung 90 -> Achse 0") und hat den Fehler
damit festgeschrieben, statt ihn zu finden. Richtig ist `Fmod(Peilung, 180)`.

**Konfliktfreiheit kommt nicht aus der Faustregel.** Achse x Abbiegeart trifft
die Regelkreuzung, aber nicht den funfarmigen Knoten, die schiefe Einmuendung
oder zwei Zufahrten, die in dieselbe Spur einfaedeln.
`MakeGroupsConflictFree` faerbt die Gruppen darum gierig nach, geprueft mit
`FWiesbadenTrafficSimulation::DoConnectionsConflict` - DERSELBEN Rechnung, mit
der die Simulation ihre Kreuzungsregel baut (sie kennt beide Faelle:
schneidende Wege UND gemeinsame Zielspur). Zwei Rechnungen fuer dieselbe Frage
laufen auseinander.

**Eine Gruppe ohne Phase ist DAUERHAFT ROT** (`GetGroupAspect` faellt nicht auf
Gruen zurueck, es prueft `Phase.Group != Group`). Solange die Phasen je ACHSE
gebaut wurden, konnte das passieren: mit `bProtectedLeftTurns = false` bekamen
die Linksabbieger-Gruppen keine Phase. Die Phasen folgen jetzt den tatsaechlich
BENUTZTEN Gruppen. Nebenwirkung: die Einstellung steuert nur noch die
Gruenzeit, nicht mehr das Ob - ein Linksabbieger kreuzt den Gegenverkehr, die
Faerbung trennt ihn also ohnehin.

**DER PREIS, gemessen - er ist hoch.** A/B auf derselben Karte, im selben
Build, Spieler 250 m daneben geparkt, je 300 s, Bahnhofsplatz (66 Spuren,
rund 1,6 Mio. Messwerte je Lauf):

| | Faustregel | konfliktfrei |
|---|---|---|
| Mitteltempo | 7,8 km/h | **4,9 km/h** |
| Anteil am Limit | 18,0 % | **11,2 %** |
| Steh-Anteil | 68,7 % | **81,2 %** |
| Umlauf im Mittel | 36 s | 51 s |
| Spanne | 20..70 s | **20..180 s** |

Also 37 Prozent weniger Tempo. 6357 Verbindungen mussten ihre Wunschgruppe
verlassen, 466 Gruppen kamen ueber die vier der Faustregel hinaus, groesste
Gruppenzahl an einer Kreuzung 10 - macht dort 10 Phasen und 180 s Umlauf.

Der Grund liegt im Verfahren: eine gierige FAERBUNG gibt jeder konfliktbehafteten
Bewegung eine eigene Phase. Ein Verkehrsplaner macht das Gegenteil und fasst
moeglichst viele VERTRAEGLICHE Bewegungen in einer Freigabe zusammen (Clique
statt Farbe). Wer hier weiterarbeitet, faengt dort an.

Zweiter Verdacht, NICHT gemessen: `DoConnectionsConflict` meldet auch
gemeinsame ZIELSPUR als Konflikt. Das ist fuer die Laufzeitregel richtig (ein
Fahrzeug wartet, bis der andere durch ist), fuer eine Freigabegruppe aber
womoeglich zu streng - zwei einfaedelnde Stroeme werden real gemeinsam
freigegeben und sortieren sich ueber Luecken. Wieviel der 6357 Verschiebungen
darauf entfaellt, ist offen.

**MESSFALLE, die zweimal Zeit gekostet hat:** `-WbGoto=<Ort>` parkt den
Spieler MITTEN INS MOTIV, und sein Fahrzeug blockiert dort eine Spur. Am
Bahnhofsplatz gemessen: 5,8 km/h mit dem Spieler auf dem Platz gegen 12,0 km/h
ohne ihn - der Aufbau halbiert das Ergebnis. Fuer Flussmessungen den Spieler
per Koordinate danebensetzen (`-WbGoto=22230,145934` ist 250 m noerdlich des
Bahnhofsplatzes, innerhalb der Spawn-Reichweite).

**Vergleichsschalter:** `-WbOhneKonfliktgruppen` laesst die Gruppen bei der
Faustregel stehen - dasselbe Muster wie `-WbOhneKreuzungsregel`, damit sich
beide Zustaende auf derselben Karte und im selben Build messen lassen.


## Karten aufraeumen: der Bake schlaegt vor, geloescht wird gefragt (20.09.2026)

Jeder Bake legt eine neue Stadtkarte an und laesst die alte stehen - richtig
so, aber JE Karte 1,9 GB externe Actors. `Tools/karten_aufraeumen.py` behaelt
genau zwei: die neue und ihre Vorgaengerin (der Rueckweg, wenn sich die neue
erst im Spiel als schlecht erweist).

    python Tools/karten_aufraeumen.py                 # nur zeigen
    python Tools/karten_aufraeumen.py --loeschen      # fragt nach
    python Tools/karten_aufraeumen.py --loeschen --ja # ohne Rueckfrage

**Warum der Bake NICHT selbst aufraeumt:** `rebuild_city.py` laeuft im Editor
mit `-unattended`, und dort gibt es niemanden, den man fragen koennte - ein
`input()` haette in einem abgesetzten Bake stundenlang gewartet, ohne dass es
jemand sieht. Der Bake legt darum nur
`Saved/Diagnose/bake_vorschlag.json` ab (neue Karte + Vorgaengerin) und nennt
im Log die naechsten Schritte. Zweiter Grund: "FERTIG" ist kein Beleg -
Alkis10 und Alkis11 meldeten Erfolg und waren im Spiel nur Gras.

**Vier Sicherungen, jede mit Gegenprobe im Test:** ohne Terminal wird nicht
geloescht (sondern der Befehl ausgegeben); die gespielte Karte aus
`GameDefaultMap` ist auch mit `--ja` unantastbar; wiegt die NEUE Karte unter
1,7 GB externe Actors, gilt sie als Leerbake und es wird nichts geloescht
(die Grenze stammt aus `bake_abnahme.py`: volle Stadt 1,8-1,9 GB, Leerbake
1,4 GB); und die Rueckfrage nimmt nur das ausgeschriebene "ja" - "j", "y",
"yes" und die blosse Eingabetaste loeschen nichts.

**ZWEI TESTFALLEN, die hier Zeit gekostet haben:**

* **`sys.stdin.isatty()` ist in dieser Umgebung NICHT verlaesslich.** Stand in
  derselben Aufrufkette vorher ein Here-Dokument (`python - <<'PY'`), ist
  stdin verbraucht und `isatty()` liefert False - sonst True. Ein Test, der
  den einen oder anderen Zustand VORFINDEN will, ist mal gruen und mal rot,
  ohne dass sich eine Zeile geaendert hat. Beide Zustaende gehoeren
  hergestellt (stdin durch eine Attrappe ersetzen), nie vorausgesetzt.
* **Der Bytecode-Zwischenspeicher taeuscht bei schnellen Umschreibungen.**
  Wird eine .py-Datei zweimal innerhalb derselben Sekunde geschrieben, kann
  Python den `.pyc` des VORIGEN Stands weiterverwenden - eine Gegenprobe
  ("faellt der Test, wenn ich die Sicherung aushebele?") misst dann den
  falschen Stand. Vor jeder solchen Messung `Tools/__pycache__` loeschen.


## Sebbo-Hauptsitz: die Treppe war nicht begehbar (20.09.2026)

Der Auftrag lautete, mit einem Bild zu belegen, dass man die Treppe
hinauflaeuft. Beim Nachrechnen stellte sich heraus, dass man es NICHT konnte.

**Der Defekt:** Das Podest jedes Geschosses deckte den GANZEN Grundriss der
Treppenhaus-Haelfte und lag damit als Decke ueber dem Lauf, der von unten
genau dorthin steigt. Kopffreiheit ueber der achten Stufe 155 cm, ueber der
fuenfzehnten 5 cm, die sechzehnte lag IM Podest. Man kam 220 von 400 cm hoch.

Die bestehenden Tests sagten gruen: Stufenhoehe 25 cm, lueckenlos, je Geschoss
ein Podest. **Kopffreiheit hatte keiner geprueft** - und ohne sie ist eine
Treppe eine Skulptur. Jetzt liegen Lauf und Podest NEBENeinander (der Lauf in
der aeusseren Y-Haelfte, das Podest in der inneren), wie im echten Bau die
Treppenoeffnung im Podest bleibt. Ueber jeder Stufe stehen 380 cm.

**Nachweis im Spiel, nicht auf dem Papier:** `-WbTreppenProbe` laesst eine
Kapsel (Radius 40, Halbhoehe 90) mit der Schrittregel des Fussgaengers
(anheben, vorwaerts, absetzen, hoechstens 40 cm) die Treppe hochsteigen -
gegen die ECHTE Kollision. Ergebnis: 157 Schritte, 57,7 m gestiegen, Hoehe
61,35 m, Dach erreicht. Ablage: `Saved/Diagnose/treppenprobe.json`.

**DREI FALLEN, die die Sonde erst blind gemacht haben:**

1. **`AddIgnoredActor(this)` aus der Bodensuche uebernommen.** Der Turm IST
   das, wogegen getastet wird - die Sonde ignorierte das ganze Gebaeude und
   traf nur das Landscape. Sie meldete ueberall "nichts unter den Fuessen",
   was wie eine kaputte Treppe aussah.
2. **`GetActorLocation()` ist NICHT der Bauort.** Der Actor wird im Ursprung
   gespawnt und nie bewegt; gesetzt werden nur seine Komponenten. Die Sonde
   sondierte bei (0,0,0) ins Leere. Der Bauort wird jetzt in `BuiltBase`
   gemerkt.
3. **Eine Kapsel, die die Trittflaeche genau beruehrt**, meldet beim Sweep
   sofort einen Treffer (`bStartPenetrating`). Als Wand gewertet kam die
   Sonde keinen Schritt weit. Sie startet jetzt 2 cm hoeher und wertet
   `bStartPenetrating` nicht als Hindernis.

**ZWEI WEITERE DEFEKTE, gefunden und gemessen, NICHT behoben:**

* Das Gelaende steht an der Treppenhausecke **250 cm ueber dem Fusspunkt** des
  Turms: der Turm setzt sich auf den Bodenpunkt seiner MITTE und hat keine
  Einschnitt- oder Sockelloesung, also liegt das Erdgeschoss am Hang im
  Erdreich. Die Sonde beginnt darum auf der ersten Stufe ueber dem Gelaende.
* **Ein Baum waechst mitten durch das Treppenhaus** (im Bild deutlich zu
  sehen). Dieselbe Ursache wie beim Nerobergbahn-Wagen: die Freihalteflaechen
  der Bewuchs-Streuung kennen nur Strassen, keine Bauwerke.

**Bildaufnahme im Turm - zwei Stolpersteine:** `-WbShotDelay` steuert
`-WbShotWhenReady`, NICHT `-WbScreenshot` (falsch gepaart entsteht gar kein
Bild). Und `-WbTeleportTo` nimmt das FAHRZEUG mit, wenn der Spieler darin
sitzt - dann fuellt das Armaturenbrett das Bild. Mit `-WbZuFuss=<Sekunden>`
vorher aussteigen.


## Die Default-Karte stellt kein Bake mehr um (20.09.2026)

**Vorher:** `AWiesbadenWorldBuilder::SaveCityAsMap` schrieb `GameDefaultMap`
und `EditorStartupMap` BEDINGUNGSLOS in `Config/DefaultEngine.ini`. Ein
PROBE-Bake - und die meisten sind Proben - stellte damit still die gespielte
Stadt um. Gemerkt hat man es erst, wenn `git status Config/` eine Aenderung
zeigte, die niemand gewollt hatte, und zurueckgenommen wurde sie jedes Mal von
Hand. Bei Alkis10 und Alkis11 war es schlimmer: die meldeten FERTIG, waren im
Spiel nur Gras - und hatten die funktionierende Karte da schon verdraengt.

**Jetzt:** `bMakeNewMapDefault` (UPROPERTY am WorldBuilder) steht auf **false**.
Ohne ausdrueckliche Ansage bleibt die gespielte Karte, wie sie ist; der Bake
sagt im Log als WARNUNG, was er nicht getan hat und wie man es tut. Der
geprueften Schreibpfad selbst (mit allem Wissen ueber den stillen
GConfig-Flush-No-Op) ist unveraendert - er laeuft nur noch auf Ansage.

    # Probe-Bake (Regelfall): gespielte Karte bleibt
    rebake_alkisNN.cmd

    # Live schalten, ausdruecklich:
    set WB_LIVE_SCHALTEN=1
    rebake_alkisNN.cmd

Im Editor: `bMakeNewMapDefault` im Details-Panel des WorldBuilders.

**Wache:** `WiesbadenReal.GIS.MapBake.DefaultKarteNurAufAnsage` prueft die
Vorgabe am CDO. Gegengeprueft - mit `= true` faellt sie. Sie prueft ausserdem,
dass der Schreibpfad selbst noch funktioniert, damit die Abschaltung ihn nicht
heimlich beschaedigt.

**Reihenfolge, die sich daraus ergibt:** backen -> abnehmen
(`bake_abnahme.py`) -> live schalten -> aufraeumen
(`karten_aufraeumen.py`). Jeder Schritt ist eine eigene Entscheidung; keiner
passiert als Nebenwirkung des vorigen.


## Galerie der Stadtansichten: Tools/galerie.py (20.09.2026)

Unter `Saved/Diagnose` liegen 270 Bilder und 1,9 GB. `Tools/galerie.py` sucht
daraus die echten SPIELANSICHTEN, verkleinert sie und schreibt
`Saved/Diagnose/galerie/index.html` - durchblaetterbar mit Pfeiltasten, nach
Datum gruppiert.

    python Tools/galerie.py --zeigen   # nur die Auswahl, mit Begruendung
    python Tools/galerie.py            # bauen (191 Bilder, 37 MB, ~25 s)

**Die Auswahl ist das Eigentliche.** Erkannt wird ein Spielbild am
Seitenverhaeltnis (1.50 bis 1.95) und an der Mindestbreite - ein Kontaktbogen
(4320 x 574) oder ein Hochformat faellt damit von selbst heraus. Dazu eine
kurze Namensliste fuer das, was im Format passt, aber keine Stadt zeigt:
Stau-Karten, Kartenausschnitte, UI-Aufnahmen. Was weggelassen wurde, gibt das
Werkzeug MIT GRUND aus.

**Nach Datum gruppiert, neueste zuerst** - und das ist keine Kosmetik: 85 der
191 Bilder stammen vom 04.09.2026, seither sind Strassenmoebel,
Signalprogramme, Fahrzeuge und der Sebbo-Hauptsitz dazugekommen. Eine Galerie,
die ein Bild von damals ohne Datum neben eines von heute haengt, behauptet
etwas Falsches.

**FUER DIE VORSCHAU BRAUCHT SIE EINEN DATEISERVER.** Der `htmlPath`-Modus von
`register_preview` serviert NUR die eine HTML-Datei - die Bilder daneben
laufen auf 404, und im Browser steht der Aufbau ohne ein einziges Bild da
(genau so gesehen). Richtig:

    cd Saved/Diagnose/galerie
    python -m http.server 8790 --bind 127.0.0.1

dann URL samt Prozessnummer registrieren. Als lokale Datei (file://) geht es
ohne Server.

**Zwei Fallen beim Erzeugen der Seite**, beide im Browser erst als SCHWARZE
BUEHNE sichtbar (Aufbau da, kein Bild):

* `%`-Formatierung und HTML/JS vertragen sich nicht: `max-width:100%` und das
  JS-Modulo werden als Formatzeichen gelesen ("unsupported format
  character"). Die Seite benutzt darum Platzhalter `@@NAME@@` und
  `str.replace`.
* Beim Umstellen blieb ein `%%` aus der alten Formatierung im JavaScript
  stehen - im Browser ein SyntaxError, der das ganze Skript kippt. Ein Test
  haelt beides fest (`test_galerie.py`, 16 Pruefungen).

Die erzeugten Dateien liegen unter `Saved/` und sind damit nicht versioniert;
versioniert ist nur das Werkzeug.


## Die Projektuebersicht zieht ihre Zahlen selbst (20.09.2026)

`preview.html` wurde von Hand gepflegt und lag entsprechend daneben: sie
meldete "Phase 1 (GIS-Pipeline) fertig" und "Noch offen: Phasen 2-12
(Fahrzeuge, Traffic-KI, Pedestrians, Player, Wanted-Level, UI, Audio, Wetter,
Optimierung)", waehrend genau das alles lief. Auch die Pfade waren die des
alten Rechners.

Jetzt erzeugt `Tools/uebersicht.py` die Seite:

    python Tools/uebersicht.py            # preview.html neu schreiben
    python Tools/uebersicht.py --zeigen   # nur die Zahlen

**Vier Regeln**, die sie von der alten unterscheiden: jede Zahl wird GEMESSEN
(Dateien und Zeilen ueber `git ls-files`, Testmakros gezaehlt, die INI
gelesen, git gefragt); jede Zahl nennt IHRE QUELLE in der Zeile daneben;
Laufzeit-Zahlen tragen den ZEITPUNKT ihres Laufs ("Gemessen im Spiel am
20.09.2026 um 21:26 - der Stand JENES Laufs, nicht der Gegenwart"); und was
sich nicht messen laesst, steht nicht drin - "Modul X ist fertig" ist keine
Messung.

**ZWEI ZAEHLFEHLER, die erst die Gegenprobe zeigte** (ein Test haelt zwei
Zaehlwege gegeneinander):

* Ein Testname in einem KOMMENTAR sieht aus wie eine Registrierung. Die erste
  Fassung las die ganze Datei und kam auf 249 Gebiets-Eintraege bei 247
  Makros; die zwei Extras waren Zeilen wie
  `// "WiesbadenReal.Vehicles.CarLights" war ...`. Gezaehlt wird jetzt der
  MAKROAUFRUF, nicht der Dateitext.
* `([^.]*)\.` schnitt die Signalprogramm-Zeile bei "Spanne 20" ab, weil die
  Spanne "20..180 s" heisst und Punkte enthaelt. Auf der Seite stand danach
  eine Zahl, die es nicht gibt - und sie sah so verbindlich aus wie jede
  andere. Gefangen wird jetzt bis Zeilenende, der Schlusspunkt faellt weg.

`preview.html` bleibt versioniert (die Vorschau zeigt sie ueber `htmlPath`),
ist aber ein ERZEUGNIS: wer sie von Hand aendert, verliert es beim naechsten
Lauf. Selbsttest: `Tools/test_uebersicht.py`, 14 Pruefungen.


## Moebel-Kalibrierung: Bankett neben Wegen ohne Gehweg (20.09.2026)

**Befund vorher:** 2012 von 3607 OSM-Moebelknoten kamen in die Stadt (55,8 %),
1464 wurden "ohne befestigten Rand" verworfen. 78 Prozent dieser Verwuerfe
liegen an `footway`, `path` und `track` - Wegtypen, denen `RoadTypeLibrary`
die Gehwegbreite 0,0 gibt. Dort ist das RICHTIG (ein Fussweg hat keinen
Gehweg), fuer die Moebel aber folgenschwer: der "befestigte Streifen" war nur
so breit wie der Weg selbst. Bei 1,80 m Fussweg endete er 0,90 m von der
Achse, und eine Bank einen Meter daneben lag schon ausserhalb.

**Die Kalibrierung sind zwei Zahlen in `FRoadFurnitureSettings`:**

    FurnitureVergeCm        = 250   (neu)   Ersatzbreite, wenn Gehweg = 0
    FurnitureDockingRangeCm = 250   (150)   Reichweite zum Heranziehen

Am 1,80-m-Fussweg heisst das: angenommen wird bis 5,90 m statt bis 2,40 m,
und alles innerhalb 3,40 m bleibt, WO OSM ES VERORTET HAT. Das Zielband
bleibt schmal (halbe Wegbreite + 50 cm) - das Bankett erweitert nur, was noch
als "am Weg" gilt.

**GEMESSEN am echten Bestand** (Laufzeit-Stadtbau auf `__AaaRuntimeShot` mit
`wiesbaden.osm.moebel.json`, je ein Lauf vor und nach der Aenderung):

| | vorher | nachher |
|---|---|---|
| uebernommen | 2012 (55,8 %) | **2986 (82,8 %)** |
| angedockt | 1357 | 1028 |
| im Gebaeude verworfen | 131 | 131 |
| ohne Rand verworfen | 1464 | **490** |

974 Moebel mehr, die Verwuerfe fallen um 67 Prozent - und 329 Objekte werden
nicht mehr von ihrer kartierten Stelle weggezogen. Die Gebaeude-Regel bleibt
unveraendert (131), wie sie soll.

Die Vorhersage aus der Untersuchung lautete "59 % -> 85 %". Der ZUWACHS
stimmte (+26 vorhergesagt, +27 gemessen), die Grundlinie lag drei Punkte zu
hoch.

**WICHTIG - die gespielte Stadt hat das noch nicht.** Die Platzierung laeuft
im BAKE; Alkis16 traegt weiter die alten 2012 Moebel. Gemessen wurde am
Laufzeit-Stadtbau. Damit es im Spiel ankommt, braucht es einen Re-Bake.

Test: `WiesbadenReal.GIS.RoadFurniture.BankettOhneGehweg` - eigener Fussweg
ohne Gehweg, vier Faelle (im Bankett bleibt es stehen, dahinter wird
herangezogen, weit weg bleibt verworfen), und die Zahl der Andockungen als
Mass dafuer, wie sehr die Regel die Kartierung noch verbiegt.


## Release-Pipeline baute mit der FALSCHEN Engine (21.09.2026)

`Tools/build_release.ps1` leitete den Engine-Pfad aus `$Root` ab:

    $Engine = Join-Path $Root "UE_5.8\Engine"     # ALT, falsch

Das ist die **Kopie vom 11.08.2026** unter `C:\freebuff\...\UE_5.8`.
`Tools/build_gate1.cmd`, `Wiesbaden_spielen.cmd`, die Desktop-Verknuepfung und
`package_game.cmd` benutzen dagegen die **installierte** Engine
(`C:\Program Files\Epic Games\UE_5.8`, 07.09.2026). Das Projekt-Intermediate
traegt deren Shared-PCH - der Pipeline-Build starb darum mitten in einem
ENGINE-Header:

    GenericPlatform.h(10,8): error C2953: "SelectIntPointerType":
                              Klassenvorlage wurde bereits definiert

Das sieht nach kaputtem Engine-Quelltext aus und ist keiner (dieselbe
Signatur wie die bekannte SharedPCH-Korruption). **Der Beweis war der
Vergleich:** `build_gate1.cmd` lief Minuten vorher am selben Baum GRUEN. Wenn
zwei Kompilierwege am gleichen Quelltext verschieden ausgehen, liegt es nicht
am Quelltext.

**Warum kein Abbruch, sondern ein Compilerfehler:** die Pipeline prueft mit
`Test-Path`, ob `Build.bat` existiert - und die alte Kopie existiert ja. Ein
Pfad, der DA ist und trotzdem FALSCH ist, faellt keiner
Vorhandenseins-Pruefung auf. Jetzt: eigener Parameter `-EngineRoot` mit der
installierten Engine als Vorgabe.

**Gate 3 fand eine echte Luecke:** `WbSpawnPursuer` ist `UFUNCTION(Exec)`,
stand aber nicht in `docs/reference/wbdev-konsolenbefehle.md`. Die Pruefung
(`Tools/check_wbdev_docs.ps1`) verlangt den Abschnitt UND die `UE_LOG`-Zeile
woertlich. Nachgetragen; jetzt 14 Exec-Befehle gedeckt.

**Gemessen:** Voller Release-Lauf 13,1 min (nicht die frueher notierten
Stunden - der Cook lief inkrementell). Ergebnis: Paket 3,2 GB, EXE 21.09.
02:45, Vorgaenger nach `Saved/Package_previous` gesichert,
Desktop-Verknuepfung `Wiesbaden Real (Paket).lnk` nachgezogen.

**Die EXE gestartet und belegt:** die 171-KB-`WiesbadenReal.exe` ist nur der
Shim - der echte Prozess ist ein ZWEITER gleichen Namens (3,2 GB, Titel
`WiesbadenReal (64-bit Development PCD3D_SM6)`). Wer nur den ersten misst,
haelt einen laufenden Build fuer tot. Das Paket-Log liegt unter
`Saved/Package/Windows/WiesbadenReal/Saved/Logs/`, NICHT in `%LOCALAPPDATA%`:
Alkis16 in 3,2 s geladen, 52.689 Schilder, 117.351 Spuren, 1.073 Ampeln,
Sebbo-Hauptsitz 449 Bauteile, 31 Chunk-Actors (0 ohne Render-Geometrie),
0 Fehlerzeilen.


## Eine Engine fuer alle Werkzeuge - und ein Waechter davor (21.09.2026)

Auf diesem Rechner liegen ZWEI Engines 5.8 nebeneinander. Sie heissen gleich,
beide existieren, beide bauen - getrennt werden sie erst durch die
PATCH-Nummer aus `Engine/Build/Build.version`:

    C:\Program Files\Epic Games\UE_5.8          5.8.2   Build.bat 09.09.2026
    C:\freebuff\WiesbadenReal_Sicherung\UE_5.8  5.8.1   Build.bat 11.08.2026

**Die eine Quelle:** `Tools/engine.cmd` (setzt `WB_ENGINE`, `WB_BUILD_BAT`,
`WB_EDITOR`, `WB_EDITOR_CMD`, `WB_RUNUAT`) und `Tools/engine.py`
(`engine_wurzel()`, `build_bat()`, `editor_cmd()`, ...) - gebaut nach dem
Vorbild von `Tools/karte.cmd`. `WB_ENGINE` in der Umgebung gewinnt. Die
gewaehlte Engine wird gegen `EngineAssociation` des .uproject geprueft, der
Ordnername entscheidet also NICHT.

**Der Waechter:** `python Tools/pruefe_engine.py` durchsucht alle von git
verfolgten Dateien und meldet jede Nennung einer anderen Engine - in
Sekunden, bevor ein Compiler dasselbe in zehn Minuten und mit irrefuehrender
Meldung tut. Er laeuft als **Gate 0** der Release-Pipeline, vor dem
Kompilieren. Regel: **ein Kommentar darf jede Engine nennen** (er warnt ja
vor ihr), **Code nicht**. Das ersetzte den groessten Teil der Ausnahmeliste -
eine Liste von Dateinamen veraltet, eine Regel nicht. Uebrig: AGENTS.md
(Fliesstext) und `Tools/fix_paths_neuer_pc.mjs` (Umzugstabelle, alter Pfad
ist die Quellseite).

**Gemessen:** 128 Nennungen in 109 Dateien. 14 Abweichler gefunden, nach der
Kommentarregel blieben 6 echte:

* `build_gate1_nouba.cmd`, `check_ka52_mats.cmd`, `import_ka52.cmd` rufen
  jetzt `engine.cmd` und tragen gar keinen Pfad mehr.
* `sweep_map_zoom.sh` auf die installierte Engine gezogen.
* **`Tools/fix_paths_neuer_pc.mjs` bildete die INSTALLIERTE Engine auf die
  Plattenkopie ab** - ein erneuter Lauf haette jeden richtigen Pfad auf die
  veraltete 5.8.1 umgeschrieben. Beim Umzug war das richtig; ein einmaliges
  Werkzeug veraltet nicht von selbst, es bleibt scharf liegen. Abbildung
  umgedreht.

**DREI EIGENE FEHLER beim Bau des Waechters, alle mit derselben Handschrift:
er meldete "alles in Ordnung", weil er nichts fand.**

1. Die Trenner-Zeichenklasse verlor durch die Shell eine Backslash-Ebene und
   passte nur noch auf Vorwaerts-Schraegstriche - KEIN Windows-Pfad wurde
   gefunden.
2. Die Wortgrenze hinter `REM` wurde zu einem echten **Backspace-Byte
   (0x08)**. `grep` zeigt das brav als "REM" an; sichtbar wurde es erst an
   `repr(muster.pattern)`. Jede Kommentarzeile galt danach als Code.
3. Der normalisierte Pfad behielt doppelte Trenner
   (`C://Program Files//`), worauf der Waechter die KANONISCHE Engine als
   Abweichler meldete.

Konsequenz: im Waechter steht jetzt **kein einziger Backslash-Escape** mehr
(Zeichenklassen aus `chr()`, Kommentarerkennung ohne Regex).

**Und ein vierter, im TEST:** die erste Testfassung normalisierte die Zeile
selbst und pruefte damit nur das Muster statt des Produktionswegs - nimmt man
dem Waechter die Normalisierung weg, blieb sie gruen. `test_backslash_pfad_im
_ECHTEN_weg` geht jetzt durch `abweichler()`. Gegengeprueft: alle drei
Fehler oben faerben die Suite rot. 19 Tests in
`Tools/test_pruefe_engine.py`.


## Die Release-Gates laufen jetzt vor dem Commit (21.09.2026)

Die Gates gab es schon - aber erst in `build_release.cmd`, also erst wenn
jemand ein Paket wollte. Ein Fehler von heute fiel damit Tage spaeter auf,
verteilt ueber mehrere Commits, und blockierte ausgerechnet den Lauf, der
Stunden dauert.

**GEMESSENE Kosten - daraus folgt der Entwurf, nicht aus einer Meinung:**

    Gate 0  Engine-Pfade        1 s
    Python-Suiten              46 s
    Gate 1  Kompilieren         2 s ohne C++-Aenderung, Minuten mit
    Gate 2  Unit-Tests          Minuten (startet den Unreal-Editor)
    Gate 3  Rauchtest           Minuten (mehrere Editor-Sitzungen)

Ein Hook, der vor JEDEM Commit eine Viertelstunde braucht, wird binnen eines
Tages mit `--no-verify` umgangen - und prueft dann gar nichts mehr. Darum
ZWEI Stufen:

* **pre-commit** (gemessen 48 s): Gate 0, alle Python-Suiten, Gate 1 **nur
  wenn C++ vorgemerkt ist**. Wer ein Python-Werkzeug aendert, wartet nicht
  auf einen Compiler.
* **pre-push** (Minuten): zusaetzlich Gates 2 und 3. Dort ist die Wartezeit
  vertretbar, und nichts verlaesst den Rechner ungeprueft. Die Blockade
  wandert damit vom Paketieren an die Stelle, an der sie noch billig ist.

Notausgang: `--no-verify` oder `WB_KEINE_GATES=1`. Absichtlich - ein
Wachposten ohne Tuer wird eingerissen, nicht benutzt.

**Die Hooks liegen unter `Tools/git-hooks/`, NICHT in `.git/hooks`** - der
Ordner ist nicht versioniert und ueberlebt keinen frischen Klon. `core.hooks
Path` zeigt dorthin; das ist eine LOKALE Einstellung und muss einmal je Klon
gesetzt werden:

    python Tools/hooks_einrichten.py            # einschalten
    python Tools/hooks_einrichten.py --zeigen   # Stand
    python Tools/hooks_einrichten.py --aus      # abschalten

**EIN BLINDER FLECK, den erst dieser Umbau zeigte:** `pruefe_engine.py` sah
nur, was `git ls-files` kennt - also nur VERFOLGTE Dateien. Eine neue Datei
faellt damit erst auf, NACHDEM sie committet wurde. Der Waechter meldete
seine eigene Testdatei und `Tools/engine.py` genau einen Commit zu spaet.
Fuer einen Pre-Commit-Hook waere das wertlos: er soll ja pruefen, was gleich
hineinwandert. `verfolgte_dateien()` nimmt jetzt auch `git diff --cached`
dazu. (Beide Fundstellen sind begruendete Ausnahmen: Testdaten und die
kanonische Quelle stellen beide Engines mit Absicht nebeneinander.)

**Nachweis gegen ein echtes Wegwerf-Repo, nicht gegen eine Nachbildung:**
rotes Gate -> 0 Commits, gruenes Gate -> 1 Commit, `--no-verify` -> 1 Commit.
Ein Test, der nur die Hook-DATEI liest, wuerde jede Verdrahtungspanne
uebersehen. 14 Tests in `Tools/test_vor_dem_commit.py`.

**UND EIN FUND, DER FAST TEUER WURDE.** Der erste Lauf des frisch scharfen
Hooks fiel rot aus - zu Recht, aber aus einem Grund, den ich nicht erwartet
hatte: git setzt fuer `git commit --only` ein TEMPORAERES `GIT_INDEX_FILE`
und vererbt es an JEDEN Unterprozess. Die Testsuiten legen Wegwerf-Repos an
und rufen dort `git add -A` - das schrieb prompt in den Index des laufenden
Commits. Nachgemessen: `datei.txt` aus einem Wegwerf-Repo stand im Index des
echten Commits, und drei Suiten fielen um, weil sie plotzlich einen fremden
Dateibestand sahen.

Waeren die Gates gruen gewesen, waere die Wegwerfdatei mitgekommen.

`cwd` allein schuetzt NICHT - die Umgebungsvariable schlaegt das
Arbeitsverzeichnis. Abgedichtet an drei Stellen, jede mit eigenem Grund:
`vor_dem_commit.py` startet alle Gates ohne `GIT_*` (eine Stelle, schuetzt
jede Suite), `test_ausliefern.py` und `test_vor_dem_commit.py` saeubern ihre
eigenen Wegwerf-Repo-Aufrufe (damit sie auch bei direktem Lauf stimmen), und
`ausliefern.py` selbst (es koennte aus einem Hook gerufen werden).

Belegt: Hook-Weg mit geerbtem Index -> alle Gates gruen, Index-Eintraege
vorher 1604, nachher 1604, keine Fremddatei.


## Freigabegruppen: die Fortsetzung, und was sie wirklich einbrachte (21.09.2026)

Der Eintrag oben endete mit "wer hier weiterarbeitet, faengt bei Clique statt
Farbe an". **Diese Formulierung war falsch, und der Irrtum ist lehrreich:**
eine Farbklasse im Konfliktgraphen IST eine Clique im Vertraeglichkeitsgraphen
- dasselbe Problem, nur vom Komplement aus gesehen. Der Hebel liegt nicht im
Verfahren, sondern in der REIHENFOLGE und in der Frage, was ueberhaupt als
Konflikt zaehlt.

**Zwei Hebel, beide schaltbar, damit ihr Anteil messbar bleibt:**

* `bSameTargetLaneBlocksGroup` (Vorgabe **false**) - der zweite, frueher
  ausdruecklich NICHT gemessene Verdacht. Die Laufzeitregel zaehlt eine
  gemeinsame Zielspur als Konflikt; das ist dort richtig. Eine
  Freigabegruppe stellt eine ANDERE Frage: duerfen beide gleichzeitig Gruen
  bekommen? Ein Verkehrsplaner gibt einfaedelnde Stroeme gemeinsam frei, sie
  sortieren sich ueber Luecken - genau dafuer gibt es die Laufzeitregel.
* `bOrderGroupsByConflictDegree` (Vorgabe **true**) - die am staerksten
  gebundenen Bewegungen zuerst einsortieren. Aufsteigende Nummer ist
  reproduzierbar, aber blind: wer viele Konflikte hat, findet spaet keinen
  Platz und bekommt eine eigene Gruppe.

Die Geometrie wurde NICHT kopiert, sondern als `FindPathCrossing`
herausgezogen; `DoConnectionsConflict` (Laufzeit) und
`DoConnectionsConflictForGroup` (Gruppen) benutzen dieselbe Rechnung mit
verschiedenen Regelwerken darum. Zwei Rechnungen fuer dieselbe Frage laufen
auseinander.

**DREI ZUSTAENDE GEMESSEN**, derselbe Build, dieselbe Karte, je 300 s,
Spieler per Koordinate 250 m noerdlich geparkt (`-WbGoto=22230,145934`),
verglichen mit `Tools/vergleich_staukarten.py` ueber 61-63 Strassen:

| Zustand | Stadt gewichtet | Bahnhofsplatz | Umlauf | Spanne | verschoben |
|---|---|---|---|---|---|
| Faustregel (`-WbOhneKonfliktgruppen`) | 39,9 % | 14,6 % (7,0 km/h) | 36 s | 20..70 | 0 |
| alt (`-WbZielspurSperrt -WbOhneGradreihenfolge`) | 29,5 % | 12,0 % (5,5 km/h) | 51 s | 20..180 | 6357 |
| **neu (Vorgabe)** | **30,8 %** | **8,5 % (3,8 km/h)** | **44 s** | **20..110** | **4683** |

**Das Ergebnis ist gemischt, und so steht es hier.** Stadtweit kostet
Konfliktfreiheit 10,4 Punkte; die beiden Hebel holen davon 1,3 zurueck -
13 Prozent. Die Struktur wird deutlich besser (466 -> 142 Zusatzgruppen,
groesste Gruppenzahl 10 -> 7, Umlaufspanne 180 -> 110 s), und die grossen
Achsen gewinnen klar: Konrad-Adenauer-Ring 13,2 -> 27,3 %, Mainzer Strasse
23,5 -> 30,1 % (973.000 Messwerte), Kaiser-Friedrich-Ring 22,8 -> 27,2 %.

**Der Bahnhofsplatz selbst wurde schlechter** (12,0 -> 8,5 %), also genau der
Ort, um den es urspruenglich ging. Einschraenkung, die dazugehoert: JE
ZUSTAND EIN LAUF. Die Messwertzahl am Bahnhofsplatz schwankte zwischen den
Laeufen um das Zehnfache (131.000 bis 1,17 Mio.) - der stadtweite gewichtete
Wert ueber 60 Strassen traegt, die einzelne Strasse traegt weniger. Wer die
Bahnhofsplatz-Zahl belastbar will, braucht Wiederholungen.

**Der Test hielt vorher eine POLITIK fest**, nicht eine Eigenschaft: "Sechs
einfaedelnde Verbindungen ergeben sechs Gruppen". Jetzt prueft er beide
Stellungen des Schalters (Vorgabe: eine Gruppe; streng: sechs) und
zusaetzlich die Invariante, die unter BEIDEN gelten muss - sich KREUZENDE
Wege bleiben getrennt. Ein Schalter, dessen zweite Stellung niemand testet,
ist eine Behauptung.

## MetaSound-Graph per Commandlet: Obertone, WaveShaper, Enum-Konstanten (25.09.2026)

- **Bausteine fuer die Zuednpuls-Synthese (WbAudioAssetsCommandlet.cpp):** der
  Standard-Node "Additive Synth" (`{Namespace, "Additive Synth", FName()}` -
  Variante ist NAME_None, nicht "Audio"!) summiert Sinusoiden auf Vielfachen
  der "Base Frequency"; "HarmonicMultipliers"/"Amplitudes" sind Float-ARRAYS,
  leere Pan-Liste = volle Pegel auf beiden Ausgaengen ("Out Left Audio" als
  Mono-Summe nehmen). Amplituden sind auf [0,1] begrenzt. "WaveShaper"
  (Variante "Audio") rechnet `tanh((x+Bias)*Amount)/tanh(Amount)` - fuer
  exakt `tanh(k*x)`: Amount=k, OutputGain=tanh(k); Typ-Pin "Type" ist ein
  ENUM (EWaveShaperType {Sin=0, ATan=1, Tanh=2, Cubic=3, HardClip=4}).
- **Enum-/Array-Konstanten am Node:** `UMetaSoundBuilderBase::SetNodeInputDefault`
  (Template 4-Arg-Variante) mit `int32`- bzw. `TArray<float>`-Literal - Enums
  sind mit ELiteralType::Integer registriert, das Integer-Literal konvertiert.
  Die FGraph::Input()-Helfer koennen nur float; fuer alles andere
  Default()-Helfer auf SetNodeInputDefault aufsetzen.
- **Beweiszeilen fuer Motor-Hoerproben:** `LogWbVehicles "Motorsound: Asset
  '...' wird verwendet."` hat Verbosity **Log** -> steht NUR in
  `Saved/Logs/WiesbadenReal.log`, nie im stdout-Redirect `audio_drive_*.log`.
  Nicht nach einem "fehlenden" Motorsound-Eintrag im Redirect suchen.
- **Skripte sind umgezogen (Aufraeumung 24./25.09.):** `make_audio_assets.cmd`
  liegt jetzt in `WiesbadenReal/Tools/` (Log dort: `Saved/Logs/make_audio_assets.log`,
  Beweiszeile "WbAudioAssets fertig: 12/12 Pakete gespeichert"); `audio_drive.cmd`
  und `vis_probe.cmd` sind nach `.planning/diagnose-reste-2026-09-24/`
  archiviert, funktionieren von dort aber weiter (feste absolute Pfade).
- **Offline-Renderer** `Tools/render_engineboxer.py` bildet den Graph 1:1 nach
  (PINK-Noise ueber Frequenzgang, Einpol-TP b1=exp(-2*pi*fc/fs), Oberton-Stack,
  Boxer-Modulation, tanh). Saetze: `alt|neu|oberton|dynamik|sport`,
  `analyse` misst alle. Der Satz `dynamik` ist der aktuelle Graph;
  `oberton` ist die menschlich freigegebene Endabnahme und darf nicht
  ueberschrieben werden; `sport` ist eine Profil-Studie (nicht im Graph,
  Uebertragung waere ein Konstanten-Satz).
- **Clamp-Knoten (25.09.):** Klasse `FNodeClassName {"Clamp", "Clamp",
  "float"}` - Namespace ist "Clamp", NICHT StandardNodes::Namespace, der
  Aufruf im FGraph braucht ein drittes Namespace-Argument. Pins
  `In`/`Min`/`Max` -> Ausgang `Value` (NICHT "Out"). Float-Konstanten
  (Clamp-Grenzen) ueber SetNodeInputDefault(float).
- **Graph-Eingabe fuer zwei Ketten (25.09.):** `Graph.Input()` legt die
  Eingabe an UND verbindet; fuer die zweite Nutzung (z.B. Throttle oder
  SpeedKmh in zwei Multiplikator-Ketten) einen Link()-Helfer auf
  `ConnectGraphInputToNode` nutzen. **Reihenfolge:** Input() muss die
  Eingabe vor der ersten Link() angelegt haben, sonst "Graph-Eingabe ...
  (zweite Leitung) fehlgeschlagen" -> "MS_EngineBoxer nicht gebaut".
- **make_audio_assets.cmd:** der stderr "Der Prozess kann nicht auf die
  Datei zugreifen" erscheint auch bei erfolgreichem Lauf (exit 0) - er
  kommt vom Log-Redirect. Einzige Belegquelle bleibt die Log-Zeile
  "WbAudioAssets fertig: 12/12 Pakete gespeichert".
- **InterpTo (25.09.):** Klasse `{Standard, "InterpTo", "Audio"}`, Pins
  `Target`(float)/`Interp Time`(time)/`Value`(float), zustandsbehaftet -
  startet bei Zielwert-Aenderung eine lineare Rampe ueber "Interp Time"
  (Block-Rate). DAS Werkzeug fuer Parameter-Glaettung und Huellkurven
  (z.B. 180-ms-Tor der Hupe) im Graph.
- **Time-Pins haben keine Literale:** FMetasoundFrontendLiteral unterstuetzt
  nur bool/int32/float/FString/UObject. Time-Eingaben ueber
  Konvertierungs-Knoten speisen: die heissen
  `Conversion{VonTypString}To{ZuTypString}` (Namespace StandardNodes,
  leere Variante, Pins `In`/`Out`) mit den Data-Type-Strings
  "Float"/"Time"/"Audio" - praktisch also **ConversionFloatToTime** und
  **ConversionFloatToAudio**. Float-Konstante als Graph-Input -> Conversion
  -> Time-Pin.
- **Additive Synth:** die Einzel-Amplitudes sind auf [0,1] geklemmt, die
  SUMME der Sinusoide aber weder normiert noch geklemmt - unnormierte
  Referenz-Gewichte (z.B. Nebelhorn-Stack {1, 0.60, 0.32, 0.16}) sind 1:1
  einsetzbar, die Pegelkontrolle kommt aus der spaeteren tanh-Saettigung.
- **Regelbare Faerbung via Delta-Stab (25.09.):** die Amplitudes-Arrays
  sind statisch - eine Oberton-Verschiebung mit einem Regler (Throttle)
  laeuft ueber einen ZWEITEN Additive Synth: Betraege des Gewichts-Deltas
  in Amplitudes, die VORZEICHEN als Phase 180 Grad, Ueberlagerung mit
  dem Reglerfaktor (z.B. 2*Throttle-1) VOR der Boxer-Modulation.
  Eichungsmuster: Faktor 0 in der Neutralstellung (halbes Gas) laesst die
  freigegebene Referenz bit-identisch - so bleiben spaetere Aenderungen
  zur Endabnahme rueckwaertskompatibel.

## WP-Ankerpersistenz: Komponenten-Transforms sind RELATIV, der Chunk-Actor nicht (25.09.2026)

BEFUND (Voll-Bake Alkis25 + Re-Bake-Versuche): Die Streaming-Verankerung der
Stadt-Zellen hielt in KEINEM Zustand. `anchor_bounds.cmd` meldete "2010 Chunks
verankert, Bounds ueber Ursprung 2010 -> 4", ein frischer Prozess
(Tools/verify_anchor_state.py) sah danach wieder 2010/2010 - auf Alkis24 wie
auf Alkis25. Zwei Ursachen, beide teuer:

1. **Komponenten-Transform != Weltort.** Ein Komponenten-Transform wird
   relativ zum Actor gespeichert. `SetWorldLocation(Anchor)` auf einem Actor,
   der (wie per Bauplan jeder AWiesbadenCityChunk) auf (0,0,0) steht, schreibt
   den Weltanker als relatives Delta in die Karte; beim naechsten Laden
   addiert der Actor das Delta erneut. Nach mehreren Laeufen lagen die
   Komponenten weit ausserhalb ihrer Zelle. FIX: der Anker liegt als
   UPROPERTY `StreamingAnchorCm` im Actor-Paket und wird in
   `PostRegisterAllComponents` (Actor-Hook - `OnRegister` ist
   USceneComponent!) sowie in BeginPlay erneut angewendet. Wer Komponenten
   positioniert, benutzt hier IMMER SetWorldLocation, nie SetRelativeLocation
   auf einen Weltwert.
2. **"Bounds ueber Ursprung" war kein WP-Mass im Commandlet.** Im
   `-run=pythonscript`-Kontext sind die gebackenen StaticMesh-Assets NICHT
   geladen; ihre Komponenten melden dann Punkt-Bounds an ihrer Komponenten-
   Position (0,0,0) und ziehen die Actor-Box scheinbar bis zum Ursprung. Das
   echte Mass ist mesh-unabhaengig: LEERE Komponenten (keine Sections, kein
   Mesh, keine Instanzen) duerfen nicht nahe am Kartenursprung stehen. Nach
   dem Fix auf Alkis25: 20.404 leere Komponenten, 0 am Ursprung.

FALLSTRICKE in derselben Kette:
- `AActor` hat KEIN `OnRegister`; `PostRegisterAllComponents()` ist der Hook
  fuer "nach dem Laden/Stream-in".
- `UHierarchicalInstancedStaticMeshComponent` ERBT von
  `UStaticMeshComponent`: eine Typpruefung muss die HISM-Variante VOR der
  StaticMesh-Variante abfragen, sonst gilt jedes HISM mit gesetztem Mesh als
  "hat Inhalt" (der Test fiel genau daran).
- UE 5.8 Python: `EditorActorSubsystem`/`EditorLoadingAndSavingUtils` haben
  KEIN `save_actor` (Sonde: Tools/probe_save_api.py). `SceneComponent` hat
  weder `get_component_location()` noch `get_world_location()` noch
  `component_to_world`; `is_a` gibt es auf Python-Objekten nicht (nur
  `isinstance`). Ein Diagnose-Skript darf an solchen Stellen nie den Lauf
  abbrechen - try/except je Komponente, sonst bleibt das Ergebnisfile alt
  und man prueft den VORLETZTEN Stand.
- Nach dem Ankerlauf sind die Python-Actor-Referenzen tot ("ObjectInstance is
  null") - im selben Prozess ist nichts mehr messbar, `load_level` liefert im
  Commandlet 0 Zell-Actoren. Zaehlen VOR dem Speichern, gegenpruefen immer in
  einem FRISCHEN Prozess.
- Ein laufender UnrealEditor (auch aus einer fremden Session) sperrt die
  Modul-DLL: Build endet mit LNK1104 und der Test laeuft still gegen das ALTE
  Binary. Deshalb ruft jeder Bake-/Testwrapper ueber
  `Tools\engine_run_lock.cmd -Modus Start` vor dem Engine-Start den Lock und
  danach ALLE `UnrealEditor*` und `zenserver` auf, wartet mindestens 3 s, fasst
  einen langsamen Shutdown mit einem zweiten Kill und bis zu 10 s Wartefrist
  nach und verweigert den Start erst, wenn wirklich ein Prozess zurueckbleibt.
  `build_release.ps1` beendet dagegen bewusst nur Editoren
  dieses Projektordners - der Kompilier-Gate soll keine fremde Sitzung
  zerstoeren.
- DER GLOBALE CLEANUP IST DESHALB GESPERRT: Am 25.09.2026 hat er den
  Editor eines bereits als rot gemeldeten Gate-Laufs abgeschossen und den
  zweiten (gruenen) Push mitgerissen. Jeder Bake-/Test-/Gate-Lauf haelt
  deshalb den `Engine-Lock`: `Tools\engine_run_lock.cmd -Modus Start -Name
  <lauf>` (ersetzt in den Wrappern den cleanup-Aufruf 1:1; der Rauchtest,
  die Health-Checks und `build_release.ps1` nehmen ihn mit `-Modus Nehmen`).
  Datei: `%LOCALAPPDATA%\WiesbadenReal\Locks\engine_run.lock` - MASCHINENweit,
  nicht pro Projekt, damit sich zwei Sitzungen (Hauptordner und Gate-Worktree)
  sehen. Drei Regeln, an denen die Sperre haengt:
  * Besitzer ist der AUFRUFENDE Prozess (die cmd.exe bzw. powershell.exe des
    Laufs), nicht der kurzlebige PowerShell-Kindprozess, der die Datei
    anlegt. Nur so lebt die Sperre genau so lange wie der Lauf - eine
    Freigabe am Ende ist damit ueberfluessig.
  * Ein Besitzer, dessen Prozess nicht mehr existiert (oder dessen
    Startzeit zu einem recycelten PID passt), gilt als VERWAIST: die Sperre
    wird uebernommen. Ein abgebrochener Lauf blockiert also nicht.
  * Gehoert der Besitzer zur eigenen Prozesskette (Gate -> Rauchtest ->
    Cleanup), ist die Sperre EIGEN und das Beenden bleibt erlaubt - ohne das
    wuerde sich das Gate selbst sperren.
  Bei belegtem Lock bricht der Cleanup VOR dem Kill ab (Exit 3, Meldung nennt
  Label/PID des Besitzers). Notausgänge, beide bewusst: der Cleanup mit
  `-SperreIgnorieren` und `engine_run_lock.cmd -Modus Freigeben -Gewalt`.
  `build_release.ps1` nimmt den Lock schon VOR Gate 0: sein
  `Stop-ProjectEditors` in Gate 1 ist zwar projektlokal, trifft im GEMEINSAMEN
  Gate-Worktree (`.gate-worktree\WiesbadenReal`, ein fester Pfad fuer alle
  Sitzungen) aber den Editor eines zweiten Gate-Laufs. Genau das hat am
  25.09.2026 den ersten Lauf zerstoert: der zweite meldete 0 Fehler und "kein
  Abschluss-Marker". Zwei Pushes laufen also nie gleichzeitig.

---

## Destillat aus dem Setup-Thread (25.09.2026)

Kompaktes Merkbuch aus `C:\freebuff\WiesbadenReal_Sicherung\AGENTS.md`,
hier als Anhang an die ausfuehrliche Projektdoku. Inhaltlich uebernommen; nur
die Default-Karte ist auf den aktuellen Stand `Alkis30` berichtigt.

### Environment (Windows 11, Git Bash shell, project root C:\freebuff\WiesbadenReal_Sicherung)
- No system ffmpeg. Get a static binary via `pip install imageio-ffmpeg`, then `python -c "import imageio_ffmpeg; print(imageio_ffmpeg.get_ffmpeg_exe())"`.
- Python 3.14 at C:\Python314 with almost no packages; pip installs land in AppData\Roaming\Python\Python314 (Scripts dir not on PATH).
- GPU is an RTX 5070 Laptop (8 GB, sm_120) but ctranslate2 4.8.2 CUDA kernels only support up to sm_90 → faster-whisper must run `device="cpu", compute_type="int8"`; do not waste time on CUDA debugging.
- CPU: 16 logical cores. Fastest Whisper setup: 2 parallel worker processes × cpu_threads=8 → ~8× realtime with large-v3-turbo.
- pyannote.audio installs on Python 3.14 (torch 2.14+cpu wheels exist), but its HF models (`pyannote/segmentation-3.0`, wespeaker) are license-gated → `GatedRepoError 401` without an HF token; this machine has NO HF token (no `~/.cache/huggingface/token`, no `HF_TOKEN`).
- torchaudio/torchcodec cannot load WAVs here (no FFmpeg DLLs: RuntimeError "Could not load libtorchcodec") — read PCM WAVs with the stdlib `wave` module + numpy instead of torchaudio/soundfile.

### Long-running jobs in this harness
- run_terminal_command caps at ~600 s and `process_type: BACKGROUND` is not implemented → launch detached with `(python job.py > log 2>&1 &)` inside the command, then poll with `sleep N; tail log` in later calls.
- Structure long jobs as resumable chunks: one output file per chunk (e.g. `parts/chunk_0004.json`), skip existing outputs on relaunch.
- A detached `(python job.py > log 2>&1 &)` launch can die with a PyAV/DLL load error (Windows app-control policy) while the identical import works foreground — if a detached Whisper/pyannote job fails on DLL load, rerun it in the foreground rather than debugging the code.
- read_files truncates files at ~20k estimated tokens — read big files in offset/limit windows instead.
- For UE editor commandlets (headless builds, re-bakes): launch detached via `powershell Start-Process` with a .cmd wrapper (e.g. `anchor_bounds.cmd`), then poll log mtime / external-actor package-save counts; the script's result file only flushes at the very end, and the process can outlive the session — always poll, never assume death.

### World-Partition Streaming (WP): Bounds-Diagnose & Re-Bake
- Empty components (0-instance HISM/ISM, empty mesh) get POINT bounds at their own position (UE 5.8 `SceneComponent.cpp`); chunk actors spawn at the origin, so unanchored empties re-span the origin and inflate `GetComponentsBoundingBox` to km scale. Anchor empties at the cell's content centroid (mesh sections, else region-asset points).
- `Streaming-Diagnose` fires after ~8 s gameplay from the subsystem tick and lands in `Saved/Logs/WiesbadenReal.log` — NOT in stdout redirects, and engine log timestamps are UTC (23:00 UTC = 01:00 local). `diag_wp.cmd` must run the current baked map at the default 2-km loading range (the old 4-km range override blurs the metric). **Map names move fast, always check before use:** `Alkis4` no longer exists (deleted 2026-09-19, together with Alkis2/3/7/8/9/10-13/15); the live default in `Config/DefaultEngine.ini` is `Alkis30` (Stand 25.09.2026, 22:06), and `Alkis16`..`Alkis31` exist. **A .umap on disk proves nothing — a bake is only finished when it has an `ok` line in `Saved/BuildHistory/CityBuilds.csv`.** A map that is *currently being baked* already has a full-size `.umap` (every WP map is ~13 KB, the geometry lives in `Content/__ExternalActors__/Maps/<Map>/`) and an external-actor folder that is still filling up: on 25.09.2026 at 22:06 `Alkis31` existed with a .umap and **1** package while `Alkis30` was complete with **2019** and an `ok` from 20:56. Both `GameDefaultMap` AND `EditorStartupMap` in `Config/DefaultEngine.ini` must name the same map, or the editor opens one map and the game starts another. Pick the map from `Config/DefaultEngine.ini` / `ls Content/Maps/*.umap`, confirm it in `CityBuilds.csv`, never from memory.
- WP cell assignment is baked into the chunk external-actor packages: C++ fixes need a re-bake (`anchor_bounds.cmd` → `Tools/anchor_chunk_bounds.py`, ~2 h for ~2060 chunks; umap itself never changes). Programmatic `SetWorldLocation` does NOT dirty packages — C++ `Modify(true)` must mark the chunk package or `save_dirty_packages` saves nothing; Python prints never reach the cmd stream, verification must go to a file (result lands at `anchor_bounds_result.txt` in the repo root, not Tools/).
- **Anchoring only survives if it is an ANCHOR property, not a component transform (fixed 2026-09-25, verified in a fresh process).** `AWiesbadenCityChunk` sits at (0,0,0) and carries world coords in its components, so `SetWorldLocation(Anchor)` stores the world anchor as a *relative* delta; the next load adds the actor origin again and the empties drift to 2x the cell. Fix: `StreamingAnchorCm` + `bHasStreamingAnchor` as serialized UPROPERTYs, applied in `PostRegisterAllComponents()`/`BeginPlay` (never `OnRegister` — that is a `USceneComponent` hook, it does not exist on the actor); `AnchorStreamingBounds()` only recomputes and stores. Proof on `Alkis30` (25.09.2026, 22:2x): **23 799 empty components, 0 at the map origin** (`Tools/verify_anchor.cmd` -> `Tools/verify_anchor_state.py` -> `Saved/Diagnose/anchor_verify.txt`; previously 20 404 / 0 on the then-default map). The empties now carry their CELL's coordinates as their relative location (e.g. `RoadMesh ... relativ (625008, -524916, 16754)` while the actor itself sits at `(0,0,0)`) - that is the anchor doing its job; UE 5.8 Python has **no** per-actor save (`EditorActorSubsystem.save_actor` and `EditorLoadingAndSavingUtils.save_actor` do not exist) — only `save_dirty_packages`/`save_current_level`, so an in-session "ok" is not evidence. Measure empty components, not `GetComponentsBoundingBox`: in `-run=pythonscript` the baked StaticMesh assets are unloaded and their components report POINT bounds at (0,0,0), which fakes a 2010/2010 origin hit. **Git Bash rewrites `/Game/...` into `C:/Program Files/Git/Game/...`**, so `WB_MAP=/Game/Maps/X cmd //c "Tools\verify_anchor.cmd"` silently measures an empty level: `load_level` returns False, the world stays `/Temp/Untitled_0`, `get_actor_descs()` returns None and the script dies with "TypeError: 'NoneType' object is not iterable" (or, in the old version, reports a worthless "WP-Actors geladen: 0 von 0"). Run `Tools\verify_anchor.cmd` WITHOUT `WB_MAP` and let `Tools/karte.py` read `Config/DefaultEngine.ini`; to force a map, change the ini or pass an already-absolute Windows path. Always check the first line of `anchor_verify.txt` says "Karte geladen: /Game/Maps/..." - anything else is a zero measurement, not a clean bill of health. `verify_anchor.cmd` (like `rebake_alkis2x.cmd`) still takes **no** engine lock - check for a running `UnrealEditor` yourself before starting it.
- Anchor functions must sweep ALL HISM components of the owner class-wide (`GetComponentsByClass`): tracked arrays like `VariedInstances` are populated only by `SpawnVaried()` at build time, and on a loaded map `BeginPlay` skips `SpawnRegionAssets` for empty cells — so serialized variant components (`Trees_01..06`/`Bushes_01..06`) stay untracked at origin. Residual "Bounds-ueber-Ursprung" counts after a pass are NOT stale render state — verify per-component in a fresh process before assuming success.

### Debugging: HuggingFace model-download race (misleading silence)
- Two processes loading the same model concurrently race on the blob download: one finishes `model.bin`, the other hangs forever re-downloading a stale `.incomplete` blob — with NO error and NO log output.
- Symptom: python process idle (~1 GB RAM, no CPU time), still at the "loaded" log line → blocked on download, not computing.
- Fix: kill both, delete `~/.cache/huggingface/hub/models--*/blobs/*.incomplete`, confirm `snapshots/*/model.bin` is intact, relaunch with `HF_HUB_OFFLINE=1 TRANSFORMERS_OFFLINE=1`.

### Nerobergbahn: OSM-Linien sind Punkt-für-Punkt gepaart, nicht bogenlangengleich
- `TRACK_A`/`TRACK_B` (die OSM-Ways) sind die WAGENMITTEN: sie liegen auf 385 von
  434 m exakt 1,00 m auseinander (= Spurweite), in der Ausweiche bis 4,15 m.
  Daraus folgt der Querschnitt: 3 Laufschienen (Mitte + ±1,00 m), 2 Zahnstangen
  bei ±0,50 m, Seilkanal bei 0, Schwellen 2,40 m, Bettkrone 2,60 m.
- `BuildTracks()` dünnt je Linie Punkte unter 3 m aus — die beiden Ketten sind
  danach bis ~3,7 m gegeneinander verschoben. Ein Vergleich bei GLEICHER
  Bogenlänge vergleicht deshalb Punkte, die bis 3,8 m auseinanderliegen: die
  Ausweiche erschien damit 138 m statt 35 m lang (`9`/`12`-Rasterfehler solcher
  Art erst nach dem Zählen der Instanzen im Log auffallen). Richtig ist die
  Zuordnung über denselben Index; die Bogenlänge der Mitte kommt von Linie A,
  damit Fahrt und Gleis dieselbe Parametrisierung nutzen.
- Einzelne OSM-Knoten liegen bis 34 cm neben der Spurweite. Für die gemeinsame
  Mittelschiene muss der Abstand EXAKT eine Spurweite sein, sonst liegen dort
  zwei Schienen 30 cm nebeneinander — der gemessene Wert wird darum auf 50 cm
  gerundet, solange er in der Nähe liegt (nur die echte Ausweiche bleibt).
- Trassen-Assets sind WERKZEUG: `mesh Z=0` = Schienenoberkante in Trassenmitte,
  X entlang, Y quer; der Platzer hebt nur noch um `RailTopCm` (= `CarFloorCm`,
  damit das Rad genau auf der Schiene läuft) und schiebt quer.
- Die Trasse liegt im unteren Drittel 5-6 m über dem Gelände (Dammmauer):
  flache Kameras von der Seite schauen gegen die Mauer. Querschnittsbilder
  deshalb steil von oben (`-WbAerial`, Pitch -30..-55), sonst sieht man nichts.

### Nerobergbahn-Wagen: aufgemalte Graphik (Schriftzug, Wasserstandsskala)
- Schrift und Skala sind im Vorbild AUF die gelbe Wand gemalt: transparente PNG
  (`Tools/make_nerobergbahn_textures.py`, Pillow) auf einer Flaeche 1 cm vor der
  Wand, im UE-Material per Alpha maskiert und zweiseitig (`M_WbNb_Decal`) - so
  bleibt der gelbe Kasten zwischen den Buchstaben sichtbar.
- `MeshBuilder.quad_tex()`: die vier Ecken werden "von aussen gelesen"
  uebergeben, die UV-Werte muessen dazu (0,0) (1,0) (1,1) (0,1) sein. Mit der
  vertauschten Reihenfolge stand die Schrift 180 Grad verdreht - im Blender-
  Render sofort zu sehen, im Spiel erst nach dem Import. Merksatz: Blender-UV
  und UE-UV stimmen ueberein, ein V-Flip ist NICHT noetig.
- Der Wagen traegt die Graphik auf beiden Seiten und ordnet die Ecken je Seite
  anders; eine Seite allein zu pruefen reicht nicht (Ansichten
  `vorschau_wagen_wand.png` / `_wand_gegen.png` in 1920 px).
- Kleine aufgemalte Details sind in der Gesamtansicht nur 1-2 Pixel breit und
  wirken dort heller/verwaschen - vor einem Farbfehler erst die Pixelwerte
  vergleichen (blaueste Pixel der Skala 61/115/174 vs. Schrift 62/114/172).

### Nerobergbahn-Wagen: Innenraum, Mitfahr-Modus, Pruefungen
- `build_wagen()` kippt das GANZE Mesh mit `tilt_grade()` in die Steigung; eine
  Hoehe im Mesh-System zu messen ist um `x * 19 %` falsch (bei x = 1,7 m: 32 cm
  — genug, um „Tacho auf/unter Augenhoehe“ zu vertauschen). `Zurueckdrehen`
  (x' = x·ca + z·sa, z' = −x·sa + z·ca) und erst dann gegen die C++-Werte
  pruefen: Wagenboden 0,85 m, Augenhöhe der Mitfahrkamera 2,45 m (Kopfreiheit
  2,62 m), Sitz 0,45 m, Lehne 0,85 m.
- Kastenende ist `5,40/2 − 0,85 = 1,85 m`; der Kommentar im Builder sagte 1,95 m.
  Pruefungen holen solche Bezugslinien aus der Geometrie (Innenboden + 2 cm),
  nicht aus Kommentaren.
- Der Wagen laeuft OHNE Kollision (`Car->SetCollisionEnabled(NoCollision)`) —
  nur deshalb kann der Fahrgast im Wagen umherlaufen. Der Einsteige-Versatz ist
  Wagenmitte + 1,75 m (Boden 85 + halbe Kapsel 90); mit 1,50 m stand er 25 cm im
  Boden, was erst mit eingerichtetem Innenraum sichtbar wurde.
- Bewuchs steht IM Wagen, weil `FWiesbadenRoadClearance` nur aus
  **Strassen**segmenten gebaut wird (`WiesbadenRegionAssets.cpp`, 150 cm
  Zuschlag) — der Bahnkorridor ist nicht enthalten. Fix = zweites Freihaltenetz
  aus der Bahnachse + Neubake der Kacheln; kein Renderlauf-Fehler.

### Nerobergbahn-Stationen: Bahnsteighalle und die Trassenmoebel
- EIN Asset `SM_WbNbBahnsteighalle` fuer beide Stationen: Laenge entlang X,
  Ursprung = Schienenoberkante in der Mitte ZWISCHEN den Gleisen (nicht auf
  TrackA oder TrackB), erstes Joch ohne Balustrade = offene Einstiegsseite. Die
  Berghalle wird um 180 Grad gedreht, damit die Oeffnung auch dort am Wagen
  liegt. Der Actor loggt die Weltkoordinaten (`Nerobergbahn: Halle Tal/Berg auf
  (...) cm`, Trassenlaenge A 43439 / B 43075 cm) - Posen-Dateien brauchen sie,
  und aus der Bogenlaenge selbst gerechnet liegen sie ~2 m falsch (die
  ausgeduennten OSM-Punkte).
- `BuildTrackMeshes` baut Backsteinmauer, Klinkerband, Handlauf und Wimpel fuer
  die GANZE Trasse, auch im Hallenbereich. `BedHalf` = 130 cm ist zugleich die
  halbe Wagenbreite: die Mauer stand mitten im Gleistrog und der Handlauf auf
  Fensterhoehe im fahrenden Wagen, die Wimpel hingen als schraege Platten in der
  Kabine. Jetzt: keine Mauer und kein Gelaender +-7 m um beide Hallen, und das
  Gelaender steht 45 cm weiter aussen (`RailOutCm`) auf der Mauerkrone.
- Runtime-Geometrie der Trasse (Mauer, Gelaender, Roste, Boeschung) braucht
  KEINEN Neubake - der Actor baut sie bei jedem Start. Ein Neubake ist nur fuer
  die Staedte-Streuung noetig (Baeume, Laternen und Schilder stehen in Halle und
  Wagen: `FWiesbadenRoadClearance` kennt den Bahnkorridor nicht) und fuer die
  OSM-Gebaeude.
- Die beiden OSM-Wege `Nerobergbahn Talstation` (145208459) und
  `Nerobergbahn Bergstation` (396465632, building=service) erzeugt die
  Gebaeude-Pipeline als mehrgeschossige Bloecke - an der Bergstation steht der
  Block IM Hallengrundriss (Dach bei 192 m gegen 172,85 m Schienenoberkante).
  Beide Namen treffen auch die Landmarkenliste; das ist nur eine Markierung.
- Posen fuer Bahnahmen: `Saved/Diagnose/poses_bahn.txt` (Trasse) und
  `poses_bahn_close.txt` (Nah) - **die liegen auf der Platte**. Das hier
  urspruenglich genannte Stationsverzeichnis
  `Saved/Diagnose/poses_nerobergbahn_stationen/berstation/halle_nah/hallen.txt`
  gibt es **nicht mehr** (am 26.09.2026 geprueft: `Saved/Diagnose/` enthaelt
  nur `poses_bahn*.txt` und `poses_alkis*.txt`, kein `poses_nerobergbahn_*`).
  Wer die Hallenaufnahmen braucht, legt die Datei nach dem Format unten neu
  an. Format: Hoehe_m, AtX_cm, AtY_cm, Yaw, Pitch, Vorwaerts_m, LookYaw,
  LookPitch. `Yaw` ist die Richtung des Vorwaerts-Versatzes, geblickt wird mit
  `LookYaw`; bergan ist Yaw -53, quer von rechts 217. Vorsicht: die Kamera
  ankert Z am **Bodentrace**, nicht an der Schienenoberkante - an Daemmen steht
  sie tiefer als gedacht, ueber Gebaeuden landet sie auf dem Dach.

## Spielprobe: was ein Nutzer wirklich erreicht (26.09.2026)

Der Auftrag "spiele es, wie der erste Nutzer" hat vier Fehler gefunden,
von denen drei seit Monaten im Buch standen und keiner davon auffiel,
weil das Log nichts Falsches sagte - es sagte gar nichts.

- **Dev-Befehle mit Argument sind ueber die Konsole NICHT AUFRUFBAR** (am
  26.09.2026 an der Engine gemessen, drei Schreibweisen an einem UFUNCTION(Exec)
  mit `int32 Sekunden`):
  | Schreibweise | Ergebnis |
  |---|---|
  | `WbHeliFly 24` | `Bad or missing property 'Sekunden'` |
  | `WbHeliFly=24` | **keine Fehlermeldung, keine Wirkung** |
  | `WbHeliFly Sekunden=24` | `Bad or missing property 'Sekunden'` |
  | `WbHeliFly` (ohne alles) | `Bad or missing property 'Sekunden'` |

  Ursache: `UObject::CallFunctionByNameWithArguments` sucht beim Aufruf ein
  **Objekt-Property**, nicht einen Funktionsparameter, und erwartet
  `Property=Wert`. Die UFUNCTION wird aber nicht gefunden, wenn ihr Parameter
  **keinen Standardwert** hat - dann bricht der Aufruf ab, egal wie er
  geschrieben ist. Behoben an der Wurzel: alle Exec-Deklarationen in
  `WiesbadenPlayerController.h` haben jetzt Standardwerte, und die
  zeitbasierten Befehle holen ihre Dauer aus der CVar **`wb.Sekunden`**
  (Vorgabe 24 s), wenn 0 ankommt. Aufruf also: `WbHeli,WbHeliFly` - ohne
  Argument, wie in `Tools/flight_check.cmd` dokumentiert.
  Merksatz: **ein Exec-Befehl braucht einen Standardwert, sonst ist er von der
  Kommandozeile aus tot - und zwar lautlos, wenn man `Name=Wert` schreibt.**
- **`Tools\flight_check.ps1` war das Messwerkzeug und hat nie gemessen.** Mit
  der dokumentierten Zeile `"WbHeli,WbHeliFly 24"` startete die Flugphase nie
  (siehe oben), das Skript wartete volle 7 Minuten, meldete
  `0 Mast-Messpunkte` - und **Exit 0**. Ein Messwerkzeug, das nichts misst und
  Erfolg meldet, ist das Schlimmste, was ein Werkzeug tun kann. Zwei Aenderungen:
  1. `exit 1`, wenn weniger als `MinSeconds` Mast-Messpunkte im Log stehen.
  2. Im Fehlerfall nennt das Skript die `Bad or missing property`-Zeilen aus
     dem Log - man sieht jetzt sofort, dass die Engine die Befehle abgewiesen
     hat, statt sich zu wundern.
  Reihenfolge dabei wichtig: **erst Sitzung beenden, dann Exitcode**. Ein
  `exit 1` vor dem `Stop-Process` hat eine 7-GB-Sitzung zehn Minuten
  weiterlaufen lassen (das war ein Fehler von mir, beim Einfuegen des
  Exitcodes entstanden und sofort wieder behoben).
- **`find /i` ist auf diesem Rechner NICHT das Windows-`find`.** `where find`
  listet `C:\Program Files\Git\usr\bin\find.exe` **zuerst**, und das Git-find
  kennt kein `/i`: es meldet `find: '/i': No such file or directory` und
  liefert errorlevel 1. In `Tools\ka52_wait_build.cmd` war damit die ganze
  Editor-Abfrage tot - das Skript meldete immer "Editor zu", baute also
  **nie** und lief bei jedem offenen Editor in `EXITCODE 6` (Live Coding).
  Umgestellt auf `findstr /b /c:` (Windows-eigen, nicht verdeckt, matcht nur
  die Prozesszeile, weil die Zeile "Keine Aufgaben..." mit `INFORMATION:`
  beginnt). **Vorher kam noch ein zweiter Fehler dazu: `tasklist //FI` in einer
  .cmd wird zu `tasklist \FI` - die Doppelschrägstriche sind eine
  Git-Bash-Konvention.** Belegt: `where find` sagt die Reihenfolge,
  `Saved/tmp/playtest_wartet.log` sagt, was das Skript wirklich erkannt hat.
  Nur `ka52_wait_build.cmd` war betroffen; alle anderen .cmd nutzen `findstr`.
- **Ein zweiter Editor neben dem ersten ist ein Absturz** (steht so im
  Projekt). Das ist keine Theorie: ein Wartelauf mit dem defekten
  `find`-Erkennen startete, während der Editor des Menschen lief - zwei
  `UnrealEditor.exe` nebeneinander. Der `engine_run_lock` hat den *richtigen*
  Lauf geschuetzt (Exit 3 mit klarer Begruendung), ein **unten** in
  `Tools/shot_pose_series.cmd` nachgezogener Lock fehlte: dieses Skript
  startete einen sichtbaren Editor ohne jede Sperrpruefung. Bei allen
  Engine-Wrappern nach `UnrealEditor` in `Tools/*.cmd` **fehlt der Lock in
  ueber 20 Dateien** - das ist die naechste Baustelle, nicht heute.
- **Ein Etikett, das nach einem Fehler aussieht, ist ein Fehler.** Die
  Flugtelemetrie meldete nach der Achsenkorrektur
  `Blatt-Drehpunkte 2.6/5.3 cm ab Nabe` - genau die Korrektur, beschriftet wie
  der gerade behobene Schiefstand. Ein Nutzer haelt das fuer genau den
  Fehler, den er abstellen lassen hat. Die Zeile nennt jetzt
  `Achsenkorrektur 2.6/5.3 cm, Drehpunkt der Scheibe 0.0/0.0 cm neben der
  Stange` (Messung in `WiesbadenHelicopter::SampleMast`, echte Welt-
  Transformation des Components - **nicht** relative Drehung auf Weltlage, das
  ergibt 2 x Korrektur, weil die Nabe mitdreht).


### Bild-Belege statt Behauptungen (Plasmacutter, Gate 4)
- `Tools/verify_cuttable.cmd` faehrt den Bildlauf UND misst die PNGs (Glut-Anteil, Lage, Blickwinkel aus dem Log). `-NurPruefen` prueft einen vorhandenen Lauf ohne Engine. `Tools/test_verify_cuttable_gate.py` baut acht Fehlerfaelle nach und verlangt, dass das Gate bei jedem ROT wird - **21 gruene Pruefungen allein beweisen nichts**.
- **Der FootPawn setzt seine Actor-Rotation selbst auf `(0, Yaw, 0)` - Pitch ist dort immer 0.** Eine Kamera an seinem SpringArm kann nicht auf ein Stueck schauen, das unter ihr liegt (`SetControlRotation` hilft auch nicht, der ACharacter zieht die Drehung aus dem Controller und der Tick ueberschreibt danach). Loesung der Bildprobe: **freie Kamera als ViewTarget**.
- Bildziel nach dem Schnitt ist `GetFallenPiece()`, NICHT die Schnittmitte - das abgefallene Stueck rutscht vom Schnittpunkt weg.
- `Saved\` ist nicht versioniert: im Commit-Worktree fehlen beim Start der Python-Suiten die Bilder, der Selbsttest ueberspringt dort. Gate 4 laeuft ohnehin **ohne Dateifilter** - im Worktree ist der Commit schon committed, eine aus dem Push-Bereich gebaute Dateiliste ist dort LEER und wurde als "nichts zu tun" gelesen.
- `waehle_stadtinhalt` verlinkt aus unversionierten Dateien **nur die Stadtkarten**. Die aus Blender importierten Meshes (`Content/Waffen/Cutpieces`) fehlen im Worktree, der Cuttable faellt auf Wuerfel zurueck. Das Bild-Gate haengt nicht daran (gemessen 5,4/13,5/2,8 % statt 16/18/4 % Glueh-Anteil, Grenze 1 %).
- **Batch: `%errorlevel%` und `%VAR%` INNERHALB eines `if`-Blocks werden VOR der Ausfuehrung expandiert und sind immer leer.** Ohne `setlocal EnableDelayedExpansion` + `!VAR!` meldet jeder erfolgreiche Lauf "abgebrochen". Kosten: ein Gate, das nie gruen werden konnte.

## Shell quirks (bash → PowerShell)
- Bash expands `$_` inside double quotes — wrap the whole PowerShell call in single quotes: `powershell -NoProfile -Command 'Get-Process python | ...'`.
- Git Bash `tail -N file1 file2` fails ("option used in invalid context"); tail one file per call.

### Batch (.cmd) auf dieser Maschine
- **Immer CRLF.** Eine mit LF geschriebene .cmd bricht mit `"." kann syntaktisch an dieser Stelle nicht verarbeitet werden` ab, sobald sie `if ( ... )`-Bloecke enthaelt. `write_file` schreibt LF — danach `sed -i 's/$/\r/'`. Pruefen: `file Tools/x.cmd | grep CRLF`.
- **In Klammerbloecken beendet das ERSTE ungeschuetzte `)` den Block**, auch eines in einem Text. Ein `echo ... errors."` am Zeilenende reisst den ganzen Block auf. Bauform deshalb: `if ... goto :marke` statt Klammerblock (siehe `Tools/verify_anchor.cmd`).
- **Der Exit-Code der Engine ist nicht die Messung.** `UnrealEditor-Cmd.exe -run=pythonscript` endet mit 127, wenn das Skript fehlt, mit -1, wenn es selbst abbricht — und mit einem Shutdown-Absturz (`UnrealEditor-MegascansPlugin.dll` in `dllmain_crt_process_detach`), *nachdem* das Ergebnis geschrieben ist. Massgeblich ist die Ergebnisdatei; ein gueltiges Ergebnis bei ungewoehnlichem Exit-Code wird laut gemeldet, nicht verworfen. Ohne Ergebnisdatei immer Fehler.
- **Ein haengender Prozess blockiert ein festes Logfile exklusiv** ("Der Prozess kann nicht auf die Datei zugreifen", auch kein `mv`). Deshalb je Lauf ein eigenes Log (`verify_anchor_%STAMP%.log`) — sonst scheitert der naechste Lauf an der Umleitung und man schliesst faelschlich, die Messung sei gescheitert.
- **Ein abgebrochener Prozess laesst sich aus einer fremden Sitzung nicht beenden** (`taskkill` meldet "Von dieser Aufgabe wird momentan keine Instanz ausgefuehrt"). Wer per `(cmd ... &)` startet und den Bash-Job killt, erbt das: zurueck bleibt ein Prozess, der CPU frisst und Dateien haelt. Den Bash-Job nicht killen, sondern den Lauf zu Ende laufen lassen.
- **Zwei Engines gleichzeitig auf demselben Projekt sind ein Absturz**, kein Zufall: ein Commandlet startet waehrend eines laufenden Editors, dann bricht es schon VOR dem Python-Start ab (Log endet bei ~18 KB, keine Zeile "Running Python script"). Vor jedem Engine-Lauf `Get-Process -Name UnrealEditor,UnrealEditor-Cmd` pruefen.

- **`rem` schuetzt NICHT vor `%~`-Parametern.** cmd expandiert Batch-Parameter auch in Kommentarzeilen und bricht mit „Die folgende Verwendung des Pfadoperators zur Ersetzung eines Batchparameters ist ungültig“ ab – der Lauf endet, BEVOR die Engine startet. Im Kommentar gehört `%%~nxf`. Gefangen in `Tools/test_verify_anchor_tool.py::test_kein_prozentparameter_im_kommentar` (es sucht ein einzelnes `%~`, nicht `%%~`).
- **`findstr /R /„Muster“ ist ein Syntaxfehler.** findstr liest `/"` als Schalter und zerlegt das Muster in `"^`, `/Game/`, `"` (`FINDSTR: /^ wurde ignoriert`, dann `Syntaxfehler`). Richtig: `findstr /C:"/Game/"` – `/C:` nimmt den Text wörtlich und frisst die führende `/` nicht als Schalter.
- **Zwei Engine-Lufe hintereinander ohne Pause scheitern am Zen-Start.** Der zweite Lauf stirbt nach ~20 s mit Exit -1, Log endet bei ~98 Zeilen bei „Launching executable … zenserver.exe“, ohne „Running Python script“ – sieht aus wie ein Skriptfehler aus, ist aber einer. `sleep 10..15` dazwischen genügt (00:21:31 zweimal hintereinander: beide tot; 00:22:32 einzeln: Exit 0).
- **Auch hier gilt: das Werkzeug muss zum Werkzeug passen.** `verify_anchor.cmd [Karte]` nimmt den Kartennamen als Argument, baut `/Game/Maps/WiesbadenCity_<Name>` in der Batch-Datei (nie auf der Kommandozeile – die schreibt Git Bash um) und schreibt je Karte `Saved\Diagnosenchor_verify[_<Karte>].txt`. Vorher: `Config\DefaultEngine.ini` umschalten und das Ergebnis überschreiben – beides macht einen Kartenvergleich unmöglich. Gemessen: ein Kartenname, den es nicht gibt, endet auf **Exit 3** und nicht auf 5 – das Skript bricht mit `RuntimeError: Karte nicht geladen` ab und schreibt nichts, also greifen die Marken 4/5 gar nicht erst; die Ursache steht als Traceback im Log.
### Tool quirks in this build
- Preview keeps the page's JS context across preview_navigate: monkeypatched window globals (e.g. window.scrollTo) survive navigation and silently eat later interactions — `location.reload()` via preview_evaluate resets them. Top-level `let`/`const` of the page are invisible to preview_evaluate (new Function scope); reference only DOM nodes or window properties.
- code_search is broken (vendored rg.exe missing, ENOENT) → use `find`/`grep` via run_terminal_command or read_files instead.
- A UCLASS header without its matching .cpp breaks the module at LINK time (constructor never defined); the error can look unrelated — check for orphaned headers (e.g. dropped mid-work) before deep compile debugging.
- File tools accept absolute paths OUTSIDE the project root (e.g. C:\Users\HP\Documents\...) despite project-root scoping.
- Serve the player/recorder with `python _analyse/range_server.py 8791` from `Audioaufzeichnungen` (takes a port arg; binds 127.0.0.1). Plain `python -m http.server` does NOT work here: Python 3.14.7 stdlib ignores Range headers (200 full-file, no 206) → Chrome media seeks silently reset to ~0 (looks like a player/SW bug, is the server). Detached launches can die silently (empty log) — verify the listener (netstat grep "abh" on German Windows, not "LIST").
- Replacing a registered preview whose pid is the dev server can kill that server ("dev server stopped responding while the previous preview was being released") — after a failed replace, restart the server before re-registering.
- preview_navigate to a same-page hash does NOT reload (hashchange on the live page); use `location.reload()` via preview_evaluate for a true reload. Harness round trips between preview_evaluate calls cost ~30-70 s of wall time — never infer playback rate from probe-to-probe deltas; read time state inside one evaluate with a short Promise+setTimeout (≤1.5 s, longer times out at 10 s).

### Reusable assets
- German audio-transcription pipeline (chunked, resumable, 2× parallel workers): C:\Users\HP\Documents\Audioaufzeichnungen\_analyse\ — worker.py + assemble.py; outputs Transkript.md / transcript_full.json / Analyse.md for "Aufzeichnung (2).m4a". Speaker labels: label_speakers.py (curated time-window map, role labels incl. "Unbekannt", OVERRIDES dict for sub-second boundary fixes) → transcript_with_speakers.json + speaker-prefixed Transkript.md. Speaker stats: Mutter ~67 % of words, Gesprächspartner:in ~22 %, Leo ~6 %, Vater (Telefon) ~1 %, Unbekannt ~3 %.
- M1 pipeline (2026-09-03): `python _analyse/archiv.py process "<Audioaufzeichnungen/<Stem>.m4a>"` runs convert→transcribe→assemble→retranscribe+splice→label→build, skip-if-done, SHA-256 check via `archiv.py check <m4a>`; artifacts `_analyse/<Stem>/`, config `<Stem>.archiv.json` beside the m4a. `speaker_mode` default `unknown` = all "Unbekannt", `builtin` = embedded curated windows, `json` = ARCHIV_SPEAKER_FILE. Scripts are parametrized via ARCHIV_* env (worker: MODEL/CPU_THREADS/CHUNK_SEC/LANGUAGE/BEAM_SIZE; label: SRC/OUT/OUT_MD/TITLE/SPEAKER_MODE/SPEAKER_FILE/OVERRIDES_FILE; build: SRC/OUT/AUDIO/TITLE; retranscribe: WAV/WINDOWS_JSON).
- M1 regression expectations: label rebuild keeps transcript_with_speakers.json byte-identical but changes Transkript.md's Duration line (computed 2:14:51 vs old hardcoded 2:16:35) — expected, not drift. Unit tests: `python -m unittest tests.test_archiv_lib -v` from _analyse.
- M1 edges: silent recordings → 0 segments → label/build stats prints divide by zero (now guarded); `load_config` raises on unknown keys (plan's sample test contradicted this — typo-tolerant configs are dangerous); a failed step's outputs are deleted so resume re-runs it (mtime skips alone can't detect "failed after writing"). Post-migration player depth: AUDIO `../../../<Stem>.m4a`, recorder link `../../recorder.html` — the plan's "Tiefe unverändert" was wrong.

## Alkis31 ist Alkis30: der Kartenvergleich (26.09.2026)

Auftrag: „Alkis31 vermessen und vergleichen“. Alkis31 ist seit 22:09:08 als `ok`
gebacken (2019 externe Pakete) und war nie gemessen worden. Ergebnis: **inhaltlich
dieselbe Karte** – ein weiterer Bake derselben Daten aendert nichts messbares.

- **`Saved/BuildHistory/CityBuilds.csv`:** Alkis28..31 sind in ALLEN Spalten
  zeichengleich (125024 Strassensegmente, 22227 Kreuzungen, 104459 Gebaeude,
  52690 Schilder, 4033 Raster, 1433829 Regions-Assets, 2010 Chunks, 500 m).
  Unterschiedlich sind nur Zeitstempel, Dauer (1075,7 s gegen 1172,9 s) und
  MapPath. Zwischen den beiden Bakes wurde kein Quelldatenbestand geaendert –
  die 97 s Unterschied sind Sache der Maschine, nicht der Karte.
- **Paketvergleich ganz ohne Engine** (`find Content/__ExternalActors__/Maps/<Karte>
  -printf '%s %P'`): 2019 Pakete je Karte, **2016 davon bytegleich gross**,
  Gesamtdifferenz 462 567 Byte = 0,024 %. Eine 22-Paar-Stichprobe mit `cmp -l`
  ergibt konstant 274–298 abweichende Byte, unabhaengig von der Paketgroesse
  (2,4 kB bis 2,9 MB) – das ist genau der Kartenname plus die Actor-GUIDs.
  Achtung: die Paarung nach Groessenrang ist bei gleich grossen Paketen
  unzuverlaessig (zwei Zellen 25886 Byte lagen 6 Ränge auseinander, 35 %
  „Unterschied”); richtig ist, in der Nachbarschaft nach dem Minimum zu suchen
  (dann 377 Byte).
- **Die drei Groessen-Ausreisser sind die zwei Riesen-Zellen:** 110 MB
  (−441 527 B) und 1,19 GB (−21 045 B); dort stimmen 56–58 % der Byte nicht,
  die Gesamtsumme aendert sich aber nur um 0,04 %. **Kontrollversuch Alkis29 gegen
  Alkis30 zeigt dasselbe Bild** (56 % abweichend, +2589 B) – die Streu-Reihenfolge
  dieser Zellen ist pro Bake anders, ihr Inhalt nicht. Wer die %-Zahl als
  Inhaltswechsel liest, zieht den falschen Schluss.
- **Ankermessung, beide Karten, dasselbe Werkzeug, zwei Minuten auseinander**
  (`Toolserify_anchor.cmd Alkis31` bzw. ohne Argument): je **2010 Zell-Actors,
  23799 leere Komponenten, 0 am Kartenursprung**, 82-zeilige Ergebnisse, die sich
  nur im Kartennamen und in der Reihenfolge der Beispielzellen unterscheiden
  (`Saved/Diagnose/anchor_verify.txt`, `anchor_verify_WiesbadenCity_Alkis31.txt`).
  Die Zeile `Bounds-über-Ursprung: 2010 von 2010` bleibt in beiden ein
  Commandlet-Artefakt (unge ladene Meshes melden Punkt-Bounds bei 0,0,0).
- **Nebenbefund aus dem Alkis31-Spielstart** (`Saved/Logs/WiesbadenReal.log`,
  21:20 UTC = 23:20 lokal): `GenerateStreaming for 'WiesbadenCity_Alkis31' took
  12.0 sec`, 27 geladene Zellen am Start – und `Dennos Laden: keine Hauswand von
  Sedanplatz 5 nach 30 s`. Der Laden fehlt also auch auf Alkis31; das ist kein
  Karten-, sondern ein Code-Problem (`AWiesbadenDennoShop` ist ein Laufzeit-Actor
  aus `GameMode.cpp`).
- **Groessenbefund mit Sprengkraft:** 2 der 2019 Pakete machen **66,7 %** der
  Nutzlast aus (1,19 GB + 110 MB von 1,96 GB). Das 1,19-GB-Paket ist der Grund,
  warum ein Bake ~20 min dauert, warum `Content/__ExternalActors__/` nie
  klonfest wird und warum ein Kartentest auf der Platte so lange braucht.

**Empfehlung: Alkis30 bleibt die Standardkarte.** Es gibt keinen gemessenen
Unterschied, und jede weitere Kartenumstellung verschlechtert die klonfeste Lage
(`.umap` untracked, `__ExternalActors__` gitignored). Alkis31 kann als Reserve
liegen bleiben – sie kostet 1,96 GB Platz und sonst nichts.
## Ka-52: der Hubschrauber (26.09.2026)

Auftrag: das angehängte Modell als spielbaren Kamow Ka-52. Gebaut sind
Flugbeleuchtung, zwei starke Suchscheinwerfer, ein sichtbares Cockpit,
3rd-Person- und Flugkamera, synchron gegenläufige Koaxialrotoren auf der
Rotorstangenachse, Bordgeschütz, Xbox-Belegung und Respawn auf dem
Helipad des Sebbotower. Belege: `Saved/Diagnose/ka52/`.

### Das Modell ist ein 1-Mesh-Fuser - das Projektmodell ist der echte Ka-52

Das **angehängte** GLB (`military helicopter 3d model.glb`, 1 007 915 Verts /
1,9 Mio. Tris in EINEM Mesh) ist eine AUSSENansicht und zum Zerlegen
unbrauchbar. Das Projektmodell `Content/Data/Raw/Ka52/ka52.glb` (6 Teile,
Echtmaß) ist der verwendete Ka-52. Sechs Meshes sind importiert
(`Fuselage`, `Rotor_Upper`, `Rotor_Lower`, `Nav_Red`, `Nav_Green`,
`Strobe_White`) plus `M_Ka52PBR`.

**Die Modellachsen:** Länge = **Y**, Nase = **-Y**. Belegt über echte
Vertexdaten (`Tools/ka52_fbxlage.py`): Heckfinner (Ende 66 cm breit,
Oberkante 295 cm) und Strobe liegen bei Modell-+Y, die Nase (189 cm breit,
Oberkante 160 cm) bei Modell--Y. Rumpf X -438..+433, Y -580..+826, Z 0..295.
Strobe Y +811..+829 / Z 281..299, Nav_Red X +426..+444,
Nav_Green X -444..-426 (beide Y 41..59, Z 151..169).

**ModelYaw = +90, nicht -90.** Mit -90 zeigte das Heck nach vorn, der
Hubschrauber flog rueckwaerts (Nase bei X = -7,1 m statt +7,1 m; Beweisbild
`flugrichtung.html`). Nebeneffekt von +90: Modell-+X (Steuerbord, rot) wird
Welt-+Y - also rot rechts, gruen links.

### Kabine und Kanone kommen aus Blender, nicht aus dem Import

Das Rumpf-Asset hat **keine** Kabine: im Spiel sieht man in einen leeren
Kasten, sobald die Kamera-Komponente den Rumpf ausblendet. Gebaut wird sie
in `Tools/Blender/build_ka52_cockpit.py` (Boden, Schott, Seitenwaende,
Kanzelholm, zwei Tandemsitze, zwei Pulte mit Schirmen, Zyklikbuegel,
Kollektivhebel, Pedale, Mittkonsole) und als **zwei getrennte FBX**
exportiert nach `Content/Vehicles/Ka52/SM_Ka52Cockpit` (am Rumpf, **nicht**
in `AddCockpitHiddenMesh` - die Kabine muss beim Ausblenden des Rumpfes
sichtbar bleiben) und `SM_Ka52GunTurret` (drehbarer Turm auf Modellpunkt
360, -230, 95).

Fuenf Fehler, die am 26.09. je einzeln einen Lauf gekostet haben - alle im
Blender-Skript kommentiert und in `ka52_export_bericht.txt` belegt:

1. **`bpy.ops.object.select_all(action="SELECT")` nimmt die GANZE Szene.**
   Beim Join der Kanone war die fertige Kabine mit ausgewahlt und wurde in
   den Pylon gejoint. Ergebnis: ein FBX mit einem Mesh `Gun_Turret`, darin
   die Kabine. UE importierte ein einziges Asset, die Kabine fehlte im
   Spiel, die Kanone war mit Sitzzeug gefuellt. Loesung: `nur_join()` joint
   nur noch die eigene Collection.
2. **Zwei Meshes in einer FBX ergeben kein `SM_`-Namenschema.** Der
   UE-Namensgeber nimmt den Mesh-Namen, nicht den Dateinamen, und der
   StaticMesh-Import fuehrt zusammen. `destination_name` auf einem
   `AssetImportTask` erzwingt den Namen nur bei einer Datei je Mesh.
   Loesung: zwei FBX, je ein Mesh.
3. **`unit_settings.scale_length` fehlt -> alles 100x zu gross.** Blender
   rechnet in Metern, das Skript in Zentimeter-Zahlen. Ohne
   `scale_length = 0.01` kam die Kabine als **162 m breites** Asset an
   (Y -50400..-23500). Mit 0.01 sind es 162 cm.
4. **Der FBX-Export dreht die Y-Achse** - bei `axis_forward="-Y"` **und**
   bei `"Y"`. Die Kabine stand bei Y +231..+500, also ueber dem Heck statt
   in der Nase. Spiegel an der Y-Achse beim Export - aber um den
   **Welt**ursprung: `ob.matrix_world.inverted() @ S @ ob.matrix_world`.
   Ein reines `S` auf den Mesh-Daten spiegelt am **Objektursprung** (die
   Kabine sitzt auf Y -367, dem Boden) und ergab Y -504..-235 statt
   -231..500. Danach `bmesh.ops.recalc_face_normals`: die Spiegelung hat
   negative Determinante und dreht die Umlaufrichtung der Dreiecke.
5. **Eine geneigte Tafel braucht eine gemeinsame Basis.** Mit +24 Grad um X
   schaute die Instrumententafel in die Nase, die Schirme steckten *in* der
   Tafel und die Pilotenvorschau war ein schwarzes Rechteck. Richtig ist
   -24 Grad (Flaeche zeigt zum Piloten, +Y) und `tafel_punkt()` fuer alles,
   was auf der Tafel sitzt.

**Gekoppelte Messkette, die den Zustand belegt statt zu behaupten**
(`ka52_export_bericht.txt` + `import_ka52_cockpit_ergebnis.txt`):

| Stufe | Cockpit X | Cockpit Y | Cockpit Z |
|---|---|---|---|
| Blender-Szene (Absicht) | -81..81 | **-500..-231** | 71..220 |
| FBX nach Rueck-Lesung | -81..81 | +231..+500 | 71..220 |
| UE-Asset | -81..81 | **-500..-231** | 71..220 |

### Der Import meldet nach Datei, nicht nach stdout

`Tools/import_ka52_cockpit.{py,cmd}` und `Tools/import_ka52_audio.{py,cmd}`
schreiben **nach `Saved/Diagnose/ka52/*_ergebnis.txt`**, und der CMD wertet
diese Datei aus. Prints aus `-run=pythonscript` erreichen weder Konsole noch
Log (AGENTS.md) - der erste Lauf meldete "Import unvollstaendig", obwohl die
Engine 5 s lang gearbeitet hatte. Jeder Lauf prueft ausserdem die **Masse**
der gelandeten Assets; ein Import, der ein Mesh an (0,0,0) bringt, sieht
sonst aus wie Erfolg.

### Bordgeschoetz und Suchscheinwerfer folgen dem Blick

`Gun->Aim(0,0)` stellte den Turm in Ruhelage nach vorn: man konnte den
Horizont drehen und der Schuss ging trotzdem geradeaus. Jetzt
`Gun->AimAt(Akteur + Blickachse * ZielDistanzCm)` (20 000 cm). Der endliche
Abstand ist beabsichtigt - bei 2 km Zieldistanz faellt der Zielpunkt in die
Nase und alle Winkel zwischen Muendung und Ziel liegen unter der
Wahrnehmungsschwelle, der Turm schiene still.

- **Rohrrichtung:** Das importierte Rohr zeigt in Modell-**+X**, der Heli
  fliegt nach Modell-**-Y**. `TurretYaw` hat deshalb eine Ruhelage von
  -90 Grad, sonst schiesst die Kanone im Stand nach Steuerbord.
- **Muendung:** `MuzzlePoint` bei (210, 0, **18**) - das Rohr sitzt im Asset
  auf Local-Z 18. Mit (210,0,0) stand die Muendung 18 cm daneben und der
  Muendungsfeuer lief neben dem Lauf statt aus ihm.
- **Suchscheinwerfer:** `SetSearchlightTarget(Weltpunkt)` rechnet im
  Modellraum (`ModelSpace->GetRelativeRotation().UnrotateVector`) auf die
  Nase als Bezug. Mit Handwerten (`SetSearchlightAim`, -1..1) kann der Kegel
  Nase und Pylon nicht gleichzeitig treffen - die sitzen 2,6 m auseinander.

### Koaxialrotor: "schnell gegeneinander", nicht "zwei Rotoren"

`ComputeCoaxialRotorRotation()` liefert beide Rotationen aus **einer**
Rechnung (gleicher Betrag, entgegengesetztes Vorzeichen). Zwei unabhaengige
Ausdruecke laufen irgendwann getrennt gepflegt. Der Test
`WiesbadenReal.Vehicles.Ka52Ausstattung` prueft Betrag, Gegen-Sinn, keinen
Roll-/Nickanteil, Stillstand bei 0 rpm und beide Naben auf derselben
XY-Position (< 5 cm Abstand, 100..130 cm Hoehenabstand).

### Flugsounds sind synthetisiert, nicht prozedural im Tick

`Tools/make_ka52_audio.py` (numpy) erzeugt vier **echte WAV-Dateien**:
Rotorblatt-Ticken, TV3-117-Turbine, 2A42-Schuss, Fahrtwind. Der prozedurale
Pfad (`FWiesbadenHelicopterAudioModel`) bleibt Rueckfall: eine
Saegezahn-Approximation ohne Transienten hoert man sofort als Rechner - und
gerade die Transienten *sind* der Hubschrauberlaut.

- **Schleifen sind periodisch gebaut**, nicht gekreuzt: das Rauschen entsteht
  im Spektralbereich (kein Fensterende = keine Naht), die Blattschlaege
  sitzen auf einem Takt mit Hannfenster. Der Generator **misst** die Naht
  selbst (Nahtsprung / typischer Sample-Schritt < 2) und schreibt die Zahl
  in den Bericht.
- **Der Rotor-Takt steht auf 30 Hz**, nicht auf 15: oben *und* unten je drei
  Blaetter, halber Versatz, zusammen der doppelte Blattschlag. Das ist der
  Unterschied zum normalen Hubschrauber und steht als Messwert im Bericht.
- **Bezugsdrehzahlen 300 / 600 rpm.** Die Pitch-Teiler in
  `UpdateAssetAudio()` sind Bezugsdrehzahlen, keine Einheiten. Mit den
  aelteren Teilern 560 / 3800 lag der Ton bei Reiseflug (350 / 700 rpm) eine
  Oktave zu tief.
- **Die Schleife sitzt am Asset, nicht nur im WAV-Header** - UE liest sie
  aus dem `USoundWave`. Der Import erprobt `looping` und `bLooping` und
  **schlaegt fehl**, statt "unbekannt" als Erfolg zu melden.

### Xbox-Belegung des Ka-52 (ReadInput / ReadDeviceInput)

| Funktion | Tastatur | Xbox-Controller |
|---|---|---|
| Nicken (zyklisch) | W / S | Linker Stick hoch / runter |
| Rollen (zyklisch) | A / D | Linker Stick links / rechts |
| Gieren (Pedal) | Q / E | Rechter Stick links / rechts |
| Kollektiv | Leertaste oder Shift hoch, Strg runter | RT hoch, LT runter |
| Triebwerk an/aus | G | **RB** (nicht Y: Y ist projektweit Ein-/Aussteigen) |
| Bordgeschoetz feuern | Linke Maustaste | **RT** rechts (ueber 0,35) |
| Suchscheinwerfer | L | D-Pad hoch |
| Landlicht | B | D-Pad runter |
| Kamera umschalten (Follow/Orbit/Cockpit) | C | - (Maus / rechter Stick) |
| Aussteigen | F | Y |

Sticks: Totzone 0,15, Expo 0,72. Der groessere Betrag zwischen Tastatur- und
Stickwert gewinnt - so stoert eine ruhende Eingabequelle die andere nicht.

### Wiederverwendbar

- **`Tools/contact_sheet.py`** - macht aus beliebigen Bildern eine
  HTML-Uebersichtsseite mit eingebetteten Data-URLs. Messungen sind nur
  belastbar, wenn das Bild daneben liegt; eingebettet bleibt die Seite
  eine Datei, die man verschieben kann, ohne dass die `<img src>`
  brechen (Saved/ ist nicht versioniert).

### Stand 26.09.2026: Build gruen, Test gruen

`Tools\build_gate1.cmd`: **Exit 0** (`Saved/Logs/wb_build_gate1.log`).
`WiesbadenReal.Vehicles.Ka52Ausstattung`: **Success**, `TEST COMPLETE. EXIT
CODE: 0` (`Saved/Logs/wb_test_ka52.log`). Die ganze Fahrzeuggruppe
`WiesbadenReal.Vehicles`: **72 Tests gruen, 0 Fehlschlaege**.

**Ein offener Editor blockiert den Build, und das ist hier der Normalfall:**
laeuft `UnrealEditor.exe` mit aktivem Live Coding, bricht UBT mit
"Unable to build while Live Coding is active" und `EXITCODE 6` ab - ohne
Compilefehler im Log, was wie ein Rechnerfehler aussieht. `Tools\
ka52_wait_build.cmd` wartet auf das Ende des Editors (prueft `cmd.exe` UND
`LiveCodingConsole.exe`, und noch einmal nach 10 s, weil in dieser Pause
manche den Editor wieder oeffnen) und baut danach Gate 1 plus den Test;
Ergebnis nach `Saved/Diagnose/ka52/build_test_ergebnis.txt`.

  **FALLE DABEI (26.09.2026, behoben):** das Skript benutzte `tasklist //FI` -
  die doppelten Schraegstriche sind eine Git-Bash-Konvention und werden in
  einer .cmd zu `\FI`. Die Abfrage lieferte daraufhin einen Fehler, `find`
  fand nichts, und das Skript meldete "Editor zu", waehrend der Editor lief -
  es hat in vier Laeufen nie gewartet. In einer .cmd gehoeren **einfache**
  Schraegstriche hin; `//FI` sieht in der Bash-Zeile daneben richtig aus und
  ist in der Batch-Datei falsch.

**Noch offen**

- **Rotorachse: behoben, nicht festgehalten.** Die alte Notiz "Scheibenmitte
  Rotor_Upper (179,4; -5,6) gegen Rotor_Lower (186,1; -4,7) = 6,66 cm" war
  **zweimal falsch**: Sie verglich die Mitten der Bounding Box, und die
  liegen 1,80 m neben dem echten Drehpunkt. Der Drehpunkt eines Dreiblatt-
  rotors ist der **Schwerpunkt** der Vertexmenge (drei gleiche Blaetter im
  120-Grad-Abstand machen die Menge 3-fach-symmetrisch), und der liegt bei
  **(-0,19; +2,59) cm** (oben) und **(+4,92; -2,06) cm** (unten) - beide fast
  auf dem Modellursprung, aber der untere 5,4 cm daneben.
  Korrigiert an **einer** Stelle: `AWiesbadenHelicopter::ComputeRotorMountOffset`
  (Versatz.xy = -(ModelYaw * Drehpunkt).xy), beide Rotoren gehen durch
  dieselbe Funktion, der Hub-Node bleibt unangetastet, der Gegenlauf und der
  Ho henabstand von 118,5 cm kommen aus `ComputeCoaxialRotorRotation`
  unveraendert. Die Zahlen stehen in `GetRotorDrehpunktCm(bool bUnten)` -
  bewusst **nicht** als Konstruktor-Lokale, damit der Test dieselbe Quelle
  liest statt einer zweiten Abschrift.
  **DREI VERFAHREN, DIE NICHT TAUGEN** (alle drei mit Zahlen in
  `Tools/ka52_rotorachse.py` dokumentiert): (1) Bounding-Box-Mitte, 1,80 m
  daneben; (2) Kreisfit ueber die Blattspitzen, Streuung 1,12 m, weil das
  Mesh keine ebene Kreisflaeche ist; (3) zusammenhaengende Teile, 4755
  Bruchstuecke, weil das Mesh nicht verschmolzen ist. Geblieben ist die
  3-fach-Rotationssymmetrie, mit einem Restfehler als Guete: 4,2 mm (oben)
  und 6,3 mm (unten) gegen **375 mm** an der Kontrollstelle (Box-Mitte).
  Ein Verfahren, das ueberall "gut" meldet, prueft nichts.
- **Messgrundlage ist die Importquelle, NICHT das Asset** - und das ist der
  wichtigste Satz dazu. `Content/Vehicles/Ka52/Rotor_*.uasset` traegt Nanite;
  die klassischen LOD-Buffer haben **773 bzw. 792 Dreiecke** gegen 208 009
  bzw. 221 119 in `Content/Data/Raw/Ka52/ka52_ue.fbx`, und die Achse dieses
  Ersatzdatensatzes liegt **3,4 bzw. 4,9 cm** daneben. Wer den Korrekturwert
  am Asset misst, korrigiert gegen einen Fehler. Belegt in
  `Saved/Diagnose/ka52/rotorachse_fbx.txt` (FBX) neben `rotorachse.txt` (GLB),
  beide mit Restfehler und Kontrollstelle.
- **Drei Tests, weil ein Test hier nicht reicht** (Auftrag: "Achsabweichung
  als Testbedingung"):
  1. `WiesbadenReal.Vehicles.Ka52Ausstattung`, Abschnitt 5b: Rechnung
     **exakt** (Versatzabweichung 0,00000 cm, Drehpunkt nach der Kette
     (-0,00000, 0,00000) cm) und Geometrie mit der **Aufloesung des
     Datensatzes** - hergeleitet aus dem Restfehler, nicht geraten:
     `4 x Restfehler + 2 cm`. Die Geometrie allein taugt nicht: bei 773
     Dreiecken ist eine 2-cm-Toleranz unter der Aufloesung des Datensatzes
     und wuerde den Ersatzdatensatz pruefen statt des Flugmodells.
  2. `WiesbadenReal.Vehicles.HelicopterModell`: dieselbe Pruefung an beiden
     Rotoren, gegen dieselbe Funktion. Dieser Test behauptete vorher
     "XY-Versatz ~0" - die alte, widerlegte Annahme. Mit der Korrektur
     haette er bei der unteren Scheibe (5,34 cm) gefeuert; er ist auf die
     Korrektur umgestellt.
  3. `Tools/test_ka52_rotorachse.py` (Python, ohne Editor): haelt die im
     Pawn eingetragenen Zahlen gegen den Messbericht und prueft, dass die
     Korrektur beide Scheiben exakt auf (0, 0) legt und die Nabenhoehen
     118,5 cm bleiben. Faellt, wenn jemand am Modell oder am Wert etwas
     aendert.
- **Zwei Kontrollstellen im Messtest, weil eine nicht reicht.** Die weite
  (Bounding-Box-Mitte, 179 cm daneben) sättigt: dort findet ein gedrehter
  Punkt keinen Nachbarn mehr, der Restfehler ist der Deckel (50 cm) gegen
  3,1 cm am Drehpunkt. Sie beweist "das Verfahren unterscheidet", nicht
  "die Lage ist aufgelöst". Dafür die **nahe** Kontrolle: Restfehler in
  5/10/20 cm Abstand, 3,13 -> 5,11 -> 6,55 -> 15,94 cm. Waere das Feld um
  den Drehpunkt flach, waere auch die Angabe "am Mesh gemessen (0,99;
  -0,58) cm" wertlos.
- **Falle fuer spaeter:** der Nanite-Ersatzdatensatz hat 1303 Vertex und
  sieht in jeder Hinsicht plausibel aus. Ein Test, der `LODResources[0]`
  liest und eine schoene Zahl bekommt, hat nichts gemessen - die LOD
  wird gegen den Authored-Bounds geprueft und der Befund kommt mit ins Log
  (`Nanite an, 1 LODs, ... 773 Dreiecke`).
- **Falle fuer spaeter:** `TestTrue` schweigt im Erfolgsfall. Die drei
  Belegzeilen (Kontrollstelle, Rechnung, Messwerte) kommen darum ausdruecklich
  als `AddInfo` - sonst steht in einem gruenen Log kein einziger Wert.
- Nicht committet: alles Ka-52 liegt uncommitted im Arbeitsbaum. Fuer den
  Build war nur `git add` der neuen **Quell**dateien noetig (adaptive
  Non-Unity-Build nimmt `git status` als Arbeitsmenge, untracked wird nicht
  gebaut); `Content/Audio/Ka52/` und die Ka-52-Assets sind weiterhin
  untracked.
- Spielprobe im laufenden Editor steht aus: Kamerawechsel C, Cockpit mit
  ausgeblendetem Rumpf, Nachtfahrt mit den beiden Suchscheinwerfern,
  Schuss auf ein Zeltdach.
- Nicht committet: alles Ka-52 liegt uncommitted im Arbeitsbaum. Fuer den
  Build war nur `git add` der neuen **Quell**dateien noetig (adaptive
  Non-Unity-Build nimmt `git status` als Arbeitsmenge, untracked wird nicht
  gebaut); `Content/Audio/Ka52/` und die Ka-52-Assets sind weiterhin
  untracked.
- Spielprobe im laufenden Editor steht aus: Kamerawechsel C, Cockpit mit
  ausgeblendetem Rumpf, Nachtfahrt mit den beiden Suchscheinwerfern,
  Schuss auf ein Zeltdach.

## Bildbelege: Kabine, Abzug und synthetische Eingabe (26.09.2026)
- **Die Cockpitaugen waren RICHTIG - die Fehldiagnose kam aus zwei Rahmen.**
  `CockpitOffset` wird im ACTORraum addiert (die Fahrzeugkamera haengt am
  SceneRoot, der Cockpit-Socket an ihr), die Bounding Box des Kabinen-Meshes
  ist MESSLOKAL. Man hatte (395, 34, 204) gegen (X -81..81) gestellt und
  daraus "314 cm davor, in freier Luft" geschlossen. Im selben Rahmen:
  Kabine Actorraum X 231..500, Y -81..81, Z 71..220, Augen 133 cm ueber dem
  Boden, genau ueber dem zweiten Sitzkissen (X +9..+59, Y -371..-421). Der
  Augpunkt sitzt im Pilotenplatz; `Tools/Blender/build_ka52_cockpit.py`
  Zeile 409 rendert die Pilotenvorschau von (34, -392, 206) - dieselbe Stelle.
  **Vor dem Vergleich immer nachrechnen, in welchem Raum die Zahl steht.**
- **Warum trotzdem keine Kabine im Bild war: Rueckseitenverwurf.** Die Huellle
  ist geschlossen (0 nicht-gepaarte Kanten) und NICHT einheitlich gewickelt -
  vom Pilotenauge zeigen 166 der 342 Flaechen vom Auge weg, 130 ihm zu. Mit
  einseitigen Materialien bleibt nur die Innenflaeche der gegenueberliegenden
  Wand: ein flaches graues Band. Behoben in `WiesbadenHelicopter.cpp`: der
  Konstruktor setzt `TwoSided` am UMaterial der vier Kabinen-Stoffe.
  ACHTUNG: die vier Instanzen teilen sich EIN Elternmaterial
  (`FBXLegacyPhongSurfaceMaterial`), das im transienten Interchange-Cache
  `/InterchangeAssets/Materials` liegt und nicht speicherbar ist - die
  Umstellung muss also zur Laufzeit erfolgen, ein Editor-Klick wäre nach dem
  naechsten Import wieder weg. `Tools/ka52_kabine_zweiseitig.py` ist der
  Versuch via Skript; es speichert die Instanzen, nicht das Elternmaterial.
- **`ReadDeviceInput` hatte keinen Aufrufer.** Die Funktion las den Abzug und
  meldete die Flanke, wurde aber nirgends aufgerufen: die Bordkanone war im
  Spiel ueberhaupt nicht ausloesbar (39 s Versuch, 0 Schuesse, kein
  "Munition leer"). Jetzt aus `Tick` aufgerufen, nachdem die Flugphysik.
- **Neue Waffe im Test:** Ka52GeraetTest misst die Augen gegen die Asset-Box
  und prueft die Aughoehe ueber dem Kabinenboden (90..150 cm). Faellt der
  Import oder die Konstante auseinander, faellt der Test.
- **Diagnose im Log:** `MeldeKabine()` meldet bei jedem Moduswechsel
  Sichtbarkeit, OwnerNoSee, Grenzen und ob die Kabine je gezeichnet wurde
  (`zuletztGezeichnet`). Ohne das sahen "nie gezeichnet" und "an anderer
  Stelle" im Bild gleich aus.
- **Kurztasten brauchen 300 ms.** UEs Eingabestapel enthaelt nur Tasten mit
  Ereignis im laufenden Frame; ein 90-ms-Druck kann dazwischenfallen. Ein
  Lauf blieb im Modus 0 (Follow) und lieferte ein Bild, das wie ein
  Cockpitbild aussah. Der Aufnahmefahrer prueft den Modus jetzt im Log
  ("Fahrzeug-Kameramodus: N") und knipst erst danach - sonst beweist das Bild
  nichts.
- **Gehaltener Abzug bleibt unmoeglich** (siehe unten): keybd_event KEYDOWN
  ohne KEYUP gilt einen Frame, mouse_event geht an das Fenster unter dem
  Zeiger, WM_LBUTTONDOWN an das Fenster direkt blieb wirkungslos. Fuer
  Dauerfeuer braucht es eine wiederholte Tastenfolge im Spiel.

## Aufnahme-Faehigkeit: was bis 26.09.2026 fehlte (Bilderserie, Konsolenweg)
- **`-WbShotSteps=<plan>` fahren eine ganze Sitzung in einem Plan.** Schritte:
  `modus=N` (Fahrzeugkamera 0 Folge / 1 Orbit / 2 Cockpit), `hold` (ein Bild
  aus der aktuellen Sicht), `turm` (Hubschrauber auf den Helipad des
  Sebbotower, neu: `WbHeliTurm`). Bilder landen als `WbSeries_000 ...`.
  Derselbe Wortschatz funktioniert ueber `-WbShotPoseFile` (eine Zeile je
  Schritt) - damit braucht man gar keine Kommandozeile fuer den Plan.
- **TRENNER IST DAS PLUSZEICHEN, NICHT DAS KOMMA.** `FParse::Value` haelt den
  Wert am ersten Komma an: aus `-WbShotSteps=modus=2,hold,hold` wurde
  "modus=2", der Plan hatte EINEN Schritt und die Sitzung lieferte ein Bild.
- **CVar setzen: Leerzeichen, nicht Gleichheitszeichen.** `wb.HeliKamera 2`
  wirkt, `wb.HeliKamera=2` bleibt wirkungslos. Auf der Kommandozeile
  wiederum darf kein Leerzeichen im Argument stehen (Start-Process zerschneidet
  es) - dort hilft nur der Weg ueber den Ausfuehrungsplan.
- **C++-Fallstricke, alle am 26.09.2026 gemessen:** `TAutoConsoleVariable` hat
  kein `Set` (sondern `AsVariable()->Set(...)`); `APlayerController::ConsoleCommand`
  nimmt in 5.8 `(FString, bool)` und liefert FString;
  `UMaterial::CacheResourceShadersForRendering` ist PRIVAT - der oeffentliche
  Weg fuer eine Shader-Aenderung zur Laufzeit ist `PostEditChange()`.
- **Die Motorsperre kennt kein "Ende".** Modi: Start / Nehmen / Freigeben /
  Status. Ein Skript, das `-Modus Ende` aufruft, haelt die Sperre und blockiert
  den naechsten Lauf - auch den des Menschen. `Tools/ka52_wait_build.cmd` nimmt
  die Sperre und gibt sie NIE frei; das ist nach jedem Lauf zu pruefen.
- **Ein "ERGEBNIS: OK" vom Build-Gate beweist nichts.** Am 26.09.2026 lief der
  Wartelauf 4 s, meldete Exit 0 und Ergebnis OK - die DLL war 28 Minuten aelter
  als die Quellen. Massgeblich ist der Zeitstempel der
  `Binaries/Win64/UnrealEditor-WiesbadenReal.dll`.
- **Ein haengender Prozess war nie der Blocker - die Sperre und ein laufender
  Editor schon.** Am 26.09.2026 11:00-11:25 liefen `Automation RunTests` UND
  eine fensterliche `-game`-Sitzung mit 21 und 25 Bildern **neben** dem
  haengenden `verify_anchor_state`-Prozess (PID 43820) - problemlos. Der
  haengende Prozess blockierte weder Build noch Test. Was blockierte: die
  Motorsperre (Label `messlauf`) und ein lebender `UnrealEditor.exe`. Vor dem
  Warten also BEIDES pruefen, nicht nur die Prozessliste.
- **.NET nummeriert unbenannte Regex-Gruppen VOR den benannten.** Ein Muster
  mit `(?<t>...)`, `(?<ms>...)` und einem nackten `(\d+)` liefert
  `$Matches[1]` = der nackte Wert, `$Matches[2]`/`[3]` = t/ms. Der Aufnahmefahrer
  las `$Matches[3]` (leer) und schrieb `[int]$null = 0` in JEDE Belegdatei:
  "Kameramodus 0" auf allen 21 Bildern, obwohl im Log 0/1/2 stand. Nur mit
  BENANNTEN Gruppen (`(?<modus>\d+)`, `$Matches['modus']`).
- **Zwei Zeitzonen in derselben Auswertung:** die Engine loggt in UTC, die
  Windows-Dateizeiten sind lokal (im Sommer 2 h Unterschied). Ein Vergleich
  "Logzeit <= Bildzeit" ist ohne Umrechnung immer wahr und liefert damit den
  LETZTEN Zustand fuer jedes Bild. `[datetime]::SpecifyKind($t,'Utc').ToLocalTime()`
  bzw. in Python `tzinfo=timezone.utc` vor `.timestamp()`.
- **Ein haengender `UnrealEditor-Cmd.exe` sperrt mehr als den Editor-Slot.** Am
  26.09.2026 lief PID 43820 seit dem 25.09. 22:31 (`-run=pythonscript
  Tools/verify_anchor_state.py`, 1 Thread, 6 MB, 100 % eines Kerns, CPU-Zeit
  waechst sekuendlich). Der Editor war zu, die Engine trotzdem nicht benutzbar:
  der Prozess haelt das Modul-DLL offen, UBT kann nicht linken, und eine zweite
  Engine daneben ist im Projekt als Absturz dokumentiert. Das Log endete um
  04:11 mit "LogExit: Exiting", die Ergebnisdatei Saved/Diagnose/
  anchor_verify.txt stammt aus 00:24 - der Lauf hat also NICHTS geliefert und
  haelt trotzdem die Engine. **Vor dem Warten fragen: liefert der Prozess
  ueberhaupt noch etwas, und schreibt er ueberhaupt eine Logzeile?**
- **`-WbTime` ist die Tageszeit in STUNDEN, nicht die Laufzeit.** Der
  Selbstabbruch heisst `-WbQuitAfter=<Sekunden>`. Ein Aufnahmefahrer ohne
  `-WbQuitAfter` laesst die Engine belegt, wenn er stirbt.
- **Live Coding sperrt den Build** (auch ohne sichtbaren Editor
  `LiveCodingConsole.exe`). Bauen erst, wenn BEIDE weg sind.
- **Ein Start mit dem Ordner statt der .uproject** laeuft scheinbar (der Editor
  erscheint im Task-Manager), schreibt aber kein Log und haelt die Sperre. Der
  Aufnahmefahrer prueft die Projektdatei jetzt vorher.
- **Der Aufnahmefahrer belegt JEDES Bild mit dem Kameramodus aus dem Log**
  (Textdatei neben dem PNG) - ein Follow-Bild mit Cockpit-Titel beweist
  nichts, genau das ist am 26.09.2026 zweimal passiert.

## Gate B (Besitz): der geteilte Arbeitsbaum gehoert nicht automatisch mir (27.09.2026)

Vier Tage lang lagen fremde Dateien uncommitted im Arbeitsbaum, der Baum stand
auf dem Wegwerf-Testbranch `wt-gatetest` eines anderen Threads, und
`git status` sah aus wie die eigene Arbeit. `git commit` nimmt ALLES mit, was
vorgemerkt ist - git kennt keine Threads, und `vor_dem_commit.py` kann das erst
seit dem 27.09.2026.

* **Anspruch nehmen:** `python Tools/vor_dem_commit.py --besitz-ansprechen
  Tools/ Source/WiesbadenReal/World/SebboHq*.cpp`. Steht in `.git/wb_besitz.json`
  (gemeinsames .git-Verzeichnis, von keinem Commit erfasst, fuer alle
  Worktrees identisch). `--besitz-zeigen` listet, `--besitz-freigeben` gibt
  zurueck.
* **Fremd heisst: anderer Branch ODER anderer Thread.** Nur den Branch zu
  vergleichen war die erste Fassung - und der erste echte Lauf meldete
  `gruen`, weil der fremde Thread auf demselben Wegwerf-Branch sass wie ich.
  Ein Wegwerf-Branch identifiziert niemanden. Thread-Name aus `--thread`,
  `WB_THREAD` oder `git config wb.thread`, sonst der Branchname.
* **Ein toter Prozess gibt den Anspruch frei** (Hinweis, kein ROT). Die erste
  Fassung blockierte auch verwaiste Ansprueche - eine Registry, die einen
  abgestuerzten Thread ewig festhält, endet in `--no-verify` fuer alle, und
  dann prueft gar nichts mehr. Eine kaputte Registry ist dagegen ROT: wer sie
  loescht, schaltet genau das Gate ab, das ihn schuetzt.
* **Gate B laeuft VOR dem Engine-Lock und vor Gate 0**, ohne Prozess. Beim
  Push laeuft es NICHT (dort ist der Commit schon geschrieben) - sonst
  koennte nach einem fremden Thread niemand mehr ausliefern.
- **Zwei Fallen beim Erweitern, beide am 27.09. gemessen:** `Lauf.bericht()`
  griff auf das Ergebnis-Objekt eines Gates OHNE Subprozess zu und endete mit
  Traceback statt mit einer Ablehnung (`fertig is None` abpruefen). Und
  `--thread` wurde geparst, aber nicht an `gates_fahren()` durchgereicht -
  Besitz_gate ermittelte den Namen selbst, landete beim Branchnamen und wies
  den Thread ab, dem die Arbeit gehoerte. Beides decken jetzt Tests in
  `Tools/test_vor_dem_commit.py` (86 Tests in der Datei, 324 in der Suite).
