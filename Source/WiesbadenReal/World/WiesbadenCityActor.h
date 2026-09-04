// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "Core/WiesbadenCityData.h"
#include "GIS/WiesbadenPedestrianSimulation.h"

#include "WiesbadenCityActor.generated.h"

class UMaterialInterface;
class UProceduralMeshComponent;
class URegionAssetSpawnerComponent;
class URoadFurnitureSpawnerComponent;
class UTrafficVehicleSpawnerComponent;
class UPedestrianSpawnerComponent;
class UWiesbadenWeatherFXComponent;

/**
 * Laufzeit-Actor der Stadt: haelt die erzeugte Geometrie als
 * Procedural-Mesh-Komponenten (Strassen, Gebaeude, Terrain-Vorschau).
 *
 * Wird vom UWiesbadenCitySubsystem gespawnt, sobald die Stadt-Daten im
 * GameInstance verfuegbar sind (Laufzeit-Build oder wiederkehrender Level).
 * Die Mesh-Sections werden aus den Datenstrukturen der Pipeline direkt auf
 * die Komponenten uebertragen - die Kollisions- und Materiallogik entspricht
 * der des Editor-Workflows (AWiesbadenWorldBuilder).
 *
 * HINWEIS WORLD PARTITION: In einer partitionierten Welt landet dieser Actor
 * im persistenten (immer geladenen) Teil der Welt. Laufzeit erzeugte
 * Geometrie wird damit NICHT zellenweise gestreamt - das Streamen gilt fuer
 * die im Editor gebackenen Zellinhalte (Produktionspfad). Fuer die
 * Entwicklung ist die immer geladene Stadt gewollt (kein Zell-Hopping waehrend
 * des Tests).
 */
UCLASS()
class WIESBADENREAL_API AWiesbadenCityActor : public AActor
{
	GENERATED_BODY()

public:
	AWiesbadenCityActor();

	/**
	 * Uebertraegt die Meshes aus den Stadt-Daten auf die Komponenten.
	 * Materialien werden vorher von aussen gesetzt (AWiesbadenGameInstance::
	 * ApplyMaterialsToCityActor). Bei bCreateCollision=false sind die Meshes
	 * render-only (kein Physics/Query).
	 */
	void ApplyCityData(const FWiesbadenCityData& Data, bool bCreateCollision);

	/** Entfernt alle Mesh-Sections. */
	void ClearMeshes();

	/**
	 * Aktualisiert die sichtbaren Fahrzeuge der Verkehrs-Simulation (ISM-Pool,
	 * 1-km-Culling um den Player). Wird vom CitySubsystem pro Tick gerufen,
	 * nachdem die Simulation weitergetickt wurde.
	 */
	void UpdateTrafficVehicles(const TArray<FTrafficVehicle>& Vehicles);

	/**
	 * Zahl der zuletzt tatsaechlich gezeichneten Verkehrsfahrzeuge.
	 *
	 * Nicht identisch mit der Zahl der simulierten Fahrzeuge: gezeichnet wird
	 * nur, was innerhalb des Cull-Radius liegt. Weicht der Wert dauerhaft
	 * 0 auf, laeuft die Simulation ins Leere - genau dieser Fall blieb bisher
	 * unbemerkt, weil nichts ihn gemeldet hat.
	 */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Verkehr")
	int32 GetVisibleTrafficVehicleCount() const;

	/**
	 * Aktualisiert die sichtbaren Fussgaenger (ISM-Pool). Analog zum Verkehr:
	 * die Simulation rechnet nur Positionen, gezeichnet wird hier.
	 */
	void UpdatePedestrians(const TArray<FPlacedPedestrian>& Placed);

	/** Zahl der zuletzt gezeichneten Fussgaenger. */
	UFUNCTION(BlueprintPure, Category = "Wiesbaden|Fussgaenger")
	int32 GetVisiblePedestrianCount() const;

	// -- Materialien (werden vom Subsystem aus der Konfiguration gesetzt) ----

	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Materialien")
	UMaterialInterface* RoadMaterial = nullptr;

	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Materialien")
	UMaterialInterface* SidewalkMaterial = nullptr;

	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Materialien")
	UMaterialInterface* BuildingWallMaterial = nullptr;

	/**
	 * Per-Adress-Override: Adresse (addr:street + addr:housenumber, z. B.
	 * "Mainzer Strasse 129") -> eigenes Fassaden-Material.
	 */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Materialien")
	TMap<FString, UMaterialInterface*> AddressFacadeMaterials;

	/**
	 * City-Prompt-Override: FacadeOverrideKey ("PromptStyle:<Stil>" oder
	 * "PromptLandmark:<Name>", vom BuildingGenerator aus der FCityPromptSpec
	 * vergeben) -> eigenes Fassaden-Material. Leer lassen = Standardfalle.
	 */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Materialien")
	TMap<FString, UMaterialInterface*> PromptFacadeMaterials;

	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Materialien")
	UMaterialInterface* BuildingRoofMaterial = nullptr;

	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Materialien")
	UMaterialInterface* TerrainMaterial = nullptr;

