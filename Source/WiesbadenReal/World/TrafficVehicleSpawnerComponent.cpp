// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/TrafficVehicleSpawnerComponent.h"

#include "WiesbadenReal.h"

#include "Vehicles/WiesbadenTrafficCars.h"
#include "World/WiesbadenStreamingSource.h"

#include "Components/BoxComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** Teile je Typ: Karosserie + vier Raeder. */
	constexpr int32 PartsPerType = 5;

	/** Freier Instanzplatz: winzig und tief unter der Stadt. */
	const FTransform HiddenTransform(FQuat::Identity, FVector(0.0, 0.0, -1.0e6), FVector(0.001));
}

UTrafficVehicleSpawnerComponent::UTrafficVehicleSpawnerComponent()
{
	// Instanzen werden von aussen (Subsystem/CityActor) je Bild aktualisiert.
	PrimaryComponentTick.bCanEverTick = false;

	// Lampenkoerper: der Engine-Wuerfel, klein skaliert - aus Fahrerabstand
	// ist eine Lampe ein Lichtpunkt, keine Form.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> LampCube(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (LampCube.Succeeded())
	{
		LampMesh = LampCube.Object;
	}

	// Lampen-Materialien: die des Kaefers, nicht selbst erzeugte - ein per
	// Python angelegtes Leuchtmaterial und das engine-eigene
	// EmissiveMeshMaterial rendeten im Spiel-Lauf dunkel (Ersatzmaterial).
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MatBrake(
		TEXT("/Game/Vehicles/Beetle/Bremslicht.Bremslicht"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MatIndicator(
		TEXT("/Game/Vehicles/Beetle/BlinkerH.BlinkerH"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MatHead(
		TEXT("/Game/Vehicles/Beetle/Silber.Silber"));

	LampMaterials.SetNum(LampPoolCount);
	LampMaterials[0] = MatBrake.Succeeded() ? MatBrake.Object : nullptr;        // Bremse
	LampMaterials[1] = MatIndicator.Succeeded() ? MatIndicator.Object : nullptr;// Blinker
	LampMaterials[2] = MatHead.Succeeded() ? MatHead.Object : nullptr;          // Scheinwerfer
	LampMaterials[3] = MatBrake.Succeeded() ? MatBrake.Object : nullptr;        // Rueckleuchte
}

void UTrafficVehicleSpawnerComponent::BeginPlay()
{
	Super::BeginPlay();
	EnsureInstancePools();
	EnsureLampPools();
}

void UTrafficVehicleSpawnerComponent::EnsureInstancePools()
{
	// NUR EINMAL: frueher wurden die Gruppen je Bild zerstoert und neu angelegt.
	if (bPoolsBuilt)
	{
		return;
	}
	bPoolsBuilt = true;

	const TArray<FWbTrafficCarType>& Types = WiesbadenTrafficCars::Types();
	BodyMeshes.SetNum(Types.Num());
	WheelMeshes.SetNum(Types.Num() * 4);
	Pools.SetNum(Types.Num());
	FString Loaded;
	for (int32 T = 0; T < Types.Num(); ++T)
	{
		FTypePool& Pool = Pools[T];
		BodyMeshes[T] = LoadObject<UStaticMesh>(nullptr, *WiesbadenTrafficCars::BodyMeshPath(Types[T]));
		for (int32 W = 0; W < 4; ++W)
		{
			WheelMeshes[T * 4 + W] = LoadObject<UStaticMesh>(nullptr, *WiesbadenTrafficCars::WheelMeshPath(Types[T], W));
		}
		Pool.WheelCenters.SetNum(4);
		for (int32 Part = 0; Part < PartsPerType; ++Part)
		{
			UStaticMesh* Mesh = Part == 0 ? BodyMeshes[T].Get() : WheelMeshes[T * 4 + Part - 1].Get();
			if (!Mesh)
			{
				UE_LOG(LogWbTraffic, Warning, TEXT("Verkehrsfahrzeug %s: Teil %d fehlt (Tools/import_traffic_cars.py)."),
					Types[T].Name, Part);
				Pool.Parts.Add(nullptr);
				continue;
			}
			if (Part > 0)
			{
				// Rad-Ursprung = Fahrzeugursprung: die Radmitte ist die Mitte der Bounds.
				Pool.WheelCenters[Part - 1] = Mesh->GetBounds().Origin;
			}
			UInstancedStaticMeshComponent* Instances = NewObject<UInstancedStaticMeshComponent>(this);
			Instances->SetupAttachment(this);
			Instances->SetStaticMesh(Mesh);
			Instances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Instances->SetCastShadow(true);
			Instances->SetMobility(EComponentMobility::Movable);
			Instances->RegisterComponent();
			Pool.Parts.Add(Instances);
			PoolComponents.Add(Instances);
		}
		Loaded += FString::Printf(TEXT("%s%s (%s)"), Loaded.IsEmpty() ? TEXT("") : TEXT(", "), Types[T].Name,
			BodyMeshes[T] ? TEXT("ok") : TEXT("FEHLT"));
	}
	UE_LOG(LogWbTraffic, Log, TEXT("Verkehrsfahrzeuge: %s - je Karosserie + 4 Raeder, Sichtweite %.0f m."),
		*Loaded, CullRadiusMeters);
}

FTransform UTrafficVehicleSpawnerComponent::ComputeChassisTransform(const FTrafficVehicle& Vehicle)
{
	const bool bHasBody = Vehicle.bBodyInitialized;
	const FVector Location = bHasBody ? Vehicle.BodyLocation : Vehicle.Location;
	double YawDeg;
	if (bHasBody)
	{
		YawDeg = FMath::RadiansToDegrees(Vehicle.BodyYawRad);
	}
	else
	{
		const FVector Forward = Vehicle.Forward.GetSafeNormal2D();
		YawDeg = FMath::RadiansToDegrees(FMath::Atan2(Forward.Y, Forward.X));
	}
	return FTransform(FRotator(Vehicle.SlopePitchDeg, YawDeg, 0.0), Location);
}

FTransform UTrafficVehicleSpawnerComponent::ComputeBodyTransform(const FTrafficVehicle& Vehicle)
{
	// Federung: Nicken/Wanken im FAHRZEUG-Rahmen, vor das Fahrgestell gelegt -
	// wie AWiesbadenCar an seiner BodyMesh. Die Raeder bleiben am Fahrgestell.
	const FTransform Tilt(FRotator(Vehicle.BodyPitchDeg, 0.0, Vehicle.BodyRollDeg));
	return Tilt * ComputeChassisTransform(Vehicle);
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
		FVector ViewLocation;
		FRotator ViewRotation;
		PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
		return ViewLocation;
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
		TEXT("Verkehrs-Kollision: %d Koerper angelegt (Umkreis %.0f m)."),
		CollisionProxies.Num(), CollisionRadiusMeters);
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

			// Kastengroesse und -lage aus der Karosserie DIESES Typs, an der
			// sichtbaren Karosserie (nicht an der Sollbahn) - man stoesst an
			// das Auto, das man sieht.
			FVector Origin = FVector(0.0, 0.0, VehicleCollisionExtent.Z);
			FVector Extent = VehicleCollisionExtent;
			const UStaticMesh* Body = BodyMeshes.IsValidIndex(Vehicle.TypeIndex) ? BodyMeshes[Vehicle.TypeIndex].Get() : nullptr;
			if (Body)
			{
				const FBoxSphereBounds B = Body->GetBounds();
				Origin = B.Origin;
				Extent = B.BoxExtent;
			}
			Box->SetBoxExtent(Extent, /*bUpdateOverlaps=*/false);
			const FTransform Chassis = ComputeChassisTransform(Vehicle);
			Box->SetWorldLocationAndRotation(Chassis.TransformPosition(Origin), Chassis.GetRotation());

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

void UTrafficVehicleSpawnerComponent::EnsureLampPools()
{
	// NUR EINMAL aufbauen: je Bild neu angelegt wurde das per SetMaterial
	// gesetzte Leuchtmaterial nicht wirksam (dunkle Kaesten auf den Autos).
	if (LampInstances.Num() == LampPoolCount && LampInstances[0] != nullptr)
	{
		return;
	}

	for (UInstancedStaticMeshComponent* Instance : LampInstances)
	{
		if (Instance && Instance->GetAttachParent())
		{
			Instance->DestroyComponent();
		}
	}
	LampInstances.Reset();

	if (!LampMesh || LampMaterials.Num() != LampPoolCount)
	{
		return;
	}

	for (int32 i = 0; i < LampPoolCount; ++i)
	{
		UInstancedStaticMeshComponent* Instances = NewObject<UInstancedStaticMeshComponent>(this);
		Instances->SetupAttachment(this);
		Instances->RegisterComponent();
		Instances->SetStaticMesh(LampMesh);
		Instances->SetCollisionEnabled(ECollisionEnabled::NoCollision);

		// Lampen werfen keinen Schatten: sie sind Lichtquellen, und der
		// Schattenwurf hunderter Wuerfelchen kostet ohne jeden Gewinn.
		Instances->SetCastShadow(false);

		Instances->SetMaterial(0, LampMaterials[i]);

		LampInstances.Add(Instances);
	}
}

void UTrafficVehicleSpawnerComponent::ComputeLampTransforms(
	const FTransform& BodyTransform,
	const FVector& LeftLampCm,
	FTransform& OutLeft,
	FTransform& OutRight)
{
	// Wuerfel ist 100 cm; eine Lampe ist rund 18 x 10 x 9 cm.
	const FVector LampScale(0.18f, 0.10f, 0.09f);
	OutLeft = FTransform(FRotator::ZeroRotator, LeftLampCm, LampScale) * BodyTransform;
	OutRight = FTransform(FRotator::ZeroRotator, FVector(LeftLampCm.X, -LeftLampCm.Y, LeftLampCm.Z), LampScale)
		* BodyTransform;
}

void UTrafficVehicleSpawnerComponent::UpdateLamps(const TArray<const FTrafficVehicle*>& Visible, bool bNight)
{
	if (LampInstances.Num() < 4)
	{
		return;
	}

	enum ELampPool { Brake = 0, Indicator = 1, Head = 2, Tail = 3 };

	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	const TArray<FWbTrafficCarType>& Types = WiesbadenTrafficCars::Types();

	TArray<TArray<FTransform>> ByPool;
	ByPool.SetNum(LampInstances.Num());

	for (const FTrafficVehicle* V : Visible)
	{
		const FWbTrafficCarType& Type = Types[FMath::Clamp(V->TypeIndex, 0, Types.Num() - 1)];
		const FTransform Body = ComputeBodyTransform(*V);
		FTransform RearLeft, RearRight, FrontLeft, FrontRight;
		ComputeLampTransforms(Body, Type.TailLampCm, RearLeft, RearRight);
		ComputeLampTransforms(Body, Type.HeadLampCm, FrontLeft, FrontRight);

		if (V->bBraking)
		{
			ByPool[Brake].Add(RearLeft);
			ByPool[Brake].Add(RearRight);
		}
		else if (bNight)
		{
			// Rueckleuchte nur, wenn NICHT gebremst wird - sonst lagen zwei
			// Lampen ineinander und die schwaechere flimmerte durch.
			ByPool[Tail].Add(RearLeft);
			ByPool[Tail].Add(RearRight);
		}

		if (bNight)
		{
			ByPool[Head].Add(FrontLeft);
			ByPool[Head].Add(FrontRight);
		}

		if (V->Indicator != EVehicleIndicator::None
			&& FWiesbadenTrafficSimulation::IsIndicatorLit(V->VehicleId, Now))
		{
			// Vorne UND hinten auf der blinkenden Seite (Unreal: links = -Y).
			const bool bLeft = (V->Indicator == EVehicleIndicator::Left);
			ByPool[Indicator].Add(bLeft ? FrontLeft : FrontRight);
			ByPool[Indicator].Add(bLeft ? RearLeft : RearRight);
		}
	}

	LastLampCounts.SetNum(LampInstances.Num());
	for (int32 i = 0; i < LampInstances.Num(); ++i)
	{
		LastLampCounts[i] = ByPool[i].Num();
		if (UInstancedStaticMeshComponent* Instances = LampInstances[i])
		{
			Instances->ClearInstances();
			if (ByPool[i].Num() > 0)
			{
				Instances->AddInstances(ByPool[i], /*bShouldReturnIndices=*/false,
					/*bWorldSpace=*/true);
			}
		}
	}
}

void UTrafficVehicleSpawnerComponent::UpdateVehicles(
	const TArray<FTrafficVehicle>& Vehicles, bool bNight)
{
	EnsureInstancePools();
	EnsureLampPools();

	// Kollisionskoerper den naechsten Fahrzeugen nachfuehren - auch ohne
	// Instanzgruppen soll der Spieler nicht durch Fahrzeuge fahren koennen.
	UpdateCollisionProxies(Vehicles);

	// Sichtbar: innerhalb der Sichtweite um den Blickpunkt.
	const FVector Observer = GetObserverLocation();
	const double CullSq = FMath::Square(static_cast<double>(CullRadiusMeters) * 100.0);
	TArray<const FTrafficVehicle*> Visible;
	Visible.Reserve(Vehicles.Num());
	for (const FTrafficVehicle& V : Vehicles)
	{
		const FVector Where = V.bBodyInitialized ? V.BodyLocation : V.Location;
		if (!V.bRemoved && (CullRadiusMeters <= 0.0f || FVector::DistSquared(Where, Observer) <= CullSq))
		{
			Visible.Add(&V);
		}
	}

	// Je Typ: Plaetze der nicht mehr sichtbaren Fahrzeuge freigeben, neue
	// Fahrzeuge auf freie (oder neue) Plaetze, dann alle Lagen auf einmal.
	TArray<TArray<const FTrafficVehicle*>> ByType;
	ByType.SetNum(Pools.Num());
	for (const FTrafficVehicle* V : Visible)
	{
		if (Pools.IsValidIndex(V->TypeIndex))
		{
			ByType[V->TypeIndex].Add(V);
		}
	}

	for (int32 T = 0; T < Pools.Num(); ++T)
	{
		FTypePool& Pool = Pools[T];
		if (Pool.Parts.Num() != PartsPerType || !Pool.Parts[0])
		{
			continue;
		}
		TSet<int32> Present;
		for (const FTrafficVehicle* V : ByType[T])
		{
			Present.Add(V->VehicleId);
		}
		TArray<int32> Teleport;   // Plaetze, deren Inhalt wechselt: ohne Vorbild-Lage (keine Schliere)
		for (int32 Slot = 0; Slot < Pool.SlotVehicle.Num(); ++Slot)
		{
			const int32 Id = Pool.SlotVehicle[Slot];
			if (Id != INDEX_NONE && !Present.Contains(Id))
			{
				Pool.VehicleSlot.Remove(Id);
				Pool.SlotVehicle[Slot] = INDEX_NONE;
				Teleport.Add(Slot);
			}
		}
		int32 NextFree = 0;
		for (const FTrafficVehicle* V : ByType[T])
		{
			if (Pool.VehicleSlot.Contains(V->VehicleId))
			{
				continue;
			}
			while (NextFree < Pool.SlotVehicle.Num() && Pool.SlotVehicle[NextFree] != INDEX_NONE)
			{
				++NextFree;
			}
			if (NextFree == Pool.SlotVehicle.Num())
			{
				Pool.SlotVehicle.Add(INDEX_NONE);
				for (UInstancedStaticMeshComponent* Part : Pool.Parts)
				{
					if (Part)
					{
						Part->AddInstance(HiddenTransform, /*bWorldSpace=*/true);
					}
				}
			}
			Pool.SlotVehicle[NextFree] = V->VehicleId;
			Pool.VehicleSlot.Add(V->VehicleId, NextFree);
			Teleport.AddUnique(NextFree);
		}

		// Lagen aller Plaetze: Karosserie + vier Raeder.
		const int32 SlotCount = Pool.SlotVehicle.Num();
		TArray<TArray<FTransform>> Parts;
		Parts.SetNum(PartsPerType);
		for (TArray<FTransform>& List : Parts)
		{
			List.Init(HiddenTransform, SlotCount);
		}
		for (const FTrafficVehicle* V : ByType[T])
		{
			const int32 Slot = Pool.VehicleSlot.FindChecked(V->VehicleId);
			const FTransform Chassis = ComputeChassisTransform(*V);
			Parts[0][Slot] = ComputeBodyTransform(*V);
			for (int32 W = 0; W < 4; ++W)
			{
				const double Steer = WiesbadenTrafficCars::IsFrontWheel(W) ? V->SteerAngleRad : 0.0;
				Parts[W + 1][Slot] = WiesbadenTrafficCars::ComputeWheelTransform(
					Pool.WheelCenters[W], V->WheelSpinRad, Steer) * Chassis;
			}
		}
		for (int32 Part = 0; Part < PartsPerType; ++Part)
		{
			UInstancedStaticMeshComponent* Instances = Pool.Parts[Part];
			if (!Instances || SlotCount == 0)
			{
				continue;
			}
			Instances->BatchUpdateInstancesTransforms(0, Parts[Part], /*bWorldSpace=*/true,
				/*bMarkRenderStateDirty=*/Teleport.Num() == 0, /*bTeleport=*/false);
			for (int32 Index = 0; Index < Teleport.Num(); ++Index)
			{
				const int32 Slot = Teleport[Index];
				Instances->UpdateInstanceTransform(Slot, Parts[Part][Slot], /*bWorldSpace=*/true,
					/*bMarkRenderStateDirty=*/Index == Teleport.Num() - 1, /*bTeleport=*/true);
			}
		}
	}

	UpdateLamps(Visible, bNight);
	LastVisibleVehicleCount = Visible.Num();
}

void UTrafficVehicleSpawnerComponent::ClearVehicles()
{
	for (FTypePool& Pool : Pools)
	{
		for (UInstancedStaticMeshComponent* Part : Pool.Parts)
		{
			if (Part)
			{
				Part->ClearInstances();
			}
		}
		Pool.SlotVehicle.Reset();
		Pool.VehicleSlot.Reset();
	}
	for (UInstancedStaticMeshComponent* Instances : LampInstances)
	{
		if (Instances)
		{
			Instances->ClearInstances();
		}
	}
	for (UBoxComponent* Box : CollisionProxies)
	{
		if (Box)
		{
			Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	}
	LastVisibleVehicleCount = 0;
	ActiveCollisionProxyCount = 0;
}
