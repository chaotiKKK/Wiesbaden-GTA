// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "NPC/WiesbadenSylvia.h"

#include "CollisionQueryParams.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GIS/WiesbadenWorldBuilder.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "UI/WiesbadenThoughtBubbleWidget.h"
#include "Vehicles/WiesbadenCarSpawn.h"

#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogWbSylvia, Log, All);

const TCHAR* AWiesbadenSylvia::GetSylviaMeshPath()
{
	return TEXT("/Game/Assets/People/Sylvia/sylvia/SkeletalMeshes/tripo_part_0.tripo_part_0");
}

FString AWiesbadenSylvia::GetSylviaMeshPartPath(int32 PartIndex)
{
	return FString::Printf(
		TEXT("/Game/Assets/People/Sylvia/sylvia/SkeletalMeshes/tripo_part_%d.tripo_part_%d"),
		PartIndex,
		PartIndex);
}

int32 AWiesbadenSylvia::GetFigurePartCount()
{
	return 15;
}

const TCHAR* AWiesbadenSylvia::GetWoodShavingsSystemPath()
{
	return TEXT("/Game/Niagara/NS_SylviaWoodShavings.NS_SylviaWoodShavings");
}

const TCHAR* AWiesbadenSylvia::GetThoughtBubbleText()
{
	return UWiesbadenThoughtBubbleWidget::GetThoughtText();
}

FGeoCoordinate AWiesbadenSylvia::GetPlatterStrasse144Coordinate()
{
	return FGeoCoordinate(8.2234186, 50.0932604, 0.0);
}

AWiesbadenSylvia::AWiesbadenSylvia()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	for (int32 PartIndex = 0; PartIndex < GetFigurePartCount(); ++PartIndex)
	{
		const FName ComponentName = PartIndex == 0
			? FName(TEXT("SylviaFigure"))
			: *FString::Printf(TEXT("SylviaFigurePart_%02d"), PartIndex);
		UPoseableMeshComponent* FigurePart = CreateDefaultSubobject<UPoseableMeshComponent>(ComponentName);
		FigurePart->SetupAttachment(Root);
		FigurePart->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		FigureParts.Add(FigurePart);

		const FString MeshPath = GetSylviaMeshPartPath(PartIndex);
		ConstructorHelpers::FObjectFinder<USkeletalMesh> FigureFinder(*MeshPath);
		if (FigureFinder.Succeeded())
		{
			FigurePart->SetSkinnedAssetAndUpdate(FigureFinder.Object);
		}
	}

	Workbench = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Workbench"));
	Workbench->SetupAttachment(Root);
	Workbench->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	WoodBlock = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WoodBlock"));
	WoodBlock->SetupAttachment(Root);
	WoodBlock->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	ThoughtBubble = CreateDefaultSubobject<UWidgetComponent>(TEXT("ThoughtBubble"));
	ThoughtBubble->SetupAttachment(Root);
	ThoughtBubble->SetWidgetSpace(EWidgetSpace::World);
	ThoughtBubble->SetDrawSize(FVector2D(620.0f, 170.0f));
	ThoughtBubble->SetDrawAtDesiredSize(false);
	ThoughtBubble->SetTwoSided(true);
	ThoughtBubble->SetBlendMode(EWidgetBlendMode::Transparent);
	ThoughtBubble->SetPivot(FVector2D(0.5f, 1.0f));
	ThoughtBubble->SetRelativeLocation(FVector(0.0f, 0.0f, 275.0f));
	ThoughtBubble->SetRelativeScale3D(FVector(0.35f));
	ThoughtBubble->SetRelativeRotation(FRotator(0.0f, 180.0f, 0.0f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeFinder.Succeeded())
	{
		PrimitiveCube = CubeFinder.Object;
		Workbench->SetStaticMesh(PrimitiveCube);
		WoodBlock->SetStaticMesh(PrimitiveCube);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> WoodFinder(
		TEXT("/Game/Nerobergbahn/Materials/MI_Nb_NbHolz.MI_Nb_NbHolz"));
	if (WoodFinder.Succeeded())
	{
		WoodMaterial = WoodFinder.Object;
		WoodBlock->SetMaterial(0, WoodMaterial);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> DarkWoodFinder(
		TEXT("/Game/Nerobergbahn/Materials/MI_Nb_NbHolzDunkel.MI_Nb_NbHolzDunkel"));
	if (DarkWoodFinder.Succeeded())
	{
		DarkWoodMaterial = DarkWoodFinder.Object;
		Workbench->SetMaterial(0, DarkWoodMaterial);
	}

	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> ShavingsFinder(
		GetWoodShavingsSystemPath());
	if (ShavingsFinder.Succeeded())
	{
		WoodShavingsSystem = ShavingsFinder.Object;
	}
}

void AWiesbadenSylvia::BeginPlay()
{
	Super::BeginPlay();

	Converter = NewObject<UGeoCoordinateConverter>(this, TEXT("SylviaGeoConverter"));
	if (Converter)
	{
		Converter->InitializeWithWiesbadenOrigin();
	}

	if (ThoughtBubble)
	{
		ThoughtBubble->SetWidgetClass(UWiesbadenThoughtBubbleWidget::StaticClass());
		ThoughtBubble->InitWidget();
		ThoughtBubble->SetVisibility(true);
		if (!ThoughtBubble->GetUserWidgetObject())
		{
			UE_LOG(LogWbSylvia, Warning, TEXT("Sylvia: Gedankenblase konnte nicht instanziiert werden."));
		}
	}

	ConfigureComponents();
}

void AWiesbadenSylvia::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bSceneBuilt)
	{
		TryBuildAtPlatterStrasse144();
		return;
	}

	UpdatePlaningPose(DeltaSeconds);
}

