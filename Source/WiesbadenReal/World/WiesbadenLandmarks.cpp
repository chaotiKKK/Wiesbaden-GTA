// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/WiesbadenLandmarks.h"

#include "GIS/GeoCoordinateConverter.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"

DEFINE_LOG_CATEGORY_STATIC(LogWbLandmark, Log, All);

AWiesbadenLandmarks::AWiesbadenLandmarks()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
}

void AWiesbadenLandmarks::BeginPlay()
{
	Super::BeginPlay();

	CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	ConeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
	CylinderMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	BrickMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/City/M_WbLmBrick.M_WbLmBrick"));
	SlateMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/City/M_WbLmSlate.M_WbLmSlate"));
	GoldMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/City/M_WbLmGold.M_WbLmGold"));
	WhiteMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/City/M_WbLmWhite.M_WbLmWhite"));

	Converter = NewObject<UGeoCoordinateConverter>(this);
	Converter->InitializeWithWiesbadenOrigin();

	// Echte Standorte (OSM/Karte), Heading grob an der Laengsachse.
	Specs.Add({ ELandmarkKind::Marktkirche,    50.08255, 8.24135, 90.0 });
	Specs.Add({ ELandmarkKind::RussischeKirche, 50.09425, 8.23195,  0.0 });
	Specs.Add({ ELandmarkKind::Shuttle,          50.13412, 8.22006,  0.0 });
}

bool AWiesbadenLandmarks::ResolveGround(const FVector& WorldXY, double& OutZ) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WbLandmarkGround), true);
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

void AWiesbadenLandmarks::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bAllBuilt || !Converter)
	{
		return;
	}

	bool bAll = true;
	for (FLandmarkSpec& Spec : Specs)
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
			bAll = false;   // Zelle noch nicht gestreamt -> naechster Tick
			continue;
		}

		const FVector Base(Ground.X, Ground.Y, Z);
		const FRotator Yaw(0.0, Spec.HeadingDeg, 0.0);
		if (Spec.Kind == ELandmarkKind::Marktkirche)
		{
			BuildMarktkirche(Base, Yaw);
		}
		else if (Spec.Kind == ELandmarkKind::Shuttle)
		{
			BuildShuttle(Base, Yaw);
		}
		else
		{
			BuildRussianChurch(Base, Yaw);
		}
		Spec.bBuilt = true;
		UE_LOG(LogWbLandmark, Log, TEXT("Landmarke gebaut bei (%.0f, %.0f, %.0f)."), Base.X, Base.Y, Base.Z);
	}

	if (bAll)
	{
		bAllBuilt = true;
		SetActorTickEnabled(false);
	}
}

void AWiesbadenLandmarks::AddPart(UStaticMesh* Mesh, const FVector& BaseWorld, const FRotator& BaseYaw,
	const FVector& LocalCm, const FRotator& LocalRot, const FVector& SizeCm, UMaterialInterface* Material)
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
	const FVector World = BaseWorld + BaseYaw.RotateVector(LocalCm);
	Comp->SetWorldLocation(World);
	Comp->SetWorldRotation((BaseYaw.Quaternion() * LocalRot.Quaternion()).Rotator());
	Comp->SetWorldScale3D(SizeCm);   // Aufrufer liefert bereits das Skalierungsverhaeltnis
	if (Material)
	{
		Comp->SetMaterial(0, Material);
	}
	Parts.Add(Comp);
}

void AWiesbadenLandmarks::AddBox(const FVector& BaseWorld, const FRotator& BaseYaw,
	double LX, double LY, double BaseZ, double WidthX, double DepthY, double HeightZ,
	UMaterialInterface* Material)
{
	// Engine-Cube = 100 cm, Ursprung Mitte. Unterkante auf BaseZ.
	const FVector Local(LX, LY, BaseZ + HeightZ * 0.5);
	const FVector Scale(WidthX / 100.0, DepthY / 100.0, HeightZ / 100.0);
	AddPart(CubeMesh, BaseWorld, BaseYaw, Local, FRotator::ZeroRotator, Scale, Material);
}

