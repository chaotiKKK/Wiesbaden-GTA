// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "GIS/WiesbadenWorldBuilder.h"

#include "WiesbadenReal.h"

#include "Async/Async.h"
#include "Components/BillboardComponent.h"
#include "GIS/HeightmapImporter.h"
#include "GIS/RoadTypeLibrary.h"
#include "GIS/WiesbadenBuildSummary.h"
#include "GIS/WiesbadenCityPipeline.h"
#include "GIS/WiesbadenRegion.h"
#include "GIS/WiesbadenSignAssets.h"
#include "GIS/WiesbadenTrafficSignCatalog.h"
#include "HAL/FileManager.h"
#include "Internationalization/Text.h"
#include "Landscape.h"
#include "LandscapeProxy.h"
#include "GIS/WiesbadenHeightAudit.h"
#include "Engine/Texture2D.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"
#include "UObject/GarbageCollection.h"
#include "UObject/UObjectGlobals.h"
#include "World/RegionAssetSpawnerComponent.h"
#include "World/RoadFurnitureSpawnerComponent.h"
#include "World/WiesbadenCityChunk.h"

#include <atomic>

#if WITH_EDITOR
#include "FileHelpers.h"
#include "Misc/ScopedSlowTask.h"
#include "WorldPartition/WorldPartition.h"
#endif

namespace
{
	FText BuildStageToText(int32 StageIndex)
	{
		using Stage = WiesbadenCityPipeline::EBuildStage;
		switch (static_cast<Stage>(StageIndex))
		{
		case Stage::Prepare:	return FText::FromString(TEXT("Vorbereitung..."));
		case Stage::ParseOsm:	return FText::FromString(TEXT("Parse OSM-Daten..."));
		case Stage::ImportDem:	return FText::FromString(TEXT("Importiere DEM..."));
		case Stage::Roads:		return FText::FromString(TEXT("Generiere Strassennetz..."));
		case Stage::Buildings:	return FText::FromString(TEXT("Generiere Gebaeude..."));
		case Stage::Terrain:	return FText::FromString(TEXT("Generiere Landscape..."));
		case Stage::Furniture:	return FText::FromString(TEXT("Platziere Strassenausstattung..."));
		case Stage::Done:		return FText::FromString(TEXT("Fertig."));
		default:				return FText::GetEmpty();
		}
	}

	/**
	 * Thread-sicherer Arbeitskontext des Hintergrund-Builds.
	 *
	 * Der Worker schreibt Fortschritt (std::atomic) und Ergebnisse; der
	 * Game-Thread liest die Ergebnisse erst, nachdem bDone true geworden ist -
	 * die seq_cst-Semantik der Atomics stellt die happens-before-Beziehung
	 * zwischen Worker-Schreibzugriffen und Game-Thread-Lesezugriffen her.
	 */
	struct FWiesbadenBuildContext
	{
		// Eingaben und Pipeline-Objekte (vom Game-Thread kopiert; die Objekte
		// selbst bleiben ueber die UPROPERTY-Member des Actors GC-erreichbar).
		WiesbadenCityPipeline::FBuildInput Input;
		WiesbadenCityPipeline::FBuildTools Tools;

		// Fortschritt / Steuerung.
		std::atomic<int32> ProgressPercent{0};
		std::atomic<int32> Stage{static_cast<int32>(WiesbadenCityPipeline::EBuildStage::Prepare)};
		std::atomic<bool> bDone{false};
		std::atomic<bool> bCancelRequested{false};

		// Ergebnis (Worker befuellt; Game-Thread liest nach bDone).
		FWiesbadenCityData Data;
		bool bCancelled = false;
	};

	/**
	 * Worker-Thread: delegiert die reine Datenverarbeitung an die gemeinsame
	 * Pipeline (WiesbadenCityPipeline::BuildCityData) und meldet nur den
	 * Fortschritt/Abbrechen-Zustand ueber die Atomics zurueck.
	 */
	void RunBuildPipeline(const TSharedRef<FWiesbadenBuildContext, ESPMode::ThreadSafe>& Context)
	{
		const WiesbadenCityPipeline::EBuildResult Result = WiesbadenCityPipeline::BuildCityData(
			Context->Input,
			Context->Tools,
			Context->Data,
			[Context](int32 Percent, WiesbadenCityPipeline::EBuildStage Stage)
			{
				Context->ProgressPercent.store(Percent);
				Context->Stage.store(static_cast<int32>(Stage));
			},
			[Context]() { return Context->bCancelRequested.load(); });

		Context->bCancelled = (Result == WiesbadenCityPipeline::EBuildResult::Cancelled);
		Context->bDone.store(true);
	}
}

AWiesbadenWorldBuilder::AWiesbadenWorldBuilder()
{
	PrimaryActorTick.bCanEverTick = false;

	CustomOrigin = FGeoCoordinate(
		UGeoCoordinateConverter::WiesbadenOriginLongitude,
		UGeoCoordinateConverter::WiesbadenOriginLatitude,
		UGeoCoordinateConverter::WiesbadenOriginHeight);

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	BillboardComponent = CreateDefaultSubobject<UBillboardComponent>(TEXT("Billboard"));
	BillboardComponent->SetupAttachment(Root);

	RoadMeshComponent = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("RoadMesh"));
	RoadMeshComponent->SetupAttachment(Root);

	BuildingMeshComponent = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("BuildingMesh"));
	BuildingMeshComponent->SetupAttachment(Root);

	TerrainMeshComponent = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("TerrainMesh"));
	TerrainMeshComponent->SetupAttachment(Root);

	FurnitureSpawner = CreateDefaultSubobject<URoadFurnitureSpawnerComponent>(TEXT("FurnitureSpawner"));
	FurnitureSpawner->SetupAttachment(Root);

	RegionAssetSpawner = CreateDefaultSubobject<URegionAssetSpawnerComponent>(TEXT("RegionAssetSpawner"));
	RegionAssetSpawner->SetupAttachment(Root);
}

void AWiesbadenWorldBuilder::ReleasePipelineObjects()
{
	// Nach Build-Ende werden die Generatoren nicht mehr gebraucht: der Worker ist
	// fertig, der Game-Thread liest nur noch Context->Data. Das Nullen der
	// UPROPERTY-Anker gibt sie fuer den GC frei, statt sie bis zum naechsten Build
	// oder zur Actor-Zerstoerung am Leben zu halten.
	PipelineConverter = nullptr;
	PipelineParser = nullptr;
	PipelineImporter = nullptr;
	PipelineTypeLibrary = nullptr;
	PipelineRoadGenerator = nullptr;
	PipelineBuildingGenerator = nullptr;
	PipelineRegionGenerator = nullptr;
	PipelineRegionAssetGenerator = nullptr;
	PipelineTerrainGenerator = nullptr;
	PipelineFurnitureGenerator = nullptr;
	PipelinePickupSpotGenerator = nullptr;
}

