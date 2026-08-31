// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "World/BuildingCollisionSpawnerComponent.h"
#include "World/TrafficVehicleSpawnerComponent.h"

/**
 * Auswahl der Fahrzeuge, die einen Kollisionskoerper bekommen.
 *
 * Die Verkehrsfahrzeuge werden als InstancedStaticMesh ohne Kollision
 * gezeichnet; kollidieren kann der Spieler nur mit den wenigen Koerpern, die
 * den naechstgelegenen Fahrzeugen nachgefuehrt werden. Waehlt diese Funktion
 * falsch, faehrt der Spieler durch Autos hindurch, die direkt vor ihm stehen -
 * ohne dass irgendetwas einen Fehler meldet.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrafficCollisionSelectionTest,
	"WiesbadenReal.Traffic.CollisionSelection",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
	FTrafficVehicle MakeVehicleAt(int32 Id, double X, double Y, double Z = 0.0)
	{
		FTrafficVehicle V;
		V.VehicleId = Id;
		V.Location = FVector(X, Y, Z);
		V.Forward = FVector(1.0, 0.0, 0.0);
		return V;
	}
}

bool FTrafficCollisionSelectionTest::RunTest(const FString& Parameters)
{
	// -- 1. Naechste zuerst -------------------------------------------------
	{
		TArray<FTrafficVehicle> Vehicles;
		Vehicles.Add(MakeVehicleAt(0, 5000.0, 0.0));   // 50 m
		Vehicles.Add(MakeVehicleAt(1, 1000.0, 0.0));   // 10 m
		Vehicles.Add(MakeVehicleAt(2, 3000.0, 0.0));   // 30 m

		TArray<int32> Result;
		UTrafficVehicleSpawnerComponent::SelectNearestVehicles(
			Vehicles, FVector::ZeroVector, 100000.0, 3, Result);

		TestEqual(TEXT("Alle drei ausgewaehlt"), Result.Num(), 3);
		if (Result.Num() == 3)
		{
			TestEqual(TEXT("Naechstes zuerst (10 m)"), Result[0], 1);
			TestEqual(TEXT("Dann 30 m"), Result[1], 2);
			TestEqual(TEXT("Dann 50 m"), Result[2], 0);
		}
	}

	// -- 2. Obergrenze ------------------------------------------------------
	// Mehr Fahrzeuge als Koerper: es duerfen nur die naechsten kommen.
	{
		TArray<FTrafficVehicle> Vehicles;
		for (int32 i = 0; i < 20; ++i)
		{
			Vehicles.Add(MakeVehicleAt(i, (i + 1) * 500.0, 0.0));
		}

		TArray<int32> Result;
		UTrafficVehicleSpawnerComponent::SelectNearestVehicles(
			Vehicles, FVector::ZeroVector, 1000000.0, 5, Result);

		TestEqual(TEXT("Genau fuenf Koerper vergeben"), Result.Num(), 5);
		if (Result.Num() == 5)
		{
			TestEqual(TEXT("Es sind die fuenf naechsten"), Result[0], 0);
			TestEqual(TEXT("Fuenftnaechstes"), Result[4], 4);
		}
	}

	// -- 3. Radius ----------------------------------------------------------
	{
		TArray<FTrafficVehicle> Vehicles;
		Vehicles.Add(MakeVehicleAt(0, 1000.0, 0.0));    // 10 m - drin
		Vehicles.Add(MakeVehicleAt(1, 900000.0, 0.0));  // 9 km - draussen

		TArray<int32> Result;
		UTrafficVehicleSpawnerComponent::SelectNearestVehicles(
			Vehicles, FVector::ZeroVector, 6000.0, 10, Result);

		TestEqual(TEXT("Nur das Fahrzeug im Radius"), Result.Num(), 1);
		if (Result.Num() == 1)
		{
			TestEqual(TEXT("Es ist das nahe"), Result[0], 0);
		}
	}

	// -- 4. Hoehenunterschied darf nicht ausschliessen ----------------------
	// Ein Fahrzeug 100 m hoeher, aber in der Draufsicht direkt daneben, steht
	// auf einer Bruecke oder am Hang - der Spieler kann es beruehren. Bei
	// raeumlicher Messung fiele es aus dem Radius.
	{
		TArray<FTrafficVehicle> Vehicles;
		Vehicles.Add(MakeVehicleAt(0, 500.0, 0.0, 11300.0)); // 5 m daneben, 113 m hoeher

		TArray<int32> Result;
		UTrafficVehicleSpawnerComponent::SelectNearestVehicles(
			Vehicles, FVector::ZeroVector, 6000.0, 10, Result);

		TestEqual(TEXT("Hoehenversatz schliesst nicht aus (horizontal gemessen)"), Result.Num(), 1);
	}

	// -- 5. Entfernte Fahrzeuge ueberspringen -------------------------------
	{
		TArray<FTrafficVehicle> Vehicles;
		FTrafficVehicle Removed = MakeVehicleAt(0, 100.0, 0.0);
		Removed.bRemoved = true;
		Vehicles.Add(Removed);
		Vehicles.Add(MakeVehicleAt(1, 2000.0, 0.0));

		TArray<int32> Result;
		UTrafficVehicleSpawnerComponent::SelectNearestVehicles(
			Vehicles, FVector::ZeroVector, 100000.0, 10, Result);

		TestEqual(TEXT("Entferntes Fahrzeug bekommt keinen Koerper"), Result.Num(), 1);
		if (Result.Num() == 1)
		{
			TestEqual(TEXT("Nur das aktive"), Result[0], 1);
		}
	}

	// -- 6. Stabile Reihenfolge --------------------------------------------
	// Bei gleichem Abstand entscheidet der Index. Ohne diese Festlegung
	// koennten die Koerper zwischen den Frames die Fahrzeuge tauschen und
	// sichtbar springen.
	{
		TArray<FTrafficVehicle> Vehicles;
		Vehicles.Add(MakeVehicleAt(0, 1000.0, 0.0));
		Vehicles.Add(MakeVehicleAt(1, -1000.0, 0.0));
		Vehicles.Add(MakeVehicleAt(2, 0.0, 1000.0));

		TArray<int32> First;
		TArray<int32> Second;
		UTrafficVehicleSpawnerComponent::SelectNearestVehicles(
			Vehicles, FVector::ZeroVector, 100000.0, 3, First);
		UTrafficVehicleSpawnerComponent::SelectNearestVehicles(
			Vehicles, FVector::ZeroVector, 100000.0, 3, Second);

		TestTrue(TEXT("Gleiche Eingabe liefert gleiche Reihenfolge"), First == Second);
		TestEqual(TEXT("Bei gleichem Abstand entscheidet der Index"), First[0], 0);
	}

	// -- 7. Randfaelle ------------------------------------------------------
	{
		TArray<FTrafficVehicle> Empty;
		TArray<int32> Result;

		UTrafficVehicleSpawnerComponent::SelectNearestVehicles(
			Empty, FVector::ZeroVector, 10000.0, 5, Result);
		TestEqual(TEXT("Keine Fahrzeuge: keine Auswahl"), Result.Num(), 0);

		TArray<FTrafficVehicle> One;
		One.Add(MakeVehicleAt(0, 100.0, 0.0));

		UTrafficVehicleSpawnerComponent::SelectNearestVehicles(
			One, FVector::ZeroVector, 10000.0, 0, Result);
		TestEqual(TEXT("Null Koerper: keine Auswahl"), Result.Num(), 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBuildingCollisionSelectionTest,
	"WiesbadenReal.World.BuildingCollisionSelection",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Auswahl der Gebaeude, die Kollisionskoerper bekommen.
 *
 * Hintergrund: Die Stadt-Meshes werden ohne Kollision gebacken - der Spieler
 * fuhr durch die Haeuser. Ein fester Pool von Box-Koerpern folgt ihm; welche
 * Gebaeude ihn belegen, entscheidet diese Auswahl.
 *
 * Gemessen wird HORIZONTAL: ein Gebaeude den Hang hinauf ist fuer den Fahrer
 * genauso nah wie eines auf gleicher Hoehe. Wuerde die Hoehe mitzaehlen, bekaeme
 * ausgerechnet die Hangbebauung keine Kollision.
 */
