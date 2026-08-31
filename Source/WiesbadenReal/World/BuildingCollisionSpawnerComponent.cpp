// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/BuildingCollisionSpawnerComponent.h"

#include "WiesbadenReal.h"

#include "Components/BoxComponent.h"

const FName UBuildingCollisionSpawnerComponent::BuildingBodyTag(TEXT("WbGebaeudeKollision"));

UBuildingCollisionSpawnerComponent::UBuildingCollisionSpawnerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UBuildingCollisionSpawnerComponent::SetBuildings(const TArray<FGeneratedBuilding>& InBuildings)
{
	Buildings = InBuildings;
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