void AWiesbadenLandmarks::AddSpire(const FVector& BaseWorld, const FRotator& BaseYaw,
	double LX, double LY, double BaseZ, double RadiusCm, double HeightCm, UMaterialInterface* Material)
{
	// Engine-Cone = 100 cm hoch (Basis unten, Spitze oben), Basisradius 50, Ursprung Mitte.
	const FVector Local(LX, LY, BaseZ + HeightCm * 0.5);
	const FVector Scale(RadiusCm / 50.0, RadiusCm / 50.0, HeightCm / 100.0);
	AddPart(ConeMesh, BaseWorld, BaseYaw, Local, FRotator::ZeroRotator, Scale, Material);
}

void AWiesbadenLandmarks::AddOnionDome(const FVector& BaseWorld, const FRotator& BaseYaw,
	double LX, double LY, double BaseZ, double RadiusCm, UMaterialInterface* DomeMat)
{
	// Trommel (weiss) + goldene Kuppel (gebauchte Kugel) + goldene Spitze.
	const double DrumR = RadiusCm * 0.72;
	const double DrumH = RadiusCm * 0.9;
	// Trommel: Cylinder = 100 hoch, Radius 50, Ursprung Mitte.
	AddPart(CylinderMesh, BaseWorld, BaseYaw, FVector(LX, LY, BaseZ + DrumH * 0.5),
		FRotator::ZeroRotator, FVector(DrumR / 50.0, DrumR / 50.0, DrumH / 100.0), WhiteMat);

	// Kuppel: Sphere Radius 50, Ursprung Mitte. Gebaucht: Z etwas gestreckt.
	const double DomeBaseZ = BaseZ + DrumH;
	const double DomeScaleXY = RadiusCm / 50.0;
	const double DomeScaleZ = (RadiusCm * 1.25) / 50.0;
	// Zentrum so, dass die Kuppel etwa auf der Trommel aufsitzt.
	AddPart(SphereMesh, BaseWorld, BaseYaw,
		FVector(LX, LY, DomeBaseZ + RadiusCm * 0.55),
		FRotator::ZeroRotator, FVector(DomeScaleXY, DomeScaleXY, DomeScaleZ), DomeMat);

	// Spitze auf der Kuppel.
	const double TipBaseZ = DomeBaseZ + RadiusCm * 1.35;
	AddSpire(BaseWorld, BaseYaw, LX, LY, TipBaseZ, RadiusCm * 0.14, RadiusCm * 0.9, DomeMat);
}

void AWiesbadenLandmarks::BuildMarktkirche(const FVector& BaseWorld, const FRotator& Yaw)
{
	// Lokales Frame: +X = Breite (quer), +Y = Laengsachse (Schiff), +Z = hoch.
	// Roter Backstein, Schiefer-Spitzen. Der hohe Westturm ist das Wahrzeichen.

	// Langhaus.
	AddBox(BaseWorld, Yaw, 0.0, 1000.0, 0.0, 1600.0, 3400.0, 1900.0, BrickMat);
	// Querhaus (Kreuz).
	AddBox(BaseWorld, Yaw, 0.0, 700.0, 0.0, 3000.0, 1000.0, 1700.0, BrickMat);
	// Dach-Kappe (dunkel) ueber dem Langhaus.
	AddBox(BaseWorld, Yaw, 0.0, 1000.0, 1900.0, 1650.0, 3400.0, 350.0, SlateMat);
	// Chor/Apsis am Ostende.
	AddBox(BaseWorld, Yaw, 0.0, 2500.0, 0.0, 1000.0, 700.0, 1500.0, BrickMat);

	// Hoher Westturm (breiter, damit die Backstein-Masse traegt) + Spitze (~92 m).
	AddBox(BaseWorld, Yaw, 0.0, -800.0, 0.0, 1500.0, 1500.0, 5200.0, BrickMat);
	// Kurzer Schiefer-Kranz als Turmabschluss, dann die Spitze.
	AddBox(BaseWorld, Yaw, 0.0, -800.0, 5200.0, 1600.0, 1600.0, 250.0, SlateMat);
	AddSpire(BaseWorld, Yaw, 0.0, -800.0, 5450.0, 900.0, 3600.0, SlateMat);

	// Vier flankierende Ecktuerme mit Spitzen.
	const double TX = 700.0, TZ = 2500.0;
	const double Corners[4][2] = { { -TX, -650.0 }, { TX, -650.0 }, { -1400.0, 700.0 }, { 1400.0, 700.0 } };
	for (const auto& C : Corners)
	{
		AddBox(BaseWorld, Yaw, C[0], C[1], 0.0, 380.0, 380.0, TZ, BrickMat);
		AddSpire(BaseWorld, Yaw, C[0], C[1], TZ, 260.0, 1300.0, SlateMat);
	}
}

