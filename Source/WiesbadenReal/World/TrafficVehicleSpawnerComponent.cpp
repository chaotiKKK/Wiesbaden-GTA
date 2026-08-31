// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/TrafficVehicleSpawnerComponent.h"

#include "WiesbadenReal.h"

#include "World/WiesbadenStreamingSource.h"

#include "Components/BoxComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"

UTrafficVehicleSpawnerComponent::UTrafficVehicleSpawnerComponent()
{
	// Instanzen werden von aussen (Subsystem/CityActor) aktualisiert; der
	// Tick haelt die Positionen der letzten Platzierung bei - kein Eigen-Tick
	// noetig, solange der Aufrufer UpdateVehicles pro Simulations-Tick ruft.
	PrimaryComponentTick.bCanEverTick = false;

	// Default-Palette: gaengige Fahrzeugfarben (deterministisch zugeordnet).
	ColorPalette = {
		FLinearColor(0.72f, 0.72f, 0.72f), // Silber
		FLinearColor(0.85f, 0.15f, 0.15f), // Rot
		FLinearColor(0.10f, 0.10f, 0.45f), // Dunkelblau
		FLinearColor(0.95f, 0.95f, 0.95f), // Weiss
		FLinearColor(0.10f, 0.10f, 0.10f), // Schwarz
		FLinearColor(0.20f, 0.55f, 0.20f), // Gruen
	};

	// Verkehrsfahrzeug: VW Kaefer 1969 als Platzhalter, in reduzierter
	// Aufloesung (rund 10.000 Dreiecke statt 172.000). Der Verkehr wird als
	// InstancedStaticMesh gezeichnet - bei mehreren hundert gleichzeitig
	// sichtbaren Fahrzeugen entscheidet die Dreieckszahl je Instanz ueber die
	// Bildrate, waehrend der Detailgewinn aus Fahrerperspektive gering ist.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> TrafficBeetle(
		TEXT("/Game/Vehicles/Beetle/SM_VWBeetle1969_Traffic.SM_VWBeetle1969_Traffic"));

	if (TrafficBeetle.Succeeded())
	{
		VehicleMesh = TrafficBeetle.Object;
	}
}

void UTrafficVehicleSpawnerComponent::BeginPlay()
{
	Super::BeginPlay();
	EnsureInstancePools();
}

void UTrafficVehicleSpawnerComponent::EnsureInstancePools()
{
	// Bestehende Pools entsorgen (bei Neuzuweisung im Editor).
	for (UInstancedStaticMeshComponent* Instance : VehicleInstances)
	{
		if (Instance && Instance->GetAttachParent())
		{
			Instance->DestroyComponent();
		}
	}
	VehicleInstances.Reset();
	InstanceMaterials.Reset();

	if (!VehicleMesh)
	{
		return;
	}

	const int32 PoolCount = FMath::Max(ColorPalette.Num(), 1);
	for (int32 i = 0; i < PoolCount; ++i)
	{
		UInstancedStaticMeshComponent* Instances = NewObject<UInstancedStaticMeshComponent>(this);
		Instances->SetupAttachment(this);
		Instances->RegisterComponent();
		Instances->SetStaticMesh(VehicleMesh);
		Instances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Instances->SetCastShadow(true);

		// Farbvariation: je Palette-Eintrag eine MID (falls ein Material und
		// der Parameter existieren; sonst rendert das Basis-Material).
		if (VehicleMaterial && ColorPalette.Num() > 0)
		{
			if (UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(VehicleMaterial, this))
			{
				MID->SetVectorParameterValue(VehicleColorParameterName, ColorPalette[i]);
				Instances->SetMaterial(0, MID);
				InstanceMaterials.Add(MID);
			}
		}
		else if (VehicleMaterial)
		{
			Instances->SetMaterial(0, VehicleMaterial);
		}

		VehicleInstances.Add(Instances);
	}
}

FVector UTrafficVehicleSpawnerComponent::GetObserverLocation() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return FVector::ZeroVector;
	}

	if (const APlayerController* PC = World->GetFirstPlayerController())
	{
		if (const APawn* Pawn = PC->GetPawn())
		{
			return Pawn->GetActorLocation();
		}
	}

	// Fallback: erste Streaming-Quelle im Level (folgt ebenfalls dem Player).
	if (const AActor* Source = UGameplayStatics::GetActorOfClass(
		World, AWiesbadenStreamingSource::StaticClass()))
	{
		return Source->GetActorLocation();
	}

	return FVector::ZeroVector;
}