	/** Basismaterial der Schild-Tafel (Textur wird als Parameter gesetzt). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Materialien")
	UMaterialInterface* SignMaterial = nullptr;

	/** Content-Ordner der Schild-Texturen (Sign_<VzKat>.png). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Materialien")
	FString SignTextureFolder = TEXT("/Game/Textures/TrafficSigns/");

	/** Aufloesung des Terrain-Vorschau-Meshes (wird vom Subsystem gesetzt). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Terrain", meta = (ClampMin = "2", ClampMax = "2049"))
	int32 TerrainPreviewGridSize = 257;

	// -- Gebaeude-Distanz-Streaming (nur Laufzeit-Build) ---------------------
	// Die Gebaeude-Geometrie liegt (draw-call-optimiert) nach Materialkanal
	// ZUSAMMENGELEGT vor - jede Section spannt die ganze Stadt. Alle ~119k
	// Gebaeude gleichzeitig resident sprengt den VRAM/die Lumen-Scene (GPU-Crash).
	// Loesung: die Geometrie beim Spawn in ein Zell-Raster re-einsortieren und nur
	// den Ausschnitt um den Spieler als Procedural-Mesh halten (Tick-gesteuert).
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Streaming")
	bool bStreamBuildings = true;

	/** Kantenlaenge einer Streaming-Rasterzelle in cm. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Streaming", meta = (ClampMin = "2000"))
	float BuildingCellSizeCm = 15000.0f;

	/** Ladehalbmesser um den Spieler in cm (quadratisches Zellfenster). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Streaming", meta = (ClampMin = "5000"))
	float BuildingStreamRadiusCm = 37500.0f;

	/** Tick-gesteuertes Distanz-Streaming der Gebaeudezellen. */
	virtual void Tick(float DeltaSeconds) override;

private:
	/** Material fuer einen Strassen-Mesh-Kanal (Fallback: RoadMaterial). */
	UMaterialInterface* ResolveRoadMaterial(ERoadMeshChannel Channel) const;

	/**
	 * Material fuer einen Gebaeude-Mesh-Kanal (Fallback: BuildingWallMaterial).
	 * FacadeOverrideKey (Adresse) waehlt ueber AddressFacadeMaterials ein
	 * eigenes Fassaden-Material.
	 */
	UMaterialInterface* ResolveBuildingMaterial(EBuildingMeshChannel Channel, const FString& FacadeOverrideKey);

	/** Baut ein herunterskaliertes Terrain-Vorschau-Mesh aus der Heightmap. */
	void BuildTerrainPreview(const FTerrainTile& Tile, int32 GridSize);

	// -- Gebaeude-Streaming intern -------------------------------------------
	/** Sortiert die zusammengelegte Gebaeude-Geometrie in ein Zell-Raster um. */
	void BuildBuildingCells(const FBuildingMeshData& BuildingMeshData);
	/** Legt/erneuert den Procedural-Mesh-Pool fuer die Streaming-Zellen an. */
	void SetupBuildingCellPool(bool bCreateCollision);
	/** Laedt/entlaedt Zellen nach Distanz zur ViewLocation. */
	void UpdateBuildingStreaming(const FVector& ViewLocation);
	void LoadBuildingCell(const FIntPoint& Cell);
	void UnloadBuildingCell(const FIntPoint& Cell);

	/** Re-einsortierte Gebaeude-Geometrie je Rasterzelle (kein UObject -> kein GC). */
	TMap<FIntPoint, TArray<FBuildingMeshSection>> BuildingCells;

	/** Komponenten-Pool (haelt die Komponenten am Leben -> UPROPERTY). */
	UPROPERTY(Transient)
	TArray<UProceduralMeshComponent*> BuildingCellPool;

	/** Freie Pool-Komponenten (Lebensdauer ueber den Pool gesichert). */
	TArray<UProceduralMeshComponent*> FreeBuildingComponents;

	/** Aktuell geladene Zelle -> zugewiesene Komponente. */
	TMap<FIntPoint, UProceduralMeshComponent*> LoadedBuildingCells;

	bool bBuildingStreamingActive = false;
	bool bBuildingCollision = false;
	float StreamTickAccumSeconds = 0.0f;

	UPROPERTY(Transient)
	USceneComponent* Root = nullptr;

	UPROPERTY(Transient)
	UProceduralMeshComponent* RoadMesh = nullptr;

	UPROPERTY(Transient)
	UProceduralMeshComponent* BuildingMesh = nullptr;

	UPROPERTY(Transient)
	UProceduralMeshComponent* TerrainMesh = nullptr;

	UPROPERTY(Transient)
	URoadFurnitureSpawnerComponent* FurnitureSpawner = nullptr;

	/** Treibt Niagara-Wetter-Effekte (Regen/Schnee/Nebel/Wolken/Gewitter). */
	UPROPERTY(Transient)
	UWiesbadenWeatherFXComponent* WeatherFX = nullptr;

	/** Rendert regionen-abhaengige Assets (Baeume HISM, Ufer/Industrie ISM). */
	UPROPERTY(Transient)
	URegionAssetSpawnerComponent* RegionAssetSpawner = nullptr;

	/** Rendert die Fahrzeuge der Verkehrs-Simulation (ISM-Pool, farbvariiert). */
	UPROPERTY(Transient)
	UTrafficVehicleSpawnerComponent* TrafficVehicleSpawner = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Fussgaenger")
	UPedestrianSpawnerComponent* PedestrianSpawner = nullptr;
};
