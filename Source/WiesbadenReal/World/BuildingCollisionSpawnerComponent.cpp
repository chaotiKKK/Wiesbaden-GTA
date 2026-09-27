// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/BuildingCollisionSpawnerComponent.h"

#include "WiesbadenReal.h"

#include "Components/BoxComponent.h"
#include "GIS/RoadNetworkTypes.h"

const FName UBuildingCollisionSpawnerComponent::BuildingBodyTag(TEXT("WbGebaeudeKollision"));

UBuildingCollisionSpawnerComponent::UBuildingCollisionSpawnerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UBuildingCollisionSpawnerComponent::SetBuildings(const TArray<FGeneratedBuilding>& InBuildings)
{
	Buildings = InBuildings;
}

UBuildingCollisionSpawnerComponent::EBoxClip UBuildingCollisionSpawnerComponent::ClipBoxAgainstPoints(
	FVector2D& Center, FVector2D& Extent, float YawDegrees,
	const TArray<FVector2D>& Points, double MarginCm, double MinKeepFraction)
{
	const double Yaw = FMath::DegreesToRadians((double)YawDegrees);
	const FVector2D U(FMath::Cos(Yaw), FMath::Sin(Yaw));
	const FVector2D V(-U.Y, U.X);
	// Lage der Punkte im Kasten (A entlang U, B entlang V), nur die im Kasten
	// samt Rand. Ihr Band wird herausgeschnitten.
	double MinA = TNumericLimits<double>::Max(), MaxA = -TNumericLimits<double>::Max();
	double MinB = MinA, MaxB = MaxA;
	int32 Inside = 0;
	for (const FVector2D& P : Points)
	{
		const FVector2D D = P - Center;
		const double A = FVector2D::DotProduct(D, U);
		const double B = FVector2D::DotProduct(D, V);
		if (FMath::Abs(A) >= Extent.X + MarginCm || FMath::Abs(B) >= Extent.Y + MarginCm) { continue; }
		++Inside;
		MinA = FMath::Min(MinA, A); MaxA = FMath::Max(MaxA, A);
		MinB = FMath::Min(MinB, B); MaxB = FMath::Max(MaxB, B);
	}
	if (Inside == 0) { return EBoxClip::Untouched; }

	// Vier Moeglichkeiten: die Seite jenseits des Strassenbands behalten.
	struct FCut { double ALo, AHi, BLo, BHi; };
	const FCut Cuts[4] = {
		{ -Extent.X, MinA - MarginCm, -Extent.Y, Extent.Y },   // Strasse an +U
		{ MaxA + MarginCm, Extent.X, -Extent.Y, Extent.Y },    // Strasse an -U
		{ -Extent.X, Extent.X, -Extent.Y, MinB - MarginCm },   // Strasse an +V
		{ -Extent.X, Extent.X, MaxB + MarginCm, Extent.Y },    // Strasse an -V
	};
	int32 Best = INDEX_NONE;
	double BestArea = 0.0;
	for (int32 i = 0; i < 4; ++i)
	{
		const double LA = Cuts[i].AHi - Cuts[i].ALo;
		const double LB = Cuts[i].BHi - Cuts[i].BLo;
		if (LA <= 0.0 || LB <= 0.0) { continue; }
		if (LA * LB > BestArea) { BestArea = LA * LB; Best = i; }
	}
	const double Full = 4.0 * Extent.X * Extent.Y;
	if (Best == INDEX_NONE || BestArea < MinKeepFraction * Full) { return EBoxClip::Removed; }
	const FCut& C = Cuts[Best];
	const double CA = (C.ALo + C.AHi) * 0.5;
	const double CB = (C.BLo + C.BHi) * 0.5;
	Center += U * CA + V * CB;
	Extent = FVector2D((C.AHi - C.ALo) * 0.5, (C.BHi - C.BLo) * 0.5);
	return EBoxClip::Clipped;
}