void UTrafficVehicleSpawnerComponent::SelectNearestVehicles(
	const TArray<FTrafficVehicle>& Vehicles,
	const FVector& Center,
	double RadiusCm,
	int32 MaxCount,
	TArray<int32>& OutIndices)
{
	OutIndices.Reset();

	if (MaxCount <= 0 || Vehicles.Num() == 0)
	{
		return;
	}

	const double RadiusSq = (RadiusCm > 0.0) ? (RadiusCm * RadiusCm) : TNumericLimits<double>::Max();

	// Index samt quadriertem Abstand sammeln, dann sortieren. Bei den hier
	// ueblichen Groessenordnungen (gut hundert Fahrzeuge) ist das guenstiger
	// als eine Teil-Auswahl mit eigener Datenstruktur.
	TArray<TPair<double, int32>> Candidates;
	Candidates.Reserve(Vehicles.Num());

	for (int32 Index = 0; Index < Vehicles.Num(); ++Index)
	{
		const FTrafficVehicle& Vehicle = Vehicles[Index];
		if (Vehicle.bRemoved)
		{
			continue;
		}

		// Horizontal messen - siehe Kommentar in der Deklaration.
		const double Dx = Vehicle.Location.X - Center.X;
		const double Dy = Vehicle.Location.Y - Center.Y;
		const double DistSq = Dx * Dx + Dy * Dy;

		if (DistSq <= RadiusSq)
		{
			Candidates.Add(TPair<double, int32>(DistSq, Index));
		}
	}

	Candidates.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B)
	{
		// Bei gleichem Abstand nach Index sortieren, damit die Zuordnung
		// zwischen den Frames stabil bleibt und die Koerper nicht springen.
		return A.Key != B.Key ? A.Key < B.Key : A.Value < B.Value;
	});

	const int32 Count = FMath::Min(MaxCount, Candidates.Num());
	OutIndices.Reserve(Count);
	for (int32 i = 0; i < Count; ++i)
	{
		OutIndices.Add(Candidates[i].Value);
	}
}

void UTrafficVehicleSpawnerComponent::EnsureCollisionProxies()
{
	const int32 Wanted = FMath::Clamp(CollisionProxyCount, 0, 128);

	if (CollisionProxies.Num() == Wanted)
	{
		return;
	}

	for (UBoxComponent* Box : CollisionProxies)
	{
		if (Box)
		{
			Box->DestroyComponent();
		}
	}
	CollisionProxies.Reset();

	AActor* Owner = GetOwner();
	if (!Owner || Wanted == 0)
	{
		return;
	}

	for (int32 i = 0; i < Wanted; ++i)
	{
		UBoxComponent* Box = NewObject<UBoxComponent>(
			Owner, *FString::Printf(TEXT("TrafficCollision_%d"), i));
		if (!Box)
		{
			continue;
		}

		Box->SetupAttachment(this);
		Box->RegisterComponent();
		Box->SetBoxExtent(VehicleCollisionExtent, /*bUpdateOverlaps=*/false);

		// Blockierend gegen alles, was sich bewegt - die Fahrzeugphysik des
		// Spielers bewegt sich per AddActorWorldOffset mit Sweep und wird
		// dadurch aufgehalten.
		Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Box->SetCollisionObjectType(ECC_WorldDynamic);
		Box->SetCollisionResponseToAllChannels(ECR_Block);

		// Untereinander nicht blockieren: die Koerper stehen dicht
		// beieinander, wenn der Verkehr steht.
		Box->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Overlap);

		Box->SetHiddenInGame(true);
		Box->SetMobility(EComponentMobility::Movable);
		Box->SetGenerateOverlapEvents(false);
		Box->SetActive(false);
		Box->SetVisibility(false);

		CollisionProxies.Add(Box);
	}

	UE_LOG(LogWbTraffic, Log,
		TEXT("Verkehrs-Kollision: %d Koerper angelegt (Umkreis %.0f m, Groesse %.0fx%.0fx%.0f cm)."),
		CollisionProxies.Num(), CollisionRadiusMeters,
		VehicleCollisionExtent.X * 2.0, VehicleCollisionExtent.Y * 2.0, VehicleCollisionExtent.Z * 2.0);
}

