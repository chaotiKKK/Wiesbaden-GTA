// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/WiesbadenSebboHq.h"

#include "GIS/GeoCoordinateConverter.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "CollisionQueryParams.h"

DEFINE_LOG_CATEGORY_STATIC(LogWbSebboHq, Log, All);

namespace
{
	/** Materialpfade je Werkstoff - dieselben Stadt-Materialien wie die Wahrzeichen. */
	const TCHAR* MaterialPath(EHqMaterial Material)
	{
		switch (Material)
		{
		case EHqMaterial::Glass:   return TEXT("/Game/Materials/City/M_WbLmSlate.M_WbLmSlate");
		case EHqMaterial::Metal:   return TEXT("/Game/Materials/City/M_WbLmSlate.M_WbLmSlate");
		case EHqMaterial::Marking: return TEXT("/Game/Materials/City/M_WbLmWhite.M_WbLmWhite");
		default:                   return TEXT("/Game/Materials/City/M_WbLmWhite.M_WbLmWhite");
		}
	}
}

AWiesbadenSebboHq::AWiesbadenSebboHq()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
}

void AWiesbadenSebboHq::BeginPlay()
{
	Super::BeginPlay();

	CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	CylinderMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));

	Materials.SetNum(static_cast<int32>(EHqMaterial::MAX));
	for (int32 i = 0; i < Materials.Num(); ++i)
	{
		Materials[i] = LoadObject<UMaterialInterface>(
			nullptr, MaterialPath(static_cast<EHqMaterial>(i)));
	}

	UGeoCoordinateConverter* Own = NewObject<UGeoCoordinateConverter>(this);
	Own->InitializeWithWiesbadenOrigin();
	Converter = Own;
}

bool AWiesbadenSebboHq::ResolveGround(const FVector& WorldXY, double& OutZ) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WbSebboHqGround), true);
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

void AWiesbadenSebboHq::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bBuilt || !Converter)
	{
		return;
	}

	FGeoCoordinate Coord;
	Coord.Latitude = PlotLatitude;
	Coord.Longitude = PlotLongitude;
	Coord.Height = 0.0;
	const FVector Ground = Converter->GeoToUnrealGround(Coord);

	double Z = 0.0;
	if (!ResolveGround(Ground, Z))
	{
		return;     // Zelle noch nicht gestreamt - naechster Tick
	}

	Build(FVector(Ground.X, Ground.Y, Z), FRotator(0.0, HeadingDegrees, 0.0));
	bBuilt = true;
	SetActorTickEnabled(false);
}

void AWiesbadenSebboHq::Build(const FVector& BaseWorld, const FRotator& BaseYaw)
{
	TArray<FHqPart> Teile;
	SebboHq::BuildShell(Dimensions, Teile);
	SebboHq::BuildVerticalCore(Dimensions, Teile);

	for (const FHqPart& Teil : Teile)
	{
		UStaticMesh* Mesh = Teil.Primitive == EHqPrimitive::Cylinder ? CylinderMesh : CubeMesh;
		if (!Mesh)
		{
			continue;
		}
		UStaticMeshComponent* Komponente = NewObject<UStaticMeshComponent>(this);
		Komponente->SetStaticMesh(Mesh);
		Komponente->SetupAttachment(Root);
		// MIT Kollision, anders als die Wahrzeichen: auf diesem Dach soll der
		// Helikopter aufsetzen und der Spieler herumlaufen koennen.
		Komponente->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Komponente->RegisterComponent();
		Komponente->SetWorldLocation(BaseWorld + BaseYaw.RotateVector(Teil.CenterCm));
		Komponente->SetWorldRotation(BaseYaw);
		// Engine-Cube und -Cylinder sind 100 cm gross und um den Ursprung
		// zentriert - die Skalierung ist darum schlicht Groesse/100.
		Komponente->SetWorldScale3D(Teil.SizeCm / 100.0);
		if (Materials.IsValidIndex(static_cast<int32>(Teil.Material)))
		{
			if (UMaterialInterface* Material = Materials[static_cast<int32>(Teil.Material)])
			{
				Komponente->SetMaterial(0, Material);
			}
		}
		Parts.Add(Komponente);
	}

	UE_LOG(LogWbSebboHq, Log,
		TEXT("Sebbo-Hauptsitz gebaut bei (%.0f, %.0f, %.0f): %d Bauteile, %d Geschosse, ")
		TEXT("%.0f m hoch, Landeplatz auf %.0f m."),
		BaseWorld.X, BaseWorld.Y, BaseWorld.Z, Parts.Num(), Dimensions.FloorCount,
		SebboHq::GetRoofHeightCm(Dimensions) / 100.0,
		SebboHq::GetHelipadHeightCm(Dimensions) / 100.0);
}