bool AWiesbadenSylvia::TryBuildAtPlatterStrasse144()
{
	UWorld* World = GetWorld();
	if (!World || !Converter)
	{
		return false;
	}

	AWiesbadenWorldBuilder* Builder = nullptr;
	for (TActorIterator<AWiesbadenWorldBuilder> It(World); It; ++It)
	{
		if (It->RoadNetwork.Lanes.Num() > 0)
		{
			Builder = *It;
			break;
		}
	}
	if (!Builder)
	{
		return false;
	}

	if (!Converter->Initialize(
		Builder->bUseWiesbadenOrigin ? FGeoCoordinate(8.2410, 50.0824, 0.0) : Builder->CustomOrigin))
	{
		return false;
	}

	const FVector AddressWorld = Converter->GeoToUnrealGround(GetPlatterStrasse144Coordinate());
	FVector RoadPoint;
	FRotator RoadRotation;
	int32 LaneId = INDEX_NONE;
	if (!FWiesbadenCarSpawn::FindNearestDrivableLanePoint(
		Builder->RoadNetwork, AddressWorld, 25000.0, RoadPoint, RoadRotation, LaneId))
	{
		return false;
	}

	FVector ToRoad = (RoadPoint - AddressWorld).GetSafeNormal2D();
	if (ToRoad.IsNearlyZero())
	{
		ToRoad = RoadRotation.RotateVector(FVector::RightVector).GetSafeNormal2D();
	}

	// Stand between the building centroid and its nearest road, so the scene
	// is visible from the address without putting Sylvia in the carriageway.
	const double RoadDistance = FVector2D::Distance(
		FVector2D(AddressWorld.X, AddressWorld.Y),
		FVector2D(RoadPoint.X, RoadPoint.Y));
	const double TowardRoad = FMath::Clamp(RoadDistance * 0.45, 250.0, 650.0);
	const FVector CandidateXY = AddressWorld + ToRoad * TowardRoad;

	double GroundZ = 0.0;
	if (!ResolveGround(CandidateXY, GroundZ))
	{
		return false;
	}

	const FVector SylviaLocation(CandidateXY.X, CandidateXY.Y, GroundZ);
	const FRotator FaceBuilding = (-ToRoad).Rotation();
	SetActorLocationAndRotation(SylviaLocation, FRotator(0.0f, FaceBuilding.Yaw, 0.0f));

	bool bAllFigurePartsLoaded = FigureParts.Num() == GetFigurePartCount();
	for (const TObjectPtr<UPoseableMeshComponent>& FigurePart : FigureParts)
	{
		bAllFigurePartsLoaded &= FigurePart && FigurePart->GetSkinnedAsset();
	}
	if (!bAllFigurePartsLoaded || !WoodShavingsSystem)
	{
		if (!bReportedMissingAsset)
		{
			bReportedMissingAsset = true;
			UE_LOG(LogWbSylvia, Error,
				TEXT("Sylvia: Asset fehlt (MeshParts=%d/%d, Niagara=%d) - Szene bleibt unsichtbar."),
				bAllFigurePartsLoaded ? FigureParts.Num() : 0,
				GetFigurePartCount(),
				WoodShavingsSystem ? 1 : 0);
		}
		return false;
	}

	if (!WoodShavingsFX)
	{
		WoodShavingsFX = UNiagaraFunctionLibrary::SpawnSystemAttached(
			WoodShavingsSystem,
			Root,
			NAME_None,
			FVector(145.0f, 0.0f, 104.0f),
			FRotator(-38.0f, 0.0f, 0.0f),
			FVector(1.35f),
			EAttachLocation::KeepRelativeOffset,
			false,
			ENCPoolMethod::None,
			true,
			true);
	}

	bSceneBuilt = WoodShavingsFX != nullptr;
	if (bSceneBuilt)
	{
		static bool bLoggedFigureDiagnostics = false;
		if (!bLoggedFigureDiagnostics && FigureParts.Num() > 0 && FigureParts[0])
		{
			bLoggedFigureDiagnostics = true;
			const FBoxSphereBounds Bounds = FigureParts[0]->CalcBounds(FTransform::Identity);
			UE_LOG(LogWbSylvia, Log,
				TEXT("Sylvia figure bounds local: min(%.1f, %.1f, %.1f) max(%.1f, %.1f, %.1f)"),
				Bounds.GetBox().Min.X, Bounds.GetBox().Min.Y, Bounds.GetBox().Min.Z,
				Bounds.GetBox().Max.X, Bounds.GetBox().Max.Y, Bounds.GetBox().Max.Z);
			UE_LOG(LogWbSylvia, Log, TEXT("Sylvia part 0 bones: %d"), FigureParts[0]->GetNumBones());
			for (int32 BoneIndex = 0; BoneIndex < FMath::Min(8, FigureParts[0]->GetNumBones()); ++BoneIndex)
			{
				UE_LOG(LogWbSylvia, Log, TEXT("Sylvia part 0 bone[%d] %s"),
				BoneIndex, *FigureParts[0]->GetBoneName(BoneIndex).ToString());
			}
			for (const FName BoneName : {FName(TEXT("mixamorig_Hips")), FName(TEXT("mixamorig_LeftUpLeg")), FName(TEXT("mixamorig_LeftLeg")), FName(TEXT("mixamorig_LeftFoot")), FName(TEXT("mixamorig_RightUpLeg")), FName(TEXT("mixamorig_RightLeg")), FName(TEXT("mixamorig_RightFoot")), FName(TEXT("mixamorig_Head"))})
			{
				UE_LOG(LogWbSylvia, Log, TEXT("Sylvia bone %s loc %s rot %s"),
					*BoneName.ToString(),
					*FigureParts[0]->GetBoneLocationByName(BoneName, EBoneSpaces::ComponentSpace).ToString(),
					*FigureParts[0]->GetBoneRotationByName(BoneName, EBoneSpaces::ComponentSpace).ToString());
			}
		}
		UE_LOG(LogWbSylvia, Log,
			TEXT("Sylvia vor Platter Strasse 144 auf Spur %d bei (%.0f, %.0f, %.0f); Hobelspane aktiv."),
			LaneId, SylviaLocation.X, SylviaLocation.Y, SylviaLocation.Z);
	}
	return bSceneBuilt;
}

