// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#include "NPC/WiesbadenStoreMerchant.h"

#include "GameFramework/PlayerController.h"
#include "Components/StaticMeshComponent.h"
#include "Core/WiesbadenGameStateSubsystem.h"
#include "Store/WiesbadenStoreSubsystem.h"
#include "UI/WiesbadenVehicleHUD.h"
#include "Vehicles/WiesbadenFootPawn.h"
#include "WiesbadenReal.h"

AWiesbadenStoreMerchant::AWiesbadenStoreMerchant()
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Marker = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Marker"));
	Marker->SetupAttachment(Root);
	Marker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Marker->SetHiddenInGame(true);

	// Nordfriedhof-Haendler: vom Friedhofseingang aus kurz links, hinter der Mauer.
	// Das ist die erste Schätzung; die endgültige Position wird im Editor an die
	// begehbare Flaeche und die gegebene Mauergeometrie angepasst.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> MarkerMesh(
		TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (MarkerMesh.Succeeded())
	{
		Marker->SetStaticMesh(MarkerMesh.Object);
	}
}

bool AWiesbadenStoreMerchant::CanInteractWith(APawn* Pawn) const
{
	if (!Pawn)
	{
		return false;
	}
	const AWiesbadenFootPawn* Foot = Cast<AWiesbadenFootPawn>(Pawn);
	if (!Foot)
	{
		return false;
	}
	const APlayerController* PC = Pawn->GetController<APlayerController>();
	const UWorld* World = Pawn->GetWorld();
	if (!PC || !World)
	{
		return false;
	}
	const float DistCm = FVector::Dist(Pawn->GetActorLocation(), GetActorLocation()) * 100.0f;
	return DistCm <= InteractRangeCm;
}

bool AWiesbadenStoreMerchant::TryInteract(APawn* Pawn)
{
	if (!CanInteractWith(Pawn))
	{
		return false;
	}

	ShowApproachHint();

	// Kaufe nur ueber das bestehende Store-Subsystem, nie direkt.
	const UWorld* World = GetWorld();
	const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	const UWiesbadenStoreSubsystem* Store = GI ? GI->GetSubsystem<UWiesbadenStoreSubsystem>() : nullptr;
	if (Store)
	{
		Store->TogglePanel();
	}

	return true;
}

void AWiesbadenStoreMerchant::ShowApproachHint()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const APlayerController* PC = World->GetFirstPlayerController();
	if (!PC)
	{
		return;
	}
	const APawn* Pawn = PC->GetPawn();
	if (!Pawn)
	{
		return;
	}
	if (!CanInteractWith(Pawn))
	{
		return;
	}
	const AWiesbadenVehicleHUD* HUD = PC->GetHUD<AWiesbadenVehicleHUD>();
	if (HUD)
	{
		HUD->ShowTransientHint(ApproachHint);
	}
}
