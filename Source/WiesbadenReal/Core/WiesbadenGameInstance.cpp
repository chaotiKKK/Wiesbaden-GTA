// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Core/WiesbadenGameInstance.h"

#include "WiesbadenReal.h"

#include "GIS/BuildingGenerator.h"
#include "GIS/GeoCoordinateConverter.h"
#include "GIS/HeightmapImporter.h"
#include "GIS/OSMDataParser.h"
#include "GIS/RoadFurnitureGenerator.h"
#include "GIS/RoadNetworkGenerator.h"
#include "GIS/RoadTypeLibrary.h"
#include "GIS/TerrainGenerator.h"
#include "GIS/WiesbadenBuildSummary.h"
#include "GIS/WiesbadenCityPipeline.h"
#include "GIS/WiesbadenRegionAssets.h"
#include "GIS/WiesbadenRegion.h"
#include "Materials/MaterialInterface.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "World/WiesbadenCityActor.h"

#include "Async/Async.h"

#include "HAL/Event.h"

#include "UObject/Package.h"

/**
 * Thread-sicherer Kontext eines Hintergrund-Loads.
 *
 * Der Worker liest nur die hier hinein kopierten Eingaben und die gerooteten
 * Pipeline-Objekte (lesend/stateless). Ergebnisse schreibt er in Context->Data;
 * der Game-Thread liest sie erst, nachdem der Worker die Fortsetzung auf dem
 * Game-Thread eingeplant hat - die Task-Reihenfolge stellt die
 * happens-before-Beziehung her (zusaetzlich Context->bDone als Absicherung).
 */
struct UWiesbadenGameInstance::FCityLoadContext
{
	// Eingaben (Kopien der Config-Werte; der Worker liest keine UObject-Props).
	WiesbadenCityPipeline::FBuildInput Input;

	// Pipeline-Objekte (Game-Thread erzeugt und gerootet).
	WiesbadenCityPipeline::FBuildTools Tools;

	// Steuerung.
	std::atomic<bool> bDone{false};

	/** Abbruch-Anforderung (vom Shutdown gesetzt; die Pipeline pollt sie). */
	std::atomic<bool> bCancel{false};

	/**
	 * Wird vom Worker nach Abschluss des Builds signalisiert - damit kann der
	 * Shutdown auf das Thread-Ende warten, bevor die Pipeline-Objekte aus dem
	 * Root-Set genommen werden. Roher Zeiger auf ein Pool-Event (kein
	 * Ownership; Rueckgabe an den Pool in FinalizeCityLoad/Shutdown).
	 */
	FEvent* DoneEvent = nullptr;

	/** Eigentuemer fuer die Game-Thread-Fortsetzung (schwacher Verweis). */
	TWeakObjectPtr<UWiesbadenGameInstance> Owner;

	// Ergebnisse.
	FWiesbadenCityData Data;
};

void UWiesbadenGameInstance::Shutdown()
{
	bIsShuttingDown = true;

	// Laeuft noch ein Hintergrund-Build, diesen abbrechen und auf das Ende des
	// Worker-Threads warten, BEVOR die Pipeline-Objekte aus dem Root-Set
	// genommen werden. Ohne den Join liefe der Worker nach dem Teardown auf
	// freigegebenen UObjects weiter (die Objekte sind nur gerootet, nicht
	// GI-Subobjects - EndPlayMap markiert sie daher nicht als Garbage).
	if (bLoadInProgress.load())
	{
		if (ActiveBuildContext.IsValid())
		{
			ActiveBuildContext->bCancel.store(true);

			if (ActiveBuildContext->DoneEvent)
			{
				// Die Pipeline pollt den Abbruch zwischen den Stufen; der Worker
				// signalisiert das Event nach Abschluss. 120 s decken auch eine
				// laufende (nicht unterbrechbare) OSM-Parse-Stufe ab.
				if (!ActiveBuildContext->DoneEvent->Wait(120000))
				{
					UE_LOG(LogWbCore, Warning,
						TEXT("Shutdown: Build-Worker hat nach 120 s nicht geantwortet - Root wird trotzdem entfernt."));
				}
				FPlatformProcess::ReturnSynchEventToPool(ActiveBuildContext->DoneEvent);
				ActiveBuildContext->DoneEvent = nullptr;
			}
		}
	}

	ReleasePipelineObjects();
	ActiveBuildContext.Reset();

	Super::Shutdown();
}