bool AWiesbadenSylvia::ResolveGround(const FVector& WorldXY, double& OutZ) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WbSylviaGround), true);
	Params.AddIgnoredActor(this);
	const FVector Start(WorldXY.X, WorldXY.Y, 100000.0);
	const FVector End(WorldXY.X, WorldXY.Y, -20000.0);
	if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params)
		&& !Hit.bStartPenetrating)
	{
		OutZ = Hit.Location.Z;
		return true;
	}
	return false;
}

void AWiesbadenSylvia::ConfigureComponents()
{
	if (!Workbench || !WoodBlock)
	{
		return;
	}

	Workbench->SetRelativeLocation(FVector(120.0f, 0.0f, 38.0f));
	Workbench->SetRelativeScale3D(FVector(1.55f, 0.58f, 0.38f));
	WoodBlock->SetRelativeLocation(FVector(135.0f, 0.0f, 82.0f));
	WoodBlock->SetRelativeRotation(FRotator(0.0f, 0.0f, -5.0f));
	WoodBlock->SetRelativeScale3D(FVector(1.35f, 0.32f, 0.10f));

	if (ThoughtBubble)
	{
		ThoughtBubble->SetRelativeLocation(FVector(0.0f, 0.0f, 210.0f));
		ThoughtBubble->SetRelativeScale3D(FVector(0.35f));
	}
}

