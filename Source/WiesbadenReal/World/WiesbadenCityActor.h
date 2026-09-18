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
	void UpdateTrafficVehicles(const TArray<FTrafficVehicle>& Vehicles, bool bNight = false);

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

	/** Gesetzte Lampen des letzten Bildes (Bremse, Blinker, Scheinwerfer, Rueckleuchte). */
	TArray<int32> GetLastLampCounts() const;

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

	/** Max. neu geladene Gebaeudezellen pro Streaming-Tick. Beim schnellen Fliegen
	 *  treten viele neue Zellen zugleich ins Fenster; alle in EINEM Tick zu bauen
	 *  reisst einen Ruckler. Auf wenige je Tick begrenzt (NAECHSTE zuerst) verteilt
	 *  die Kosten und laesst die Ferne zuletzt erscheinen. 0 = unbegrenzt (alt). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Streaming", meta = (ClampMin = "0"))
	int32 MaxBuildingLoadsPerTick = 4;

	/** Einblend-Dauer neuer Gebaeudezellen in Sekunden. Statt hartem Pop-in steigt
	 *  die Zelle in dieser Zeit aus dem Boden auf ihre Endlage (materialunabhaengig,
	 *  kein Masked-Shader noetig). 0 = aus. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Streaming", meta = (ClampMin = "0.0"))
	float BuildingCellFadeInSeconds = 0.35f;

	/** Tiefe, aus der eine neue Zelle einblendet (cm unter Endlage). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Streaming", meta = (ClampMin = "0.0"))
	float BuildingCellFadeRiseCm = 1500.0f;

	/** Strassen ebenfalls entfernungsabhaengig streamen (nur Laufzeit-Build).
	 *  Nutzt DASSELBE Zell-Raster (BuildingCellSizeCm) wie die Gebaeude, damit die
	 *  Zellen zusammenfallen. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Streaming")
	bool bStreamRoads = true;

	/** Ladehalbmesser fuer Strassenzellen in cm. Grosszuegiger als bei Gebaeuden:
	 *  der Spieler FAEHRT auf der Fahrbahn, sie muss weit voraus geladen sein, sonst
	 *  faellt das Auto bei Tempo durch eine noch nicht geladene Zelle. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Streaming", meta = (ClampMin = "5000"))
	float RoadStreamRadiusCm = 50000.0f;

	/** Tick-gesteuertes Distanz-Streaming der Gebaeude- UND Strassenzellen. */
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
	/** Laedt/entlaedt Zellen nach Distanz zur ViewLocation. bImmediate=true umgeht
	 *  das Pro-Tick-Budget (fuer den Erstladevorgang am Spawn - die Umgebung soll
	 *  sofort stehen; budgetiert wird nur das Nachladen bei Bewegung). */
	void UpdateBuildingStreaming(const FVector& ViewLocation, bool bImmediate = false);
	void LoadBuildingCell(const FIntPoint& Cell);
	void UnloadBuildingCell(const FIntPoint& Cell);
	/** Schreitet das Einblenden (Aufsteigen aus dem Boden) neu geladener Zellen
	 *  JEDES Bild fort - nicht nur im gedrosselten Streaming-Takt. */
	void AdvanceBuildingCellFades(float DeltaSeconds);

	// -- Strassen-Streaming intern (dasselbe Zell-Raster wie die Gebaeude) ----
	/** Sortiert die zusammengelegte Strassen-Geometrie in das Zell-Raster um. */
	void BuildRoadCells(const FRoadMeshData& RoadMeshData);
	void SetupRoadCellPool(bool bCreateCollision);
	void UpdateRoadStreaming(const FVector& ViewLocation);
	void LoadRoadCell(const FIntPoint& Cell);
	void UnloadRoadCell(const FIntPoint& Cell);

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

	/** Aktuell einblendende Zellen-Komponente -> bisher vergangene Einblend-Sekunden.
	 *  Die Komponente sitzt um BuildingCellFadeRiseCm*(1-alpha) unter ihrer Endlage
	 *  und steigt bis alpha==1 auf. Lebensdauer haengt am Pool (kein eigenes GC). */
	TMap<UProceduralMeshComponent*, float> FadingBuildingComponents;

	// -- Strassen-Streaming (analog zu den Gebaeuden, dasselbe Raster) -------
	/** Re-einsortierte Strassen-Geometrie je Rasterzelle (kein UObject -> kein GC). */
	TMap<FIntPoint, TArray<FRoadMeshSection>> RoadCells;

	/** Komponenten-Pool (haelt die Komponenten am Leben -> UPROPERTY). */
	UPROPERTY(Transient)
	TArray<UProceduralMeshComponent*> RoadCellPool;

	TArray<UProceduralMeshComponent*> FreeRoadComponents;

	TMap<FIntPoint, UProceduralMeshComponent*> LoadedRoadCells;

	bool bRoadStreamingActive = false;
	bool bRoadCollision = false;

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

	/** Rendert regionen-abhaengige Assets (Baeume HISM, Ufer/Industrie ISM). */
	UPROPERTY(Transient)
	URegionAssetSpawnerComponent* RegionAssetSpawner = nullptr;

	/** Rendert die Fahrzeuge der Verkehrs-Simulation (ISM-Pool, farbvariiert). */
	UPROPERTY(Transient)
	UTrafficVehicleSpawnerComponent* TrafficVehicleSpawner = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Fussgaenger")
	UPedestrianSpawnerComponent* PedestrianSpawner = nullptr;
};
