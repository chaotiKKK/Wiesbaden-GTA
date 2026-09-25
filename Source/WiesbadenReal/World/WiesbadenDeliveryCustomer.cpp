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
	/** Figur je Koerpertyp - schlank und breit; Kinder bestellen bei Denno
	 *  keinen Kuchen. Dieselben Meshes wie die Fussgaenger, je vier Gangphasen.
	 *  Gewartet wird in Phase 1: die Durchgangsstellung mit geschlossenen
	 *  Beinen (31 cm tief). Phase 0 ist ein Schritt (81 cm) - der Kunde sah
	 *  damit aus, als liefe er gerade los. */
	const TCHAR* PosePaths[2][4] = {
		{
			TEXT("/Game/Assets/People/Varied/SM_WbPed2_0/StaticMeshes/SM_WbPed2_0.SM_WbPed2_0"),
			TEXT("/Game/Assets/People/Varied/SM_WbPed2_1/StaticMeshes/SM_WbPed2_1.SM_WbPed2_1"),
			TEXT("/Game/Assets/People/Varied/SM_WbPed2_2/StaticMeshes/SM_WbPed2_2.SM_WbPed2_2"),
			TEXT("/Game/Assets/People/Varied/SM_WbPed2_3/StaticMeshes/SM_WbPed2_3.SM_WbPed2_3"),
		},
		{
			TEXT("/Game/Assets/People/Varied/SM_WbPed2B_0/StaticMeshes/SM_WbPed2B_0.SM_WbPed2B_0"),
			TEXT("/Game/Assets/People/Varied/SM_WbPed2B_1/StaticMeshes/SM_WbPed2B_1.SM_WbPed2B_1"),
			TEXT("/Game/Assets/People/Varied/SM_WbPed2B_2/StaticMeshes/SM_WbPed2B_2.SM_WbPed2B_2"),
			TEXT("/Game/Assets/People/Varied/SM_WbPed2B_3/StaticMeshes/SM_WbPed2B_3.SM_WbPed2B_3"),
		},
	};
	constexpr int32 StandingPose = 1;
	/** Hemd RGB, Hose RGB, Hautton - dieselbe Belegung wie im Fussgaenger-Spawner. */
	constexpr int32 CustomDataFloats = 7;
	/** Drehgeschwindigkeit zum Spieler (Grad/s). */
	constexpr float TurnRateDegS = 120.0f;
	/** Umdrehen zur Tuer (Grad/s); erst unter WalkStartDeg Abweichung geht er los. */
	constexpr float HomeTurnRateDegS = 240.0f;
	constexpr float WalkStartDeg = 25.0f;
	/** Bodenstrahl beim Gehen: von so hoch ueber / bis so tief unter den Fuessen. */
	constexpr double GroundProbeUpCm = 150.0;
	constexpr double GroundProbeDownCm = 300.0;
	/** Hoehe des Wandstrahls ueber dem Boden (Huefte - unter Vordaechern, ueber Stufen). */
	constexpr double WallProbeHeightCm = 100.0;

	FCollisionObjectQueryParams StaticWorld() { return FCollisionObjectQueryParams(ECC_WorldStatic); }
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
	if (!World)
	{
		return;
	}
	if (bThanked)
	{
		TickWalkHome(DeltaSeconds);
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
	FacePlayerOrStreet(PlayerLocation, DeltaSeconds);
}