void AWiesbadenWorldBuilder::BuildCity()
{
	if (bBuildInProgress)
	{
		UE_LOG(LogWbCore, Warning, TEXT("BuildCity: Ein Build laeuft bereits - Aufruf ignoriert."));
		return;
	}

	bBuildInProgress = true;
	BuildProgress = 0.0f;
	BuildStatus = TEXT("Vorbereitung...");
	ResetResults();

	// Leere Material-Slots aus dem Content fuellen, bevor Geometrie entsteht:
	// die Zuweisung passiert beim Backen der Chunks und laesst sich spaeter
	// nicht nachholen (die Chunks speichern nur die Mesh-Komponenten).
	EnsureDefaultMaterials();

	// Ein frisch erzeugtes Level hat keine Lichtakteure - ohne sie ist die
	// fertige Stadt vollstaendig schwarz.
	EnsureLightingActors();

	const double StartTime = FPlatformTime::Seconds();

	// Pipeline-Objekte im Game-Thread erzeugen und als UPROPERTY halten, damit
	// GC sie waehrend des Hintergrund-Builds nicht einsammelt. Der Worker nutzt
	// sie nur stateless/lesend. URoadNetworkGenerator ist bewusst nicht
	// reentrant - genau ein Build laeuft gleichzeitig (bBuildInProgress).
	PipelineConverter = NewObject<UGeoCoordinateConverter>(this);
	PipelineParser = NewObject<UOSMDataParser>(this);
	PipelineImporter = NewObject<UHeightmapImporter>(this);
	PipelineTypeLibrary = NewObject<URoadTypeLibrary>(this);
	PipelineRoadGenerator = NewObject<URoadNetworkGenerator>(this);
	PipelineBuildingGenerator = NewObject<UBuildingGenerator>(this);
	PipelineRegionGenerator = NewObject<UWiesbadenRegionGenerator>(this);
	PipelineRegionAssetGenerator = NewObject<UWiesbadenRegionAssetGenerator>(this);
	PipelineTerrainGenerator = NewObject<UTerrainGenerator>(this);
	PipelineFurnitureGenerator = NewObject<URoadFurnitureGenerator>(this);
	PipelinePickupSpotGenerator = NewObject<UWiesbadenPickupSpotGenerator>(this);

	if (!(bUseWiesbadenOrigin
		? PipelineConverter->InitializeWithWiesbadenOrigin()
		: PipelineConverter->Initialize(CustomOrigin)))
	{
		LastError = TEXT("Georeferenzierung konnte nicht initialisiert werden.");
		UE_LOG(LogWbCore, Error, TEXT("BuildCity abgebrochen: %s"), *LastError);
		bBuildInProgress = false;
		// Frueh-Abbruch: die Generatoren wurden erzeugt, aber der Worker nie
		// gestartet - sofort wieder freigeben.
		ReleasePipelineObjects();

		// Historie auch fuer fehlgeschlagene Laeufe (kein Pipeline-Kontext).
		WriteBuildSummaryToCsv(nullptr, nullptr, nullptr, nullptr,
			FString::Printf(TEXT("fehlgeschlagen: %s"), *LastError),
			FPlatformTime::Seconds() - StartTime);
		return;
	}

	// Eingaben in einen thread-sicheren Kontext kopieren. Der Worker liest
	// bewusst keine Actor-Properties, damit eine parallele Aenderung im
	// Details-Panel keine Datenrennen erzeugt.
	const TSharedRef<FWiesbadenBuildContext, ESPMode::ThreadSafe> Context =
		MakeShared<FWiesbadenBuildContext, ESPMode::ThreadSafe>();

	Context->Input.OsmFilePath = OsmFilePath;
	Context->Input.CityPrompt = CityPrompt;
	Context->Input.DemFilePath = DemFilePath;
	Context->Input.AlkisFilePath = AlkisFilePath;
	Context->Input.bImportDem = bImportDem;
	Context->Input.bGenerateRoads = bGenerateRoads;
	Context->Input.bGenerateBuildings = bGenerateBuildings;
	Context->Input.bGenerateTerrain = bGenerateTerrain;
	Context->Input.bGenerateRegionAssets = bGenerateRegionAssets;
	Context->Input.bGenerateFurniture = bGenerateFurniture;
	Context->Input.bGeneratePickupSpots = bGeneratePickupSpots;
	Context->Input.PickupSpotSettings = PickupSpotSettings;
	Context->Input.RegionAssetSettings = RegionAssetSettings;
	Context->Input.VerticalReferenceMeters = VerticalReferenceMeters;
	Context->Input.RoadSettings = RoadSettings;
	Context->Input.BuildingSettings = BuildingSettings;
	AddressFacadeMaterials.GetKeys(Context->Input.BuildingSettings.FacadeOverrideAddresses);
	Context->Input.TerrainSettings = TerrainSettings;
	Context->Input.FurnitureSettings = FurnitureSettings;
	Context->Input.TrafficSettings = TrafficSettings;
	Context->Input.RoadTypeConfigPath = RoadTypeConfigPath;
	Context->Tools.Converter = PipelineConverter;
	Context->Tools.Parser = PipelineParser;
	Context->Tools.Importer = PipelineImporter;
	Context->Tools.TypeLibrary = PipelineTypeLibrary;
	Context->Tools.RoadGenerator = PipelineRoadGenerator;
	Context->Tools.BuildingGenerator = PipelineBuildingGenerator;
	Context->Tools.RegionGenerator = PipelineRegionGenerator;
	Context->Tools.RegionAssetGenerator = PipelineRegionAssetGenerator;
	Context->Tools.TerrainGenerator = PipelineTerrainGenerator;
	Context->Tools.FurnitureGenerator = PipelineFurnitureGenerator;
	Context->Tools.PickupSpotGenerator = PipelinePickupSpotGenerator;

	// Garbage Collection waehrend des Worker-Baus SPERREN.
	//
	// Der Worker verarbeitet die Stadt (~Zehntausende Gebaeude/Strassen) auf
	// dem Thread-Pool, waehrend der Game-Thread unten den Fortschrittsdialog
	// pumpt - und dabei Slate tickt, was eine Garbage Collection ausloesen
	// kann. Laeuft die GC (immer Game-Thread), gibt sie UObjekte samt der von
	// ihnen gehaltenen Container frei oder verschiebt sie, waehrend der Worker
	// sie noch benutzt: eine Use-after-free-Heap-Korruption, die an
	// WECHSELNDEN, voellig unbeteiligten Stellen einschlaegt (mal beim
	// TArray-Wachstum in FWiesbadenRoadClearance::Build, mal in
	// FPolygonUtils::TriangulatePolygon) - der klassische, nicht
	// reproduzierbare "Background Worker"-Absturz. Der Guard haelt die GC an,
	// bis der Bau fertig ist und die Ergebnisse auf dem Game-Thread angewandt
	// wurden; danach holt sie regulaer nach.
	FGCScopeGuard NoGCWhileBuilding;

	// Schwergewichtige Verarbeitung auf den Thread-Pool auslagern.
	Async(EAsyncExecution::ThreadPool,
		[Context]() { RunBuildPipeline(Context); });

	// Warten und Fortschritt anzeigen. Im Editor pumpt FScopedSlowTask die
	// Slate-Nachrichtenschleife, wodurch der Editor bedienbar bleibt und der
	// Abbrechen-Button funktioniert. Ohne dieses Pumpen wuerde ein schlichter
	// Busy-Wait den Editor trotz Worker weiterhin einfrieren.
#if WITH_EDITOR
	FScopedSlowTask SlowTask(1.0f, FText::FromString(TEXT("Baue Stadt Wiesbaden...")));
	SlowTask.MakeDialog(/*bInShowCancelButton=*/true);

	float LastProgress = 0.0f;
	while (!Context->bDone.load())
	{
		const float Progress = static_cast<float>(Context->ProgressPercent.load()) / 100.0f;
		SlowTask.EnterProgressFrame(Progress - LastProgress, BuildStageToText(Context->Stage.load()));
		LastProgress = Progress;

		BuildProgress = Progress;
		BuildStatus = BuildStageToText(Context->Stage.load()).ToString();

		if (SlowTask.ShouldCancel())
		{
			Context->bCancelRequested.store(true);
		}

		FPlatformProcess::Sleep(0.016f);
	}

	// Restarbeit auffuellen, falls der Worker vor dem ersten Schleifendurchlauf
	// fertig war - sonst meldet FScopedSlowTask "did not complete".
	if (LastProgress < 1.0f)
	{
		SlowTask.EnterProgressFrame(1.0f - LastProgress);
	}
#else
	while (!Context->bDone.load())
	{
		BuildProgress = static_cast<float>(Context->ProgressPercent.load()) / 100.0f;
		BuildStatus = BuildStageToText(Context->Stage.load()).ToString();
		FPlatformProcess::Sleep(0.01f);
	}
#endif

	// Worker ist fertig (bDone). Die Pipeline-Generatoren werden ab hier nicht
	// mehr gebraucht - der Game-Thread liest nur noch Context->Data. Einmal hier
	// freigeben deckt alle Ausgaenge ab (Erfolg, Benutzer-Abbruch, Fehler).
	ReleasePipelineObjects();

	// Ergebnisse auf dem Game-Thread anwenden: Mesh- und Landscape-Erzeugung
	// sind nicht thread-sicher und bleiben deshalb hier.
	LastParseResult = Context->Data.ParseResult;
	LastDemImportResult = Context->Data.DemImportResult;
	LastRoadReport = Context->Data.RoadReport;
	LastBuildingReport = Context->Data.BuildingReport;
	LastTerrainReport = Context->Data.TerrainReport;
	LastFurnitureReport = Context->Data.FurnitureReport;
	LastTerrainQuality = Context->Data.TerrainQuality;

	if (Context->bCancelled)
	{
		LastError = TEXT("Build vom Benutzer abgebrochen.");
		UE_LOG(LogWbCore, Warning, TEXT("BuildCity abgebrochen (Benutzer)."));
		bBuildInProgress = false;

		WriteBuildSummaryToCsv(&Context->Data, nullptr, nullptr, nullptr,
			FString::Printf(TEXT("abgebrochen: %s"), *LastError),
			FPlatformTime::Seconds() - StartTime);
		return;
	}

	if (!Context->Data.ErrorMessage.IsEmpty())
	{
		LastError = Context->Data.ErrorMessage;
		UE_LOG(LogWbCore, Error, TEXT("BuildCity fehlgeschlagen: %s"), *LastError);
		bBuildInProgress = false;

		WriteBuildSummaryToCsv(&Context->Data, nullptr, nullptr, nullptr,
			FString::Printf(TEXT("fehlgeschlagen: %s"), *LastError),
			FPlatformTime::Seconds() - StartTime);
		return;
	}

	RoadNetwork = MoveTemp(Context->Data.RoadNetwork);
	Buildings = MoveTemp(Context->Data.Buildings);
	FurnitureLayout = MoveTemp(Context->Data.FurnitureLayout);

	RebuildFurnitureInstances();

	// Baeume, Ufer- und Industrie-Objekte.
	//
	// Hier und nicht erst am CityActor: der gebackene Pfad ruft ApplyCityData
	// nie auf, weshalb von 1,53 Millionen erzeugten Baeumen kein einziger in
	// der Stadt stand. Beim Bauen erzeugt, werden die Instanzen mit der Map
	// gespeichert - genau wie die Strassenausstattung.
	RegionAssetLayout = MoveTemp(Context->Data.RegionAssetLayout);
	if (!bGenerateCityChunks)
	{
		// Nur im ungechunkten Pfad haengen sie am Builder. Mit Chunks bekommt
		// jede Zelle ihre eigenen - sonst stuenden alle Baeume doppelt in der
		// Stadt, und der teurere der beiden Bestaende waere weiter permanent
		// geladen.
		RebuildRegionAssetInstances();
	}

	if (bGenerateCityChunks)
	{
		// World-Partition-Pfad: Geometrie in Grid-Zellen aufteilen und je Zelle
		// einen eigenen Chunk-Actor spawnen. Die monolithischen Komponenten
		// bleiben leer - sonst waere die Geometrie doppelt im Level.
		const bool bHasRoads = Context->Input.bGenerateRoads && Context->Data.RoadReport.bSuccess;
		const bool bHasBuildings = Context->Input.bGenerateBuildings && Context->Data.BuildingReport.bSuccess;
		if (bHasRoads || bHasBuildings)
		{
			// WICHTIG: World Partition VOR dem Spawn aktivieren, damit die
			// Chunk-Actors in einer partitionierten Welt gespawnt und beim
			// SaveMap als WP-Zellen externalisiert werden (External-Actor-
			// Packages). Eine nachtraegliche Aktivierung (z. B. erst in
			// SaveCityAsMap) erfasst bereits gespawnte Actors nicht - die
			// Chunks landen dann ungestreamt in der .umap bzw. gar nicht auf
			// der Platte (Stadt beim Oeffnen unsichtbar).
			EnsureWorldPartition();
			SpawnCityChunks(Context->Data.RoadMesh, Context->Data.BuildingMesh);
		}
	}
	else
	{
		if (Context->Input.bGenerateRoads && Context->Data.RoadReport.bSuccess)
		{
			ApplyRoadMesh(Context->Data.RoadMesh);
		}

		if (Context->Input.bGenerateBuildings && Context->Data.BuildingReport.bSuccess)
		{
			ApplyBuildingMesh(Context->Data.BuildingMesh);
		}
	}

	if (Context->Input.bGenerateTerrain && Context->Data.TerrainReport.bSuccess)
	{
		if (bCreateLandscapeActor)
		{
			GeneratedLandscape = CreateLandscapeFromTile(Context->Data.TerrainTile);
			if (!GeneratedLandscape)
			{
				UE_LOG(LogWbCore, Warning, TEXT("Landscape konnte nicht erzeugt werden - Vorschau-Mesh als Fallback."));
				ApplyTerrainPreview(Context->Data.TerrainTile);
			}
		}
		else
		{
			ApplyTerrainPreview(Context->Data.TerrainTile);
		}
	}

	// Stadt-Geometrie liegt jetzt im Level - SaveCityAsMap kann sie als echte
	// Map speichern; das CitySubsystem erkennt daran eine gebackene Stadt und
	// baut zur Laufzeit nicht doppelt.
	bCityBaked = true;

	// Erst den Build-Status schliessen, damit der SaveCityAsMap-Guard
	// (bBuildInProgress) nicht greift.
	bBuildInProgress = false;

	// Optionaler Auto-Save: Speichert die Stadt direkt nach erfolgreichem Build
	// als Map (inkl. World-Partition-Aktivierung + Default-Map-Verdrahtung),
	// wenn der Nutzer bAutoSaveCityAsMap im Details-Panel aktiviert hat.
	if (bAutoSaveCityAsMap)
	{
		SaveCityAsMap();
	}

	// Kompakter Zusammenfassungs-Block je BuildCity-Lauf: Dauer (inkl. Auto-
	// Save), Umfang, Chunks und Auto-Save-Ergebnis auf einen Blick im
	// Output-Log. Die Detail-Logs waehrend des Laufs (z. B. "Stadt als Map
	// gespeichert", "World-Partition-Verifikation ok") bleiben unveraendert.
	const double TotalSeconds = FPlatformTime::Seconds() - StartTime;
	UE_LOG(LogWbCore, Log, TEXT("===== BuildCity-Zusammenfassung ====="));
	UE_LOG(LogWbCore, Log, TEXT("  Dauer:    %.1f s"), TotalSeconds);
	UE_LOG(LogWbCore, Log, TEXT("  Strassen: %d Segmente / %d Kreuzungen"),
		RoadNetwork.Segments.Num(), RoadNetwork.Intersections.Num());
	UE_LOG(LogWbCore, Log, TEXT("  Gebaeude: %d"), Buildings.Num());
	UE_LOG(LogWbCore, Log, TEXT("  Schilder: %d"), FurnitureLayout.Signs.Num());

	// Regionen je Typ (datenrein gezaehlt) - der Regionen-Pass laeuft in der
	// Pipeline immer, daher ist die Zeile immer sichtbar.
	{
		int32 Water = 0, Green = 0, Residential = 0, Commercial = 0, Industrial = 0;
		UWiesbadenRegionGenerator::GetRegionTypeCounts(
			Context->Data.Regions, Water, Green, Residential, Commercial, Industrial);
		UE_LOG(LogWbCore, Log,
			TEXT("  Regionen: %d (Wasser %d, Gruen %d, Wohnen %d, Gewerbe %d, Industrie %d)"),
			Context->Data.Regions.Num(), Water, Green, Residential, Commercial, Industrial);
	}
	if (bGenerateRegionAssets)
	{
		UE_LOG(LogWbCore, Log, TEXT("  Region-Assets: %d (Baeume %d, Ufer %d, Industrie %d)"),
			Context->Data.RegionAssetReport.AssetCount,
			Context->Data.RegionAssetReport.TreeCount,
			Context->Data.RegionAssetReport.WaterfrontCount,
			Context->Data.RegionAssetReport.IndustrialCount);
	}
	if (bGenerateTerrain)
	{
		UE_LOG(LogWbCore, Log, TEXT("  Terrain:  %dx%d (%s)"),
			LastTerrainReport.GridSize, LastTerrainReport.GridSize,
			bCreateLandscapeActor ? TEXT("Landscape") : TEXT("Vorschau"));

		// Terrain-Qualitaetskontrolle: Warnt, wenn das Tile deutlich groesser
		// als die OSM-Ausdehnung ist (Crop fehlt?) oder die Hoehenspanne
		// unplausibel gross/klein ist. Meldung steht auch im Details-Panel
		// (LastTerrainQuality).
		if (!LastTerrainQuality.WarningMessage.IsEmpty())
		{
			UE_LOG(LogWbCore, Warning, TEXT("  Terrain-Warnung: %s"), *LastTerrainQuality.WarningMessage);
		}
	}
	if (bGenerateCityChunks)
	{
		UE_LOG(LogWbCore, Log, TEXT("  Chunks:   %d (Zellgroesse %.0f m)"),
			CityChunks.Num(), CityChunkSizeMeters);
	}
	if (bAutoSaveCityAsMap)
	{
		// Fehlerfall als Error, damit der Misserfolg im Log heraussticht.
		if (bAutoSaveSucceeded)
		{
			UE_LOG(LogWbCore, Log, TEXT("  Auto-Save: Erfolgreich -> %s"), *MapAssetPath);
		}
		else
		{
			UE_LOG(LogWbCore, Error,
				TEXT("  Auto-Save: Fehlgeschlagen (%s) -> %s"), *LastError, *MapAssetPath);
		}
	}
	else
	{
		UE_LOG(LogWbCore, Log,
			TEXT("  Auto-Save: aus (Tipp: 'Save City as Map' klicken oder bAutoSaveCityAsMap aktivieren)"));
	}
	UE_LOG(LogWbCore, Log, TEXT("======================================"));

	// CSV-Historie: je BuildCity-Lauf eine Zeile (Erfolg wie Abbruch/Fehler),
	// damit Build-Zeiten ueber mehrere Laeufe vergleichbar sind.
	{
		const FString SuccessResult = bAutoSaveCityAsMap
			? (bAutoSaveSucceeded
				? TEXT("ok")
				: FString::Printf(TEXT("fehlgeschlagen: %s"), *LastError))
			: TEXT("ok (kein Auto-Save)");
		WriteBuildSummaryToCsv(&Context->Data, &RoadNetwork, &Buildings, &FurnitureLayout,
			SuccessResult, TotalSeconds);
	}
}

