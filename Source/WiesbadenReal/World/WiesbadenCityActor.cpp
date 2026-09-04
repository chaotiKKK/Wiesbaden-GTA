// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/WiesbadenCityActor.h"

#include "WiesbadenReal.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "World/RegionAssetSpawnerComponent.h"
#include "World/RoadFurnitureSpawnerComponent.h"
#include "World/PedestrianSpawnerComponent.h"
#include "World/TrafficVehicleSpawnerComponent.h"
#include "World/WiesbadenWeatherFX.h"

AWiesbadenCityActor::AWiesbadenCityActor()
{
	// Tickt fuer das Gebaeude-Distanz-Streaming (nur aktiv, wenn ApplyCityData den
	// Streaming-Pfad gewaehlt hat; der gebackene Traeger-Actor tickt effektiv leer).
	PrimaryActorTick.bCanEverTick = true;

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

	// Gebaeude-Streaming zuruecksetzen (Pool-Komponenten bleiben zur Wiederverwendung).
	for (UProceduralMeshComponent* C : BuildingCellPool)
	{
		if (C) { C->ClearAllMeshSections(); }
	}
	LoadedBuildingCells.Empty();
	FreeBuildingComponents.Reset();
	FreeBuildingComponents.Append(BuildingCellPool);
	BuildingCells.Empty();
	bBuildingStreamingActive = false;
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
	if (bStreamBuildings && Data.BuildingMesh.Sections.Num() > 0)
	{
		// Distanz-Streaming: die (nach Materialkanal zusammengelegte) Geometrie in
		// ein Zell-Raster re-einsortieren und nur den Ausschnitt um den Spieler
		// als Procedural-Mesh halten. Verhindert, dass alle ~119k Gebaeude
		// gleichzeitig resident sind (VRAM-/Lumen-Crash im Laufzeit-Build).
		BuildBuildingCells(Data.BuildingMesh);
		SetupBuildingCellPool(bCreateCollision);
		bBuildingStreamingActive = true;
		StreamTickAccumSeconds = 0.0f;

		// Startausschnitt sofort laden (Spieler startet nahe dem Georeferenz-Origin).
		FVector InitialView = GetActorLocation();
		if (UWorld* World = GetWorld())
		{
			if (APlayerController* PC = World->GetFirstPlayerController())
			{
				if (PC->PlayerCameraManager) { InitialView = PC->PlayerCameraManager->GetCameraLocation(); }
				else if (APawn* ViewPawn = PC->GetPawn()) { InitialView = ViewPawn->GetActorLocation(); }
			}
		}
		UpdateBuildingStreaming(InitialView);

		UE_LOG(LogWbCore, Log,
			TEXT("Gebaeude-Streaming aktiv: %d Rasterzellen a %.0f m, Radius %.0f m, %d sofort geladen."),
			BuildingCells.Num(), BuildingCellSizeCm / 100.0f, BuildingStreamRadiusCm / 100.0f, LoadedBuildingCells.Num());
	}
	else if (BuildingMesh && Data.BuildingMesh.Sections.Num() > 0)
	{
		// Nicht-Streaming-Fallback: alles auf die eine Gebaeude-Komponente.
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

void AWiesbadenCityActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bBuildingStreamingActive)
	{
		return;
	}

	// Nicht jedes Bild: das Nachladen ist bei ruhiger Bewegung selten noetig.
	StreamTickAccumSeconds += DeltaSeconds;
	if (StreamTickAccumSeconds < 0.25f)
	{
		return;
	}
	StreamTickAccumSeconds = 0.0f;

	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC)
	{
		return;
	}

	FVector ViewLocation;
	if (PC->PlayerCameraManager)
	{
		ViewLocation = PC->PlayerCameraManager->GetCameraLocation();
	}
	else if (APawn* ViewPawn = PC->GetPawn())
	{
		ViewLocation = ViewPawn->GetActorLocation();
	}
	else
	{
		return;
	}

	UpdateBuildingStreaming(ViewLocation);
}

