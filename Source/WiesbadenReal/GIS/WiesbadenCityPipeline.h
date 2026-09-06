// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Core/WiesbadenCityData.h"
#include "GIS/RoadFurnitureGenerator.h"
#include "GIS/WiesbadenPickupSpots.h"
#include "GIS/WiesbadenRegionAssets.h"

class UBuildingGenerator;
class UGeoCoordinateConverter;
class UHeightmapImporter;
class UOSMDataParser;
class URoadFurnitureGenerator;
class URoadNetworkGenerator;
class URoadTypeLibrary;
class UTerrainGenerator;
class UWiesbadenPickupSpotGenerator;
class UWiesbadenRegionAssetGenerator;
class UWiesbadenRegionGenerator;

/**
 * Gemeinsame, daten-reine Stadt-Generierung fuer Editor- und Runtime-Pfad.
 *
 * BuildCityData() parst OSM, importiert das DEM und generiert Strassen,
 * Gebaeude, Terrain und Strassenausstattung in ein FWiesbadenCityData-Objekt.
 * Die Funktion ist bewusst frei von Engine-Zugriffen (kein World-, Rendering-
 * oder Komponenten-Zugriff) und kann daher direkt auf einem Worker-Thread
 * laufen; UE_LOG ist thread-sicher. Die Mesh-/Landscape-Erzeugung bleibt
 * Aufgabe des Aufrufers auf dem Game-Thread.
 *
 * Fortschritt und Abbruch laufen ueber optionale Callbacks: der Editor bindet
 * darueber eine FScopedSlowTask (inkl. Abbrechen) an, die Runtime kommt ohne
 * Callbacks aus.
 */
namespace WiesbadenCityPipeline
{
	/** Fortschrittsstufen des Builds (Index fuer die Editor-Anzeige). */
	enum class EBuildStage : int32
	{
		Prepare = 0,
		ParseOsm = 1,
		ImportDem = 2,
		Roads = 3,
		Buildings = 4,
		Regions = 5,
		Terrain = 6,
		RegionAssets = 7,
		Furniture = 8,
		PickupSpots = 9,
		Done = 10,
	};

	/** Ergebnis der Datenverarbeitung. */
	enum class EBuildResult : uint8
	{
		Success,
		Failed,    // Fehlertext steht in FWiesbadenCityData::ErrorMessage.
		Cancelled, // vom Abbruch-Callback abgebrochen.
	};

	/** Eingaben (Kopien der Config; der Worker liest keine UObject-Properties). */
	struct FBuildInput
	{
		FString OsmFilePath;
		FString DemFilePath;

		/**
		 * Optionale amtliche Gebaeudegrundrisse (ALKIS), erzeugt von
		 * Tools/alkis_extract.mjs in Overpass-JSON-Form.
		 *
		 * Ist der Pfad gesetzt, liefern DIESE Daten die Gebaeude; das
		 * OSM-Extrakt steuert dann nur noch Strassen, Regionen und
		 * Ausstattung bei. ALKIS ist vermessungsgenau und vollstaendig,
		 * waehrend OSM-Grundrisse von Hand erfasst sind - Genauigkeit rund
		 * 1 m, mit Luecken.
		 *
		 * Leer = Gebaeude wie bisher aus OSM.
		 */
		FString AlkisFilePath;

		/**
		 * Textuelle Stadtbeschreibung (z. B. "dichte Gruenderzeit-Innenstadt
		 * mit Marktkirche, wolkig"). Wird regelbasiert in Pipeline-Parameter
		 * uebersetzt (CityPromptParser): Dichte -> Mindest-Grundflaeche der
		 * Gebaeude; Fassadenstile/Landmarken/Wetter stehen in der Spec.
		 */
		FString CityPrompt;

		bool bImportDem = true;
		bool bGenerateRoads = true;
		bool bGenerateBuildings = true;
		bool bGenerateRegions = true;
		bool bGenerateRegionAssets = true;
		bool bGenerateTerrain = true;
		bool bGenerateFurniture = true;

		/**
		 * Pickup-Standorte aus OSM-Amenities (Tankstellen -> Treibstoff,
		 * Apotheken/Krankenhaeuser -> Gesundheit). Zur Abschaltung setzen,
		 * wenn ein Gameplay-Modus ohne Sammelobjekte laeuft.
		 */
		bool bGeneratePickupSpots = true;
		FWiesbadenPickupSpotSettings PickupSpotSettings;

		/**
		 * Gebaeude, deren Schwerpunkt in einer Wasser-Region liegt, entfernen
		 * (WorldClaw-Schritt 3: Objekte logisch platzieren - kein Haus im See).
		 */
		bool bRemoveWaterBuildings = true;
		double VerticalReferenceMeters = 75.0;
		FRoadGenerationSettings RoadSettings;
		FBuildingGenerationSettings BuildingSettings;
		FTerrainGenerationSettings TerrainSettings;
		FRoadFurnitureSettings FurnitureSettings;
		FRegionAssetSettings RegionAssetSettings;

		/**
		 * Parameter der Verkehrs-Simulation (Spawn-Rate, Abstaende, Seed). Die
		 * TrafficDensity wird durch den City-Prompt ueberschrieben, wenn einer
		 * gesetzt ist - der Prompt ist die hoeherwertige Absicht.
		 */
		FWiesbadenTrafficSettings TrafficSettings;
		FString RoadTypeConfigPath;
	};

	/** Pipeline-Objekte (Game-Thread erzeugt und waehrend des Builds GC-erreichbar). */
	struct FBuildTools
	{
		UGeoCoordinateConverter* Converter = nullptr;
		UOSMDataParser* Parser = nullptr;
		UHeightmapImporter* Importer = nullptr;
		URoadTypeLibrary* TypeLibrary = nullptr;
		URoadNetworkGenerator* RoadGenerator = nullptr;
		UBuildingGenerator* BuildingGenerator = nullptr;
		UWiesbadenRegionGenerator* RegionGenerator = nullptr;
		UWiesbadenRegionAssetGenerator* RegionAssetGenerator = nullptr;
		UTerrainGenerator* TerrainGenerator = nullptr;
		URoadFurnitureGenerator* FurnitureGenerator = nullptr;
		UWiesbadenPickupSpotGenerator* PickupSpotGenerator = nullptr;
	};

	/** Meldet Fortschritt (0..100) und Stufe. */
	using FProgressCallback = TFunction<void(int32 Percent, EBuildStage Stage)>;

	/** Liefert true, wenn der Build abgebrochen werden soll. */
	using FCancelCallback = TFunction<bool()>;

	/**
	 * Fuehrt die komplette GIS-Pipeline aus (OSM -> DEM -> Strassen -> Gebaeude
	 * -> Terrain -> Ausstattung) und befuellt OutData. Reine Datenverarbeitung,
	 * daher worker-thread-sicher.
	 *
	 * @return Success, Failed (ErrorMessage in OutData) oder Cancelled.
	 */
	EBuildResult BuildCityData(
		const FBuildInput& Input,
		const FBuildTools& Tools,
		FWiesbadenCityData& OutData,
		FProgressCallback Progress = nullptr,
		FCancelCallback Cancel = nullptr);
}
