// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/WiesbadenPickupSpawnerComponent.h"

#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Vehicles/WiesbadenCar.h"
#include "Vehicles/WiesbadenFootPawn.h"
#include "WiesbadenReal.h"

UWiesbadenPickupSpawnerComponent::UWiesbadenPickupSpawnerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UWiesbadenPickupSpawnerComponent::BeginPlay()
{
	Super::BeginPlay();

	// Bewusst KEIN automatisches Spawnen hier: das Layout liegt an den
	// Stadt-Daten, die (im Laufzeit-Build) erst nach dem BeginPlay des
	// Actors fertig sind. Der CitySubsystem-Aufruf von SpawnFromLayout ist
	// der eine, deterministic Einstiegspunkt.
}

void UWiesbadenPickupSpawnerComponent::SpawnFromLayout(const FWiesbadenPickupSpotLayout& Layout)
{
	ClearPickups();

	UWorld* World = GetWorld();
	if (!World || Layout.Spots.Num() == 0)
	{
		return;
	}

	if (!PickupMesh)
	{
		// Engine-Kugel als Rueckfall - der Lauf darf nie am fehlenden Asset
		// scheitern (gleiche Haltung wie beim RegionAssetSpawner).
		PickupMesh = LoadObject<UStaticMesh>(nullptr,
			TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	}

	LivePickups.Reserve(Layout.Spots.Num());

	for (const FWiesbadenPickupSpot& Spot : Layout.Spots)
	{
		if (AWiesbadenPickup* Pickup = SpawnPickup(Spot))
		{
			LivePickups.Add(Pickup);
		}
	}

	UE_LOG(LogWbStreaming, Log, TEXT("Pickup-Spawner: %d Pickups erzeugt (%s)."),
		LivePickups.Num(), *Layout.GetStatisticsString());
}

void UWiesbadenPickupSpawnerComponent::ClearPickups()
{
	for (const TWeakObjectPtr<AWiesbadenPickup>& Weak : LivePickups)
	{
		if (AWiesbadenPickup* Pickup = Weak.Get())
		{
			Pickup->Destroy();
		}
	}
	LivePickups.Reset();
}

int32 UWiesbadenPickupSpawnerComponent::GetPickupCount() const
{
	int32 Count = 0;
	for (const TWeakObjectPtr<AWiesbadenPickup>& Weak : LivePickups)
	{
		if (Weak.IsValid())
		{
			++Count;
		}
	}
	return Count;
}

AWiesbadenPickup* UWiesbadenPickupSpawnerComponent::SpawnPickup(const FWiesbadenPickupSpot& Spot)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.Owner = GetOwner();
	Params.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AWiesbadenPickup* Pickup = World->SpawnActor<AWiesbadenPickup>(
		AWiesbadenPickup::StaticClass(),
		Spot.Location + FVector(0.0, 0.0, HoverHeightCm),
		FRotator::ZeroRotator, Params);
	if (!Pickup)
	{
		return nullptr;
	}

	Pickup->Kind = MapKind(Spot.Kind);
	Pickup->Amount = (Pickup->Kind == EWiesbadenPickupKind::Fuel)
		? FuelAmountLiters
		: HealthAmount;

	// Sichtbarkeit: Engine-Kugel, 50 cm Radius, in der Farbe der Art.
	if (UStaticMeshComponent* MeshComp = Pickup->GetStaticMeshComponent())
	{
		if (PickupMesh)
		{
			MeshComp->SetStaticMesh(PickupMesh);
			// Engine-Kugel misst 100 cm Kantenlaenge; der Trigger-Radius (80 cm)
			// soll das Sichtmesh voll abdecken.
			MeshComp->SetWorldScale3D(FVector(1.0));
		}
		// Farbkennung der Art (gleiche Technik wie die Nerobergbahn-Anzeige).
		if (UMaterialInstanceDynamic* Mid = MeshComp->CreateAndSetMaterialInstanceDynamic(0))
		{
			Mid->SetVectorParameterValue(TEXT("Color"),
				Pickup->Kind == EWiesbadenPickupKind::Fuel ? FuelColor : HealthColor);
		}
	}

	Pickup->OnCollected.AddDynamic(this, &UWiesbadenPickupSpawnerComponent::HandlePickupCollected);

	return Pickup;
}

void UWiesbadenPickupSpawnerComponent::HandlePickupCollected(
	AWiesbadenPickup* Pickup, AActor* Collector)
{
	if (!Pickup || !Collector)
	{
		return;
	}

	// Die Wirkung am SAMMLER: Fahrzeug tankt, Fuss-Pawn heilt. Andere
	// Sammler (NPC-Fahrzeug) lassen das Pickup unbeeinflusst - es ist dann
	// einsammelbar, aber wirkungslos.
	if (AWiesbadenCar* Car = Cast<AWiesbadenCar>(Collector))
	{
		if (Pickup->Kind == EWiesbadenPickupKind::Fuel)
		{
			const bool bRefueled = Car->VehiclePhysics.Refuel(
				static_cast<float>(Pickup->Amount));
			UE_LOG(LogWbVehicles, Log,
				TEXT("Pickup '%s' von %s aufgenommen: %s (%.1f/%.1f l)."),
				*Pickup->GetName(), *Collector->GetName(),
				bRefueled ? TEXT("getankt") : TEXT("Tank bereits voll"),
				Car->VehiclePhysics.FuelLiters,
				Car->VehiclePhysics.TankCapacityLiters);
		}
	}
	else if (AWiesbadenFootPawn* Foot = Cast<AWiesbadenFootPawn>(Collector))
	{
		if (Pickup->Kind == EWiesbadenPickupKind::Health)
		{
			const bool bHealed = Foot->Heal(static_cast<float>(Pickup->Amount));
			UE_LOG(LogWbVehicles, Log,
				TEXT("Pickup '%s' von %s aufgenommen: %s (%.0f/%.0f Punkte)."),
				*Pickup->GetName(), *Collector->GetName(),
				bHealed ? TEXT("geheilt") : TEXT("bereits bei voller Gesundheit"),
				Foot->HealthPoints, Foot->MaxHealthPoints);
		}
	}
}

EWiesbadenPickupKind UWiesbadenPickupSpawnerComponent::MapKind(EWiesbadenPickupSpotKind Kind)
{
	switch (Kind)
	{
	case EWiesbadenPickupSpotKind::Fuel:
		return EWiesbadenPickupKind::Fuel;
	case EWiesbadenPickupSpotKind::Health:
	default:
		return EWiesbadenPickupKind::Health;
	}
}
