// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "GIS/WiesbadenCityChunking.h"

#include "WiesbadenCityChunk.generated.h"

class UProceduralMeshComponent;
class URegionAssetSpawnerComponent;

/**
 * Eine World-Partition-Zelle der gebackenen Stadt.
 *
 * Traegt die Road-/Building-Mesh-Sections einer Grid-Zelle (FCityChunkMesh)
 * als zwei Procedural-Mesh-Komponenten. Der AWiesbadenWorldBuilder spawnt
 * nach BuildCity einen Chunk je belegter Zelle; die Chunks werden - nicht
 * transient - in der Map serialisiert. Beim Oeffnen der Map verteilt World
 * Partition die Chunk-Actors anhand ihrer kleinen Bounds auf Streaming-Zellen,
 * sodass die Stadt zellenweise geladen/entladen wird statt als ein
 * monolithischer Riesen-Actor komplett im Speicher zu liegen.
 *
 * Materialien werden vom WorldBuilder nach dem Spawn gesetzt (die
 * ResolveRoadMaterial/ResolveBuildingMaterial-Lookup-Pfade haengen an dessen
 * UPROPERTY-Material-Maps).
 */
UCLASS()
class WIESBADENREAL_API AWiesbadenCityChunk : public AActor
{
	GENERATED_BODY()

public:
	AWiesbadenCityChunk();

	/**
	 * Uebernimmt die Mesh-Sections einer Zelle (Road + Building). Die
	 * Section-Indizes starten je Komponente bei 0 in der Reihenfolge der
	 * Chunk-Arrays; danach koennen Materialien ueber SetRoadSectionMaterial/
	 * SetBuildingSectionMaterial gesetzt werden.
	 *
	 * Kollision getrennt schaltbar: Die FAHRBAHN braucht sie zwingend - das
	 * Spielerfahrzeug tastet per Raycast nach unten und sass sonst auf dem
	 * Landscape, also rund 45 cm UNTER der Strasse (sichtbar im Boden
	 * versunken). Gebaeude kommen dagegen mit den guenstigeren Box-Koerpern
	 * des BuildingCollisionSpawners aus; Dreieckskollision fuer 5.825
	 * Gebaeude-Abschnitte waere unbezahlbar.
	 */
	void ApplyChunk(const FCityChunkMesh& Chunk, bool bRoadCollision, bool bBuildingCollision);

	/** Setzt das Material einer Road-Section (Index wie in ApplyChunk). */
	void SetRoadSectionMaterial(int32 SectionIndex, UMaterialInterface* Material);

	/** Setzt das Material einer Building-Section (Index wie in ApplyChunk). */
	void SetBuildingSectionMaterial(int32 SectionIndex, UMaterialInterface* Material);

	UProceduralMeshComponent* GetRoadMesh() const { return RoadMesh; }

	/** Kanal einer Road-Section; INDEX_NONE, wenn unbekannt. */
	int32 GetRoadSectionChannel(int32 SectionIndex) const
	{
		return RoadSectionChannels.IsValidIndex(SectionIndex)
			? static_cast<int32>(RoadSectionChannels[SectionIndex]) : INDEX_NONE;
	}
	UProceduralMeshComponent* GetBuildingMesh() const { return BuildingMesh; }

	/**
	 * Setzt die Regionsobjekte dieser Zelle und baut ihre Instanzen auf.
	 *
	 * Getrennt von ApplyChunk, weil die Objekte auch NACHTRAEGLICH in eine
	 * bereits gebackene Stadt verteilt werden koennen. Der Alternativweg waere
	 * ein kompletter Neubau; laut Bauhistorie dauert der 40 bis 64 Minuten,
	 * waehrend hier nur Punkte umsortiert werden.
	 */
	void SetRegionAssets(const TArray<FPlacedRegionAsset>& InAssets);

	/**
	 * Anzahl der Regionsobjekte dieser Zelle.
	 *
	 * BlueprintCallable, damit das Verteil-Skript NACHZAEHLEN kann. Eine
	 * Verteilung, die nichts verteilt, meldet sich sonst genauso ruhig wie
	 * eine erfolgreiche - und gespeichert waere dann eine Stadt ohne Baeume.
	 */
	UFUNCTION(BlueprintCallable, Category = "Wiesbaden|Chunk")
	int32 GetRegionAssetCount() const { return RegionAssets.Num(); }

protected:
	virtual void BeginPlay() override;

private:
	/**
	 * Baeume, Ufer- und Industrie-Objekte dieser Zelle.
	 *
	 * KEIN Transient - die Instanz-Komponente dagegen schon, weshalb die
	 * Instanzen beim Oeffnen der Karte aus diesen Daten neu entstehen muessen
	 * (BeginPlay). Genau daran ist die Strassenausstattung schon einmal
	 * gescheitert: Das Build-Protokoll meldete 50.875 Schilder, in der
	 * gebackenen Stadt stand keines - die Meldung beschrieb die
	 * Editor-Sitzung, in der gebaut wurde.
	 */
	UPROPERTY()
	TArray<FPlacedRegionAsset> RegionAssets;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Chunk")
	URegionAssetSpawnerComponent* RegionAssetSpawner = nullptr;

	/**
	 * Kanal je Road-Section (Reihenfolge wie in ApplyChunk).
	 *
	 * KEIN Transient: Ohne Serialisierung waere die Zuordnung nach dem
	 * Speichern der Karte verloren, und die Diagnose koennte Fahrbahn nicht
	 * mehr von Boeschung unterscheiden. Genau daran ist die Verdeckungs-
	 * Statistik gescheitert.
	 */
	UPROPERTY()
	TArray<uint8> RoadSectionChannels;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Chunk")
	UProceduralMeshComponent* RoadMesh = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Chunk")
	UProceduralMeshComponent* BuildingMesh = nullptr;
};
