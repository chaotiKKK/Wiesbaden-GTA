// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/WiesbadenCityActor.h"

#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "World/RegionAssetSpawnerComponent.h"
#include "World/RoadFurnitureSpawnerComponent.h"
#include "World/PedestrianSpawnerComponent.h"
#include "World/TrafficVehicleSpawnerComponent.h"
#include "World/WiesbadenWeatherFX.h"

AWiesbadenCityActor::AWiesbadenCityActor()
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	RoadMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("RoadMesh"));
	RoadMesh->SetupAttachment(Root);

	BuildingMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("BuildingMesh"));
	BuildingMesh->SetupAttachment(Root);

	TerrainMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("TerrainMesh"));
	TerrainMesh->SetupAttachment(Root);

	FurnitureSpawner = CreateDefaultSubobject<URoadFurnitureSpawnerComponent>(TEXT("FurnitureSpawner"));
	FurnitureSpawner->SetupAttachment(Root);

	WeatherFX = CreateDefaultSubobject<UWiesbadenWeatherFXComponent>(TEXT("WeatherFX"));
	WeatherFX->SetComponentTickEnabled(true);

	RegionAssetSpawner = CreateDefaultSubobject<URegionAssetSpawnerComponent>(TEXT("RegionAssetSpawner"));
	RegionAssetSpawner->SetupAttachment(Root);

	TrafficVehicleSpawner = CreateDefaultSubobject<UTrafficVehicleSpawnerComponent>(TEXT("TrafficVehicleSpawner"));
	TrafficVehicleSpawner->SetupAttachment(Root);

	PedestrianSpawner = CreateDefaultSubobject<UPedestrianSpawnerComponent>(TEXT("PedestrianSpawner"));
	PedestrianSpawner->SetupAttachment(Root);
}

void AWiesbadenCityActor::UpdateTrafficVehicles(const TArray<FTrafficVehicle>& Vehicles)
{
	if (TrafficVehicleSpawner)
	{
		TrafficVehicleSpawner->UpdateVehicles(Vehicles);
	}
}

int32 AWiesbadenCityActor::GetVisibleTrafficVehicleCount() const
{
	return TrafficVehicleSpawner ? TrafficVehicleSpawner->LastVisibleVehicleCount : 0;
}

void AWiesbadenCityActor::UpdatePedestrians(const TArray<FPlacedPedestrian>& Placed)
{
	if (PedestrianSpawner)
	{
		PedestrianSpawner->UpdateInstances(Placed);
	}
}

int32 AWiesbadenCityActor::GetVisiblePedestrianCount() const
{
	return PedestrianSpawner ? PedestrianSpawner->GetVisibleCount() : 0;
}

void AWiesbadenCityActor::ClearMeshes()
{
	if (RoadMesh) { RoadMesh->ClearAllMeshSections(); }
	if (BuildingMesh) { BuildingMesh->ClearAllMeshSections(); }
	if (TerrainMesh) { TerrainMesh->ClearAllMeshSections(); }
	if (FurnitureSpawner) { FurnitureSpawner->ClearFurniture(); }
	if (TrafficVehicleSpawner) { TrafficVehicleSpawner->ClearVehicles(); }
}

