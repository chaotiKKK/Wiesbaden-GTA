// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GIS/WiesbadenPickupSpots.h"
#include "Vehicles/WiesbadenCar.h"
#include "Vehicles/WiesbadenFootPawn.h"
#include "World/WiesbadenPickup.h"
#include "World/WiesbadenPickupSpawnerComponent.h"

/**
 * Pickup-Spawner (Laufzeit): erzeugt Pickup-Actors an den Plaetzen des
 * Layouts und bindet die WIRKUNG - Treibstoff an das Fahrzeug (Refuel),
 * Gesundheit an den Fuss-Pawn (Heal).
 *
 * Der Wirkungsweg wird ueber den oeffentlichen Pfad gefahren:
 * SpawnFromLayout -> Collect() -> OnCollected -> HandlePickupCollected.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPickupSpawnerTest,
	"WiesbadenReal.Pickups.SpawnerAndEffects",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPickupSpawnerTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld*/ false);
	if (!TestNotNull(TEXT("Test-Welt erstellt"), World))
	{
		return false;
	}

	AActor* Owner = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("Besitzer-Actor gespawnt"), Owner))
	{
		World->DestroyWorld(false);
		return false;
	}

	UWiesbadenPickupSpawnerComponent* Spawner =
		NewObject<UWiesbadenPickupSpawnerComponent>(Owner);
	Spawner->RegisterComponent();

	// Layout mit einem Treibstoff- und einem Gesundheits-Spot.
	FWiesbadenPickupSpotLayout Layout;

	FWiesbadenPickupSpot FuelSpot;
	FuelSpot.Kind = EWiesbadenPickupSpotKind::Fuel;
	FuelSpot.Location = FVector(1000.0, 1000.0, 0.0);
	Layout.Spots.Add(FuelSpot);

	FWiesbadenPickupSpot HealthSpot;
	HealthSpot.Kind = EWiesbadenPickupSpotKind::Health;
	HealthSpot.Location = FVector(-1000.0, -1000.0, 0.0);
	Layout.Spots.Add(HealthSpot);

	Spawner->SpawnFromLayout(Layout);
	TestEqual(TEXT("Zwei Pickups gespawnt"), Spawner->GetPickupCount(), 2);

	// Gespawnte Pickup-Actors im Welt aufsuchen.
	AWiesbadenPickup* FuelPickup = nullptr;
	AWiesbadenPickup* HealthPickup = nullptr;
	for (TActorIterator<AWiesbadenPickup> It(World); It; ++It)
	{
		if (It->Kind == EWiesbadenPickupKind::Fuel)
		{
			FuelPickup = *It;
		}
		else if (It->Kind == EWiesbadenPickupKind::Health)
		{
			HealthPickup = *It;
		}
	}
	TestNotNull(TEXT("Treibstoff-Pickup gespawnt"), FuelPickup);
	TestNotNull(TEXT("Gesundheits-Pickup gespawnt"), HealthPickup);

	if (FuelPickup)
	{
		TestTrue(TEXT("Pickup schwebt ueber dem Spot (HoverHeight 120 cm)"),
			FuelPickup->GetActorLocation().Equals(FVector(1000.0, 1000.0, 120.0), 1.0));
	}

	// Sammler: Auto mit wenig Treibstoff, Fuss-Pawn mit Verletzung.
	AWiesbadenCar* Car = World->SpawnActor<AWiesbadenCar>(
		FVector(5000.0, 0.0, 0.0), FRotator::ZeroRotator);
	TestNotNull(TEXT("Auto gespawnt"), Car);
	if (Car)
	{
		Car->VehiclePhysics.FuelLiters = 5.0f;
	}

	AWiesbadenFootPawn* Foot = World->SpawnActor<AWiesbadenFootPawn>(
		FVector(6000.0, 0.0, 0.0), FRotator::ZeroRotator);
	TestNotNull(TEXT("Fuss-Pawn gespawnt"), Foot);
	if (Foot)
	{
		Foot->HealthPoints = 30.0f;
	}

	// Einsammeln: Collect -> OnCollected -> HandlePickupCollected -> Wirkung.
	if (FuelPickup && Car)
	{
		FuelPickup->Collect(Car);
		TestTrue(TEXT("Auto nach Treibstoff-Pickup getankt (5 + 20 l)"),
			FMath::IsNearlyEqual(Car->VehiclePhysics.FuelLiters, 25.0f, 0.01f));
	}

	if (HealthPickup && Foot)
	{
		HealthPickup->Collect(Foot);
		TestTrue(TEXT("Fuss-Pawn nach Gesundheits-Pickup geheilt (30 + 50)"),
			FMath::IsNearlyEqual(Foot->HealthPoints, 80.0f, 0.01f));
	}

	// Einsammeln zerstoert das Pickup (bDestroyOnCollect).
	if (FuelPickup)
	{
		TestTrue(TEXT("Eingesammeltes Pickup zur Zerstoerung markiert"),
			FuelPickup->IsActorBeingDestroyed());
	}

	// Refuel-Grenzfall: voller Tank akzeptiert keinen Treibstoff mehr.
	FWiesbadenVehiclePhysics Physics;
	Physics.FuelLiters = Physics.TankCapacityLiters;
	TestFalse(TEXT("Voller Tank lehnt Refuel ab"), Physics.Refuel(10.0f));
	TestTrue(TEXT("Tank bleibt nach abgelehntem Refuel voll"),
		FMath::IsNearlyEqual(Physics.FuelLiters, Physics.TankCapacityLiters, 0.01f));

	World->DestroyWorld(false);
	return true;
}