FString UWiesbadenGameInstance::GetCityStatus() const
{
	return CityData.IsValid() ? CityData->Status : TEXT("Keine Stadt-Daten");
}

FString UWiesbadenGameInstance::GetLastCityError() const
{
	return CityData.IsValid() ? CityData->ErrorMessage : TEXT("Keine Stadt-Daten");
}

FTerrainQualityReport UWiesbadenGameInstance::GetTerrainQuality() const
{
	return CityData.IsValid() ? CityData->TerrainQuality : FTerrainQualityReport();
}

FLastBuildInfo UWiesbadenGameInstance::GetLastBuildInfo() const
{
	return CityData.IsValid() ? CityData->LastBuild : FLastBuildInfo();
}

FString UWiesbadenGameInstance::GetLastBuildTimestamp() const
{
	return GetLastBuildInfo().Timestamp;
}

double UWiesbadenGameInstance::GetLastBuildDurationSeconds() const
{
	return GetLastBuildInfo().DurationSeconds;
}

FString UWiesbadenGameInstance::GetLastBuildResult() const
{
	return GetLastBuildInfo().Result;
}

FString UWiesbadenGameInstance::GetLastBuildSummary() const
{
	return GetLastBuildInfo().GetSummary();
}

void UWiesbadenGameInstance::CreatePipelineObjects()
{
	// BEWUSST NICHT mit dem GameInstance als Outer (NewObject<X>(this)):
	// UEditorEngine::EndPlayMap markiert beim PIE-Ende ALLE Subobjects aller
	// GameInstances als Garbage (inkl. verschachtelt, PlayLevel.cpp). Ein dort
	// gerootetes Objekt wuerde den Assert !IsRooted() in MarkAsGarbage
	// ausloesen - Absturz beim Play-Stopp mitten im Build. Mit dem
	// Transient-Package als Outer sind die Objekte keine GI-Subobjects und
	// bleiben (gerootet) bis zur expliziten Freigabe am Leben.
	PipelineConverter = NewObject<UGeoCoordinateConverter>(GetTransientPackage());
	PipelineParser = NewObject<UOSMDataParser>(GetTransientPackage());
	PipelineImporter = NewObject<UHeightmapImporter>(GetTransientPackage());
	PipelineTypeLibrary = NewObject<URoadTypeLibrary>(GetTransientPackage());
	PipelineRoadGenerator = NewObject<URoadNetworkGenerator>(GetTransientPackage());
	PipelineBuildingGenerator = NewObject<UBuildingGenerator>(GetTransientPackage());
	PipelineRegionGenerator = NewObject<UWiesbadenRegionGenerator>(GetTransientPackage());
	PipelineRegionAssetGenerator = NewObject<UWiesbadenRegionAssetGenerator>(GetTransientPackage());
	PipelineTerrainGenerator = NewObject<UTerrainGenerator>(GetTransientPackage());
	PipelineFurnitureGenerator = NewObject<URoadFurnitureGenerator>(GetTransientPackage());

	// Rooten schuetzt die Objekte waehrend des Hintergrund-Builds vor GC - der
	// Worker haelt nur rohe Zeiger (FBuildTools), die der GC nicht sieht. Die
	// Freigabe erfolgt in ReleasePipelineObjects() nach Abschluss des Builds
	// (bzw. im Shutdown nach dem Worker-Join).
	PipelineConverter->AddToRoot();
	PipelineParser->AddToRoot();
	PipelineImporter->AddToRoot();
	PipelineTypeLibrary->AddToRoot();
	PipelineRoadGenerator->AddToRoot();
	PipelineBuildingGenerator->AddToRoot();
	PipelineRegionGenerator->AddToRoot();
	PipelineRegionAssetGenerator->AddToRoot();
	PipelineTerrainGenerator->AddToRoot();
	PipelineFurnitureGenerator->AddToRoot();
}