void AWiesbadenCityActor::BuildBuildingCells(const FBuildingMeshData& BuildingMeshData)
{
	BuildingCells.Empty();
	const double CellSize = FMath::Max(1.0f, BuildingCellSizeCm);

	for (const FBuildingMeshSection& Src : BuildingMeshData.Sections)
	{
		if (Src.IsEmpty())
		{
			continue;
		}

		// Pro Zelle: alte Vertex-Indizes dieses Quell-Abschnitts -> neue Indizes.
		// Haelt die Vertex-Teilung INNERHALB der Zelle (statt jedes Dreieck zu
		// verdreifachen) -> die re-einsortierte Kopie bleibt ~so gross wie das
		// Original; nur an den Zellgrenzen werden wenige Vertices dupliziert.
		TMap<FIntPoint, TMap<int32, int32>> Remap;

		const int32 TriCount = Src.Triangles.Num();
		for (int32 t = 0; t + 2 < TriCount; t += 3)
		{
			const int32 Idx[3] = { Src.Triangles[t], Src.Triangles[t + 1], Src.Triangles[t + 2] };
			if (!Src.Vertices.IsValidIndex(Idx[0]) || !Src.Vertices.IsValidIndex(Idx[1]) || !Src.Vertices.IsValidIndex(Idx[2]))
			{
				continue;
			}

			const FVector Centroid = (Src.Vertices[Idx[0]] + Src.Vertices[Idx[1]] + Src.Vertices[Idx[2]]) / 3.0;
			const FIntPoint Cell(FMath::FloorToInt(Centroid.X / CellSize), FMath::FloorToInt(Centroid.Y / CellSize));

			// Ziel-Sub-Abschnitt der Zelle, gruppiert nach Kanal/Variante/Adresse.
			TArray<FBuildingMeshSection>& CellSubs = BuildingCells.FindOrAdd(Cell);
			FBuildingMeshSection* Dst = nullptr;
			for (FBuildingMeshSection& S : CellSubs)
			{
				if (S.Channel == Src.Channel && S.MaterialVariant == Src.MaterialVariant && S.FacadeOverrideKey == Src.FacadeOverrideKey)
				{
					Dst = &S;
					break;
				}
			}
			if (!Dst)
			{
				FBuildingMeshSection NewSub;
				NewSub.Channel = Src.Channel;
				NewSub.MaterialVariant = Src.MaterialVariant;
				NewSub.FacadeOverrideKey = Src.FacadeOverrideKey;
				Dst = &CellSubs[CellSubs.Add(MoveTemp(NewSub))];
			}

			TMap<int32, int32>& CellRemap = Remap.FindOrAdd(Cell);
			for (int32 k = 0; k < 3; ++k)
			{
				const int32 Old = Idx[k];
				int32 New;
				if (const int32* FoundNew = CellRemap.Find(Old))
				{
					New = *FoundNew;
				}
				else
				{
					New = Dst->Vertices.Num();
					Dst->Vertices.Add(Src.Vertices[Old]);
					Dst->Normals.Add(Src.Normals.IsValidIndex(Old) ? Src.Normals[Old] : FVector::UpVector);
					Dst->UVs.Add(Src.UVs.IsValidIndex(Old) ? Src.UVs[Old] : FVector2D::ZeroVector);
					Dst->VertexColors.Add(Src.VertexColors.IsValidIndex(Old) ? Src.VertexColors[Old] : FColor::White);
					Dst->Tangents.Add(Src.Tangents.IsValidIndex(Old) ? Src.Tangents[Old] : FProcMeshTangent());
					CellRemap.Add(Old, New);
				}
				Dst->Triangles.Add(New);
			}
		}
	}
}

