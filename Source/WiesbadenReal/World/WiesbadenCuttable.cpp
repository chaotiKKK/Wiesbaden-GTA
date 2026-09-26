// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/WiesbadenCuttable.h"

#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogWbCut, Log, All);

#include "Weapons/WiesbadenCutMath.h"

AWiesbadenCuttable::AWiesbadenCuttable()
{
	PrimaryActorTick.bCanEverTick = true;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	// Zwei vorbereitete Stuecke uebereinander: der Maschinenblock aus
	// Tools/Blender/build_cutpieces.py, die Schnittfuge bei Z 50. Die
	// Blender-Meshes sind massstaeblich (50 cm Kern je Stueck, Ursprung in
	// der Stueck-Mitte) - die Engine-Wuerfel bleiben der Rueckfall, weil die
	// uassets git-ignoriert sind und nicht voraussetzbar.
	PieceBelow = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PieceBelow"));
	PieceBelow->SetupAttachment(Root);
	PieceBelow->SetRelativeLocation(FVector(0.0f, 0.0f, 25.0f));
	PieceBelow->SetRelativeScale3D(FVector(0.5f, 0.5f, 0.5f));

	PieceAbove = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PieceAbove"));
	PieceAbove->SetupAttachment(Root);
	PieceAbove->SetRelativeLocation(FVector(0.0f, 0.0f, 75.0f));
	PieceAbove->SetRelativeScale3D(FVector(0.5f, 0.5f, 0.5f));

	EmberFace = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("EmberFace"));
	EmberFace->SetupAttachment(Root);
	EmberFace->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	EmberFace->SetVisibility(false);

	EmberLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("EmberLight"));
	EmberLight->SetupAttachment(Root);
	EmberLight->SetIntensity(0.0f);
	EmberLight->SetVisibility(false);
	EmberLight->SetCastShadows(false);
	EmberLight->SetLightColor(FLinearColor(1.0f, 0.32f, 0.06f));

	// Blender-Meshes (import_cutpieces.py) vorziehen; was fehlt, faellt auf
	// den Engine-Wuerfel zurueck - nie auf nichts.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> UntenFinder(
		TEXT("/Game/Waffen/Cutpieces/SM_Cutpiece_Unten.SM_Cutpiece_Unten"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ObenFinder(
		TEXT("/Game/Waffen/Cutpieces/SM_Cutpiece_Oben.SM_Cutpiece_Oben"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> GlutFinder(
		TEXT("/Game/Waffen/Cutpieces/SM_Cutpiece_Glut.SM_Cutpiece_Glut"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(
		TEXT("/Engine/BasicShapes/Cube.Cube"));

	if (UntenFinder.Succeeded() && ObenFinder.Succeeded())
	{
		// Massstaeblich (Ursprung in der Stueck-Mitte): nicht skalieren.
		PieceBelow->SetStaticMesh(UntenFinder.Object);
		PieceBelow->SetRelativeScale3D(FVector::OneVector);
		PieceAbove->SetStaticMesh(ObenFinder.Object);
		PieceAbove->SetRelativeScale3D(FVector::OneVector);
	}
	else if (CubeFinder.Succeeded())
	{
		UE_LOG(LogWbCut, Warning, TEXT("Cutpiece-Meshes fehlen "
			"(Tools/import_cutpieces.cmd) - Engine-Wuerfel als Rueckfall."));
		PieceBelow->SetStaticMesh(CubeFinder.Object);
		PieceAbove->SetStaticMesh(CubeFinder.Object);
	}

	if (GlutFinder.Succeeded())
	{
		// Glutkante: 52 x 52 x 2 cm aus Blender, nicht skalieren.
		EmberFace->SetStaticMesh(GlutFinder.Object);
		EmberFace->SetRelativeScale3D(FVector::OneVector);
	}
	else if (CubeFinder.Succeeded())
	{
		EmberFace->SetStaticMesh(CubeFinder.Object);
		EmberFace->SetRelativeScale3D(FVector(0.52f, 0.52f, 0.02f));
	}
}

void AWiesbadenCuttable::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (EmberRemaining <= 0.0f)
	{
		return;
	}

	EmberRemaining = FMath::Max(0.0f, EmberRemaining - DeltaSeconds);
	const float Alpha = EmberSeconds > KINDA_SMALL_NUMBER
		? EmberRemaining / EmberSeconds
		: 0.0f;

	if (EmberLight)
	{
		EmberLight->SetIntensity(EmberIntensity * Alpha);
	}

	if (EmberRemaining <= 0.0f)
	{
		if (EmberLight)
		{
			EmberLight->SetVisibility(false);
		}
		if (EmberFace)
		{
			EmberFace->SetVisibility(false);
		}
	}
}

bool AWiesbadenCuttable::ApplyCut(const FVector& CutPoint, const FVector& PlaneNormal)
{
	if (bCutDone || !PieceBelow || !PieceAbove)
	{
		return false;
	}

	USceneComponent* Pieces[2] = { PieceBelow, PieceAbove };
	const FVector Centres[2] = {
		PieceBelow->GetComponentLocation(), PieceAbove->GetComponentLocation() };

	// Das abfallende Stueck: auf der negativen Seite. Streifen allein laesst
	// alles stehen; liegen beide Stuecke daneben, faellt das tiefere ab.
	int32 FallIndex = INDEX_NONE;
	float Deepest = 0.0f;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		if (!WiesbadenCutMath::ShouldDetach(Centres[Index], CutPoint, PlaneNormal))
		{
			continue;
		}
		const float Side = WiesbadenCutMath::SignedDistanceToPlane(
			Centres[Index], CutPoint, PlaneNormal);
		if (FallIndex == INDEX_NONE || Side < Deepest)
		{
			Deepest = Side;
			FallIndex = Index;
		}
	}

	if (FallIndex == INDEX_NONE)
	{
		// Die Kante hat nur gestriffen: kein Stueck liegt auf der
		// abfallenden Seite.
		return false;
	}

	USceneComponent* Fall = Pieces[FallIndex];
	UPrimitiveComponent* FallPrimitive = Cast<UPrimitiveComponent>(Fall);
	if (FallPrimitive)
	{
		FallPrimitive->SetMobility(EComponentMobility::Movable);

		// Physik nur, wenn die Welt eine Szene hat: in reinen Testwelten
		// (UWorld::CreateWorld ohne Physik) wuerde das Umstellen scheitern.
		UWorld* World = GetWorld();
		if (World && World->GetPhysicsScene())
		{
			FallPrimitive->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			FallPrimitive->SetSimulatePhysics(true);
		}
	}

	FallenPiece = Fall;
	bCutDone = true;
	AttachEmber(Fall, CutPoint);
	return true;
}

void AWiesbadenCuttable::AttachEmber(USceneComponent* Piece, const FVector& CutPoint)
{
	if (EmberFace)
	{
		EmberFace->AttachToComponent(Piece, FAttachmentTransformRules::KeepWorldTransform);
		EmberFace->SetWorldLocation(CutPoint);
		EmberFace->SetVisibility(true);

		// Glut: warmes Orange auf der Schnittflaeche (Grundmaterial mit
		// Farbparameter - die Projekt-Materialien sind eigene Assets und
		// nicht voraussetzbar).
		if (UMaterialInterface* Base = EmberFace->GetMaterial(0))
		{
			UMaterialInstanceDynamic* Glut =
				UMaterialInstanceDynamic::Create(Base, this);
			if (Glut)
			{
				Glut->SetVectorParameterValue(
					TEXT("Color"), FLinearColor(1.0f, 0.22f, 0.03f));
				EmberFace->SetMaterial(0, Glut);
			}
		}
	}

	if (EmberLight)
	{
		EmberLight->AttachToComponent(Piece, FAttachmentTransformRules::KeepWorldTransform);
		EmberLight->SetWorldLocation(CutPoint);
		EmberLight->SetIntensity(EmberIntensity);
		EmberLight->SetVisibility(true);
	}

	EmberRemaining = EmberSeconds;
}
