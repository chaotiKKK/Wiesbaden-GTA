// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "GIS/BuildingGenerator.h"
#include "GIS/RoadNetworkGenerator.h"
#include "GIS/WiesbadenRegionAssets.h"

#include "WiesbadenCityChunking.generated.h"

/**
 * Mesh-Inhalt einer World-Partition-Grid-Zelle: die Road-/Building-Sections,
 * deren Dreiecke (Schwerpunkt) in diese Zelle fallen. Wird in einen eigenen
 * AWiesbadenCityChunk-Actor ueberfuehrt, damit World Partition die Stadt in
 * Streaming-Zellen auslagern kann statt sie als einen monolithischen Actor
 * komplett zu laden.
 */
USTRUCT()
struct WIESBADENREAL_API FCityChunkMesh
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FRoadMeshSection> RoadSections;

	UPROPERTY()
	TArray<FBuildingMeshSection> BuildingSections;

	/**
	 * Baeume, Ufer- und Industrie-Objekte dieser Zelle.
	 *
	 * Sie lagen bis zum 26.08.2026 vollstaendig in EINER Komponente am
	 * WorldBuilder - einem Actor, den World Partition nie streamt. Alle
	 * 1.532.254 Baeume waren damit jederzeit vollstaendig geladen, und die
	 * Grafikkarte arbeitete den ganzen Bestand jedes Bild durch, um vier
	 * sichtbare Kegel zu finden. Gemessen an einer Stelle, an der ausser einer
	 * Handvoll winziger Kegel am Horizont kein Baum im Bild stand:
	 *
	 *     1.532.254 Instanzen   82 ms Bildzeit
	 *       153.226 Instanzen   64 ms
	 *             0 Instanzen   54 ms
	 *
	 * Ueber die Zelle gehen sie denselben Weg wie Strassen und Gebaeude.
	 */
	UPROPERTY()
	TArray<FPlacedRegionAsset> RegionAssets;

	bool IsEmpty() const
	{
		return RoadSections.Num() == 0 && BuildingSections.Num() == 0
			&& RegionAssets.Num() == 0;
	}
};

/**
 * Datenreine Aufteilung der Stadt-Geometrie in World-Partition-Zellen.
 *
 * World Partition streamt auf Actor-Ebene: Ein einzelner Actor mit Riesen-
 * Bounds (alle Straessen/Gebaeude in drei Komponenten) landet in EINER Zelle
 * und wird als Ganzes geladen - das Streaming wuerde nichts bringen. Diese
 * Funktion teilt die Mesh-Sections nach dem Schwerpunkt ihrer Dreiecke auf
 * ein Grid (CellSizeCm) auf; je Zelle entsteht eine eigene Section pro
 * Kanal, deren Vertices neu indiziert werden (Index-Remap, damit geteilte
 * Kanten konsistent bleiben). Kanal/Surface/MaterialVariant/FacadeOverrideKey
 * bleiben erhalten, sodass die Material-Lookup-Pfade (ResolveRoadMaterial/
 * ResolveBuildingMaterial) unveraendert funktionieren.
 */
class WIESBADENREAL_API FWiesbadenCityChunking
{
public:
	/**
	 * Teilt die Mesh-Daten auf ein Grid auf. Zellgroesse in cm; 0/negativ
	 * liefert keine Ausgabe. Die Ausgabe ist deterministisch (Zell-Schluessel
	 * aufsteigend nach X, dann Y).
	 */
	static void BuildChunks(
		const FRoadMeshData& RoadMesh,
		const FBuildingMeshData& BuildingMesh,
		const FRegionAssetLayout& RegionAssets,
		double CellSizeCm,
		TMap<FIntPoint, FCityChunkMesh>& OutChunks);
};
