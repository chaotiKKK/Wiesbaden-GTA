// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/WiesbadenParkFeatures.h"

#include "GIS/GeoCoordinateConverter.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"

DEFINE_LOG_CATEGORY_STATIC(LogWbPark, Log, All);

AWiesbadenParkFeatures::AWiesbadenParkFeatures()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
}

void AWiesbadenParkFeatures::BeginPlay()
{
	Super::BeginPlay();

	CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	CylinderMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	WaterMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/City/M_WbLmWater.M_WbLmWater"));
	StoneMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/City/M_WbLmWhite.M_WbLmWhite"));
	GravelMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/City/M_WbLmGravel.M_WbLmGravel"));
	HedgeMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/City/M_WbLmHedge.M_WbLmHedge"));

	Converter = NewObject<UGeoCoordinateConverter>(this);
	Converter->InitializeWithWiesbadenOrigin();

	// Echte Standorte (Karte/Referenzvideo).
	Specs.Add({ EParkKind::BowlingGreen, 50.0855, 8.2470, 55.0 });
	Specs.Add({ EParkKind::Reisinger,    50.0718, 8.2440,  0.0 });
}

bool AWiesbadenParkFeatures::ResolveGround(const FVector& WorldXY, double& OutZ) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WbParkGround), true);
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

void AWiesbadenParkFeatures::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bAllBuilt || !Converter)
	{
		return;
	}

	bool bAll = true;
	for (FParkSpec& Spec : Specs)
	{
		if (Spec.bBuilt)
		{
			continue;
		}
		FGeoCoordinate Coord;
		Coord.Latitude = Spec.Lat;
		Coord.Longitude = Spec.Lon;
		Coord.Height = 0.0;
		const FVector Ground = Converter->GeoToUnrealGround(Coord);

		double Z = 0.0;
		if (!ResolveGround(Ground, Z))
		{
			bAll = false;   // Zelle noch nicht gestreamt
			continue;
		}

		const FVector Base(Ground.X, Ground.Y, Z);
		const FRotator Yaw(0.0, Spec.HeadingDeg, 0.0);
		if (Spec.Kind == EParkKind::BowlingGreen)
		{
			BuildBowlingGreen(Base, Yaw);
		}
		else
		{
			BuildReisinger(Base, Yaw);
		}
		Spec.bBuilt = true;
		UE_LOG(LogWbPark, Log, TEXT("Parkanlage gebaut bei (%.0f, %.0f, %.0f)."), Base.X, Base.Y, Base.Z);
	}

	if (bAll)
	{
		bAllBuilt = true;
		SetActorTickEnabled(false);
	}
}

void AWiesbadenParkFeatures::AddPart(UStaticMesh* Mesh, const FVector& BaseWorld, const FRotator& BaseYaw,
	const FVector& LocalCm, const FVector& SizeCm, UMaterialInterface* Material)
{
	if (!Mesh)
	{
		return;
	}
	UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(this);
	Comp->SetStaticMesh(Mesh);
	Comp->SetupAttachment(Root);
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Comp->RegisterComponent();
	Comp->SetWorldLocation(BaseWorld + BaseYaw.RotateVector(LocalCm));
	Comp->SetWorldRotation(BaseYaw);
	Comp->SetWorldScale3D(SizeCm);
	if (Material)
	{
		Comp->SetMaterial(0, Material);
	}
	Parts.Add(Comp);
}

void AWiesbadenParkFeatures::AddBox(const FVector& BaseWorld, const FRotator& BaseYaw,
	double LX, double LY, double BaseZ, double WidthX, double DepthY, double HeightZ,
	UMaterialInterface* Material)
{
	const FVector Local(LX, LY, BaseZ + HeightZ * 0.5);
	const FVector Scale(WidthX / 100.0, DepthY / 100.0, HeightZ / 100.0);
	AddPart(CubeMesh, BaseWorld, BaseYaw, Local, Scale, Material);
}