void UWiesbadenGameInstance::ReleasePipelineObjects()
{
	if (PipelineConverter) { PipelineConverter->RemoveFromRoot(); PipelineConverter = nullptr; }
	if (PipelineParser) { PipelineParser->RemoveFromRoot(); PipelineParser = nullptr; }
	if (PipelineImporter) { PipelineImporter->RemoveFromRoot(); PipelineImporter = nullptr; }
	if (PipelineTypeLibrary) { PipelineTypeLibrary->RemoveFromRoot(); PipelineTypeLibrary = nullptr; }
	if (PipelineRoadGenerator) { PipelineRoadGenerator->RemoveFromRoot(); PipelineRoadGenerator = nullptr; }
	if (PipelineBuildingGenerator) { PipelineBuildingGenerator->RemoveFromRoot(); PipelineBuildingGenerator = nullptr; }
	if (PipelineRegionGenerator) { PipelineRegionGenerator->RemoveFromRoot(); PipelineRegionGenerator = nullptr; }
	if (PipelineRegionAssetGenerator) { PipelineRegionAssetGenerator->RemoveFromRoot(); PipelineRegionAssetGenerator = nullptr; }
	if (PipelineTerrainGenerator) { PipelineTerrainGenerator->RemoveFromRoot(); PipelineTerrainGenerator = nullptr; }
	if (PipelineFurnitureGenerator) { PipelineFurnitureGenerator->RemoveFromRoot(); PipelineFurnitureGenerator = nullptr; }
}

void UWiesbadenGameInstance::LoadCityDataAsync()
{
	if (bIsShuttingDown)
	{
		return;
	}

	if (bLoadInProgress.load())
	{
		UE_LOG(LogWbCore, Warning, TEXT("LoadCityDataAsync: Ladevorgang laeuft bereits - Aufruf ignoriert."));
		return;
	}

	if (HasCityData())
	{
		// Bereits geladen (z. B. nach Levelwechsel) - einfach melden.
		BroadcastCityDataReady();
		return;
	}

	if (OsmFilePath.IsEmpty())
	{
		CityData = MakeShared<FWiesbadenCityData>();
		CityData->bReady = false;
		CityData->Status = TEXT("Fehlgeschlagen");
		CityData->ErrorMessage = TEXT("Kein OsmFilePath konfiguriert - Laufzeit-Build nicht moeglich.");
		UE_LOG(LogWbCore, Error, TEXT("%s"), *CityData->ErrorMessage);
		BroadcastCityDataReady();
		return;
	}

	bLoadInProgress.store(true);

	CreatePipelineObjects();

	if (!PipelineConverter->InitializeWithWiesbadenOrigin())
	{
		bLoadInProgress.store(false);
		CityData = MakeShared<FWiesbadenCityData>();
		CityData->bReady = false;
		CityData->Status = TEXT("Fehlgeschlagen");
		CityData->ErrorMessage = TEXT("Georeferenzierung konnte nicht initialisiert werden.");
		UE_LOG(LogWbCore, Error, TEXT("%s"), *CityData->ErrorMessage);
		ReleasePipelineObjects();
		BroadcastCityDataReady();
		return;
	}

	// Relative Pfade (aus DefaultGame.ini) gegen das Projektverzeichnis
	// aufloesen - die Parser nutzen FPaths::FileExists direkt, das relativ zum
	// Arbeitsverzeichnis aufloesen wuerde (Engine-Binaries statt Projekt).
	const FString ResolvedOsmPath = FPaths::IsRelative(OsmFilePath)
		? FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), OsmFilePath)
		: OsmFilePath;
	const FString ResolvedDemPath = FPaths::IsRelative(DemFilePath)
		? FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), DemFilePath)
		: DemFilePath;

	// Eingaben in einen thread-sicheren Kontext kopieren. Der Worker liest
	// bewusst keine UObject-Properties des GameInstance.
	const TSharedRef<FCityLoadContext, ESPMode::ThreadSafe> Context =
		MakeShared<FCityLoadContext, ESPMode::ThreadSafe>();

	Context->Input.OsmFilePath = ResolvedOsmPath;
	Context->Input.CityPrompt = CityPrompt;
	Context->Input.DemFilePath = ResolvedDemPath;
	Context->Input.bImportDem = bImportDem;
	Context->Input.bGenerateRoads = bGenerateRoads;
	Context->Input.bGenerateBuildings = bGenerateBuildings;
	Context->Input.bGenerateTerrain = bGenerateTerrain;
	Context->Input.bGenerateFurniture = bGenerateFurniture;
	Context->Input.bGenerateRegionAssets = bGenerateRegionAssets;
	Context->Input.VerticalReferenceMeters = VerticalReferenceMeters;
	Context->Input.RoadTypeConfigPath = RoadTypeConfigPath;
	Context->Input.RoadSettings = FRoadGenerationSettings();
	Context->Input.BuildingSettings = FBuildingGenerationSettings();
	AddressFacadeMaterials.GetKeys(Context->Input.BuildingSettings.FacadeOverrideAddresses);
	Context->Input.TerrainSettings = FTerrainGenerationSettings();
	Context->Input.FurnitureSettings = FRoadFurnitureSettings();
	Context->Input.RegionAssetSettings = FRegionAssetSettings();

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
	Context->Owner = this;

	UE_LOG(LogWbCore, Log, TEXT("Starte Laufzeit-Build der Stadt aus %s (%.1f MB OSM, DEM: %s)..."),
		*ResolvedOsmPath,
		IFileManager::Get().FileSize(*ResolvedOsmPath) / (1024.0 * 1024.0),
		bImportDem && !ResolvedDemPath.IsEmpty() ? *ResolvedDemPath : TEXT("keins"));

	// Abbruch-/Join-Zustand fuer den Shutdown aufsetzen (siehe Shutdown()).
	Context->bCancel.store(false);
	Context->DoneEvent = FPlatformProcess::GetSynchEventFromPool(false);
	ActiveBuildContext = Context;

	Async(EAsyncExecution::ThreadPool,
		[Context]() { UWiesbadenGameInstance::RunCityLoadPipeline(Context); });
}

