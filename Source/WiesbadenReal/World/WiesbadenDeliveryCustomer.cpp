// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/WiesbadenDeliveryCustomer.h"

#include "Core/WiesbadenGameStateSubsystem.h"
#include "Missions/WiesbadenDennoDelivery.h"
#include "Missions/WiesbadenMissionSubsystem.h"
#include "World/PedestrianSpawnerComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "CollisionQueryParams.h"

DEFINE_LOG_CATEGORY_STATIC(LogWbDeliveryCustomer, Log, All);

namespace
{
	/** Wartende Figur je Koerpertyp - schlank und breit; Kinder bestellen bei
	 *  Denno keinen Kuchen. Dieselben Meshes wie die Fussgaenger, Gangphase 1:
	 *  die Durchgangsstellung mit geschlossenen Beinen (31 cm tief). Phase 0 ist
	 *  ein Schritt (81 cm) - der Kunde sah damit aus, als liefe er gerade los. */
	const TCHAR* StandingPaths[2] = {
		TEXT("/Game/Assets/People/Varied/SM_WbPed2_1/StaticMeshes/SM_WbPed2_1.SM_WbPed2_1"),
		TEXT("/Game/Assets/People/Varied/SM_WbPed2B_1/StaticMeshes/SM_WbPed2B_1.SM_WbPed2B_1"),
	};
	/** Hemd RGB, Hose RGB, Hautton - dieselbe Belegung wie im Fussgaenger-Spawner. */
	constexpr int32 CustomDataFloats = 7;
	/** Drehgeschwindigkeit zum Spieler (Grad/s). */
	constexpr float TurnRateDegS = 120.0f;
}

AWiesbadenDeliveryCustomer::AWiesbadenDeliveryCustomer()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("CustomerRoot"));
	RootComponent = Root;
}

void AWiesbadenDeliveryCustomer::Setup(FName InMissionId, const FVector& InDropPoint,
	const FVector& InAddressLocation, int32 InSeed)
{
	MissionId = InMissionId;
	DropPoint = InDropPoint;
	AddressLocation = InAddressLocation;
	Seed = InSeed;
}

void AWiesbadenDeliveryCustomer::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UWorld* World = GetWorld();
	if (!World || bThanked)
	{
		return;
	}
	const UWiesbadenMissionSubsystem* Missions = World->GetSubsystem<UWiesbadenMissionSubsystem>();
	if (!Missions || Missions->GetActiveMissionId() != MissionId)
	{
		// Auftrag ohne Abgabe zu Ende (Frist verpasst): still gehen.
		UE_LOG(LogWbDeliveryCustomer, Log, TEXT("Kunde fuer %s geht - der Auftrag ist ohne Abgabe beendet."),
			*MissionId.ToString());
		Destroy();
		return;
	}
	LastRemainingSeconds = Missions->GetActiveMissionRemainingSeconds();

	const APlayerController* PC = World->GetFirstPlayerController();
	const APawn* Player = PC ? PC->GetPawn() : nullptr;
	if (!Player)
	{
		return;
	}
	const FVector PlayerLocation = Player->GetActorLocation();
	if (!bPlaced)
	{
		if (FVector::Dist2D(PlayerLocation, DropPoint) <= WiesbadenDennoDelivery::CustomerAppearCm)
		{
			TryPlace(PlayerLocation);
		}
		return;
	}
	// Zur Strasse schauen; kommt der Spieler naeher, sich ihm zuwenden.
	const FVector ToPlayer = PlayerLocation - GetActorLocation();
	const double TargetYaw = ToPlayer.Size2D() <= FacePlayerCm ? ToPlayer.Rotation().Yaw : StandYawDeg;
	const float Yaw = FMath::FixedTurn(GetActorRotation().Yaw, static_cast<float>(TargetYaw), TurnRateDegS * DeltaSeconds);
	SetActorRotation(FRotator(0.0f, Yaw, 0.0f));
}

