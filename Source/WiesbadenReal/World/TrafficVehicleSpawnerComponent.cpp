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

	// Lampenkoerper: der Engine-Wuerfel, klein skaliert. Ein eigenes Mesh
	// dafuer waere ein Asset mehr ohne jeden Gewinn - aus Fahrerabstand ist
	// eine Lampe ein Lichtpunkt, keine Form.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> LampCube(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (LampCube.Succeeded())
	{
		LampMesh = LampCube.Object;
	}

	// Lampen-Materialien: die des Kaefers, nicht selbst erzeugte.
	//
	// Erst ein per Python angelegtes Leuchtmaterial, dann das engine-eigene
	// EmissiveMeshMaterial - BEIDE rendeten im Spiel-Lauf dunkel (im Bild
	// standen schwarze Kaesten auf den Autos, mit Karomuster = Ersatzmaterial).
	// Die Materialien des Kaefers rendern nachweislich; sie sind beleuchtet
	// statt emissiv, sehen bei Nacht aber sauber aus, weil Himmelslicht und
	// Strassenlaternen sie treffen.
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

void UTrafficVehicleSpawnerComponent::EnsureLampPools()
{
	// NUR EINMAL aufbauen.
	//
	// UpdateVehicles laeuft je Bild; ohne diese Sperre wurden die vier
	// Komponenten JEDES BILD zerstoert und neu angelegt. Die Instanzen kamen
	// dabei zwar an (die Zaehlung stimmte), aber das per SetMaterial gesetzte
	// Leuchtmaterial wurde nicht wirksam - im Bild standen dunkle Kaesten auf
	// den Autos. Die Fahrzeug-Gruppen fallen nicht auf, weil sie die
	// Materialien des Meshes benutzen und gar kein SetMaterial brauchen.
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
	const FTransform& VehicleTransform,
	const FVector& BoundsOrigin,
	const FVector& BoundsExtent,
	bool bFront,
	FTransform& OutLeft,
	FTransform& OutRight)
{
	// Alles aus den Bounds ableiten: Laengsachse X, Querachse Y, Hoehe Z.
	// Die Einrueckungen sind Anteile, keine festen Zentimeter - ein laengeres
	// Fahrzeug bekommt seine Lampen damit von selbst weiter aussen.
	const double LengthSign = bFront ? 1.0 : -1.0;
	const double AlongCm = BoundsOrigin.X + LengthSign * (BoundsExtent.X - 8.0);
	const double SideCm = FMath::Max(BoundsExtent.Y - 16.0, 5.0);

	// Lampenhoehe: knapp ueber dem unteren Rand des Fahrzeugs, nicht in der
	// Mitte - dort saessen sie im Fenster.
	const double UpCm = BoundsOrigin.Z - BoundsExtent.Z + BoundsExtent.Z * 0.72;

	// Wuerfel ist 100 cm; eine Lampe ist rund 18 x 10 x 9 cm.
	const FVector LampScale(0.20f, 0.12f, 0.10f);

	OutLeft = FTransform(FRotator::ZeroRotator,
		FVector(AlongCm, BoundsOrigin.Y + SideCm, UpCm), LampScale) * VehicleTransform;
	OutRight = FTransform(FRotator::ZeroRotator,
		FVector(AlongCm, BoundsOrigin.Y - SideCm, UpCm), LampScale) * VehicleTransform;
}

void UTrafficVehicleSpawnerComponent::UpdateLamps(
	const TArray<FPlacedTrafficVehicle>& Placed, bool bNight)
{
	if (LampInstances.Num() < 4 || !VehicleMesh)
	{
		return;
	}

	enum ELampPool { Brake = 0, Indicator = 1, Head = 2, Tail = 3 };

	const FBoxSphereBounds MeshBounds = VehicleMesh->GetBounds();
	const FVector Origin = MeshBounds.Origin;
	const FVector Extent = MeshBounds.BoxExtent;

	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

	TArray<TArray<FTransform>> ByPool;
	ByPool.SetNum(LampInstances.Num());

	for (const FPlacedTrafficVehicle& P : Placed)
	{
		FTransform RearLeft, RearRight, FrontLeft, FrontRight;
		ComputeLampTransforms(P.Transform, Origin, Extent, /*bFront=*/false, RearLeft, RearRight);
		ComputeLampTransforms(P.Transform, Origin, Extent, /*bFront=*/true, FrontLeft, FrontRight);

		if (P.bBraking)
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

		if (P.Indicator != EVehicleIndicator::None
			&& FWiesbadenTrafficSimulation::IsIndicatorLit(P.VehicleId, Now))
		{
			// Vorne UND hinten auf der blinkenden Seite - so ist die Richtung
			// aus beiden Blickwinkeln zu erkennen.
			const bool bLeft = (P.Indicator == EVehicleIndicator::Left);
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

	UpdateLamps(Placed, bNight);

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

	// Ohne das blieben die Lampen als frei schwebende Lichtpunkte stehen, wo
	// zuletzt Fahrzeuge waren - besonders auffaellig beim Levelwechsel.
	for (UInstancedStaticMeshComponent* Instances : LampInstances)
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