void AWiesbadenSylvia::UpdatePlaningPose(float DeltaSeconds)
{
	if (FigureParts.Num() == 0)
	{
		return;
	}

	PlaningPhase = FMath::Fmod(PlaningPhase + DeltaSeconds * 9.0f, 2.0f * UE_PI);
	const float Saw = FMath::Sin(PlaningPhase);
	const float Push = FMath::Max(0.0f, Saw);

	// The GLB has a Mixamo-style armature but no animation clips. These cached
	// names are stable across the supplied mesh and keep the motion allocation-free.
	ApplyBoneRotation(TEXT("mixamorig_Spine"), FRotator(0.0f, 0.0f, Saw * 2.0f));
	ApplyBoneRotation(TEXT("mixamorig_LeftArm"), FRotator(0.0f, -20.0f - Push * 18.0f, 18.0f));
	ApplyBoneRotation(TEXT("mixamorig_RightArm"), FRotator(0.0f, -18.0f - Push * 22.0f, -18.0f));
	ApplyBoneRotation(TEXT("mixamorig_LeftForeArm"), FRotator(0.0f, -12.0f + Push * 35.0f, 0.0f));
	ApplyBoneRotation(TEXT("mixamorig_RightForeArm"), FRotator(0.0f, -10.0f + Push * 42.0f, 0.0f));
	ApplyBoneRotation(TEXT("mixamorig_LeftHand"), FRotator(0.0f, Push * 10.0f, 0.0f));
	ApplyBoneRotation(TEXT("mixamorig_RightHand"), FRotator(0.0f, Push * 10.0f, 0.0f));
}

void AWiesbadenSylvia::ApplyBoneRotation(FName BoneName, const FRotator& Rotation)
{
	for (const TObjectPtr<UPoseableMeshComponent>& FigurePart : FigureParts)
	{
		if (FigurePart && FigurePart->GetBoneIndex(BoneName) != INDEX_NONE)
		{
			FigurePart->SetBoneRotationByName(BoneName, Rotation, EBoneSpaces::ComponentSpace);
		}
	}
}