void UBuildingCollisionSpawnerComponent::ClipAgainstRoads(const FRoadNetwork& Network)
{
	ClippedCount = RemovedCount = 0;
	if (Buildings.Num() == 0 || Network.Lanes.Num() == 0) { return; }

	// Spurpunkte (alle 2 m) im 50-m-Raster. Nur Fahrbahnen bis living_street:
	// Erschliessungswege laufen durchaus durch Gebaeude (Parkhaus-Rampen,
	// Hofdurchfahrten) - an ihnen wuerde sonst ein ganzes Haus aufgeschnitten.
	const double CellCm = 5000.0;
	auto Key = [CellCm](double X, double Y)
	{
		return ((int64)FMath::FloorToDouble(X / CellCm) << 32) ^ ((int64)FMath::FloorToDouble(Y / CellCm) & 0xffffffffLL);
	};
	TMap<int64, TArray<FVector3f>> Grid;
	for (const FRoadLane& Lane : Network.Lanes)
	{
		if (!Network.Segments.IsValidIndex(Lane.SegmentId)) { continue; }
		const FRoadSegment& Seg = Network.Segments[Lane.SegmentId];
		if (Seg.bIsArea || (uint8)Seg.HighwayType >= (uint8)EOSMHighwayType::Service) { continue; }
		const TArray<FVector>& C = Lane.Centerline;
		for (int32 i = 0; i + 1 < C.Num(); ++i)
		{
			const int32 Steps = FMath::Max(1, FMath::CeilToInt(FVector::Dist2D(C[i], C[i + 1]) / 200.0));
			for (int32 s = 0; s < Steps; ++s)
			{
				const FVector P = FMath::Lerp(C[i], C[i + 1], (double)s / (double)Steps);
				Grid.FindOrAdd(Key(P.X, P.Y)).Add(FVector3f(P));
			}
		}
	}

	TArray<FVector2D> Near;
	TArray<FString> Examples;
	for (FGeneratedBuilding& B : Buildings)
	{
		if (!B.Bounds.IsValid || B.FootprintExtentCm.X <= 1.0 || B.FootprintExtentCm.Y <= 1.0) { continue; }
		const double R = B.FootprintExtentCm.Size() + 400.0;
		const double FootZ = B.Bounds.Min.Z;
		Near.Reset();
		for (double X = B.FootprintCenterCm.X - R; X <= B.FootprintCenterCm.X + R + CellCm; X += CellCm)
		{
			for (double Y = B.FootprintCenterCm.Y - R; Y <= B.FootprintCenterCm.Y + R + CellCm; Y += CellCm)
			{
				const TArray<FVector3f>* Cell = Grid.Find(Key(X, Y));
				if (!Cell) { continue; }
				for (const FVector3f& P : *Cell)
				{
					// Nur Spuren auf Hoehe des Gebaeudefusses (keine Bruecke darueber,
					// kein Tunnel darunter).
					if (P.Z < FootZ - 300.0 || P.Z > FootZ + 800.0) { continue; }
					Near.Add(FVector2D(P.X, P.Y));
				}
			}
		}
		if (Near.Num() == 0) { continue; }
		// Rand: halbe Spur + gut ein Meter - der Kasten endet an der Bordsteinkante.
		const EBoxClip Result = ClipBoxAgainstPoints(B.FootprintCenterCm, B.FootprintExtentCm,
			B.FootprintYawDegrees, Near, 300.0, 0.3);
		if (Result == EBoxClip::Clipped) { ++ClippedCount; }
		if (Result == EBoxClip::Removed)
		{
			++RemovedCount;
			B.Bounds = FBox(ForceInit);   // SelectNearestBuildings ueberspringt ungueltige Bounds
		}
		if (Result != EBoxClip::Untouched && Examples.Num() < 12 && !B.BuildingName.IsEmpty())
		{
			Examples.Add(FString::Printf(TEXT("%s (%s)"), *B.BuildingName,
				Result == EBoxClip::Clipped ? TEXT("gekuerzt") : TEXT("ohne Kasten")));
		}
	}
	UE_LOG(LogWbCore, Log,
		TEXT("Gebaeude-Kollision: %d Kaesten an Fahrspuren gekuerzt, %d ohne Kasten (Strasse unter/durch das Gebaeude)%s%s."),
		ClippedCount, RemovedCount, Examples.Num() > 0 ? TEXT(", u. a. ") : TEXT(""), *FString::Join(Examples, TEXT(", ")));
}

void UBuildingCollisionSpawnerComponent::SelectNearestBuildings(
	const TArray<FGeneratedBuilding>& Buildings,
	const FVector& Center,
	double RadiusCm,
	int32 MaxCount,
	TArray<int32>& OutIndices)
{
	OutIndices.Reset();
	if (MaxCount <= 0 || RadiusCm <= 0.0)
	{
		return;
	}

	// Index samt Abstand sammeln, dann sortieren. Bei einigen Zehntausend
	// Gebaeuden ist das guenstiger als ein Sortieren der Gebaeude selbst
	// (FGeneratedBuilding traegt Strings und waere teuer zu kopieren).
	struct FCandidate
	{
		int32 Index;
		double DistanceSq;
	};

	TArray<FCandidate> Candidates;
	const double RadiusSq = RadiusCm * RadiusCm;

	for (int32 Index = 0; Index < Buildings.Num(); ++Index)
	{
		const FGeneratedBuilding& Building = Buildings[Index];
		if (!Building.Bounds.IsValid)
		{
			continue;
		}

		// Horizontal messen - die Hoehe darf nicht darueber entscheiden, ob ein
		// Gebaeude am Hang Kollision bekommt.
		const double Dx = Building.Centroid.X - Center.X;
		const double Dy = Building.Centroid.Y - Center.Y;
		const double DistanceSq = Dx * Dx + Dy * Dy;

		if (DistanceSq <= RadiusSq)
		{
			Candidates.Add({ Index, DistanceSq });
		}
	}

	Candidates.Sort([](const FCandidate& A, const FCandidate& B)
	{
		return A.DistanceSq < B.DistanceSq;
	});

	const int32 Count = FMath::Min(MaxCount, Candidates.Num());
	OutIndices.Reserve(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		OutIndices.Add(Candidates[Index].Index);
	}
}

