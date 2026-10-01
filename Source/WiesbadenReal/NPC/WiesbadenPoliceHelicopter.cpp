// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "NPC/WiesbadenPoliceHelicopter.h"

#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "NPC/WiesbadenPoliceSubsystem.h"
#include "UObject/ConstructorHelpers.h"
#include "WiesbadenReal.h"

AWiesbadenPoliceHelicopter::AWiesbadenPoliceHelicopter()
{
	PrimaryActorTick.bCanEverTick = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	// Ka-52-Rumpf wie der fliegbare Heli - ohne Cockpit/Waffe, das ist ein NPC.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> BodyMesh(
		TEXT("/Game/Vehicles/Ka52/Fuselage.Fuselage"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> RotorUpperMesh(
		TEXT("/Game/Vehicles/Ka52/Rotor_Upper.Rotor_Upper"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> RotorLowerMesh(
		TEXT("/Game/Vehicles/Ka52/Rotor_Lower.Rotor_Lower"));

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(SceneRoot);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (BodyMesh.Succeeded()) { Body->SetStaticMesh(BodyMesh.Object); }

	RotorUpper = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RotorUpper"));
	RotorUpper->SetupAttachment(Body);
	RotorUpper->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (RotorUpperMesh.Succeeded()) { RotorUpper->SetStaticMesh(RotorUpperMesh.Object); }

	RotorLower = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RotorLower"));
	RotorLower->SetupAttachment(Body);
	RotorLower->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (RotorLowerMesh.Succeeded()) { RotorLower->SetStaticMesh(RotorLowerMesh.Object); }

	Searchlight = CreateDefaultSubobject<USpotLightComponent>(TEXT("Searchlight"));
	Searchlight->SetupAttachment(SceneRoot);
	Searchlight->SetIntensity(8000.0f);
	Searchlight->SetAttenuationRadius(15000.0f);
	Searchlight->SetOuterConeAngle(22.0f);
	Searchlight->SetInnerConeAngle(12.0f);
	Searchlight->SetCastShadows(false);

	BlueLeft = CreateDefaultSubobject<UPointLightComponent>(TEXT("BlueLeft"));
	BlueRight = CreateDefaultSubobject<UPointLightComponent>(TEXT("BlueRight"));
	for (UPointLightComponent* Light : { BlueLeft.Get(), BlueRight.Get() })
	{
		Light->SetupAttachment(SceneRoot);
		Light->SetLightColor(FLinearColor(0.02f, 0.12f, 1.0f));
		Light->SetIntensity(0.0f);
		Light->SetAttenuationRadius(2500.0f);
		Light->SetCastShadows(false);
	}
	BlueLeft->SetRelativeLocation(FVector(60.0f, -120.0f, 0.0f));
	BlueRight->SetRelativeLocation(FVector(60.0f, 120.0f, 0.0f));

	UE_LOG(LogWbCore, Log, TEXT("Polizei-Heli: Luft-Verfolger gespawnt bei (%.0f, %.0f, %.0f)."),
		GetActorLocation().X, GetActorLocation().Y, GetActorLocation().Z);
}

float AWiesbadenPoliceHelicopter::GetDistanceToPlayerMeters() const
{
	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	return Pawn ? FVector::Dist(GetActorLocation(), Pawn->GetActorLocation()) / 100.0f : -1.0f;
}

void AWiesbadenPoliceHelicopter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	FlashTime += DeltaSeconds;
	const bool Left = FMath::Fmod(FlashTime, 0.6f) < 0.3f;
	BlueLeft->SetIntensity(Left ? 2200.0f : 0.0f);
	BlueRight->SetIntensity(Left ? 0.0f : 2200.0f);
	RotorUpper->AddLocalRotation(FRotator(0.0f, 0.0f, 1400.0f * DeltaSeconds));
	RotorLower->AddLocalRotation(FRotator(0.0f, 0.0f, -1100.0f * DeltaSeconds));

	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	APawn* Player = PC ? PC->GetPawn() : nullptr;
	if (!Player)
	{
		return; // ohne Spieler nichts zu verfolgen
	}

	State = FWiesbadenPoliceHeli::Step(State, Player->GetActorLocation(), Params, DeltaSeconds);

	// Bodenabstand: das Modell rechnet Spieler-Z + Hoehe - ueber dem Taunus
	// wuerde das Ziel im Hang liegen. Ein Strahl nach unten begrenzt die Hoehe.
	FVector Ziel = State.Position;
	FHitResult Ground;
	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(PoliceHeliGround), false);
	TraceParams.AddIgnoredActor(this);
	if (World->LineTraceSingleByChannel(Ground, Ziel + FVector(0, 0, 20000), Ziel - FVector(0, 0, 50000),
		ECC_WorldStatic, TraceParams))
	{
		Ziel.Z = FMath::Max(Ziel.Z, Ground.ImpactPoint.Z + 3000.0f); // mind. 30 m ueber Grund
	}
	SetActorLocation(Ziel, false);

	// Nase in Flugrichtung.
	const FVector Geschwindigkeit = State.Position - GetActorLocation();
	if (!FVector2D(Geschwindigkeit.X, Geschwindigkeit.Y).IsNearlyZero(10.0))
	{
		SetActorRotation(FRotator(0.0f, Geschwindigkeit.Rotation().Yaw, 0.0f));
	}

	// Echte Sicht UND Modell-Hysterese: Gebaeude duerfen die Luftverfolgung
	// unterbrechen, sonst ist "im Blick" eine leere Behauptung.
	const bool Sicht = WiesbadenPolice::CanSee(World, this, Player, Params.SpotRadiusCm);
	const bool Spotted = State.bSpotted && Sicht;
	if (Spotted != bPlayerSpotted)
	{
		bPlayerSpotted = Spotted;
		UE_LOG(LogWbCore, Log, TEXT("Polizei-Heli: Spieler %s (Abstand %.0f m)."),
			Spotted ? TEXT("im Blick") : TEXT("aus dem Blick"), GetDistanceToPlayerMeters());
	}

	// Suchscheinwerfer zeigt immer auf den Spieler - auch durch Rauch/Dunst
	// sichtbar, dass die Verfolgung laeuft.
	const FVector LichtZiel = Player->GetActorLocation() + FVector(0, 0, 70.0f);
	Searchlight->SetWorldRotation((LichtZiel - Searchlight->GetComponentLocation()).Rotation());
}