void AWiesbadenWorldBuilder::ClearGeneratedGeometry()
{
	ResetResults();
	UE_LOG(LogWbCore, Log, TEXT("Generierte Geometrie geloescht."));
}

void AWiesbadenWorldBuilder::ReloadTrafficSignCatalog()
{
	FString Error;
	if (FWiesbadenTrafficSignCatalog::ReloadFromJsonFile(
			FWiesbadenTrafficSignCatalog::GetDefaultCatalogPath(), Error))
	{
		UE_LOG(LogWbCore, Log, TEXT("Verkehrszeichen-Katalog neu geladen (Hot-Reload)."));
		return;
	}

	UE_LOG(LogWbCore, Warning,
		TEXT("Verkehrszeichen-Katalog-Reload fehlgeschlagen: %s"), *Error);
}

void AWiesbadenWorldBuilder::SaveCityAsMap()
{
#if WITH_EDITOR
	// Ergebnis-Feedback fuer das Details-Panel: false bis zum erfolgreichen
	// SaveMap, damit LastError/bAutoSaveSucceeded immer den letzten Versuch
	// widerspiegeln (auch bei fruehen Abbruechen).
	bAutoSaveSucceeded = false;

	if (bBuildInProgress)
	{
		LastError = TEXT("Build laeuft noch - Speichern ignoriert.");
		UE_LOG(LogWbCore, Warning, TEXT("SaveCityAsMap: %s"), *LastError);
		return;
	}

	if (!bCityBaked)
	{
		LastError = TEXT("Keine gebackene Stadt im Level - erst BuildCity ausfuehren.");
		UE_LOG(LogWbCore, Warning, TEXT("SaveCityAsMap: %s"), *LastError);
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		LastError = TEXT("Kein World verfuegbar.");
		UE_LOG(LogWbCore, Error, TEXT("SaveCityAsMap: %s"), *LastError);
		return;
	}

	FString AssetPath = MapAssetPath.TrimStartAndEnd();
	if (AssetPath.IsEmpty())
	{
		AssetPath = TEXT("/Game/Maps/WiesbadenCity");
	}

	// World Partition aktivieren (wenn Chunks erzeugt wurden): Die Chunk-Actors
	// werden anhand ihrer kleinen Bounds auf Streaming-Zellen verteilt, sodass
	// die Stadt beim Oeffnen der Map zellenweise geladen/entladen wird. Der
	// WorldBuilder selbst und die Landscape bleiben 'always loaded'
	// (bIsSpatiallyLoaded=false), damit die Backing-Erkennung
	// (HasBakedCityInLevel/FindBakedCityBuilder inkl. Traffic-Simulation) auch
	// bei entladenen Zellen funktioniert. Die eigentliche Aktivierung passiert
	// bereits in BuildCity VOR SpawnCityChunks (sonst werden die Chunks nicht
	// als WP-Zellen externalisiert); hier nur noch idempotent sicherstellen,
	// falls die Map bereits partitioniert gebacken wurde.
	if (bGenerateCityChunks)
	{
		EnsureWorldPartition();
	}

	// Die komplette gebaute Stadt (Procedural-Meshes, Landscape, Ausstattung)
	// liegt als Actor-Komponenten im aktuellen Level - SaveMap persistiert sie
	// in eine echte .umap, die ohne Pipeline-Neulauf geoeffnet werden kann.
	if (!UEditorLoadingAndSavingUtils::SaveMap(World, AssetPath))
	{
		LastError = FString::Printf(TEXT("Map-Speichern fehlgeschlagen: %s"), *AssetPath);
		UE_LOG(LogWbCore, Error, TEXT("SaveCityAsMap: %s"), *LastError);
		return;
	}

	// Als Default-Map verdrahten - NUR wenn ausdruecklich gewollt.
	//
	// Frueher geschah das bedingungslos. Ein PROBE-Bake - und die meisten sind
	// Proben - stellte damit still die gespielte Stadt um; gemerkt hat man es
	// erst an `git status Config/`, und zurueckgenommen wurde es jedes Mal von
	// Hand. Schlimmer noch bei einem Bake, der sich spaeter als untauglich
	// erwies (Alkis10 und Alkis11 meldeten FERTIG und waren im Spiel nur Gras):
	// der hatte die funktionierende Karte da schon verdraengt.
	const FString MapName = FPackageName::GetShortName(AssetPath);
	const FString MapRef = AssetPath + TEXT(".") + MapName;

	if (!bMakeNewMapDefault)
	{
		// LAUT sagen, was NICHT passiert ist. Eine stille Unterlassung waere
		// genauso schlecht wie die stille Umstellung: wer die neue Karte
		// spielen will, soll wissen, wie.
		UE_LOG(LogWbCore, Warning,
			TEXT("SaveCityAsMap: Karte %s gespeichert, aber NICHT als Default verdrahtet ")
			TEXT("(bMakeNewMapDefault = false). Die gespielte Karte bleibt unveraendert. ")
			TEXT("Zum Umstellen: bMakeNewMapDefault im Details-Panel setzen oder den Bake ")
			TEXT("mit WB_LIVE_SCHALTEN=1 laufen lassen."),
			*MapRef);
	}
	else
	{
	// WICHTIG (UE 5.8, im Editor verifiziert): GConfig->SetString + Flush(
	// GEngineIni) kann STILL nichts schreiben - Flush ueberspringt die Datei,
	// wenn FindBranch den Branch unter dem vollen Pfad nicht findet, und
	// meldet trotzdem Erfolg (SaveBranch-Early-Out, kein Fehlerlog). Die
	// DefaultEngine.ini wird deshalb direkt ueber FFileHelper mit dem
	// datenreinen Helfer geschrieben und danach gegen die Platte verifiziert;
	// GConfig bleibt zusaetzlich im Speicher aktuell (harmlos, falls ein
	// spaeterer Flush den Branch doch findet).
	const FString DefaultMapSection = TEXT("/Script/EngineSettings.GameMapsSettings");
	GConfig->SetString(*DefaultMapSection, TEXT("GameDefaultMap"), *MapRef, GEngineIni);
	GConfig->SetString(*DefaultMapSection, TEXT("EditorStartupMap"), *MapRef, GEngineIni);

	// WICHTIG: GEngineIni ist NUR der Ini-Name ("Engine"), kein Dateipfad - die
	// Config-Cache-API (SetString/Flush) loest ihn intern auf, FFileHelper aber
	// nicht (LoadFileToString("Engine") liest relativ zum CWD und scheitert im
	// Cmd-/Batch-Kontext; im GUI wurde so eine falsche Datei geschrieben und
	// die Verifikation las sie wieder zurueck). Fuer den direkten Dateizugriff
	// den kanonischen Projektpfad verwenden (dort liest/schreibt GConfig die
	// "Engine"-Ini: GetDestIniFilename -> ProjectConfigDir/DefaultEngine.ini).
	const FString EngineIniPath = FPaths::ProjectConfigDir() + TEXT("DefaultEngine.ini");

	FString IniText;
	if (FFileHelper::LoadFileToString(IniText, *EngineIniPath))
	{
		const FString NewIniText = ApplyDefaultMapToIniText(IniText, MapRef);
		// Ohne BOM schreiben (Datei hat keins; GConfig-Leser kaemen damit klar,
		// aber die Datei soll byte-kompatibel bleiben).
		if (!FFileHelper::SaveStringToFile(NewIniText, *EngineIniPath,
				FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			LastError = FString::Printf(TEXT("DefaultEngine.ini konnte nicht geschrieben werden: %s"), *EngineIniPath);
			UE_LOG(LogWbCore, Error, TEXT("SaveCityAsMap: %s"), *LastError);
			return;
		}
	}
	else
	{
		LastError = FString::Printf(TEXT("DefaultEngine.ini konnte nicht gelesen werden: %s"), *EngineIniPath);
		UE_LOG(LogWbCore, Error, TEXT("SaveCityAsMap: %s"), *LastError);
		return;
	}

	// Verifizieren, dass die Verdrahtung wirklich auf der Platte steht (sonst
	// wuerde Play die Stadt ohne Neugenerierung nicht laden - genau der Fehler,
	// den der GConfig-No-Op verursachte).
	{
		FString VerifyText;
		if (FFileHelper::LoadFileToString(VerifyText, *EngineIniPath))
		{
			if (!VerifyText.Contains(TEXT("GameDefaultMap=") + MapRef) ||
				!VerifyText.Contains(TEXT("EditorStartupMap=") + MapRef))
			{
				LastError = TEXT("Default-Map-Verdrahtung nicht auf der Platte verifiziert (Keys fehlen in DefaultEngine.ini).");
				UE_LOG(LogWbCore, Error, TEXT("SaveCityAsMap: %s"), *LastError);
				return;
			}
		}
		else
		{
			LastError = TEXT("Default-Map-Verdrahtung nicht verifizierbar (DefaultEngine.ini nicht lesbar nach dem Schreiben).");
			UE_LOG(LogWbCore, Error, TEXT("SaveCityAsMap: %s"), *LastError);
			return;
		}
	}

	UE_LOG(LogWbCore, Log,
		TEXT("SaveCityAsMap: %s ist jetzt die Default-Karte (bMakeNewMapDefault war gesetzt)."),
		*MapRef);
	}

	GConfig->Flush(false, GEngineIni);

	// World-Partition-Verifikation: Nur wenn Chunks (und damit World Partition)
	// erwartet wurden, muss die gespeicherte Map wirklich ein streamendes
	// WP-Level sein - sonst liegen die Chunk-Actors ungestreamt im Speicher und
	// das Chunking haette keinen Effekt. Beweis ist der External-Actor-
	// Platten-Check (nicht IsStreamingEnabled: das frisch erzeugte WP-Objekt
	// ist im selben Prozess nie initialisiert, daher waere die Verifikation
	// fuer frische WP-Maps immer fehlgeschlagen). Bei Fehler: LastError setzen
	// und bAutoSaveSucceeded bleibt false.
	if (bGenerateCityChunks)
	{
		// Die Chunk-Actors muessen in EIGENEN External-Actor-Packages liegen.
		// Der 08-2026-ALKIS-Rebuild zeigte die Luecke: HasExternalActorPackages
		// war true (1990 Packages auf der Platte), aber alle 1984 Chunks lagen
		// in EINEM 531-MB-Package - der Game-Lauf scheiterte mit 1984
		// "Failed import for WiesbadenCityChunk"-Fehlern und die Stadt war
		// unsichtbar, obwohl die Verifikation "ok" gemeldet hatte.
		TSet<FString> ChunkPackages;
		int32 ChunksWithoutPackage = 0;
		for (AWiesbadenCityChunk* Chunk : CityChunks)
		{
			if (!Chunk)
			{
				++ChunksWithoutPackage;
				continue;
			}
			if (UPackage* ChunkPackage = Chunk->GetPackage())
			{
				ChunkPackages.Add(ChunkPackage->GetName());
			}
			else
			{
				++ChunksWithoutPackage;
			}
		}
		const bool bChunksInSeparatePackages =
			ChunksWithoutPackage == 0 && ChunkPackages.Num() == CityChunks.Num();

		FString VerifyError;
		const bool bVerifyOk = VerifyWorldPartitionSave(
			World->IsPartitionedWorld(),
			FPackageName::DoesPackageExist(AssetPath),
			HasExternalActorPackages(AssetPath),
			bChunksInSeparatePackages,
			VerifyError);
		if (!bVerifyOk)
		{
			LastError = FString::Printf(TEXT("World-Partition-Verifikation fehlgeschlagen: %s"), *VerifyError);
			UE_LOG(LogWbCore, Warning, TEXT("SaveCityAsMap: %s"), *LastError);
			return;
		}

		UE_LOG(LogWbCore, Log,
			TEXT("World-Partition-Verifikation ok: %d Chunk-Actors in %d eigenen External-Actor-Packages."),
			CityChunks.Num(), ChunkPackages.Num());
	}

	LastError.Reset();
	bAutoSaveSucceeded = true;
	UE_LOG(LogWbCore, Log,
		TEXT("Stadt als Map gespeichert: %s (Default-Map verdrahtet - Play laedt die Stadt ohne Neugenerierung)."),
		*AssetPath);
#endif
}bool AWiesbadenWorldBuilder::VerifyWorldPartitionSave(
	bool bIsPartitioned, bool bMapExists, bool bExternalActors,
	bool bChunksInSeparatePackages, FString& OutError)
{
	if (!bIsPartitioned)
	{
		OutError = TEXT("bIsPartitioned=false: die Welt traegt kein UWorldPartition-Objekt (World Partition nicht aktiviert).");
		return false;
	}

	if (!bMapExists)
	{
		OutError = TEXT("Map-Datei nicht auf der Platte gefunden.");
		return false;
	}

	if (!bExternalActors)
	{
		OutError = TEXT("Keine External-Actor-Packages gefunden - die Chunk-Actors wurden nicht als World-Partition-Zellen externalisiert (Stadt waere beim Oeffnen unsichtbar).");
		return false;
	}

	if (!bChunksInSeparatePackages)
	{
		OutError = TEXT("Die Chunk-Actors liegen nicht in eigenen External-Actor-Packages - sie teilen sich ein Package (z. B. alle in einem Riesen-Package). Beim Laden der Map scheitern dann die Chunk-Imports und die Stadt ist unsichtbar, obwohl External-Actor-Packages existieren.");
		return false;
	}

	OutError.Reset();
	return true;

}

void AWiesbadenWorldBuilder::EnsureWorldPartition()
{
#if WITH_EDITOR
	UWorld* World = GetWorld();
	if (!World || !World->PersistentLevel)
	{
		return;
	}

	if (AWorldSettings* WorldSettings = World->GetWorldSettings())
	{
		// Vollstaendige WP-Konvertierung wie FWorldPartitionConverter::Convert -
		// der echte Weg der "Enable World Partition"-Checkbox. Nur
		// CreateOrRepairWorldPartition reicht NICHT: Ohne die weiteren Schritte
		// werden gespawnte Chunk-Actors beim SaveMap nicht als WP-Zellen
		// externalisiert (0 External-Actor-Packages, Stadt unsichtbar im Spiel).
		//
		// 1) Alle Actors (auch spaeter gespawnte Chunks/Landscape) auf externe
		//    Packages umstellen: bUseExternalActors=true + Reduced-Schema.
		if (!World->PersistentLevel->IsUsingExternalActors())
		{
			World->PersistentLevel->ConvertAllActorsToPackaging(true);
		}

		UWorldPartition* WorldPartition = UWorldPartition::CreateOrRepairWorldPartition(WorldSettings);
		if (WorldPartition)
		{
			// 2) Streaming explizit aktivieren (wie der Converter). Initialize
			//    setzt bStreamingWasEnabled selbst, sobald bEnableStreaming true ist.
			WorldPartition->bEnableStreaming = true;

			// 3) Initialize registriert ActorDescContainerInstance + EditorHash
			//    und hoert ueber OnObjectPreSave auf jede Actor-Speicherung -
			//    erst DADURCH schreibt SaveMap die Actors als External-Actor-
			//    Packages (pro Chunk ein Package unter __ExternalActors__).
			if (!WorldPartition->IsInitialized() && World->IsInitialized())
			{
				WorldPartition->Initialize(World, FTransform::Identity);
			}

			UWorldPartition::WorldPartitionChangedEvent.Broadcast(World);

			SetIsSpatiallyLoaded(false);
			if (GeneratedLandscape)
			{
				GeneratedLandscape->SetIsSpatiallyLoaded(false);
			}
			UE_LOG(LogWbCore, Log,
				TEXT("World Partition aktiviert (idempotent): %d Chunk-Actors werden auf Streaming-Zellen verteilt."),
				CityChunks.Num());
		}
	}
#endif
}

bool AWiesbadenWorldBuilder::HasExternalActorPackages(const FString& MapAssetPath)
{
	// /Game/Maps/WiesbadenCity -> Content/__ExternalActors__/Game/Maps/WiesbadenCity
	FString RelativePath = MapAssetPath;
	if (RelativePath.StartsWith(TEXT("/Game/")))
	{
		RelativePath.RightChopInline(6); // "/Game/" entfernen
	}
	const FString ExternalActorsDir = FPaths::ProjectContentDir() + TEXT("__ExternalActors__/") + RelativePath;

	TArray<FString> FoundFiles;
	IFileManager::Get().FindFilesRecursive(FoundFiles, *ExternalActorsDir, TEXT("*.uasset"), true, false);
	return FoundFiles.Num() > 0;
}

void AWiesbadenWorldBuilder::WriteBuildSummaryToCsv(
	const FWiesbadenCityData* CityData,
	const FRoadNetwork* MovedRoadNetwork, const TArray<FGeneratedBuilding>* MovedBuildings,
	const FRoadFurnitureLayout* MovedFurniture,
	const FString& Result, double DurationSeconds)
{
	// Gemeinsame Factory (datenrein, identisch zum Runtime-Pfad): Regionen/
	// Assets/Terrain kommen aus dem CityData, die gemovten Ergebnis-Member
	// (Erfolgspfad) als Overrides. TerrainMode nur im Erfolgspfad mit Terrain.
	const FString TerrainMode = (bGenerateTerrain && MovedRoadNetwork)
		? (bCreateLandscapeActor ? TEXT("Landscape") : TEXT("Vorschau"))
		: TEXT("");
	FWiesbadenBuildSummary Summary = BuildSummaryFromCityData(
		CityData ? *CityData : FWiesbadenCityData(),
		TEXT("Editor"),
		Result,
		DurationSeconds,
		FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M:%S")),
		MovedRoadNetwork,
		MovedBuildings,
		MovedFurniture,
		TerrainMode);

	// Editor-spezifische Felder (nicht im CityData): Chunks + Map-Pfad.
	if (bGenerateCityChunks)
	{
		Summary.Chunks = CityChunks.Num();
		Summary.ChunkSizeMeters = CityChunkSizeMeters;
	}
	if (bAutoSaveCityAsMap)
	{
		Summary.MapPath = MapAssetPath;
	}

	// Details-Panel-Feedback: letzter Build ohne CSV-/Datei-Lookup sichtbar.
	// Wird VOR dem (fehlschlagbaren) CSV-Anhang gesetzt, damit das Feedback
	// auch bei Schreibfehlern aktuell bleibt. Zeitpunkt/Dauer/Ergebnis liegen
	// in der gemeinsamen FLastBuildInfo (dieselbe Quelle wie im Laufzeit-Pfad).
	LastBuild = FLastBuildInfo::FromBuildSummary(Summary);
	LastBuildSummary = LastBuild.GetSummary();

	FString CsvError;
	if (!AppendBuildSummaryToCsv(Summary, CsvError))
	{
		UE_LOG(LogWbCore, Warning,
			TEXT("BuildCity-Zusammenfassung konnte nicht in CSV geschrieben werden: %s"), *CsvError);
	}
}

void AWiesbadenWorldBuilder::ResetResults()
{
	// Nach einem Clear/Loeschvorgang ist die Stadt nicht mehr gebacken und das
	// Save-Ergebnis-Feedback veraltet.
	bCityBaked = false;
	bAutoSaveSucceeded = false;

	LastError.Reset();
	LastParseResult = FOSMParseResult();
	LastDemImportResult = FHeightmapImportResult();
	LastRoadReport = FRoadGenerationReport();
	LastBuildingReport = FBuildingGenerationReport();
	LastTerrainReport = FTerrainGenerationReport();
	LastTerrainQuality = FTerrainQualityReport();
	LastFurnitureReport = FRoadFurnitureReport();
	RoadNetwork.Reset();
	Buildings.Reset();
	FurnitureLayout.Reset();
	RegionAssetLayout.Reset();

	if (GeneratedLandscape)
	{
		if (UWorld* World = GetWorld())
		{
			World->DestroyActor(GeneratedLandscape);
		}
		GeneratedLandscape = nullptr;
	}

	if (RoadMeshComponent) { RoadMeshComponent->ClearAllMeshSections(); }
	if (BuildingMeshComponent) { BuildingMeshComponent->ClearAllMeshSections(); }
	if (TerrainMeshComponent) { TerrainMeshComponent->ClearAllMeshSections(); }
	if (FurnitureSpawner) { FurnitureSpawner->ClearFurniture(); }

	DestroyCityChunks();
}

void AWiesbadenWorldBuilder::ApplyRoadMesh(const FRoadMeshData& MeshData)
{
	if (!RoadMeshComponent)
	{
		return;
	}

	RoadMeshComponent->ClearAllMeshSections();

	int32 SectionIndex = 0;
	for (const FRoadMeshSection& Section : MeshData.Sections)
	{
		if (Section.IsEmpty())
		{
			continue;
		}

		RoadMeshComponent->CreateMeshSection(
			SectionIndex,
			Section.Vertices,
			Section.Triangles,
			Section.Normals,
			Section.UVs,
			Section.VertexColors,
			Section.Tangents,
			bCreateCollision);

		if (UMaterialInterface* Material = ResolveRoadMaterial(Section.Channel, Section.Surface))
		{
			RoadMeshComponent->SetMaterial(SectionIndex, Material);
		}

		++SectionIndex;
	}

	RoadMeshComponent->SetCollisionEnabled(
		bCreateCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
}

void AWiesbadenWorldBuilder::SpawnCityChunks(const FRoadMeshData& RoadMesh, const FBuildingMeshData& BuildingMesh)
{
	DestroyCityChunks();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Geometrie nach Dreieck-Schwerpunkt auf das Grid aufteilen. Die Zellgroesse
	// wird in cm gerechnet (Unreal-Einheit), daher * 100.
	TMap<FIntPoint, FCityChunkMesh> Chunks;
	FWiesbadenCityChunking::BuildChunks(
		RoadMesh, BuildingMesh, RegionAssetLayout, CityChunkSizeMeters * 100.0, Chunks);

	if (Chunks.Num() == 0)
	{
		UE_LOG(LogWbCore, Warning, TEXT("SpawnCityChunks: keine Zellen erzeugt - Geometrie leer?"));
		return;
	}

	// Deterministische Reihenfolge (X, dann Y) fuer reproduzierbare Levels.
	Chunks.KeySort([](const FIntPoint& A, const FIntPoint& B)
		{ return A.X != B.X ? A.X < B.X : A.Y < B.Y; });

	CityChunks.Reserve(Chunks.Num());
	int32 ChunkedRegionAssets = 0;
	for (const auto& [Cell, ChunkMesh] : Chunks)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AWiesbadenCityChunk* Chunk = World->SpawnActor<AWiesbadenCityChunk>(
			AWiesbadenCityChunk::StaticClass(), FTransform::Identity, Params);
		if (!Chunk)
		{
			UE_LOG(LogWbCore, Warning, TEXT("SpawnCityChunks: Chunk-Actor fuer Zelle (%d,%d) konnte nicht gespawnt werden."),
				Cell.X, Cell.Y);
			continue;
		}

		// Beschriftung nur im Editor - im Spiel gibt es SetActorLabel nicht.
		//
		// Sie ist trotzdem wichtig: DistributeRegionAssetsToChunks liest die
		// Zellkoordinaten aus dem Namen zurueck. Gebaut wird ohnehin nur im
		// Editor, im Spiel wird die fertige Karte geladen.
#if WITH_EDITOR
		Chunk->SetActorLabel(FString::Printf(TEXT("CityChunk_%d_%d"), Cell.X, Cell.Y));
#endif
		Chunk->ApplyChunk(ChunkMesh, bCreateRoadCollision, bCreateCollision);

		// Chunk als vorgekochte StaticMeshes ablegen (Render + Kollision serialisiert):
		// beim Stream-in wird nur geladen, kein ProcMesh-Render-Proxy neu aufgebaut und
		// keine Trimesh-Kollision gekocht - genau die 736-848-ms-ProcessLoadedPackages-
		// Aussetzer beim Fahren. Nur im Editor/Bake-Pfad wirksam; danach zielen die
		// Material-Setter auf die StaticMesh-Slots.
		Chunk->BakeToStaticMeshes(Cell.X, Cell.Y, bCreateRoadCollision, bCreateCollision);

		// Materialien: Die Resolve-Pfade haengen an den WorldBuilder-Material-Maps
		// (AddressFacadeMaterials/PromptFacadeMaterials ueber FacadeOverrideKey).
		int32 SectionIndex = 0;
		for (const FRoadMeshSection& Section : ChunkMesh.RoadSections)
		{
			if (Section.IsEmpty())
			{
				continue;
			}
			if (UMaterialInterface* Material = ResolveRoadMaterial(Section.Channel, Section.Surface))
			{
				Chunk->SetRoadSectionMaterial(SectionIndex, Material);
			}
			++SectionIndex;
		}

		SectionIndex = 0;
		for (const FBuildingMeshSection& Section : ChunkMesh.BuildingSections)
		{
			if (Section.IsEmpty())
			{
				continue;
			}
			if (UMaterialInterface* Material = ResolveBuildingMaterial(Section.Channel, Section.MaterialVariant, Section.FacadeOverrideKey))
			{
				Chunk->SetBuildingSectionMaterial(SectionIndex, Material);
			}
			++SectionIndex;
		}

		CityChunks.Add(Chunk);
		ChunkedRegionAssets += ChunkMesh.RegionAssets.Num();
	}

		// FRUEHERER FEHLER (behoben 2026-09-16): Hier wurden per
	// UnloadBakedChunkMeshes ALLE Chunk-StaticMesh-Verweise auf nullptr gesetzt,
	// um Commit-Speicher zu sparen - aber VOR dem Karten-Save. Die Chunk-Actors
	// wurden damit OHNE Mesh serialisiert; die geladene Karte zeigte 444/444
	// leere Chunks (keine Strassen/Gebaeude), Chunk am Ursprung mit km-Bounds.
	// Die Annahme "wird beim Stream-in neu verdrahtet" war falsch - es gibt
	// keinen solchen Mechanismus. Die Verweise MUESSEN bis zum Save erhalten
	// bleiben. Der Peak (~85 GiB Commit) passt in das 127-GiB-Limit (vergroesserte
	// Auslagerungsdatei); das Nullen sparte ohnehin keinen Speicher.
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

	UE_LOG(LogWbCore, Log,
		TEXT("Stadt in %d World-Partition-Zellen aufgeteilt (%.0f m Zellen), ")
		TEXT("darin %d Regionsobjekte (Baeume/Ufer/Industrie)."),
		CityChunks.Num(), CityChunkSizeMeters, ChunkedRegionAssets);

	// Die Zahl MUSS mit dem Layout uebereinstimmen. Weicht sie ab, sind
	// Objekte beim Aufteilen verlorengegangen - und das faellt im Spiel nur
	// als "hier fehlen Baeume" auf, also gar nicht.
	if (ChunkedRegionAssets != RegionAssetLayout.Assets.Num())
	{
		UE_LOG(LogWbCore, Warning,
			TEXT("Regionsobjekte: %d in Zellen, aber %d im Layout - %d fehlen."),
			ChunkedRegionAssets, RegionAssetLayout.Assets.Num(),
			RegionAssetLayout.Assets.Num() - ChunkedRegionAssets);
	}
}

void AWiesbadenWorldBuilder::DestroyCityChunks()
{
	if (UWorld* World = GetWorld())
	{
		// Array-Pass: die vom WorldBuilder registrierten Chunks.
		for (AWiesbadenCityChunk* Chunk : CityChunks)
		{
			if (Chunk)
			{
				World->DestroyActor(Chunk);
			}
		}
		// Level-Pass: auch nicht registrierte Chunk-Actors entfernen. Nach einem
		// Rebuild auf einer gebackenen Map haengen sonst die alten, serialisierten
		// Chunks aus dem vorherigen Lauf weiter im Level (Doppel-Geometrie).
		for (TActorIterator<AWiesbadenCityChunk> It(World); It; ++It)
		{
			World->DestroyActor(*It);
		}
	}
	CityChunks.Reset();
}

void AWiesbadenWorldBuilder::ApplyBuildingMesh(const FBuildingMeshData& MeshData)
{
	if (!BuildingMeshComponent)
	{
		return;
	}

	BuildingMeshComponent->ClearAllMeshSections();

	int32 SectionIndex = 0;
	for (const FBuildingMeshSection& Section : MeshData.Sections)
	{
		if (Section.IsEmpty())
		{
			continue;
		}

		BuildingMeshComponent->CreateMeshSection(
			SectionIndex,
			Section.Vertices,
			Section.Triangles,
			Section.Normals,
			Section.UVs,
			Section.VertexColors,
			Section.Tangents,
			bCreateCollision);

		if (UMaterialInterface* Material = ResolveBuildingMaterial(Section.Channel, Section.MaterialVariant, Section.FacadeOverrideKey))
		{
			BuildingMeshComponent->SetMaterial(SectionIndex, Material);
		}

		++SectionIndex;
	}

	BuildingMeshComponent->SetCollisionEnabled(
		bCreateCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
}

void AWiesbadenWorldBuilder::ApplyTerrainPreview(const FTerrainTile& Tile)
{
	if (!TerrainMeshComponent || !Tile.IsValid() || TerrainPreviewGridSize < 2)
	{
		return;
	}

	const int32 N = FMath::Clamp(TerrainPreviewGridSize, 2, 2049);
	const double WorldSizeCm = Tile.CellSizeCm * static_cast<double>(Tile.GridSize - 1);
	const double StepCm = WorldSizeCm / static_cast<double>(N - 1);

	// Hoehenraster der Vorschau zuerst aufbauen - die Normalenberechnung liest
	// die Nachbarhoehen mehrfach, ein Direkt-Sampling je Nachbar waere 4x teurer.
	TArray<float> Heights;
	Heights.SetNumUninitialized(N * N);

	TArray<FVector> Vertices;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<int32> Triangles;

	Vertices.Reserve(N * N);
	Normals.Reserve(N * N);
	UVs.Reserve(N * N);
	Triangles.Reserve((N - 1) * (N - 1) * 6);

	for (int32 Y = 0; Y < N; ++Y)
	{
		const double WorldY = Tile.WorldMinXY.Y + static_cast<double>(Y) * StepCm;
		for (int32 X = 0; X < N; ++X)
		{
			const double WorldX = Tile.WorldMinXY.X + static_cast<double>(X) * StepCm;
			const float Height = Tile.SampleHeightBilinearCm(FVector2D(WorldX, WorldY));
			Heights[Y * N + X] = Height;
			Vertices.Add(FVector(WorldX, WorldY, Height));
			UVs.Add(FVector2D(
				static_cast<float>(X) / static_cast<float>(N - 1),
				static_cast<float>(Y) / static_cast<float>(N - 1)));
		}
	}

	// Normalen ueber zentrale Differenzen: normal ~= (-dZ/dx, -dZ/dy, 1).
	for (int32 Y = 0; Y < N; ++Y)
	{
		for (int32 X = 0; X < N; ++X)
		{
			const float Left = Heights[Y * N + FMath::Max(X - 1, 0)];
			const float Right = Heights[Y * N + FMath::Min(X + 1, N - 1)];
			const float Down = Heights[FMath::Max(Y - 1, 0) * N + X];
			const float Up = Heights[FMath::Min(Y + 1, N - 1) * N + X];

			const FVector Normal = FVector(
				(Left - Right) / (2.0 * StepCm),
				(Down - Up) / (2.0 * StepCm),
				1.0).GetSafeNormal();

			Normals.Add(Normal);
		}
	}

	// Zwei Dreiecke je Quad, Frontseite nach oben (+Z).
	for (int32 Y = 0; Y < N - 1; ++Y)
	{
		for (int32 X = 0; X < N - 1; ++X)
		{
			const int32 V00 = Y * N + X;
			const int32 V10 = Y * N + (X + 1);
			const int32 V01 = (Y + 1) * N + X;
			const int32 V11 = (Y + 1) * N + (X + 1);

			Triangles.Add(V00);
			Triangles.Add(V10);
			Triangles.Add(V11);

			Triangles.Add(V00);
			Triangles.Add(V11);
			Triangles.Add(V01);
		}
	}

	TerrainMeshComponent->ClearAllMeshSections();
	TerrainMeshComponent->CreateMeshSection(
		0,
		Vertices,
		Triangles,
		Normals,
		UVs,
		TArray<FColor>(),
		TArray<FProcMeshTangent>(),
		bCreateCollision);

	if (TerrainMaterial)
	{
		TerrainMeshComponent->SetMaterial(0, TerrainMaterial);
	}

	TerrainMeshComponent->SetCollisionEnabled(
		bCreateCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
}

void AWiesbadenWorldBuilder::BuildLandscapeImportMaps(
	TArray<uint16>&& Heightmap,
	TMap<FGuid, TArray<uint16>>& OutHeightData,
	TMap<FGuid, TArray<FLandscapeImportLayerInfo>>& OutMaterialLayerInfos)
{
	// Die Engine sucht die Import-Daten ausschliesslich unter dem Default-Guid
	// (ALandscapeProxy::Import: FindChecked(FGuid()) in LandscapeEdit.cpp) -
	// ein FGuid::NewGuid() wuerde die Groessenpruefung bestehen und danach an
	// FindChecked scheitern (Editor-Absturz, kein abfangbarer Fehler).
	const FGuid ImportKey = FGuid();

	// Reset: CreateLandscapeFromTile kann je Kachel mehrfach laufen; Restdaten
	// aus einem vorherigen Aufruf wuerden die Groessenpruefung verletzen.
	OutHeightData.Reset();
	OutMaterialLayerInfos.Reset();

	OutHeightData.Add(ImportKey, MoveTemp(Heightmap));

	// Keine Gewichtsmaps: der Eintrag existiert, ist aber leer. Beides ist
	// erforderlich - ein fehlender Eintrag stuerzt ab (Groessenpruefung),
	// ein gefuellter wuerde Layer-Infos verlangen, die es noch nicht gibt.
	OutMaterialLayerInfos.Add(ImportKey, TArray<FLandscapeImportLayerInfo>());
}

ALandscape* AWiesbadenWorldBuilder::CreateLandscapeFromTile(const FTerrainTile& Tile)
{
#if WITH_EDITOR
	if (!Tile.IsValid())
	{
		UE_LOG(LogWbCore, Warning, TEXT("CreateLandscapeFromTile: ungueltiges TerrainTile."));
		return nullptr;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	const int32 SubsectionSizeQuads = FMath::Clamp(LandscapeSubsectionSizeQuads, 1, 255);
	const int32 NumSubsections = FMath::Clamp(LandscapeNumSubsections, 1, 4);
	const int32 ComponentSizeQuads = SubsectionSizeQuads * NumSubsections;

	// Quads je Seite auf ein Vielfaches der Component-Groesse aufrunden, damit
	// die Import-API die Heightmap verlustfrei in Components zerlegen kann.
	// Ueberstehende Zellen sampeln den Kartenrand (Clamping im Tile).
	const int32 Quads = Tile.GridSize - 1;
	const int32 ComponentCountPerSide = FMath::Max(1, FMath::DivideAndRoundUp(Quads, ComponentSizeQuads));
	const int32 SnappedQuads = ComponentCountPerSide * ComponentSizeQuads;
	const int32 Grid = SnappedQuads + 1;

	// UE interpretiert die uint16-Heightmap als (value - 32768) / 128 * ZScale
	// in Unreal Units. Unsere Hoehen liegen bereits in cm relativ zur
	// Bezugshoehe vor; die Codierung (FTerrainTile::EncodeLandscapeHeightCm,
	// datenrein getestet) bildet HeightCm 1:1 auf worldZ ab.
	//
	// Die Skalierung wird an die TATSAECHLICHEN Hoehen angepasst.
	//
	// Aus gutem Grund: 16 Bit fassen nur 65.536 Stufen, und die Codierung
	// klemmt still, was nicht hineinpasst. Bei ZScale 100 reicht der
	// darstellbare Bereich von -256 m bis +255,99 m um die Bezugshoehe -
	// bei 117 m Bezug also bis 373 m ueber Null. Der Taunuskamm liegt
	// darueber: die Hohe Wurzel misst 618 m. Alles daruber wurde zu einer
	// waagerechten Platte bei exakt 255,99 m.
	//
	// Sichtbar war das als schwebende Strassen: die Fahrbahn nimmt ihre
	// Hoehe direkt aus dem Hoehenmodell und lag daher richtig, waehrend das
	// Gelaende unter ihr abgeschnitten war. Die Messung ueber alle 22.227
	// Kreuzungen hat 1.612 Faelle ueber 10 m gefunden - und an jedem
	// einzelnen meldete das Gelaende denselben Wert 25.599 cm.
	//
	// Aufloesungsverlust ist dabei kein Thema: ZScale 300 stuft in 2,3 cm,
	// das Quellmaterial (SRTM) liefert ganze Meter.
	double ZScale = FMath::Max(1.0, LandscapeZScale);
	{
		// Groesster Betrag, der codiert werden muss - der Nullpunkt der
		// Codierung liegt in der Mitte, also zaehlt der Abstand nach oben
		// und nach unten gleichermassen.
		double PeakCm = 0.0;
		for (const float HeightCm : Tile.HeightsCm)
		{
			PeakCm = FMath::Max(PeakCm, static_cast<double>(FMath::Abs(HeightCm)));
		}

		// value = 32768 + HeightCm * 128 / ZScale muss in [0, 65535] bleiben,
		// also ZScale >= PeakCm * 128 / 32767. Fuenf Prozent Zuschlag, damit
		// die Rundung nicht doch noch an den Anschlag stoesst.
		const double RequiredZScale = PeakCm * 128.0 / 32767.0 * 1.05;
		if (RequiredZScale > ZScale)
		{
			UE_LOG(LogWbCore, Warning,
				TEXT("Landscape: ZScale %.0f reicht fuer die Hoehen nicht aus ")
				TEXT("(groesster Betrag %.0f m, darstellbar waeren %.0f m). ")
				TEXT("Wird auf %.0f angehoben - sonst wuerde das Gelaende oben ")
				TEXT("flach abgeschnitten."),
				ZScale, PeakCm / 100.0, ZScale * 32767.0 / 128.0 / 100.0,
				RequiredZScale);
			ZScale = RequiredZScale;
		}
	}

	// Heightmap resampeln: Weltposition je Vertex -> bilinear aus dem Tile.
	TArray<uint16> Heightmap;
	Heightmap.SetNumUninitialized(Grid * Grid);

	for (int32 Y = 0; Y < Grid; ++Y)
	{
		const double WorldY = Tile.WorldMinXY.Y + static_cast<double>(Y) * Tile.CellSizeCm;
		for (int32 X = 0; X < Grid; ++X)
		{
			const double WorldX = Tile.WorldMinXY.X + static_cast<double>(X) * Tile.CellSizeCm;
			const float HeightCm = Tile.SampleHeightBilinearCm(FVector2D(WorldX, WorldY));
			Heightmap[Y * Grid + X] = FTerrainTile::EncodeLandscapeHeightCm(HeightCm, ZScale);
		}
	}

	// Landscape platzieren: Pivot auf der Nordwest-Ecke des Tiles, Z = 0
	// entspricht der gemeinsamen Bezugshoehe (identisch zu Strassen/Gebaeuden).
	ALandscape* Landscape = World->SpawnActor<ALandscape>();
	if (!Landscape)
	{
		UE_LOG(LogWbCore, Error, TEXT("CreateLandscapeFromTile: SpawnActor fehlgeschlagen."));
		return nullptr;
	}

	Landscape->SetActorLocation(FVector(Tile.WorldMinXY.X, Tile.WorldMinXY.Y, 0.0));
	Landscape->SetActorScale3D(FVector(Tile.CellSizeCm, Tile.CellSizeCm, ZScale));
	Landscape->LandscapeMaterial = TerrainMaterial;

	// Import-Maps bauen (datenrein, FGuid()-Key fuer FindChecked in
	// ALandscapeProxy::Import): Heightmap + leerer Layer-Infos-Eintrag.
	TMap<FGuid, TArray<uint16>> HeightData;
	TMap<FGuid, TArray<FLandscapeImportLayerInfo>> MaterialLayerInfos;
	BuildLandscapeImportMaps(MoveTemp(Heightmap), HeightData, MaterialLayerInfos);

	Landscape->Import(
		FGuid::NewGuid(),
		0, 0,
		SnappedQuads, SnappedQuads,
		NumSubsections,
		SubsectionSizeQuads,
		HeightData,
		nullptr,
		MaterialLayerInfos,
		ELandscapeImportAlphamapType::Additive,
		TArrayView<const FLandscapeLayer>());

	Landscape->RegisterAllComponents();

#if WITH_EDITOR
	Landscape->PostEditChange();
#endif

	UE_LOG(LogWbCore, Log,
		TEXT("Landscape erzeugt: %dx%d Vertices, %d Components je Seite, %.1f cm/Quad, ZScale %.0f."),
		Grid, Grid, ComponentCountPerSide, Tile.CellSizeCm, ZScale);

	return Landscape;
#else
	UE_LOG(LogWbCore, Warning, TEXT("Landscape-Erzeugung ist nur im Editor verfuegbar (Fruehstuecksfall: Vorschau-Mesh)."));
	return nullptr;
#endif
}

void AWiesbadenWorldBuilder::EnsureDefaultMaterials()
{
	// Kanonische Pfade der Stadt-Materialien. Es werden ausschliesslich leere
	// Slots gefuellt - im Editor von Hand gesetzte Sondermaterialien bleiben
	// unangetastet.
	struct FDefaultMaterial
	{
		UMaterialInterface** Slot;
		const TCHAR* Path;
		const TCHAR* Label;
	};

	const FDefaultMaterial Defaults[] = {
		{ &RoadMaterial,         TEXT("/Game/Materials/City/M_WbRoad.M_WbRoad"),                 TEXT("Fahrbahn") },
		{ &SidewalkMaterial,     TEXT("/Game/Materials/City/M_WbSidewalk.M_WbSidewalk"),         TEXT("Gehweg") },
		{ &KerbMaterial,         TEXT("/Game/Materials/City/M_WbKerb.M_WbKerb"),                 TEXT("Bordstein") },
		{ &BuildingWallMaterial, TEXT("/Game/Materials/City/M_WbBuildingWall.M_WbBuildingWall"), TEXT("Fassade") },
		{ &BuildingRoofMaterial, TEXT("/Game/Materials/City/M_WbBuildingRoof.M_WbBuildingRoof"), TEXT("Dach") },
		{ &TerrainMaterial,      TEXT("/Game/Materials/City/M_WbTerrain.M_WbTerrain"),           TEXT("Gelaende") },
		{ &LaneMarkingMaterial, TEXT("/Game/Materials/City/M_WbLaneMarking.M_WbLaneMarking"),     TEXT("Markierung") },
		{ &CyclewayMaterial,    TEXT("/Game/Materials/City/M_WbCycleway.M_WbCycleway"),           TEXT("Radweg") },
		{ &UnpavedMaterial,     TEXT("/Game/Materials/City/M_WbUnpaved.M_WbUnpaved"),             TEXT("Unbefestigt") },
		{ &PavedStoneMaterial,  TEXT("/Game/Materials/City/M_WbPavedStone.M_WbPavedStone"),       TEXT("Pflaster") },
		{ &SignMaterial,        TEXT("/Game/Materials/City/M_WbSign.M_WbSign"),                   TEXT("Schild") },
		{ &PoleMaterial,        TEXT("/Game/Materials/City/M_WbPole.M_WbPole"),                   TEXT("Pfosten") },
		{ &DelineatorMaterial,  TEXT("/Game/Materials/City/M_WbDelineator.M_WbDelineator"),       TEXT("Leitpfosten") },
		{ &ReflectorMaterial,   TEXT("/Game/Materials/City/M_WbReflector.M_WbReflector"),         TEXT("Reflektor") },
		{ &MarkingMaterial,     TEXT("/Game/Materials/City/M_WbLaneMarking.M_WbLaneMarking"),     TEXT("Markierungs-Instanz") },
	};

	int32 Filled = 0;
	int32 Missing = 0;

	for (const FDefaultMaterial& Entry : Defaults)
	{
		if (*Entry.Slot != nullptr)
		{
			continue;
		}

		if (UMaterialInterface* Loaded = LoadObject<UMaterialInterface>(nullptr, Entry.Path))
		{
			*Entry.Slot = Loaded;
			++Filled;
		}
		else
		{
			++Missing;
			UE_LOG(LogWbCore, Warning,
				TEXT("EnsureDefaultMaterials: Material '%s' nicht ladbar (%s) - dieser Kanal ")
				TEXT("rendert mit dem Default-Material (Schachbrett)."),
				Entry.Label, Entry.Path);
		}
	}

	// Fassadenvarianten - Index entspricht FBuildingMeshSection::MaterialVariant.
	static const TCHAR* const VariantPaths[] = {
		TEXT("/Game/Materials/City/M_WbFacade_Putz.M_WbFacade_Putz"),
		TEXT("/Game/Materials/City/M_WbFacade_Backstein.M_WbFacade_Backstein"),
		TEXT("/Game/Materials/City/M_WbFacade_Sandstein.M_WbFacade_Sandstein"),
		TEXT("/Game/Materials/City/M_WbFacade_Glas.M_WbFacade_Glas"),
		TEXT("/Game/Materials/City/M_WbFacade_Beton.M_WbFacade_Beton"),
		TEXT("/Game/Materials/City/M_WbFacade_Fachwerk.M_WbFacade_Fachwerk"),
	};

	constexpr int32 VariantCount = UE_ARRAY_COUNT(VariantPaths);
	if (FacadeVariantMaterials.Num() < VariantCount)
	{
		// Nicht schrumpfen: im Editor ergaenzte Zusatzvarianten bleiben erhalten.
		FacadeVariantMaterials.SetNumZeroed(VariantCount);
	}

	int32 VariantsFilled = 0;
	for (int32 Index = 0; Index < VariantCount; ++Index)
	{
		if (FacadeVariantMaterials[Index] != nullptr)
		{
			continue;
		}

		if (UMaterialInterface* Loaded = LoadObject<UMaterialInterface>(nullptr, VariantPaths[Index]))
		{
			FacadeVariantMaterials[Index] = Loaded;
			++VariantsFilled;
		}
	}

	UE_LOG(LogWbCore, Log,
		TEXT("EnsureDefaultMaterials: %d Slot(s) gefuellt, %d fehlend, %d/%d Fassadenvarianten."),
		Filled, Missing, VariantsFilled, VariantCount);
}


/**
 * Markiert einen Actor als nicht raeumlich geladen - sofern er das zulaesst.
 *
 * Volumes sind Brushes und verbieten die Aenderung; ein ungeprueftes
 * SetIsSpatiallyLoaded loest dort eine Assertion aus und reisst den gesamten
 * Stadt-Build mit (genau so abgestuerzt an APostProcessVolume).
 */
static void MarkAlwaysLoaded(AActor* Actor)
{
	// Nur im Editor. SetIsSpatiallyLoaded und die zugehoerige Pruefung sind
	// Editor-API - im Spiel steht die Einstellung bereits in der gebackenen
	// Karte, es gibt also nichts umzuschalten.
#if WITH_EDITOR
	if (Actor && Actor->CanChangeIsSpatiallyLoadedFlag())
	{
		Actor->SetIsSpatiallyLoaded(false);
	}
#endif
}

void AWiesbadenWorldBuilder::EnsureDefaultRegionAssets()
{
	if (!RegionAssetSpawner)
	{
		return;
	}

	// Ein echtes Baum-Mesh braucht ein Asset, das das Projekt nicht mitbringt;
	// die Pfadliste der Platzhalter steht in der Komponente, weil sie seit der
	// Verteilung auf die Chunks zwei Aufrufer hat.
	RegionAssetSpawner->EnsureDefaultAssets();

	UE_LOG(LogWbCore, Log,
		TEXT("Region-Assets: Baum-Mesh %s, Baum-Material %s."),
		RegionAssetSpawner->TreeMesh ? TEXT("ok") : TEXT("FEHLT"),
		RegionAssetSpawner->TreeMaterial ? TEXT("ok") : TEXT("FEHLT"));
}

void AWiesbadenWorldBuilder::EnsureLightingActors()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Bestand aufnehmen. Je Typ genuegt einer; ein zweiter waere schaedlich
	// (zwei Sonnen addieren sich).
	ADirectionalLight* Sun = nullptr;
	ASkyLight* Sky = nullptr;
	bool bHasAtmosphere = false;
	bool bHasFog = false;

	for (TActorIterator<ADirectionalLight> It(World); It; ++It) { Sun = *It; break; }
	for (TActorIterator<ASkyLight> It(World); It; ++It) { Sky = *It; break; }
	for (TActorIterator<ASkyAtmosphere> It(World); It; ++It) { bHasAtmosphere = true; break; }
	for (TActorIterator<AExponentialHeightFog> It(World); It; ++It) { bHasFog = true; break; }

	int32 Created = 0;

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// Hoch genug, dass die Akteure nicht im Gelaende stecken.
	const FVector High(0.0, 0.0, 20000.0);

	if (!Sun)
	{
		// Pitch -45 Grad: HOEHERER, mittaghafter Sonnenstand fuer den hellen,
		// sonnigen Referenz-Look (echtes Wiesbaden bei Tageslicht). Der fruehere
		// tiefe Streifstand (-24) gab lange, plastische Schatten, liess die Stadt
		// aber duester wirken; die Referenz ist hell und sonnig. Zusammen mit dem
		// angehobenen Himmelslicht (2.2, siehe unten) saufen die Schattenseiten
		// nicht ab.
		Sun = World->SpawnActor<ADirectionalLight>(High, FRotator(-45.0f, -35.0f, 0.0f), Params);
		++Created;
	}
	if (Sun)
	{
		Sun->SetMobility(EComponentMobility::Movable);
		if (UDirectionalLightComponent* Comp =
			Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
		{
			// 10 lx entspricht dem UE5-Default einer platzierten Sonne.
			// WeatherFX skaliert diesen Wert zur Laufzeit nach Sonnenstand.
			Comp->SetIntensity(10.0f);
			Comp->SetAtmosphereSunLight(true);
		}
		MarkAlwaysLoaded(Sun);
	}

	if (!Sky)
	{
		Sky = World->SpawnActor<ASkyLight>(High, FRotator::ZeroRotator, Params);
		++Created;
	}
	if (Sky)
	{
		if (USkyLightComponent* Comp = Sky->GetLightComponent())
		{
			// ASkyLight erbt von AInfo, nicht von ALight - Mobility gibt es nur
			// an der Komponente.
			Comp->SetMobility(EComponentMobility::Movable);
			// Ohne Umgebungslicht faellt jede verschattete Fassade auf Schwarz.
			// Echtzeit-Aufnahme, damit das Licht dem wandernden Sonnenstand folgt.
			Comp->bRealTimeCapture = true;
			// Himmelslicht gegen die Sonne (10.0, siehe oben) abgewogen.
			//
			// Beide sichtbaren Fassadenseiten einer Strassenschlucht liegen fast
			// immer im Schatten und werden NUR vom Himmelslicht aufgehellt; beim
			// UE-Default 1.0 liefen sie gegen Schwarz. Daraufhin stand hier 6.0 -
			// also 60 Prozent der Sonnenstaerke. In echtem Tageslicht liegt das
			// Verhaeltnis bei etwa 15 Prozent, und entsprechend sah die Stadt aus:
			// kontrastarm und milchig-blau ueberlagert, Ziegeldaecher lachsfarben,
			// Gehwege fast weiss.
			//
			// 2.0 raeumte den Schleier weg, liess die Fassaden aber in Richtung
			// Schwarz kippen - beides ist am Bild nachgeprueft. 3.5 lag dazwischen
			// und hielt die Schattenseiten lesbar.
			//
			// Mit dem jetzt tiefen, streifenden Sonnenstand (Pitch -24) fuellte 3.5
			// die langen Schatten wieder auf. 1.3 hielt die Streiflicht-Schatten.
			//
			// ZIEL JETZT: der helle, sonnige Referenz-Look (echtes Wiesbaden,
			// Street-View-Mittagslicht) - hoehere Sonne (Pitch -45, siehe oben) UND
			// mehr Himmelslicht, damit die verschatteten Fassaden/Gehwege nicht
			// dunkel absaufen. 2.2 hebt die Schattenseiten sichtbar an und bleibt
			// unter dem milchigen 3.5.
			Comp->SetIntensity(2.2f);
		}
		MarkAlwaysLoaded(Sky);
	}

	if (!bHasAtmosphere)
	{
		if (ASkyAtmosphere* Atmosphere = World->SpawnActor<ASkyAtmosphere>(High, FRotator::ZeroRotator, Params))
		{
			MarkAlwaysLoaded(Atmosphere);
			++Created;
		}
	}

	if (!bHasFog)
	{
		if (AExponentialHeightFog* Fog = World->SpawnActor<AExponentialHeightFog>(
			FVector::ZeroVector, FRotator::ZeroRotator, Params))
		{
			if (UExponentialHeightFogComponent* Comp = Fog->GetComponent())
			{
				// Dezent: Tiefenwirkung ueber die Stadt, ohne Sicht zu nehmen.
				Comp->SetFogDensity(0.008f);
				Comp->SetFogHeightFalloff(0.15f);
				Comp->SetStartDistance(4000.0f);
			}
			MarkAlwaysLoaded(Fog);
			++Created;
		}
	}

	// -- Belichtung ---------------------------------------------------------
	//
	// Ohne Begrenzung hebt die Auto-Belichtung die Szene so weit an, dass die
	// Farben verwaschen: Ziegeldaecher werden lachsfarben, Gehwege fast weiss.
	// Grund ist die niedrige mittlere Helligkeit (Asphalt und Wiese haben
	// physikalisch kleine Albedo-Werte) - die Automatik gleicht das aus und
	// nimmt dabei den helleren Flaechen den Kontrast.
	bool bHasPostProcess = false;
	for (TActorIterator<APostProcessVolume> It(World); It; ++It)
	{
		bHasPostProcess = true;
		break;
	}

	if (!bHasPostProcess)
	{
		if (APostProcessVolume* Volume = World->SpawnActor<APostProcessVolume>(
			FVector::ZeroVector, FRotator::ZeroRotator, Params))
		{
			Volume->bUnbound = true;
			MarkAlwaysLoaded(Volume);

			FPostProcessSettings& PP = Volume->Settings;

			PP.bOverride_AutoExposureMethod = true;
			PP.AutoExposureMethod = EAutoExposureMethod::AEM_Histogram;

			// Enges Fenster statt fester Belichtung: Tag/Nacht-Wechsel bleibt
			// moeglich, das Ausbleichen am Tag nicht.
			PP.bOverride_AutoExposureMinBrightness = true;
			PP.AutoExposureMinBrightness = 0.6f;
			PP.bOverride_AutoExposureMaxBrightness = true;
			PP.AutoExposureMaxBrightness = 1.6f;

			PP.bOverride_AutoExposureBias = true;
			PP.AutoExposureBias = 0.0f;

			++Created;
		}
	}

	UE_LOG(LogWbCore, Log,
		TEXT("EnsureLightingActors: %d Lichtakteur(e) angelegt (Sonne %s, Himmelslicht %s)."),
		Created,
		Sun ? TEXT("ok") : TEXT("FEHLT"),
		Sky ? TEXT("ok") : TEXT("FEHLT"));
}
UMaterialInterface* AWiesbadenWorldBuilder::ResolveRoadMaterial(ERoadMeshChannel Channel, EOSMSurfaceType Surface) const
{
	// Belag vor Kanal: Ein Feldweg ist zwar eine "Fahrbahn", aber kein Asphalt.
	// Die Abschnitte sind bereits nach Oberflaeche gruppiert, die Information
	// lag also vor und wurde nur nicht ausgewertet.
	// Die Boeschung ist Erdreich, kein Belag - aber AUCH KEIN GRAS.
	//
	// Hier stand kurzzeitig das Gelaende-Material, damit der Uebergang zur
	// Wiese nicht als Kante auffaellt. Das war ein Rueckschritt: Die
	// Boeschungen sind allein in Sichtweite 2,4 Millionen Dreiecke entlang
	// jeder Strassenkante. Grasfarben eingefaerbt haben sie die Strassen
	// sichtbar zuwuchern lassen - genau die "Gruenflaechen auf der Fahrbahn",
	// die abgestellt werden sollten.
	//
	// Sie behalten deshalb das unbefestigte Material, mit dem sie vorher schon
	// gezeichnet wurden (ueber Kanal Fahrbahn + Oberflaeche Erde).
	if (Channel == ERoadMeshChannel::Embankment)
	{
		return UnpavedMaterial ? UnpavedMaterial : RoadMaterial;
	}

	if (Channel == ERoadMeshChannel::Carriageway || Channel == ERoadMeshChannel::Intersection)
	{
		switch (Surface)
		{
		case EOSMSurfaceType::Gravel:
		case EOSMSurfaceType::Compacted:
		case EOSMSurfaceType::Ground:
		case EOSMSurfaceType::Grass:
			if (UnpavedMaterial) { return UnpavedMaterial; }
			break;

		case EOSMSurfaceType::PavingStones:
		case EOSMSurfaceType::Sett:
		case EOSMSurfaceType::Cobblestone:
			if (PavedStoneMaterial) { return PavedStoneMaterial; }
			break;

		default:
			break;
		}
	}

	switch (Channel)
	{
	case ERoadMeshChannel::Kerb:
		// Eigener Bordstein-Look; ohne Zuweisung wie der Gehweg.
		return KerbMaterial ? KerbMaterial : SidewalkMaterial;

	case ERoadMeshChannel::Sidewalk:
		return SidewalkMaterial;

	case ERoadMeshChannel::LaneMarking:
	case ERoadMeshChannel::Crossing:
		// Weiss. Fiele beides in den Fahrbahn-Zweig, waeren Striche und
		// Zebrastreifen auf dem dunklen Asphalt nicht zu erkennen.
		return LaneMarkingMaterial ? LaneMarkingMaterial : RoadMaterial;

	case ERoadMeshChannel::Cycleway:
		return CyclewayMaterial ? CyclewayMaterial : RoadMaterial;

	case ERoadMeshChannel::Carriageway:
	case ERoadMeshChannel::Intersection:
	default:
		return RoadMaterial;
	}
}

UMaterialInterface* AWiesbadenWorldBuilder::ResolveBuildingMaterial(EBuildingMeshChannel Channel, int32 MaterialVariant, const FString& FacadeOverrideKey)
{
	if (Channel == EBuildingMeshChannel::Roof)
	{
		return BuildingRoofMaterial;
	}

	// Per-Adress-Override (z. B. Mainzer Strasse 129): eigenes Fassaden-
	// Material, sonst die Standard-Fassade.
	if (!FacadeOverrideKey.IsEmpty())
	{
		if (const UMaterialInterface* const* OverrideMaterial = AddressFacadeMaterials.Find(FacadeOverrideKey))
		{
			if (*OverrideMaterial)
			{
				return const_cast<UMaterialInterface*>(*OverrideMaterial);
			}
		}
		// City-Prompt-Overrides (Stil/Landmarke aus der Spec): gleicher
		// Mechanismus, eigene Map.
		if (const UMaterialInterface* const* OverrideMaterial = PromptFacadeMaterials.Find(FacadeOverrideKey))
		{
			if (*OverrideMaterial)
			{
				return const_cast<UMaterialInterface*>(*OverrideMaterial);
			}
		}
	}

	// Bauweise der Fassade (Putz/Backstein/Sandstein/Glas/Beton/Fachwerk).
	if (FacadeVariantMaterials.IsValidIndex(MaterialVariant) && FacadeVariantMaterials[MaterialVariant])
	{
		return FacadeVariantMaterials[MaterialVariant];
	}

	return BuildingWallMaterial;
}

FString AWiesbadenWorldBuilder::BuildSignTextureName(const FString& SignId)
{
	// Gemeinsame Konvention (auch vom Atlas-Baker/Spawner genutzt).
	return WiesbadenSignAssets::BuildTextureName(SignId);
}

UTexture2D* AWiesbadenWorldBuilder::ResolveSignTexture(const FString& SignId) const
{
	return WiesbadenSignAssets::ResolveTexture(SignId, SignTextureFolder);
}

UMaterialInstanceDynamic* AWiesbadenWorldBuilder::ResolveSignMaterial(const FString& SignId)
{
	return WiesbadenSignAssets::CreateMaterial(
		SignId, SignTextureFolder, SignMaterial, SignTextureParameterName, this);
}

void AWiesbadenWorldBuilder::RebuildFurnitureInstances()
{
	if (!FurnitureSpawner)
	{
		return;
	}

	FurnitureSpawner->SignTextureFolder = SignTextureFolder;
	FurnitureSpawner->SignMaterial = SignMaterial;
	FurnitureSpawner->PoleMaterial = PoleMaterial;
	FurnitureSpawner->DelineatorMaterial = DelineatorMaterial;
	FurnitureSpawner->ReflectorMaterial = ReflectorMaterial;
	FurnitureSpawner->MarkingMaterial = MarkingMaterial;
	FurnitureSpawner->SignTextureParameterName = SignTextureParameterName;
	FurnitureSpawner->SpawnFurniture(FurnitureLayout);
}

void AWiesbadenWorldBuilder::CheckRoadTerrainHeights()
{
	UWorld* AuditWorld = GetWorld();
	if (!AuditWorld)
	{
		return;
	}

	if (RoadNetwork.Intersections.Num() == 0)
	{
		UE_LOG(LogWbRoads, Warning,
			TEXT("Hoehenpruefung: Das Strassennetz ist leer. Der WorldBuilder haelt es nur "
				"nach einem Bau in dieser Sitzung - die gebackene Karte bringt es nicht mit."));
		return;
	}

	TArray<ALandscapeProxy*> Proxies;
	for (TActorIterator<ALandscapeProxy> It(AuditWorld); It; ++It)
	{
		Proxies.Add(*It);
	}

	const FHeightAuditReport Report = FWiesbadenHeightAudit::Run(
		RoadNetwork, Proxies, /*ToleranceCm=*/50.0, /*MaxWorst=*/40);

	UE_LOG(LogWbRoads, Log, TEXT("%s"), *Report.ToString());

	FWiesbadenHeightAudit::WriteJson(
		Report, FPaths::ProjectDir() / TEXT("hoehen_report.json"));
}

void AWiesbadenWorldBuilder::DistributeRegionAssetsToChunks()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (RegionAssetLayout.Assets.Num() == 0)
	{
		UE_LOG(LogWbCore, Warning,
			TEXT("Verteilung: kein Regions-Layout am WorldBuilder - nichts zu tun."));
		return;
	}

	// Zellen einsammeln. Der Schluessel kommt aus dem Actor-Namen, den
	// SpawnCityChunks vergibt ("CityChunk_<X>_<Y>"). Der Umweg ueber den Namen
	// ist unschoen, aber die Alternative - die Zelle aus den Mesh-Bounds
	// zurueckrechnen - waere an den Zellgrenzen mehrdeutig, und ein Baum in
	// der falschen Zelle poppt beim Fahren sichtbar auf.
	TMap<FIntPoint, AWiesbadenCityChunk*> ByCell;
	int32 UnnamedChunks = 0;
	for (TActorIterator<AWiesbadenCityChunk> It(World); It; ++It)
	{
		// Objektname UND Beschriftung pruefen: SetActorLabel benennt das Objekt
		// normalerweise mit um, aber verlassen sollte man sich darauf nicht -
		// ein Actor mit abweichendem Namen wuerde sonst stumm leer ausgehen.
		static const FString Prefix = TEXT("CityChunk_");
		TArray<FString, TInlineAllocator<2>> Candidates;
		Candidates.Add(It->GetName());
#if WITH_EDITOR
		Candidates.Add(It->GetActorLabel());
#endif

		bool bAssigned = false;
		for (const FString& Candidate : Candidates)
		{
			if (!Candidate.StartsWith(Prefix))
			{
				continue;
			}

			// Beide Koordinaten koennen negativ sein ("CityChunk_-15_7"), der
			// Trenner ist deshalb der ERSTE Unterstrich nach dem Praefix.
			const FString Rest = Candidate.RightChop(Prefix.Len());
			FString XPart;
			FString YPart;
			if (Rest.Split(TEXT("_"), &XPart, &YPart)
				&& XPart.IsNumeric() && YPart.IsNumeric())
			{
				ByCell.Add(FIntPoint(FCString::Atoi(*XPart), FCString::Atoi(*YPart)), *It);
				bAssigned = true;
				break;
			}
		}

		if (!bAssigned)
		{
			++UnnamedChunks;
		}
	}

	if (ByCell.Num() == 0)
	{
		UE_LOG(LogWbCore, Warning,
			TEXT("Verteilung: keine Zell-Actors mit lesbarem Namen gefunden (%d ohne)."),
			UnnamedChunks);
		return;
	}

	// Dieselbe Zellformel wie beim Aufteilen der Dreiecke - eine zweite
	// Rundung wuerde Objekte an den Raendern in Nachbarzellen legen.
	const double CellSizeCm = CityChunkSizeMeters * 100.0;
	TMap<FIntPoint, TArray<FPlacedRegionAsset>> PerCell;
	int32 OutsideAnyChunk = 0;
	for (const FPlacedRegionAsset& Asset : RegionAssetLayout.Assets)
	{
		const FIntPoint Cell(
			FMath::FloorToInt(Asset.Location.X / CellSizeCm),
			FMath::FloorToInt(Asset.Location.Y / CellSizeCm));

		if (!ByCell.Contains(Cell))
		{
			// Zellen ohne Strasse und ohne Haus existieren nicht als Actor -
			// reines Feld oder Wald. Diese Objekte fallen weg; das ist der
			// Preis dafuer, keine neuen Actors anzulegen, und betrifft
			// ausschliesslich Gegenden ohne jede Bebauung.
			++OutsideAnyChunk;
			continue;
		}

		FPlacedRegionAsset& Placed = PerCell.FindOrAdd(Cell).Add_GetRef(Asset);
		Placed.RegionName.Empty();
	}

	int32 Distributed = 0;
	for (const auto& [Cell, Assets] : PerCell)
	{
		AWiesbadenCityChunk* Chunk = ByCell[Cell];
		Chunk->SetRegionAssets(Assets);
		Chunk->MarkPackageDirty();
		Distributed += Assets.Num();
	}

	// Der Gesamtbestand am WorldBuilder muss weg, sonst liegt er zusaetzlich
	// zu den verteilten Kopien im Speicher - und genau der war das Problem.
	if (RegionAssetSpawner)
	{
		RegionAssetSpawner->ClearRegionAssets();
	}
	RegionAssetLayout.Reset();
	MarkPackageDirty();

	UE_LOG(LogWbCore, Log,
		TEXT("Verteilung: %d Regionsobjekte auf %d von %d Zellen verteilt; ")
		TEXT("%d lagen in Zellen ohne Actor und entfallen."),
		Distributed, PerCell.Num(), ByCell.Num(), OutsideAnyChunk);
}

void AWiesbadenWorldBuilder::RebuildRegionAssetInstances()
{
	if (!RegionAssetSpawner || RegionAssetLayout.Assets.Num() == 0)
	{
		return;
	}

	EnsureDefaultRegionAssets();
	RegionAssetSpawner->SpawnRegionAssets(RegionAssetLayout);
}

void AWiesbadenWorldBuilder::BeginPlay()
{
	Super::BeginPlay();

	// Ausstattung aus dem gespeicherten Layout neu aufbauen.
	//
	// Die ISM-Komponenten sind Transient und ueberleben das Speichern nicht;
	// die Layout-Daten dagegen schon. Ohne diesen Aufruf stand in der
	// gebackenen Stadt kein einziges Schild, kein Leitpfosten und keine
	// Laterne - obwohl das Build-Log 50.875 Schilder meldete. Diese Meldung
	// beschrieb die Editor-Sitzung, in der gebaut wurde.
	const int32 LayoutCount = FurnitureLayout.Signs.Num()
		+ FurnitureLayout.Delineators.Num()
		+ FurnitureLayout.Markings.Num()
		+ FurnitureLayout.StreetLamps.Num();

	if (LayoutCount > 0)
	{
		UE_LOG(LogWbCore, Log,
			TEXT("Ausstattung wird aus dem gespeicherten Layout aufgebaut: ")
			TEXT("%d Schilder, %d Leitpfosten, %d Markierungen, %d Laternen."),
			FurnitureLayout.Signs.Num(), FurnitureLayout.Delineators.Num(),
			FurnitureLayout.Markings.Num(), FurnitureLayout.StreetLamps.Num());

		RebuildFurnitureInstances();
	}
	else
	{
		UE_LOG(LogWbCore, Verbose,
			TEXT("Kein gespeichertes Ausstattungs-Layout - nichts aufzubauen."));
	}

	// Regionsobjekte (Baeume, Ufer-, Industrieobjekte).
	//
	// NUR im ungechunkten Pfad. Mit Chunks baut jede Zelle in ihrem eigenen
	// BeginPlay auf, was sie traegt - und nur die geladenen Zellen tun das
	// ueberhaupt. Wuerde hier zusaetzlich der Gesamtbestand aufgebaut, waere
	// die Verteilung wirkungslos: Die 1,53 Millionen Instanzen laegen wieder
	// vollstaendig an einem nie gestreamten Actor.
	if (!bGenerateCityChunks && RegionAssetLayout.Assets.Num() > 0)
	{
		UE_LOG(LogWbCore, Log,
			TEXT("Regionsobjekte werden aus dem gespeicherten Layout aufgebaut: %s"),
			*RegionAssetLayout.GetStatisticsString());

		RebuildRegionAssetInstances();
	}
}
