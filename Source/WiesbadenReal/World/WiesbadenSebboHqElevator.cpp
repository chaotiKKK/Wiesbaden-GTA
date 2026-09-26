// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/WiesbadenSebboHqElevator.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Vehicles/WiesbadenFootPawn.h"

DEFINE_LOG_CATEGORY_STATIC(LogWbHqElevator, Log, All);

namespace
{
	constexpr double CabinY = 220.0;
	constexpr double DoorHalfWidthCm = 55.0;
	constexpr double DoorPanelWidthCm = 55.0;
	constexpr double DoorSlideCm = 55.0;
}

AWiesbadenSebboHqElevator::AWiesbadenSebboHqElevator()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
}

UStaticMeshComponent* AWiesbadenSebboHqElevator::AddBox(
	FName Name, USceneComponent* Parent, const FVector& LocalCenter,
	const FVector& SizeCm, UMaterialInterface* Material, bool bBlocking)
{
	if (!CubeMesh || !Parent)
	{
		return nullptr;
	}
	UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(this, Name);
	Component->SetStaticMesh(CubeMesh);
	Component->SetupAttachment(Parent);
	Component->SetRelativeLocation(LocalCenter);
	Component->SetRelativeScale3D(SizeCm / 100.0);
	Component->SetCollisionEnabled(bBlocking
		? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	if (bBlocking)
	{
		Component->SetCollisionProfileName(TEXT("BlockAll"));
	}
	if (Material)
	{
		Component->SetMaterial(0, Material);
	}
	Component->RegisterComponent();
	return Component;
}

UStaticMeshComponent* AWiesbadenSebboHqElevator::AddCylinder(
	FName Name, USceneComponent* Parent, const FVector& LocalCenter,
	const FVector& SizeCm, UMaterialInterface* Material, const FRotator& Rotation)
{
	if (!CylinderMesh || !Parent)
	{
		return nullptr;
	}
	UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(this, Name);
	Component->SetStaticMesh(CylinderMesh);
	Component->SetupAttachment(Parent);
	Component->SetRelativeLocation(LocalCenter);
	Component->SetRelativeRotation(Rotation);
	Component->SetRelativeScale3D(SizeCm / 100.0);
	// Knoepfe sind Deko: ohne Kollision, sonst stolpert die Kapsel davor.
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (Material)
	{
		Component->SetMaterial(0, Material);
	}
	Component->RegisterComponent();
	return Component;
}

void AWiesbadenSebboHqElevator::Initialize(const FSebboHqDimensions& Dimensions)
{
	if (bInitialized)
	{
		return;
	}
	Dims = Dimensions;
	Lift.FloorCount = Dims.FloorCount;
	Lift.FloorHeightCm = Dims.FloorHeightCm;
	CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	CylinderMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UMaterialInterface* Metal = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Game/Materials/City/M_WbLmSlate.M_WbLmSlate"));
	UMaterialInterface* Light = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Game/Materials/City/M_WbLmWhite.M_WbLmWhite"));
	UMaterialInterface* Glass = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Game/Materials/City/M_WbFacade_Glas.M_WbFacade_Glas"));
	if (!CubeMesh)
	{
		UE_LOG(LogWbHqElevator, Error, TEXT("Aufzug ohne Kabinenmesh nicht gebaut."));
		return;
	}

	CabRoot = NewObject<USceneComponent>(this, TEXT("CabRoot"));
	CabRoot->SetupAttachment(Root);
	CabRoot->RegisterComponent();
	CabRoot->SetRelativeLocation(FVector(0.0, 0.0, Dims.SlabCm));

	// Die Kabine hat einen echten Boden, drei Waende und eine Decke. Die
	// Schachtwand auf -X bleibt als 110-cm-Tueroeffnung frei.
	AddBox(TEXT("CabFloor"), CabRoot, FVector(-20.0, CabinY, -8.0),
		FVector(740.0, 330.0, 16.0), Metal);
	AddBox(TEXT("CabBack"), CabRoot, FVector(350.0, CabinY, 140.0),
		FVector(12.0, 330.0, 280.0), Light);
	AddBox(TEXT("CabSideNorth"), CabRoot, FVector(-20.0, 380.0, 140.0),
		FVector(740.0, 12.0, 280.0), Glass);
	AddBox(TEXT("CabSideSouth"), CabRoot, FVector(-20.0, 60.0, 140.0),
		FVector(740.0, 12.0, 280.0), Glass);
	AddBox(TEXT("CabFrontNorth"), CabRoot, FVector(-390.0, 326.0, 140.0),
		FVector(12.0, 108.0, 280.0), Metal);
	AddBox(TEXT("CabFrontSouth"), CabRoot, FVector(-390.0, 114.0, 140.0),
		FVector(12.0, 108.0, 280.0), Metal);
	AddBox(TEXT("CabCeiling"), CabRoot, FVector(-20.0, CabinY, 287.0),
		FVector(740.0, 330.0, 14.0), Light);
	CabDoorLeft = AddBox(TEXT("CabDoorLeft"), CabRoot,
		FVector(-393.0, CabinY - DoorHalfWidthCm * 0.5, 130.0),
		FVector(10.0, DoorPanelWidthCm, 260.0), Metal);
	CabDoorRight = AddBox(TEXT("CabDoorRight"), CabRoot,
		FVector(-393.0, CabinY + DoorHalfWidthCm * 0.5, 130.0),
		FVector(10.0, DoorPanelWidthCm, 260.0), Metal);

	// --- Kabinen-AAA: Bedienfeld, Anzeige, Lampenfeld, Handlauf --------------
	// Alles ohne Kollision (bBlocking = false): der Spieler steht dicht davor.
	// Das Bedienfeld sitzt auf der inneren Gesichtsseite der Frontwand neben
	// der Tuer, die Knoepfe in drei Reihen zu fuenf (15 Etagen).
	AddBox(TEXT("CabPanel"), CabRoot, FVector(-381.0, 326.0, 150.0),
		FVector(4.0, 90.0, 190.0), Metal, false);
	for (int32 Floor = 0; Floor < Dims.FloorCount; ++Floor)
	{
		const int32 Reihe = Floor / 5;
		const int32 Spalte = Floor % 5;
		AddCylinder(*FString::Printf(TEXT("CabButton_%02d"), Floor), CabRoot,
			FVector(-378.0, 356.0 - Spalte * 15.0, 205.0 - Reihe * 55.0),
			FVector(6.0, 6.0, 3.0), Light, FRotator(90.0, 0.0, 0.0));
	}
	// Etagenanzeige ueber der Tuer, Lampenfeld in der Decke, Handlauf hinten.
	AddBox(TEXT("CabDisplay"), CabRoot, FVector(-381.0, CabinY, 268.0),
		FVector(4.0, 110.0, 18.0), Light, false);
	AddBox(TEXT("CabLight"), CabRoot, FVector(-20.0, CabinY, 278.0),
		FVector(280.0, 110.0, 3.0), Light, false);
	AddBox(TEXT("CabRail"), CabRoot, FVector(341.0, CabinY, 100.0),
		FVector(6.0, 250.0, 6.0), Metal, false);

	LandingLeft.Reserve(Dims.FloorCount);
	LandingRight.Reserve(Dims.FloorCount);
	for (int32 Floor = 0; Floor < Dims.FloorCount; ++Floor)
	{
		const double Z = Floor * Dims.FloorHeightCm + Dims.SlabCm + 130.0;
		LandingLeft.Add(AddBox(*FString::Printf(TEXT("LandingLeft_%02d"), Floor), Root,
			FVector(-437.5, CabinY - DoorHalfWidthCm * 0.5, Z),
			FVector(10.0, DoorPanelWidthCm, 260.0), Metal));
		LandingRight.Add(AddBox(*FString::Printf(TEXT("LandingRight_%02d"), Floor), Root,
			FVector(-437.5, CabinY + DoorHalfWidthCm * 0.5, Z),
			FVector(10.0, DoorPanelWidthCm, 260.0), Metal));
		// Ruf-Taster auf der Bueroseite der Wand (-X), auf Hoehe der
		// Interaktionsposition (Handhoehe ueber dem Geschossboden). Schild und
		// Knopf sitzen buendig auf der Wand, ohne Kollision.
		AddBox(*FString::Printf(TEXT("CallPlate_%02d"), Floor), Root,
			FVector(-451.5, CabinY, Z - 40.0), FVector(3.0, 30.0, 80.0), Metal, false);
		AddCylinder(*FString::Printf(TEXT("CallButton_%02d"), Floor), Root,
			FVector(-454.5, CabinY, Z - 40.0), FVector(8.0, 8.0, 3.0), Light,
			FRotator(90.0, 0.0, 0.0));
	}
	UpdateDoorPositions();
	bInitialized = true;
	if (FParse::Param(FCommandLine::Get(), TEXT("WbLiftProbe")))
	{
		RequestFloor(Dims.FloorCount - 1);
	}
	UE_LOG(LogWbHqElevator, Log, TEXT("Tower-Aufzug: %d Etagen, Kabine und Schachttueren gebaut."),
		Dims.FloorCount);
}

bool AWiesbadenSebboHqElevator::RequestFloor(int32 Floor)
{
	const bool bAccepted = bInitialized && Lift.RequestFloor(Floor);
	if (bAccepted)
	{
		UE_LOG(LogWbHqElevator, Log, TEXT("Tower-Aufzug: Etage %d angefordert (aktuell %d)."),
			Floor, Lift.CurrentFloor);
	}
	return bAccepted;
}

bool AWiesbadenSebboHqElevator::IsPlayerInCab(const FVector& LocalPlayer) const
{
	const double ExpectedZ = Dims.SlabCm + Lift.PositionCm + 90.0;
	return LocalPlayer.X > -350.0 && LocalPlayer.X < 320.0
		&& LocalPlayer.Y > 80.0 && LocalPlayer.Y < 360.0
		&& FMath::Abs(LocalPlayer.Z - ExpectedZ) < 110.0;
}

void AWiesbadenSebboHqElevator::HandlePlayerInput()
{
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	AWiesbadenFootPawn* Foot = PC ? Cast<AWiesbadenFootPawn>(PC->GetPawn()) : nullptr;
	if (!Foot)
	{
		return;
	}
	const FVector Local = GetActorTransform().InverseTransformPosition(Foot->GetActorLocation());
	if (IsPlayerInCab(Local))
	{
		int32 Selected = INDEX_NONE;
		const FKey DigitKeys[] = { EKeys::Zero, EKeys::One, EKeys::Two, EKeys::Three,
			EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine };
		for (int32 Floor = 0; Floor < UE_ARRAY_COUNT(DigitKeys); ++Floor)
		{
			if (PC->WasInputKeyJustPressed(DigitKeys[Floor]))
			{
				Selected = Floor;
				break;
			}
		}
		if (PC->WasInputKeyJustPressed(EKeys::PageUp))
		{
			Selected = FMath::Min(Lift.CurrentFloor + 1, Dims.FloorCount - 1);
		}
		if (PC->WasInputKeyJustPressed(EKeys::PageDown))
		{
			Selected = FMath::Max(Lift.CurrentFloor - 1, 0);
		}
		if (Selected != INDEX_NONE && RequestFloor(Selected))
		{
			PC->ClientMessage(FString::Printf(TEXT("SebboTower: Aufzug zu Etage %d"), Selected));
		}
		return;
	}

	if (!PC->WasInputKeyJustPressed(EKeys::E))
	{
		return;
	}
	const int32 Floor = FMath::RoundToInt(
		(Local.Z - Dims.SlabCm - 90.0) / Dims.FloorHeightCm);
	if (Floor < 0 || Floor >= Dims.FloorCount)
	{
		return;
	}
	const FVector Button(-520.0, CabinY, Floor * Dims.FloorHeightCm + Dims.SlabCm + 90.0);
	if (FVector::Dist(Local, Button) <= 230.0 && RequestFloor(Floor))
	{
		PC->ClientMessage(FString::Printf(TEXT("SebboTower: Aufzug kommt zu Etage %d"), Floor));
	}
}

void AWiesbadenSebboHqElevator::UpdateDoorPositions()
{
	const auto SetDoorY = [](UStaticMeshComponent* Door, double Y)
	{
		if (Door)
		{
			FVector P = Door->GetRelativeLocation();
			P.Y = Y;
			Door->SetRelativeLocation(P);
		}
	};
	SetDoorY(CabDoorLeft, CabinY - DoorHalfWidthCm * 0.5 - DoorSlideCm * Lift.DoorOpenFraction);
	SetDoorY(CabDoorRight, CabinY + DoorHalfWidthCm * 0.5 + DoorSlideCm * Lift.DoorOpenFraction);
	for (int32 Floor = 0; Floor < LandingLeft.Num(); ++Floor)
	{
		const double Open = Floor == Lift.CurrentFloor && Lift.Phase != EHqElevatorPhase::Moving
			? Lift.DoorOpenFraction : 0.0;
		SetDoorY(LandingLeft[Floor], CabinY - DoorHalfWidthCm * 0.5 - DoorSlideCm * Open);
		SetDoorY(LandingRight[Floor], CabinY + DoorHalfWidthCm * 0.5 + DoorSlideCm * Open);
	}
}

void AWiesbadenSebboHqElevator::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bInitialized || !CabRoot)
	{
		return;
	}
	HandlePlayerInput();
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	AWiesbadenFootPawn* Foot = PC ? Cast<AWiesbadenFootPawn>(PC->GetPawn()) : nullptr;
	const bool bCarryPlayer = Foot
		&& IsPlayerInCab(GetActorTransform().InverseTransformPosition(Foot->GetActorLocation()));
	const double BeforeCm = Lift.PositionCm;
	Lift.Tick(DeltaSeconds);
	const double MovedCm = Lift.PositionCm - BeforeCm;
	CabRoot->SetRelativeLocation(FVector(0.0, 0.0, Dims.SlabCm + Lift.PositionCm));
	UpdateDoorPositions();
	if (bCarryPlayer && !FMath::IsNearlyZero(MovedCm))
	{
		Foot->AddActorWorldOffset(FVector(0.0, 0.0, MovedCm), false);
	}
	if (!bProbeComplete && FParse::Param(FCommandLine::Get(), TEXT("WbLiftProbe"))
		&& Lift.IsStoppedAt(Dims.FloorCount - 1) && Lift.DoorOpenFraction >= 1.0)
	{
		bProbeComplete = true;
		UE_LOG(LogWbHqElevator, Log,
			TEXT("Tower-Aufzug-Probe: Etage %d erreicht, Kabine Z=%.0f cm, Tueren offen."),
			Lift.CurrentFloor, Dims.SlabCm + Lift.PositionCm);
	}
}