void AWiesbadenLandmarks::BuildRussianChurch(const FVector& BaseWorld, const FRotator& Yaw)
{
	// Weisser Koerper mit fuenf goldenen Zwiebelkuppeln (Referenz Neroberg).
	AddBox(BaseWorld, Yaw, 0.0, 0.0, 0.0, 1500.0, 1500.0, 1400.0, WhiteMat);
	// Kurze Kreuzarme.
	AddBox(BaseWorld, Yaw, 0.0, 0.0, 0.0, 2100.0, 850.0, 1150.0, WhiteMat);
	AddBox(BaseWorld, Yaw, 0.0, 0.0, 0.0, 850.0, 2100.0, 1150.0, WhiteMat);
	// Dach-Uebergang.
	AddBox(BaseWorld, Yaw, 0.0, 0.0, 1400.0, 1550.0, 1550.0, 200.0, WhiteMat);

	// Zentrale grosse Kuppel.
	AddOnionDome(BaseWorld, Yaw, 0.0, 0.0, 1600.0, 420.0, GoldMat);
	// Vier kleinere Eckkuppeln.
	const double D = 520.0;
	const double Off[4][2] = { { -D, -D }, { D, -D }, { -D, D }, { D, D } };
	for (const auto& O : Off)
	{
		AddOnionDome(BaseWorld, Yaw, O[0], O[1], 1400.0, 190.0, GoldMat);
	}
}

void AWiesbadenLandmarks::BuildShuttle(const FVector& BaseWorld, const FRotator& Yaw)
{
    UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Props/Shuttle/SM_SpaceShuttle.SM_SpaceShuttle"));
    if (!Mesh)
    {
        UE_LOG(LogWbLandmark, Warning, TEXT("Shuttle: Mesh nicht gefunden."));
        return;
    }
    const FVector Size = Mesh->GetBoundingBox().GetSize();
    const double Longest = FMath::Max3(Size.X, Size.Y, Size.Z);
    const double TargetCm = 3700.0; // laengste (Hoch-)Achse ~37 m
    const double S = (Longest > 1.0) ? (TargetCm / Longest) : 1.0;

    UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(this);
    Comp->SetStaticMesh(Mesh);
    Comp->SetupAttachment(Root);
    Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Comp->RegisterComponent();
    Comp->SetWorldScale3D(FVector(S));
    // Das glTF landet mit seiner Hochachse auf UE-Y -> per Roll -90 auf +Z aufrichten.
    Comp->SetWorldRotation(FRotator(0.0, Yaw.Yaw, -90.0));
    Comp->SetWorldLocation(BaseWorld);
    Comp->UpdateBounds();
    // Unterkante exakt auf den Boden: echte Weltbounds nach Transform auswerten.
    const FBoxSphereBounds WB = Comp->Bounds;
    const double WorldMinZ = WB.Origin.Z - WB.BoxExtent.Z;
    Comp->AddWorldOffset(FVector(0.0, 0.0, BaseWorld.Z - WorldMinZ));
    Parts.Add(Comp);
    UE_LOG(LogWbLandmark, Log,
        TEXT("Shuttle bei (%.0f, %.0f, %.0f), Skala %.2f, Bounds %.0f x %.0f x %.0f cm."),
        BaseWorld.X, BaseWorld.Y, BaseWorld.Z, S, WB.BoxExtent.X * 2, WB.BoxExtent.Y * 2, WB.BoxExtent.Z * 2);
}