void UTrafficVehicleSpawnerComponent::UpdateCollisionProxies(const TArray<FTrafficVehicle>& Vehicles)
{
	EnsureCollisionProxies();

	if (CollisionProxies.Num() == 0)
	{
		ActiveCollisionProxyCount = 0;
		return;
	}

	TArray<int32> Nearest;
	SelectNearestVehicles(
		Vehicles,
		GetObserverLocation(),
		static_cast<double>(CollisionRadiusMeters) * 100.0,
		CollisionProxies.Num(),
		Nearest);

	for (int32 i = 0; i < CollisionProxies.Num(); ++i)
	{
		UBoxComponent* Box = CollisionProxies[i];
		if (!Box)
		{
			continue;
		}

		if (i < Nearest.Num())
		{
			const FTrafficVehicle& Vehicle = Vehicles[Nearest[i]];

			// Der Koerper sitzt auf halber Fahrzeughoehe ueber der Fahrbahn,
			// weil die Fahrzeugposition der Radaufstandspunkt ist.
			const FVector Center = Vehicle.Location + FVector(0.0, 0.0, VehicleCollisionExtent.Z);

			Box->SetWorldLocationAndRotation(Center, Vehicle.Forward.Rotation());

			if (Box->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
			{
				Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			}
		}
		else
		{
			// Ueberzaehlige Koerper abschalten statt weit wegzuschieben:
			// ein vergessener Koerper irgendwo in der Stadt waere eine
			// unsichtbare Wand, die niemand mehr zuordnen kann.
			Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	}

	ActiveCollisionProxyCount = Nearest.Num();
}

void UTrafficVehicleSpawnerComponent::UpdateVehicles(const TArray<FTrafficVehicle>& Vehicles)
{
	EnsureInstancePools();

	// Kollisionskoerper den naechsten Fahrzeugen nachfuehren. Bewusst vor der
	// Sichtbarkeitspruefung: auch wenn kein Instanz-Pool existiert, soll der
	// Spieler nicht durch Fahrzeuge fahren koennen.
	UpdateCollisionProxies(Vehicles);

	if (VehicleInstances.Num() == 0)
	{
		LastVisibleVehicleCount = 0;
		return;
	}

	// Dateneine Platzierung: Culling + Yaw + deterministische Farb-Zuordnung.
	TArray<FPlacedTrafficVehicle> Placed;
	FWiesbadenTrafficSimulation::PlaceTrafficVehicles(
		Vehicles,
		GetObserverLocation(),
		static_cast<double>(CullRadiusMeters) * 100.0, // m -> cm
		VehicleInstances.Num(),
		Placed);

	// Instanzen je Farb-Gruppe neu aufbauen (nur die sichtbaren).
	TArray<TArray<FTransform>> TransformsByPool;
	TransformsByPool.SetNum(VehicleInstances.Num());
	for (const FPlacedTrafficVehicle& P : Placed)
	{
		const int32 Pool = FMath::Clamp(P.ColorIndex, 0, VehicleInstances.Num() - 1);
		TransformsByPool[Pool].Add(P.Transform);
	}

	for (int32 i = 0; i < VehicleInstances.Num(); ++i)
	{
		UInstancedStaticMeshComponent* Instances = VehicleInstances[i];
		if (!Instances)
		{
			continue;
		}

		Instances->ClearInstances();
		if (TransformsByPool[i].Num() > 0)
		{
			Instances->AddInstances(TransformsByPool[i], /*bWorldSpace=*/true);
		}
	}

	LastVisibleVehicleCount = Placed.Num();
}

void UTrafficVehicleSpawnerComponent::ClearVehicles()
{
	for (UInstancedStaticMeshComponent* Instances : VehicleInstances)
	{
		if (Instances)
		{
			Instances->ClearInstances();
		}
	}
	LastVisibleVehicleCount = 0;

	// Kollisionskoerper mit abschalten. Bleiben sie aktiv, stehen unsichtbare
	// Waende dort, wo zuletzt Fahrzeuge waren - besonders tueckisch beim
	// Levelwechsel, weil die Ursache dann nicht mehr im Level ist.
	for (UBoxComponent* Box : CollisionProxies)
	{
		if (Box)
		{
			Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	}
	ActiveCollisionProxyCount = 0;
}
