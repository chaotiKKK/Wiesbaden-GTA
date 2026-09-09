// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#include "NPC/WiesbadenPursuerActor.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "UObject/ConstructorHelpers.h"
#include "WiesbadenReal.h"

AWiesbadenPursuerActor::AWiesbadenPursuerActor()
{
	PrimaryActorTick.bCanEverTick = true;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); // kinematischer Mover, keine Physik
	Mesh->SetRelativeScale3D(FVector(2.0f, 2.0f, 2.0f));       // ~2 m Wuerfel, sichtbar

	// Engine-Wuerfel als Platzhalter-Mesh; scheitert das (uncooked), bleibt der
	// Verfolger unsichtbar, funktioniert aber trotzdem (Bewegung/Log).
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		Mesh->SetStaticMesh(Cube.Object);
	}
}

void AWiesbadenPursuerActor::BeginPlay()
{
	Super::BeginPlay();

	State.Position = GetActorLocation();
	State.Mode = EWiesbadenPursuerMode::Idle;
	LastLoggedMode = State.Mode;
	UE_LOG(LogWbCore, Log, TEXT("Verfolger gespawnt bei (%.0f, %.0f)."),
		GetActorLocation().X, GetActorLocation().Y);
}

void AWiesbadenPursuerActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Pawn)
	{
		return; // ohne Spieler keine Reaktion
	}

	const FVector PlayerPos = Pawn->GetActorLocation();
	State = FWiesbadenPursuer::Step(State, PlayerPos, Params, DeltaSeconds);

	// Reine 2D-Verfolgung: X/Y aus dem Modell uebernehmen, Hoehe (Z) beibehalten.
	SetActorLocation(FVector(State.Position.X, State.Position.Y, GetActorLocation().Z), false);

	if (State.Mode != LastLoggedMode)
	{
		const TCHAR* ModeStr =
			State.Mode == EWiesbadenPursuerMode::Chasing ? TEXT("verfolgt")
			: State.Mode == EWiesbadenPursuerMode::Caught ? TEXT("eingeholt")
			: TEXT("wartet");
		UE_LOG(LogWbCore, Log, TEXT("Verfolger: %s (Abstand %.0f m)."),
			ModeStr, FVector::Dist2D(GetActorLocation(), PlayerPos) / 100.0);
		LastLoggedMode = State.Mode;
	}
}