bool AWiesbadenDeliveryCustomer::TryPlace(const FVector& PlayerLocation)
{
	UWorld* World = GetWorld();
	const FVector Spot = WiesbadenDennoDelivery::ComputeCustomerSpot(DropPoint, AddressLocation);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WbDeliveryCustomer), true);
	Params.AddIgnoredActor(this);
	FHitResult Ground;
	// Nur statische Welt: ein parkendes Auto ist kein Gehweg.
	if (!World->LineTraceSingleByObjectType(Ground, Spot + FVector(0.0, 0.0, 3000.0),
		Spot - FVector(0.0, 0.0, 3000.0), FCollisionObjectQueryParams(ECC_WorldStatic), Params))
	{
		return false;   // Boden noch nicht gestreamt - naechster Tick
	}

	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, StandingPaths[FMath::Abs(Seed) % 2]);
	if (!Mesh)
	{
		UE_LOG(LogWbDeliveryCustomer, Warning, TEXT("Kunde: Figur fehlt (%s)."), StandingPaths[FMath::Abs(Seed) % 2]);
		bPlaced = true;   // nicht jeden Tick erneut suchen; Trinkgeld gibt es trotzdem
		return false;
	}
	Figure = NewObject<UInstancedStaticMeshComponent>(this, TEXT("CustomerFigure"));
	Figure->SetupAttachment(Root);
	Figure->SetStaticMesh(Mesh);
	Figure->NumCustomDataFloats = CustomDataFloats;
	Figure->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Figure->RegisterComponent();
	// Figur auf ihre Unterkante heben (Ursprung nicht zwingend an den Fuessen).
	const FBoxSphereBounds Bounds = Mesh->GetBounds();
	const double Lift = -(Bounds.Origin.Z - Bounds.BoxExtent.Z);
	Figure->AddInstance(FTransform(FRotator::ZeroRotator, FVector(0.0, 0.0, Lift)), /*bWorldSpace=*/false);
	FLinearColor Shirt, Trouser;
	float SkinT = 0.5f;
	UPedestrianSpawnerComponent::ComputePedestrianColors(Seed, Shirt, Trouser, SkinT);
	const TArray<float> Data = { Shirt.R, Shirt.G, Shirt.B, Trouser.R, Trouser.G, Trouser.B, SkinT };
	Figure->SetCustomData(0, Data, /*bMarkRenderStateDirty=*/true);

	// Die Figur blickt nach +X; zur Strasse (Abgabepunkt) drehen.
	StandYawDeg = (DropPoint - Spot).GetSafeNormal2D().Rotation().Yaw;
	SetActorLocationAndRotation(Ground.ImpactPoint, FRotator(0.0f, StandYawDeg, 0.0f));
	bPlaced = true;
	UE_LOG(LogWbDeliveryCustomer, Log,
		TEXT("Kunde wartet fuer %s bei (%.0f, %.0f, %.0f), %.0f m vom Abgabepunkt, Spieler %.0f m entfernt."),
		*MissionId.ToString(), Ground.ImpactPoint.X, Ground.ImpactPoint.Y, Ground.ImpactPoint.Z,
		FVector::Dist2D(Ground.ImpactPoint, DropPoint) / 100.0, FVector::Dist2D(PlayerLocation, DropPoint) / 100.0);
	return true;
}

int32 AWiesbadenDeliveryCustomer::ThankAndTip(int32 Payout, double DeadlineSeconds, FString& OutThanks)
{
	bThanked = true;
	const FDennoTip Tip = WiesbadenDennoDelivery::ComputeTip(Payout, LastRemainingSeconds, DeadlineSeconds);
	OutThanks = Tip.Thanks;
	if (Tip.Amount > 0)
	{
		const UGameInstance* GI = GetGameInstance();
		if (UWiesbadenGameStateSubsystem* GameState = GI ? GI->GetSubsystem<UWiesbadenGameStateSubsystem>() : nullptr)
		{
			GameState->AddGuthaben(Tip.Amount);
		}
	}
	UE_LOG(LogWbDeliveryCustomer, Log,
		TEXT("Kunde bedankt sich (%s): Restzeit %.0f von %.0f s, Trinkgeld %d EUR - \"%s\"."),
		*MissionId.ToString(), LastRemainingSeconds, DeadlineSeconds, Tip.Amount, *Tip.Thanks);
	// Noch einen Moment stehen bleiben (der Dank ist zu sehen), dann gehen.
	SetLifeSpan(10.0f);
	return Tip.Amount;
}
