// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Vehicles/WiesbadenLegacyHelicopter.h"

#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

AWiesbadenLegacyHelicopter::AWiesbadenLegacyHelicopter()
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	// Dieselben Netze und Materialien wie der Spielerheli VOR dem Ka-52-Neubau.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> BodyAsset(
		TEXT("/Game/Assets/Landmarks/HeliBody/StaticMeshes/SM_HeliBody.SM_HeliBody"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> UpperAsset(
		TEXT("/Game/Assets/Landmarks/HeliRotorUpper/StaticMeshes/SM_HeliRotorUpper.SM_HeliRotorUpper"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> LowerAsset(
		TEXT("/Game/Assets/Landmarks/HeliRotorLower/StaticMeshes/SM_HeliRotorLower.SM_HeliRotorLower"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> PaintAsset(
		TEXT("/Game/Materials/City/M_WbHelicopter.M_WbHelicopter"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> RotorAsset(
		TEXT("/Game/Assets/Landmarks/M_HeliRotorBase.M_HeliRotorBase"));

	// Massstab des ALTEN Modells: es ist im Original nur 100,7 cm lang
	// (Rumpflaenge 14,2 m des echten Ka-52 -> Faktor 14,5).
	constexpr float ModelBodyLengthCm = 100.7f;
	constexpr float TargetBodyLengthCm = 1460.0f;
	const float ModelScale = TargetBodyLengthCm / ModelBodyLengthCm;

	const FRotator ModelYaw(0.0f, -90.0f, 0.0f);
	constexpr float UpperRotorHeightCm = 345.0f;
	constexpr float LowerRotorHeightCm = 300.0f;

	// Wo sitzt der Mast am Rumpf bzw. die Nabe im Rotormesh? Beides sind
	// MESSUNGEN am alten Modell (Vertex-Median der Rotoren, Schwerpunkt der
	// hohen Aufbauten am Rumpf) - ohne sie kreisen die Blaetter neben dem Mast.
	const FVector2D MastInBodyModelCm(1.0f, -5.0f);
	const FVector2D RotorHubInModelCm(0.0f, 33.0f);
	const FVector MastOffset =
		ModelYaw.RotateVector(FVector(MastInBodyModelCm.X, MastInBodyModelCm.Y, 0.0f)) * ModelScale;
	const FVector RotorSelfHub =
		ModelYaw.RotateVector(FVector(RotorHubInModelCm.X, RotorHubInModelCm.Y, 0.0f)) * ModelScale;

	FuselageMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LegacyFuselage"));
	FuselageMesh->SetupAttachment(Root);
	if (BodyAsset.Succeeded())
	{
		FuselageMesh->SetStaticMesh(BodyAsset.Object);
		FuselageMesh->SetRelativeRotation(ModelYaw);
		FuselageMesh->SetRelativeScale3D(FVector(ModelScale));
		// Sichtbar solide: das Standstueck soll nicht durchschreitbar sein.
		FuselageMesh->SetCollisionProfileName(TEXT("BlockAll"));
		if (PaintAsset.Succeeded()) { FuselageMesh->SetMaterial(0, PaintAsset.Object); }
	}

	UpperRotorHub = CreateDefaultSubobject<USceneComponent>(TEXT("LegacyUpperRotorHub"));
	UpperRotorHub->SetupAttachment(Root);
	UpperRotorHub->SetRelativeLocation(FVector(MastOffset.X, MastOffset.Y, UpperRotorHeightCm));

	UpperRotorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LegacyUpperRotor"));
	UpperRotorMesh->SetupAttachment(UpperRotorHub);
	if (UpperAsset.Succeeded())
	{
		UpperRotorMesh->SetStaticMesh(UpperAsset.Object);
		UpperRotorMesh->SetRelativeRotation(ModelYaw);
		UpperRotorMesh->SetRelativeScale3D(FVector(ModelScale));
		// Der Rotor traegt seinen eigenen Ursprung: um den Nabenversatz
		// zurueckgeschoben (GEDREHT abgezogen, in denselben Achsen wie die
		// Geometrie) kreist die Scheibe genau ueber dem Mast.
		UpperRotorMesh->SetRelativeLocation(-RotorSelfHub);
		UpperRotorMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		if (RotorAsset.Succeeded()) { UpperRotorMesh->SetMaterial(0, RotorAsset.Object); }
	}

	LowerRotorHub = CreateDefaultSubobject<USceneComponent>(TEXT("LegacyLowerRotorHub"));
	LowerRotorHub->SetupAttachment(Root);
	LowerRotorHub->SetRelativeLocation(FVector(MastOffset.X, MastOffset.Y, LowerRotorHeightCm));

	LowerRotorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LegacyLowerRotor"));
	LowerRotorMesh->SetupAttachment(LowerRotorHub);
	if (LowerAsset.Succeeded())
	{
		LowerRotorMesh->SetStaticMesh(LowerAsset.Object);
		LowerRotorMesh->SetRelativeRotation(ModelYaw);
		LowerRotorMesh->SetRelativeScale3D(FVector(ModelScale));
		LowerRotorMesh->SetRelativeLocation(-RotorSelfHub);
		LowerRotorMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		if (RotorAsset.Succeeded()) { LowerRotorMesh->SetMaterial(0, RotorAsset.Object); }
	}

#if WITH_EDITOR
	// Ohne die Netze bleibt nur ein leerer Actor - im Log sagen, warum.
	UE_LOG(LogTemp, Log, TEXT("Alter Heli als Standstueck: Rumpf=%d oberer Rotor=%d unterer Rotor=%d."),
		BodyAsset.Succeeded() ? 1 : 0, UpperAsset.Succeeded() ? 1 : 0, LowerAsset.Succeeded() ? 1 : 0);
#endif
}

double AWiesbadenLegacyHelicopter::GetNoseToTailCm() const
{
	if (!FuselageMesh || !FuselageMesh->GetStaticMesh())
	{
		return 0.0;
	}
	const FBox Box = FuselageMesh->GetStaticMesh()->GetBoundingBox();
	// Das Modell ist 1,007 m lang und wird auf 14,6 m skaliert; die Laengsachse
	// liegt im Modell auf X (deshalb die Gierdrehung fuer die Blickrichtung).
	const double Scale = FuselageMesh->GetRelativeScale3D().X;
	return FMath::Max(Box.GetSize().X, Box.GetSize().Y) * Scale;
}

double AWiesbadenLegacyHelicopter::GetUpperRotorDiameterCm() const
{
	if (!UpperRotorMesh || !UpperRotorMesh->GetStaticMesh())
	{
		return 0.0;
	}
	const FVector Size = UpperRotorMesh->GetStaticMesh()->GetBoundingBox().GetSize();
	const double Scale = UpperRotorMesh->GetRelativeScale3D().X;
	return FMath::Max(Size.X, Size.Y) * Scale;
}