void UBuildingCollisionSpawnerComponent::EnsurePool()
{
	if (Bodies.Num() == BodyCount)
	{
		return;
	}

	for (UBoxComponent* Body : Bodies)
	{
		if (Body)
		{
			Body->DestroyComponent();
		}
	}
	Bodies.Reset();

	for (int32 Index = 0; Index < BodyCount; ++Index)
	{
		UBoxComponent* Body = NewObject<UBoxComponent>(GetOwner());
		if (!Body)
		{
			continue;
		}

		Body->SetupAttachment(this);
		Body->RegisterComponent();

		// Nur blockieren, nicht zeichnen: der Koerper liegt in der sichtbaren
		// Gebaeudegeometrie und darf sie nicht ueberdecken.
		Body->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Body->SetCollisionObjectType(ECC_WorldStatic);
		Body->SetCollisionResponseToAllChannels(ECR_Block);
		Body->SetHiddenInGame(true);
		Body->SetVisibility(false);
		Body->SetMobility(EComponentMobility::Movable);

		// Kennzeichnen: der Verkehr haelt ebenfalls Box-Koerper bereit. Ohne
		// Marke laesst sich in einer Diagnose nicht unterscheiden, ob ein
		// Treffer von einem Haus oder von einem Auto stammt.
		Body->ComponentTags.Add(BuildingBodyTag);

		Bodies.Add(Body);
	}

	UE_LOG(LogWbCore, Log,
		TEXT("Gebaeude-Kollision: Pool mit %d Koerpern angelegt (Radius %.0f m)."),
		Bodies.Num(), CollisionRadiusMeters);
}

void UBuildingCollisionSpawnerComponent::UpdateAround(const FVector& Observer)
{
	if (Buildings.Num() == 0)
	{
		return;
	}

	EnsurePool();

	TArray<int32> Selected;
	SelectNearestBuildings(Buildings, Observer, CollisionRadiusMeters * 100.0, Bodies.Num(), Selected);

	ActiveBodyCount = 0;

	for (int32 Slot = 0; Slot < Bodies.Num(); ++Slot)
	{
		UBoxComponent* Body = Bodies[Slot];
		if (!Body)
		{
			continue;
		}

		if (!Selected.IsValidIndex(Slot))
		{
			// Ueberzaehlige Koerper abschalten statt sie irgendwo stehen zu
			// lassen - ein vergessener Koerper waere eine unsichtbare Wand.
			Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			continue;
		}

		const FGeneratedBuilding& Building = Buildings[Selected[Slot]];
		const FBox& BuildingBox = Building.Bounds;

		Body->SetCollisionEnabled(ECollisionEnabled::QueryOnly);

		if (Building.FootprintExtentCm.X > 1.0 && Building.FootprintExtentCm.Y > 1.0)
		{
			// Gedrehte Grundriss-Box: fuer rechteckige Gebaeude exakt. Die
			// achsparallele Variante deckte im Mittel das 2,1-fache ab und
			// haette Fahrbahnen blockiert.
			Body->SetWorldLocation(FVector(
				Building.FootprintCenterCm.X,
				Building.FootprintCenterCm.Y,
				BuildingBox.GetCenter().Z));
			Body->SetWorldRotation(FRotator(0.0f, Building.FootprintYawDegrees, 0.0f));
			Body->SetBoxExtent(FVector(
				Building.FootprintExtentCm.X,
				Building.FootprintExtentCm.Y,
				BuildingBox.GetExtent().Z), /*bUpdateOverlaps=*/false);
		}
		else
		{
			// Rueckfall fuer Gebaeude aus einem aelteren Build, die die
			// gedrehte Box noch nicht mitfuehren.
			Body->SetWorldLocation(BuildingBox.GetCenter());
			Body->SetWorldRotation(FRotator::ZeroRotator);
			Body->SetBoxExtent(BuildingBox.GetExtent(), /*bUpdateOverlaps=*/false);
		}
		++ActiveBodyCount;
	}
}