void AWiesbadenDeliveryCustomer::FacePlayerOrStreet(const FVector& PlayerLocation, float DeltaSeconds)
{
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
		Spot - FVector(0.0, 0.0, 3000.0), StaticWorld(), Params))
	{
		return false;   // Boden noch nicht gestreamt - naechster Tick
	}

	const int32 Body = FMath::Abs(Seed) % 2;
	FLinearColor Shirt, Trouser;
	float SkinT = 0.5f;
	UPedestrianSpawnerComponent::ComputePedestrianColors(Seed, Shirt, Trouser, SkinT);
	const TArray<float> Colors = { Shirt.R, Shirt.G, Shirt.B, Trouser.R, Trouser.G, Trouser.B, SkinT };
	PoseMeshes.SetNumZeroed(4);
	for (int32 Pose = 0; Pose < 4; ++Pose)
	{
		const TCHAR* Path = PosePaths[Body][Pose];
		PoseMeshes[Pose] = LoadObject<UStaticMesh>(nullptr, Path);
		if (!PoseMeshes[Pose])
		{
			// Fehlt eine Gangphase, bleibt beim Gehen die vorige stehen.
			UE_LOG(LogWbDeliveryCustomer, Warning, TEXT("Kunde: Figur fehlt (%s)."), Path);
		}
	}
	if (!PoseMeshes[StandingPose])
	{
		bPlaced = true;   // nicht jeden Tick erneut suchen; Trinkgeld gibt es trotzdem
		return false;
	}
	CreateFigure(Colors);

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

void AWiesbadenDeliveryCustomer::CreateFigure(const TArray<float>& Colors)
{
	// EINE Instanz, deren Material die Kleidung aus den Instanz-Daten liest.
	Figure = NewObject<UInstancedStaticMeshComponent>(this, TEXT("CustomerFigure"));
	Figure->SetupAttachment(Root);
	Figure->SetStaticMesh(PoseMeshes[StandingPose]);
	Figure->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Figure->NumCustomDataFloats = CustomDataFloats;
	Figure->RegisterComponent();
	Figure->AddInstance(FTransform::Identity, /*bWorldSpace=*/false);
	Figure->SetCustomData(0, Colors, /*bMarkRenderStateDirty=*/true);
	ShownPose = INDEX_NONE;
	ShowPose(StandingPose);
}

void AWiesbadenDeliveryCustomer::ShowPose(int32 Pose)
{
	UStaticMesh* Mesh = PoseMeshes.IsValidIndex(Pose) ? PoseMeshes[Pose] : nullptr;
	if (!Figure || !Mesh)
	{
		return;   // Phase fehlt: die sichtbare bleibt
	}
	if (Figure->GetStaticMesh() != Mesh)
	{
		Figure->SetStaticMesh(Mesh);
	}
	// Figur auf ihre Unterkante heben (Ursprung nicht zwingend an den Fuessen).
	const FBoxSphereBounds Bounds = Mesh->GetBounds();
	const FTransform Lift(FVector(0.0, 0.0, -(Bounds.Origin.Z - Bounds.BoxExtent.Z)));
	Figure->UpdateInstanceTransform(0, Lift, /*bWorldSpace=*/false, /*bMarkRenderStateDirty=*/true);
	ShownPose = Pose;
}

FDennoTip AWiesbadenDeliveryCustomer::ThankAndTip(int32 Payout, double DeadlineSeconds)
{
	bThanked = true;
	const FDennoTip Tip = WiesbadenDennoDelivery::ComputeTip(Payout, LastRemainingSeconds, DeadlineSeconds);
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
	BeginWalkHome();
	return Tip;
}

