// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "GIS/BuildingGenerator.h"
#include "GIS/CityPrompt.h"
#include "GIS/HeightmapImporter.h"
#include "GIS/OSMDataParser.h"
#include "GIS/RoadFurnitureGenerator.h"
#include "GIS/RoadNetworkGenerator.h"
#include "GIS/TerrainGenerator.h"
#include "GIS/WiesbadenBuildSummary.h"
#include "GIS/WiesbadenRegion.h"
#include "GIS/WiesbadenRegionAssets.h"
#include "GIS/WiesbadenTrafficSimulation.h"

/**
 * Session-weite Stadt-Daten, die der UWiesbadenGameInstance haelt und die damit
 * einen Levelwechsel ueberleben (die aufwendige Pipeline laeuft nur einmal).
 *
 * Enthaelt sowohl die Eingaben (Pfade, Bezugshoehe) als auch alle Ergebnisse
 * der Pipeline: Reports, das geparste OSM, das DEM-Raster, das Strassennetz,
 * die Gebaeudemetadaten sowie die erzeugten Meshes. Die Meshes sind reine
 * Datenstrukturen (FRoadMeshData/FBuildingMeshData/FTerrainTile) und werden
 * erst auf dem Game-Thread in Komponenten ueberfuehrt (AWiesbadenCityActor).
 *
 * Ein Objekt wird auf dem Worker-Thread befuellt und nach Abschluss per Move
 * in den GameInstance uebernommen; danach ist es read-only und darf aus
 * mehreren Threads gelesen werden (gleiche Konvention wie FRoadNetwork).
 */
struct WIESBADENREAL_API FWiesbadenCityData
{
	// -- Eingaben (fuer Status- und Fehlerausgabe) ---------------------------

	/** Quell-OSM-Datei (.osm/.xml oder Overpass-.json). */
	FString OsmFilePath;

	/** Quell-DEM-Datei (.asc/.hgt); leer, wenn kein Terrain importiert wurde. */
	FString DemFilePath;

	/** City-Prompt-Spezifikation (Text -> Parameter), falls ein Prompt gesetzt war. */
	FCityPromptSpec CityPromptSpec;

	/** Orthometrische Hoehe in Metern, die auf Unreal Z = 0 abgebildet wird. */
	double VerticalReferenceMeters = 75.0;

	// -- Reports --------------------------------------------------------------

	FOSMParseResult ParseResult;
	FHeightmapImportResult DemImportResult;
	FRoadGenerationReport RoadReport;
	FBuildingGenerationReport BuildingReport;
	FRegionGenerationReport RegionReport;
	FRegionAssetReport RegionAssetReport;
	FTerrainGenerationReport TerrainReport;
	FRoadFurnitureReport FurnitureReport;

	/**
	 * Ergebnis der Terrain-Qualitaetskontrolle (leer = ok): Warnt, wenn das
	 * Tile deutlich groesser als die OSM-Ausdehnung ist (fehlender Crop) oder
	 * die Hoehenspanne unplausibel gross/klein ist. Wird von der Pipeline
	 * berechnet; Editor (Details-Panel) und Runtime (Summary-Log) zeigen die
	 * Warnmeldung.
	 */
	FTerrainQualityReport TerrainQuality;

	// -- Geometrie / Metadaten -------------------------------------------------

	/** Geparster OSM-Datensatz (fuer spaetere Systeme: GPS-Suche, POIs, Ki). */
	FOSMDataSet OSMData;

	/**
	 * Amtliche Gebaeudegrundrisse (ALKIS), nur befuellt wenn ein
	 * AlkisFilePath gesetzt war. Liegt in derselben Struktur vor wie OSMData,
	 * weil Tools/alkis_extract.mjs die Overpass-Form erzeugt - der
	 * Gebaeudegenerator braucht dadurch keinen eigenen Pfad.
	 */
	FOSMDataSet AlkisData;

	/** Importiertes Hoehenraster (nur gueltig, wenn ein DEM geladen wurde). */
	FHeightmapRaster DemRaster;

	/** Fahrspur-Graph fuer Verkehrs-KI und GPS-Navigation. */
	FRoadNetwork RoadNetwork;

	/**
	 * Parameter der Verkehrs-Simulation; TrafficDensity wird in der Pipeline
	 * aus dem City-Prompt uebernommen ("leere Strassen" 0.2 .. "Stau" 0.95).
	 * Die Simulation selbst initialisiert/tickt das City-Subsystem zur
	 * Laufzeit auf diesem Container (nach dem Move -> stabile Adressen).
	 */
	FWiesbadenTrafficSettings TrafficSettings;

	/** Metadaten aller erzeugten Gebaeude (Adressen, Typen, Bounds, Region). */
	TArray<FGeneratedBuilding> Buildings;

	/** Stadt-Regionen aus OSM-Flaechen (WorldClaw-Schritt 3). */
	TArray<FWiesbadenRegion> Regions;

	/** Platzierungsdaten regionen-abhaengiger Assets (Baeume, Ufer, Industrie). */
	FRegionAssetLayout RegionAssetLayout;

	/** Strassen-Geometrie (Fahrbahn, Gehwege, Markierungen, Kreuzungen). */
	FRoadMeshData RoadMesh;

	/** Gebaeude-Geometrie (Fassaden, Daecher, Erdgeschosse). */
	FBuildingMeshData BuildingMesh;

	/** Landscape-Heightmap (relativ zur Bezugshoehe, in cm). */
	FTerrainTile TerrainTile;

	/** Platzierungsdaten der Strassenausstattung (Schilder, Leitpfosten, Markierungen). */
	FRoadFurnitureLayout FurnitureLayout;

	// -- Zustand ---------------------------------------------------------------

	/** True, wenn der Ladevorgang erfolgreich abgeschlossen wurde. */
	bool bReady = false;

	/** Menschenlesbarer Status (Reports, Zaehler, Fehler). */
	FString Status;

	/** Leer bei Erfolg, sonst die Abbruchursache. */
	FString ErrorMessage;

	/**
	 * Letzter Laufzeit-Build (Zeitpunkt/Dauer/Ergebnis in einer USTRUCT;
	 * leer = noch keiner gelaufen). Blueprints lesen ihn ueber die
	 * GetLastBuild*()-Getter des GameInstance/Subsystem/GameMode.
	 */
	FLastBuildInfo LastBuild;
};