void AWiesbadenCityActor::SetupBuildingCellPool(bool bCreateCollision)
{
	bBuildingCollision = bCreateCollision;

	// Pool gross genug fuer das quadratische Ladefenster + einen Randring, damit
	// beim Bewegen neue Zellen laden koennen, bevor alte entladen sind.
	const int32 R = FMath::Max(1, FMath::CeilToInt(BuildingStreamRadiusCm / FMath::Max(1.0f, BuildingCellSizeCm)));
	const int32 Side = 2 * (R + 1) + 1;
	const int32 Needed = Side * Side;

	FreeBuildingComponents.Reset();
	for (UProceduralMeshComponent* C : BuildingCellPool)
	{
		if (C)
		{
			C->ClearAllMeshSections();
			FreeBuildingComponents.Add(C);
		}
	}
	while (BuildingCellPool.Num() < Needed)
	{
		UProceduralMeshComponent* C = NewObject<UProceduralMeshComponent>(this);
		C->SetupAttachment(Root);
		// Fuer Hardware-Raytracing-Lumen (-WbLumenHW): die gestreamten Gebaeude
		// muessen in der RT-Szene liegen und indirektes Licht beeinflussen duerfen,
		// sonst traced Lumen ins Leere. Beides ist Default true - hier ausdruecklich
		// gesetzt, damit der Bounce-Pfad nicht an einer stillen Vorgabe scheitert.
		C->SetVisibleInRayTracing(true);
		C->SetAffectDynamicIndirectLighting(true);
		C->RegisterComponent();
		BuildingCellPool.Add(C);
		FreeBuildingComponents.Add(C);
	}
	LoadedBuildingCells.Empty();
}

void AWiesbadenCityActor::UpdateBuildingStreaming(const FVector& ViewLocation)
{
	if (!bBuildingStreamingActive)
	{
		return;
	}

	const double CellSize = FMath::Max(1.0f, BuildingCellSizeCm);
	const FIntPoint Center(FMath::FloorToInt(ViewLocation.X / CellSize), FMath::FloorToInt(ViewLocation.Y / CellSize));
	const int32 R = FMath::Max(1, FMath::CeilToInt(BuildingStreamRadiusCm / (float)CellSize));

	// Gewuenschte Zellen: quadratisches Fenster um den Spieler, sofern Geometrie da ist.
	TSet<FIntPoint> Desired;
	Desired.Reserve((2 * R + 1) * (2 * R + 1));
	for (int32 dy = -R; dy <= R; ++dy)
	{
		for (int32 dx = -R; dx <= R; ++dx)
		{
			const FIntPoint Cell(Center.X + dx, Center.Y + dy);
			if (BuildingCells.Contains(Cell))
			{
				Desired.Add(Cell);
			}
		}
	}

	// Nicht mehr gewuenschte Zellen entladen (gibt Pool-Komponenten frei).
	TArray<FIntPoint> ToUnload;
	for (const TPair<FIntPoint, UProceduralMeshComponent*>& P : LoadedBuildingCells)
	{
		if (!Desired.Contains(P.Key))
		{
			ToUnload.Add(P.Key);
		}
	}
	for (const FIntPoint& Cell : ToUnload)
	{
		UnloadBuildingCell(Cell);
	}

	// Neu gewuenschte Zellen laden.
	for (const FIntPoint& Cell : Desired)
	{
		if (!LoadedBuildingCells.Contains(Cell))
		{
			LoadBuildingCell(Cell);
		}
	}
}

void AWiesbadenCityActor::LoadBuildingCell(const FIntPoint& Cell)
{
	if (FreeBuildingComponents.Num() == 0)
	{
		return; // Pool erschoepft (bei korrekter Pool-Groesse nicht zu erwarten).
	}
	const TArray<FBuildingMeshSection>* Subs = BuildingCells.Find(Cell);
	if (!Subs)
	{
		return;
	}

	UProceduralMeshComponent* C = FreeBuildingComponents.Pop(EAllowShrinking::No);
	int32 SecIdx = 0;
	for (const FBuildingMeshSection& S : *Subs)
	{
		if (S.IsEmpty())
		{
			continue;
		}
		C->CreateMeshSection(SecIdx, S.Vertices, S.Triangles, S.Normals, S.UVs, S.VertexColors, S.Tangents, bBuildingCollision);
		if (UMaterialInterface* Material = ResolveBuildingMaterial(S.Channel, S.FacadeOverrideKey))
		{
			C->SetMaterial(SecIdx, Material);
		}
		++SecIdx;
	}
	C->SetCollisionEnabled(bBuildingCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	LoadedBuildingCells.Add(Cell, C);
}

void AWiesbadenCityActor::UnloadBuildingCell(const FIntPoint& Cell)
{
	UProceduralMeshComponent** Found = LoadedBuildingCells.Find(Cell);
	if (Found && *Found)
	{
		(*Found)->ClearAllMeshSections();
		FreeBuildingComponents.Add(*Found);
	}
	LoadedBuildingCells.Remove(Cell);
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