void AWiesbadenParkFeatures::AddCyl(const FVector& BaseWorld, const FRotator& BaseYaw,
	double LX, double LY, double BaseZ, double RadiusCm, double HeightCm, UMaterialInterface* Material)
{
	// Engine-Cylinder = 100 cm hoch, Radius 50, Ursprung Mitte.
	const FVector Local(LX, LY, BaseZ + HeightCm * 0.5);
	const FVector Scale(RadiusCm / 50.0, RadiusCm / 50.0, HeightCm / 100.0);
	AddPart(CylinderMesh, BaseWorld, BaseYaw, Local, Scale, Material);
}

void AWiesbadenParkFeatures::AddBasin(const FVector& BaseWorld, const FRotator& BaseYaw,
	double LX, double LY, double HalfWidthCm, double HalfLenCm)
{
	// Steinfassung (etwas groesser) + eingesenktes Wasser knapp unter der Kante.
	AddBox(BaseWorld, BaseYaw, LX, LY, 0.0,
		HalfWidthCm * 2.0 + 70.0, HalfLenCm * 2.0 + 70.0, 32.0, StoneMat);
	AddBox(BaseWorld, BaseYaw, LX, LY, 8.0,
		HalfWidthCm * 2.0, HalfLenCm * 2.0, 20.0, WaterMat);
}

void AWiesbadenParkFeatures::AddFountain(const FVector& BaseWorld, const FRotator& BaseYaw,
	double LX, double LY, double RadiusCm)
{
	AddCyl(BaseWorld, BaseYaw, LX, LY, 0.0, RadiusCm, 46.0, StoneMat);        // Beckenschale
	AddCyl(BaseWorld, BaseYaw, LX, LY, 26.0, RadiusCm - 14.0, 18.0, WaterMat); // Wasserspiegel
	AddCyl(BaseWorld, BaseYaw, LX, LY, 44.0, 11.0, 300.0, WaterMat);           // Fontaene
}

void AWiesbadenParkFeatures::BuildBowlingGreen(const FVector& BaseWorld, const FRotator& Yaw)
{
	// Lokales Frame: +Y = Laengsachse (Kurhaus->Wilhelmstrasse), +X = quer.
	// Zwei lange parallele Becken mit Fontaenen, mittiger Kiesweg, Formhecken.
	AddBasin(BaseWorld, Yaw, -750.0, 0.0, 300.0, 2600.0);
	AddBasin(BaseWorld, Yaw,  750.0, 0.0, 300.0, 2600.0);
	AddFountain(BaseWorld, Yaw, 0.0,  2450.0, 340.0);
	AddFountain(BaseWorld, Yaw, 0.0, -2450.0, 340.0);
	// Kiesweg in der Mitte.
	AddBox(BaseWorld, Yaw, 0.0, 0.0, 0.0, 300.0, 5600.0, 6.0, GravelMat);
	// Niedrige Formhecken an den Aussenkanten.
	AddBox(BaseWorld, Yaw, -1300.0, 0.0, 0.0, 110.0, 5600.0, 110.0, HedgeMat);
	AddBox(BaseWorld, Yaw,  1300.0, 0.0, 0.0, 110.0, 5600.0, 110.0, HedgeMat);
}

void AWiesbadenParkFeatures::BuildReisinger(const FVector& BaseWorld, const FRotator& Yaw)
{
	// Reisinger-Anlagen vor dem Hauptbahnhof: zwei lange Becken + Mittelbrunnen.
	AddBasin(BaseWorld, Yaw, -650.0, 0.0, 340.0, 2300.0);
	AddBasin(BaseWorld, Yaw,  650.0, 0.0, 340.0, 2300.0);
	AddFountain(BaseWorld, Yaw, 0.0, 0.0, 300.0);
	AddBox(BaseWorld, Yaw, 0.0, 0.0, 0.0, 250.0, 5000.0, 6.0, GravelMat);
}