void AWiesbadenDeliveryCustomer::BeginWalkHome()
{
	UWorld* World = GetWorld();
	// Noch nicht aufgestellt (Spieler schneller da als ihr Takt, oder ihr Boden
	// war beim Heranfahren noch nicht gestreamt): jetzt aufstellen - sie soll
	// sich sichtbar bedanken und heimgehen, nicht ungesehen verschwinden.
	if (World && !bPlaced)
	{
		const APlayerController* PC = World->GetFirstPlayerController();
		const APawn* Player = PC ? PC->GetPawn() : nullptr;
		TryPlace(Player ? Player->GetActorLocation() : DropPoint);
	}
	if (!World || !bPlaced || !HasFigure())
	{
		SetLifeSpan(1.0f);   // nie sichtbar gewesen - niemand sieht ihn gehen
		return;
	}
	// Die Tuer: vom Warteplatz Richtung Schwerpunkt bis an die gebackene Wand.
	const FVector Start = GetActorLocation() + FVector(0.0, 0.0, WallProbeHeightCm);
	FVector ToHouse = AddressLocation - GetActorLocation();
	ToHouse.Z = 0.0;
	double WallCm = -1.0;
	if (ToHouse.Size() > 1.0)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(WbDeliveryCustomerWall), true);
		Params.AddIgnoredActor(this);
		FHitResult Wall;
		if (World->LineTraceSingleByObjectType(Wall, Start, Start + ToHouse, StaticWorld(), Params))
		{
			WallCm = Wall.Distance;
		}
	}
	DoorPoint = WiesbadenDennoDelivery::ComputeDoorPoint(GetActorLocation(), AddressLocation, WallCm);
	WalkDistanceCm = FVector::Dist2D(GetActorLocation(), DoorPoint);
	WalkedCm = 0.0;
	ThankedAtSeconds = World->GetTimeSeconds();
	// Gehen braucht jedes Bild (10 Hz waeren 13-cm-Spruenge); Notbremse, falls
	// er irgendwo haengt: Pause + Weg + reichlich Luft.
	SetActorTickInterval(0.0f);
	SetLifeSpan(static_cast<float>(WiesbadenDennoDelivery::CustomerThankPauseSeconds
		+ WalkDistanceCm / WiesbadenDennoDelivery::CustomerWalkSpeedCmS + 15.0));
	const FString WallText = WallCm >= 0.0
		? FString::Printf(TEXT("Wand nach %.1f m"), WallCm / 100.0) : FString(TEXT("keine Wand getroffen"));
	UE_LOG(LogWbDeliveryCustomer, Log, TEXT("Kunde geht zur Haustuer: %.1f m (%s)."),
		WalkDistanceCm / 100.0, *WallText);
}

void AWiesbadenDeliveryCustomer::TickWalkHome(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	if (!HasFigure())
	{
		return;   // nie aufgestellt - die Lebensdauer raeumt ihn weg
	}
	// Erst der Dank: stehen bleiben, dem Spieler zugewandt.
	if (World->GetTimeSeconds() - ThankedAtSeconds < WiesbadenDennoDelivery::CustomerThankPauseSeconds)
	{
		const APlayerController* PC = World->GetFirstPlayerController();
		if (const APawn* Player = PC ? PC->GetPawn() : nullptr)
		{
			FacePlayerOrStreet(Player->GetActorLocation(), DeltaSeconds);
		}
		return;
	}

	const FVector Here = GetActorLocation();
	FVector ToDoor = DoorPoint - Here;
	ToDoor.Z = 0.0;
	const double Left = ToDoor.Size();
	if (Left <= 1.0)
	{
		UE_LOG(LogWbDeliveryCustomer, Log, TEXT("Kunde ist im Haus (%.1f m gegangen)."), WalkedCm / 100.0);
		Destroy();
		return;
	}
	// Zur Tuer umdrehen; losgehen erst, wenn er ungefaehr hinschaut.
	const float TargetYaw = static_cast<float>(ToDoor.Rotation().Yaw);
	const float Yaw = FMath::FixedTurn(GetActorRotation().Yaw, TargetYaw, HomeTurnRateDegS * DeltaSeconds);
	SetActorRotation(FRotator(0.0f, Yaw, 0.0f));
	if (FMath::Abs(FMath::FindDeltaAngleDegrees(Yaw, TargetYaw)) > WalkStartDeg)
	{
		return;
	}

	const double Step = FMath::Min(WiesbadenDennoDelivery::CustomerWalkSpeedCmS * DeltaSeconds, Left);
	FVector Next = Here + ToDoor / Left * Step;
	// Dem Boden folgen (Gehweg, Stufe, Hofeinfahrt) - nur statische Welt.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WbDeliveryCustomerGround), true);
	Params.AddIgnoredActor(this);
	FHitResult Ground;
	if (World->LineTraceSingleByObjectType(Ground, Next + FVector(0.0, 0.0, GroundProbeUpCm),
		Next - FVector(0.0, 0.0, GroundProbeDownCm), StaticWorld(), Params))
	{
		Next.Z = Ground.ImpactPoint.Z;
	}
	SetActorLocation(Next);
	WalkedCm += Step;
	const int32 Pose = WiesbadenDennoDelivery::ComputeWalkPose(WalkedCm);
	if (Pose != ShownPose)
	{
		ShowPose(Pose);
	}
}