bool FBuildingCollisionSelectionTest::RunTest(const FString& Parameters)
{
	auto MakeBuilding = [](double X, double Y, double Z)
	{
		FGeneratedBuilding Building;
		Building.Centroid = FVector(X, Y, Z);
		Building.Bounds = FBox(FVector(X - 500.0, Y - 500.0, Z), FVector(X + 500.0, Y + 500.0, Z + 1000.0));
		return Building;
	};

	TArray<FGeneratedBuilding> Buildings;
	Buildings.Add(MakeBuilding(1000.0, 0.0, 0.0));        // 10 m
	Buildings.Add(MakeBuilding(3000.0, 0.0, 0.0));        // 30 m
	Buildings.Add(MakeBuilding(2000.0, 0.0, 50000.0));    // 20 m horizontal, 500 m hoeher
	Buildings.Add(MakeBuilding(90000.0, 0.0, 0.0));       // 900 m - ausserhalb

	TArray<int32> Selected;
	UBuildingCollisionSpawnerComponent::SelectNearestBuildings(
		Buildings, FVector::ZeroVector, /*RadiusCm=*/50000.0, /*MaxCount=*/3, Selected);

	TestEqual(TEXT("Drei Gebaeude im Radius"), Selected.Num(), 3);
	TestEqual(TEXT("Naechstes zuerst"), Selected[0], 0);

	// Das hoch gelegene Gebaeude steht horizontal an zweiter Stelle und muss
	// vor dem weiter entfernten kommen - trotz 500 m Hoehenunterschied.
	TestEqual(TEXT("Hanglage zaehlt horizontal"), Selected[1], 2);
	TestEqual(TEXT("Weiter entferntes danach"), Selected[2], 1);

	// Ein leerer Grundriss darf keinen Koerper belegen: ein Koerper ohne
	// Ausdehnung waere eine unsichtbare Wand an falscher Stelle.
	FGeneratedBuilding Invalid;
	Invalid.Centroid = FVector(100.0, 0.0, 0.0);
	Invalid.Bounds = FBox(ForceInit);

	TArray<FGeneratedBuilding> WithInvalid;
	WithInvalid.Add(Invalid);
	WithInvalid.Add(MakeBuilding(2000.0, 0.0, 0.0));

	UBuildingCollisionSpawnerComponent::SelectNearestBuildings(
		WithInvalid, FVector::ZeroVector, 50000.0, 5, Selected);

	TestEqual(TEXT("Ungueltiger Grundriss uebersprungen"), Selected.Num(), 1);
	TestEqual(TEXT("Gueltiger uebernommen"), Selected[0], 1);

	return true;
}