void AWiesbadenCityActor::ApplyCityData(const FWiesbadenCityData& Data, bool bCreateCollision)
{
	ClearMeshes();

	// -- Strassen -----------------------------------------------------------
	if (RoadMesh && Data.RoadMesh.Sections.Num() > 0)
	{
		int32 SectionIndex = 0;
		for (const FRoadMeshSection& Section : Data.RoadMesh.Sections)
		{
			if (Section.IsEmpty())
			{
				continue;
			}

			RoadMesh->CreateMeshSection(
				SectionIndex,
				Section.Vertices,
				Section.Triangles,
				Section.Normals,
				Section.UVs,
				Section.VertexColors,
				Section.Tangents,
				bCreateCollision);

			if (UMaterialInterface* Material = ResolveRoadMaterial(Section.Channel))
			{
				RoadMesh->SetMaterial(SectionIndex, Material);
			}

			++SectionIndex;
		}

		RoadMesh->SetCollisionEnabled(
			bCreateCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	}

	// -- Gebaeude -----------------------------------------------------------
	if (BuildingMesh && Data.BuildingMesh.Sections.Num() > 0)
	{
		int32 SectionIndex = 0;
		for (const FBuildingMeshSection& Section : Data.BuildingMesh.Sections)
		{
			if (Section.IsEmpty())
			{
				continue;
			}

			BuildingMesh->CreateMeshSection(
				SectionIndex,
				Section.Vertices,
				Section.Triangles,
				Section.Normals,
				Section.UVs,
				Section.VertexColors,
				Section.Tangents,
				bCreateCollision);

			if (UMaterialInterface* Material = ResolveBuildingMaterial(Section.Channel, Section.FacadeOverrideKey))
			{
				BuildingMesh->SetMaterial(SectionIndex, Material);
			}

			++SectionIndex;
		}

		BuildingMesh->SetCollisionEnabled(
			bCreateCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	}

	// -- Terrain-Vorschau ---------------------------------------------------
	if (TerrainMesh && Data.TerrainTile.IsValid())
	{
		BuildTerrainPreview(Data.TerrainTile, TerrainPreviewGridSize);
	}

	// -- Strassenausstattung (Schilder, Leitpfosten, Markierungen) ----------
	if (FurnitureSpawner
		&& (Data.FurnitureLayout.Signs.Num() > 0
			|| Data.FurnitureLayout.Delineators.Num() > 0
			|| Data.FurnitureLayout.Markings.Num() > 0))
	{
		FurnitureSpawner->SignTextureFolder = SignTextureFolder;
		FurnitureSpawner->SignMaterial = SignMaterial;
		FurnitureSpawner->SpawnFurniture(Data.FurnitureLayout);
	}

	// -- Regionen-abhaengige Assets (Baeume, Ufer, Industrie) ------------------
	if (RegionAssetSpawner && Data.RegionAssetLayout.Assets.Num() > 0)
	{
		RegionAssetSpawner->SpawnRegionAssets(Data.RegionAssetLayout);
	}
}

UMaterialInterface* AWiesbadenCityActor::ResolveRoadMaterial(ERoadMeshChannel Channel) const
{
	switch (Channel)
	{
	case ERoadMeshChannel::Sidewalk:
	case ERoadMeshChannel::Crossing:
		return SidewalkMaterial ? SidewalkMaterial : RoadMaterial;
	case ERoadMeshChannel::Carriageway:
	case ERoadMeshChannel::Kerb:
	case ERoadMeshChannel::Intersection:
	case ERoadMeshChannel::LaneMarking:
	case ERoadMeshChannel::Cycleway:
	default:
		return RoadMaterial;
	}
}

UMaterialInterface* AWiesbadenCityActor::ResolveBuildingMaterial(EBuildingMeshChannel Channel, const FString& FacadeOverrideKey)
{
	if (Channel == EBuildingMeshChannel::Roof)
	{
		return BuildingRoofMaterial ? BuildingRoofMaterial : BuildingWallMaterial;
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

	return BuildingWallMaterial;
}

void AWiesbadenCityActor::BuildTerrainPreview(const FTerrainTile& Tile, int32 GridSize)
{
	if (!TerrainMesh || !Tile.IsValid() || GridSize < 2)
	{
		return;
	}

	const int32 N = FMath::Clamp(GridSize, 2, 2049);
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
				-0.5 * (Right - Left),
				-0.5 * (Down - Up),
				1.0).GetSafeNormal();
			Normals.Add(Normal);
		}
	}

	// Zwei Dreiecke je Quad, gegen den Uhrzeigersinn (UE-Konvention, +X Ost, +Y Sued).
	for (int32 Y = 0; Y < N - 1; ++Y)
	{
		for (int32 X = 0; X < N - 1; ++X)
		{
			const int32 A = Y * N + X;
			const int32 B = A + 1;
			const int32 C = A + N;
			const int32 D = C + 1;

			Triangles.Add(A);
			Triangles.Add(C);
			Triangles.Add(B);

			Triangles.Add(B);
			Triangles.Add(C);
			Triangles.Add(D);
		}
	}

	TerrainMesh->ClearAllMeshSections();
	TerrainMesh->CreateMeshSection(
		0, Vertices, Triangles, Normals, UVs, TArray<FColor>(), TArray<FProcMeshTangent>(), /*bCreateCollision=*/false);

	if (TerrainMaterial)
	{
		TerrainMesh->SetMaterial(0, TerrainMaterial);
	}

	TerrainMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}