void UWiesbadenGameInstance::RunCityLoadPipeline(const TSharedRef<FCityLoadContext, ESPMode::ThreadSafe>& Context)
{
	// Reine Datenverarbeitung ueber die gemeinsame Editor-/Runtime-Pipeline
	// (WiesbadenCityPipeline::BuildCityData). Der Abbruch-Callback pollt das
	// Shutdown-Flag zwischen den Stufen; ein Fehlschlag landet in
	// Context->Data.ErrorMessage.
	//
	// Fortschritts-Callback: loggt jede Stufe einmal (der 243-MB-OSM-Build
	// dauert Minuten - ohne Feedback wirkt Play wie ein Haenger). Die Pipeline
	// ruft den Callback mehrfach pro Stufe mit steigendem Prozentwert auf;
	// hier wird nur der Stufen-WECHSEL ausgegeben, damit das Log nicht
	// ueberlaeuft.
	const double RunStart = FPlatformTime::Seconds();
	int32 LastStage = -1;
	const WiesbadenCityPipeline::EBuildResult BuildResult = WiesbadenCityPipeline::BuildCityData(
		Context->Input,
		Context->Tools,
		Context->Data,
		[&LastStage](int32 /*Percent*/, WiesbadenCityPipeline::EBuildStage Stage)
		{
			const int32 Index = static_cast<int32>(Stage);
			if (Index != LastStage)
			{
				LastStage = Index;
				const TCHAR* Name = TEXT("unbekannt");
				switch (Stage)
				{
				case WiesbadenCityPipeline::EBuildStage::Prepare:	Name = TEXT("Vorbereitung"); break;
				case WiesbadenCityPipeline::EBuildStage::ParseOsm:	Name = TEXT("OSM-Daten laden"); break;
				case WiesbadenCityPipeline::EBuildStage::ImportDem:	Name = TEXT("DEM importieren"); break;
				case WiesbadenCityPipeline::EBuildStage::Roads:		Name = TEXT("Strassennetz bauen"); break;
				case WiesbadenCityPipeline::EBuildStage::Buildings:	Name = TEXT("Gebaeude generieren"); break;
				case WiesbadenCityPipeline::EBuildStage::Regions:	Name = TEXT("Regionen planen"); break;
				case WiesbadenCityPipeline::EBuildStage::Terrain:	Name = TEXT("Landscape generieren"); break;
				case WiesbadenCityPipeline::EBuildStage::RegionAssets:Name = TEXT("Regionen-Assets platzieren"); break;
				case WiesbadenCityPipeline::EBuildStage::Furniture:	Name = TEXT("Strassenausstattung platzieren"); break;
				case WiesbadenCityPipeline::EBuildStage::Done:		Name = TEXT("Fertig"); break;
				}
				UE_LOG(LogWbCore, Log, TEXT("Laufzeit-Build: [%d/10] %s"), Index, Name);
			}
		},		[Context]() { return Context->bCancel.load(); });

	// Abbruch (Shutdown/Stop waehrend des Builds) korrekt kennzeichnen: Die
	// Pipeline setzt bei Abbruch KEINE ErrorMessage, sondern liefert nur
	// EBuildResult::Cancelled zurueck. Ohne diese Uebernahme wuerde die
	// Zusammenfassung einen abgebrochenen Build faelschlich als "ok"
	// verbuchen (Log + CSV + LastBuildResult). Der Result-Text ist identisch
	// formatiert zum Editor-Pfad ("ok" / "abgebrochen: ..." /
	// "fehlgeschlagen: ...").
	FString BuildResultText;
	if (BuildResult == WiesbadenCityPipeline::EBuildResult::Cancelled)
	{
		BuildResultText = TEXT("abgebrochen: Shutdown/Stop waehrend des Builds.");
		// Auch in der ErrorMessage ablegen, damit FinalizeCityLoad den Build
		// als nicht-ready (bReady=false) uebernimmt, falls er noch laeuft.
		Context->Data.ErrorMessage = BuildResultText;
	}
	else if (!Context->Data.ErrorMessage.IsEmpty())
	{
		BuildResultText = FString::Printf(TEXT("fehlgeschlagen: %s"), *Context->Data.ErrorMessage);
	}
	else
	{
		BuildResultText = TEXT("ok");
	}

	// Kompakter Zusammenfassungs-Block am Ende des Laufzeit-Builds (analog zum
	// Editor-BuildCity): Dauer, Umfang und Ergebnis auf einen Blick im
	// Output-Log. Laeuft auf dem Worker-Thread (UE_LOG ist thread-sicher); die
	// Daten wurden von diesem Thread gerade erst geschrieben.
	const double TotalSeconds = FPlatformTime::Seconds() - RunStart;
	UE_LOG(LogWbCore, Log, TEXT("===== Laufzeit-Build-Zusammenfassung ====="));
	UE_LOG(LogWbCore, Log, TEXT("  Dauer:    %.1f s"), TotalSeconds);
	UE_LOG(LogWbCore, Log, TEXT("  Strassen: %d Segmente / %d Kreuzungen"),
		Context->Data.RoadNetwork.Segments.Num(), Context->Data.RoadNetwork.Intersections.Num());
	UE_LOG(LogWbCore, Log, TEXT("  Gebaeude: %d"), Context->Data.Buildings.Num());
	UE_LOG(LogWbCore, Log, TEXT("  Schilder: %d"), Context->Data.FurnitureLayout.Signs.Num());

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
	if (Context->Input.bGenerateRegionAssets)
	{
		UE_LOG(LogWbCore, Log, TEXT("  Region-Assets: %d (Baeume %d, Ufer %d, Industrie %d)"),
			Context->Data.RegionAssetReport.AssetCount,
			Context->Data.RegionAssetReport.TreeCount,
			Context->Data.RegionAssetReport.WaterfrontCount,
			Context->Data.RegionAssetReport.IndustrialCount);
	}
	if (Context->Input.bGenerateTerrain)
	{
		UE_LOG(LogWbCore, Log, TEXT("  Terrain:  %dx%d"),
			Context->Data.TerrainReport.GridSize, Context->Data.TerrainReport.GridSize);

		// Terrain-Qualitaetskontrolle (datenrein, identisch zum Editor-Pfad):
		// Warnt, wenn das Tile deutlich groesser als die OSM-Ausdehnung ist
		// (Crop fehlt?) oder die Hoehenspanne unplausibel gross/klein ist.
		if (!Context->Data.TerrainQuality.WarningMessage.IsEmpty())
		{
			UE_LOG(LogWbCore, Warning, TEXT("  Terrain-Warnung: %s"), *Context->Data.TerrainQuality.WarningMessage);
		}
	}
	if (BuildResultText == TEXT("ok"))
	{
		UE_LOG(LogWbCore, Log, TEXT("  Ergebnis: Erfolgreich"));
	}
	else
	{
		// Fehler-/Abbruchfall als Error, damit der Misserfolg im Log heraussticht.
		UE_LOG(LogWbCore, Error, TEXT("  Ergebnis: %s"), *BuildResultText);
	}
	UE_LOG(LogWbCore, Log, TEXT("======================================"));

	// CSV-Historie (analog zum Editor-Pfad): je Lauf eine Zeile in
	// Saved/BuildHistory/CityBuilds.csv, damit Build-Zeiten ueber mehrere
	// Laeufe vergleichbar sind. Die Summary kommt aus der gemeinsamen
	// BuildSummaryFromCityData-Factory (identisch zum Editor-Pfad); im
	// Laufzeit-Kontext wird nichts gemovt, daher keine Overrides.
	{
		const FWiesbadenBuildSummary Summary = BuildSummaryFromCityData(
			Context->Data,
			TEXT("Runtime"),
			BuildResultText,
			TotalSeconds,
			FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M:%S")));

		// Letzter Laufzeit-Build in den CityData ablegen (Blueprints lesen ihn
		// ueber GetLastBuild*; FinalizeCityLoad movt die Daten in CityData).
		// VOR dem fehlschlagbaren CSV-Anhang setzen, damit das Feedback auch
		// bei Schreibfehlern aktuell bleibt.
		Context->Data.LastBuild = FLastBuildInfo::FromBuildSummary(Summary);

		FString CsvError;
		if (!AppendBuildSummaryToCsv(Summary, CsvError))
		{
			UE_LOG(LogWbCore, Warning,
				TEXT("Laufzeit-Build-Zusammenfassung konnte nicht in CSV geschrieben werden: %s"), *CsvError);
		}
	}

	Context->bDone.store(true);

	// Worker ist fertig - den Shutdown-Join freigeben.
	if (Context->DoneEvent)
	{
		Context->DoneEvent->Trigger();
	}

	// Fortsetzung auf dem Game-Thread. Wird erst hier (nach allen
	// Schreibzugriffen des Workers) eingeplant.
	Async(EAsyncExecution::TaskGraphMainThread,
		[Context]()
		{
			if (UWiesbadenGameInstance* GI = Context->Owner.Get())
			{
				GI->FinalizeCityLoad(Context);
			}
		});
}

void UWiesbadenGameInstance::FinalizeCityLoad(const TSharedRef<FCityLoadContext, ESPMode::ThreadSafe>& Context)
{
	if (bIsShuttingDown)
	{
		return;
	}

	bLoadInProgress.store(false);

	// Ergebnisse per Move uebernehmen (kein teures Kopieren der Meshes).
	CityData = MakeShared<FWiesbadenCityData>(MoveTemp(Context->Data));

	if (CityData->ErrorMessage.IsEmpty())
	{
		CityData->bReady = true;
		UE_LOG(LogWbCore, Log, TEXT("Stadt-Daten bereit: %s"), *CityData->Status);
	}
	else
	{
		CityData->bReady = false;
		CityData->Status = TEXT("Fehlgeschlagen");
		UE_LOG(LogWbCore, Error, TEXT("Stadt-Load fehlgeschlagen: %s"), *CityData->ErrorMessage);
	}

	// Pipeline-Objekte wieder freigeben (Root entfernen, GC darf einsammeln).
	ReleasePipelineObjects();

	// Event/Context zurueckgeben - der naechste Build erzeugt neue. (Im
	// Shutdown-Pfad geschieht das bereits in Shutdown(); dort endet die
	// Funktion frueher ueber die bIsShuttingDown-Abfrage.)
	if (Context->DoneEvent)
	{
		FPlatformProcess::ReturnSynchEventToPool(Context->DoneEvent);
		Context->DoneEvent = nullptr;
	}
	ActiveBuildContext.Reset();

	BroadcastCityDataReady();
}

void UWiesbadenGameInstance::BroadcastCityDataReady()
{
	OnCityDataReady.Broadcast();
	OnCityDataLoadedNative.Broadcast();
}

void UWiesbadenGameInstance::ApplyMaterialsToCityActor(AWiesbadenCityActor* CityActor) const
{
	if (!CityActor)
	{
		return;
	}

	CityActor->RoadMaterial = RoadMaterial.LoadSynchronous();
	CityActor->SidewalkMaterial = SidewalkMaterial.LoadSynchronous();
	CityActor->BuildingWallMaterial = BuildingWallMaterial.LoadSynchronous();
	CityActor->BuildingRoofMaterial = BuildingRoofMaterial.LoadSynchronous();
	CityActor->TerrainMaterial = TerrainMaterial.LoadSynchronous();
	CityActor->SignMaterial = SignMaterial.LoadSynchronous();
	CityActor->SignTextureFolder = SignTextureFolder;

	CityActor->AddressFacadeMaterials.Reset();
	for (const TPair<FString, TSoftObjectPtr<UMaterialInterface>>& Pair : AddressFacadeMaterials)
	{
		if (UMaterialInterface* Material = Pair.Value.LoadSynchronous())
		{
			CityActor->AddressFacadeMaterials.Add(Pair.Key, Material);
		}
	}
}
