// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/WiesbadenPlatterParking.h"

#include "GIS/WiesbadenWorldBuilder.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "LandscapeProxy.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "CollisionQueryParams.h"

DEFINE_LOG_CATEGORY_STATIC(LogWbPlatterParking, Log, All);

namespace
{
	constexpr double AddressLongitude = 8.2234186;
	constexpr double AddressLatitude = 50.0932604;

	// Strassen-Rahmen: Sehne der Platter Strasse (OSM 1295994878) zwischen den
	// Knoten bei (15,7 | 30,4) und (42,8 | 17,4) m. U laeuft stadteinwaerts
	// nach Suedost, N zeigt von der Fahrbahn weg zum Hof.
	const FVector2D FrameOrigin(15.7, 30.4);
	const FVector2D FrameU = FVector2D(42.8 - 15.7, 17.4 - 30.4).GetSafeNormal();
	const FVector2D FrameN(FrameU.Y, -FrameU.X);

	// Hofausdehnung laengs der Strasse. Westlich beginnt er vor der Nordost-
	// ecke von Nr. 144 (dort die Zufahrt), oestlich endet er vor dem Knick
	// der Platter Strasse.
	constexpr double CourtU0 = 1.5;
	constexpr double CourtU1 = 40.5;
	// Zufahrt mit abgesenktem Bordstein am Westende.
	constexpr double EntryU0 = 1.5;
	constexpr double EntryU1 = 8.5;
	// Rampe beginnt so weit VOR dem Bordstein auf der Fahrbahn - sonst
	// stuende die 12-cm-Bordkante durch die Rampe.
	constexpr double RampRunM = 1.0;
	// Zwoelf Buchten a 2,5 m entlang der Gehweg-Hinterkante.
	constexpr int32 SpaceCount = 12;
	constexpr double SpaceWidthM = 2.5;
	constexpr double SpaceLengthM = 5.0;
	constexpr double SpacesU0 = 9.5;
	// Kleiner Abstand zwischen Gehweg-Hinterkante und Buchtanfang.
	constexpr double SpaceSetbackM = 0.2;
	// Sperrflaeche vor Nr. 144 (im Luftbild als Schraffur sichtbar).
	constexpr double HatchU0 = 2.5;
	constexpr double HatchU1 = 8.5;
	constexpr double HatchN0 = 14.5;
	constexpr double HatchN1 = 18.2;
	// Der Hof endet 5 cm vor der Garagenwand.
	constexpr double WallGapM = 0.05;
	constexpr int32 GarageDoorCount = 4;

	FVector2D OsmToFrame(const FVector2D& EastNorth)
	{
		const FVector2D D = EastNorth - FrameOrigin;
		return FVector2D(FVector2D::DotProduct(D, FrameU), FVector2D::DotProduct(D, FrameN));
	}
}

AWiesbadenPlatterParking::AWiesbadenPlatterParking()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("ParkingRoot"));
	RootComponent = Root;
	Pavement = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Pavement"));
	Pavement->SetupAttachment(Root);
	Pavement->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Pavement->bUseComplexAsSimpleCollision = true;
	Markings = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Markings"));
	Markings->SetupAttachment(Root);
	Markings->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Markings->SetCastShadow(false);
}

FVector2D AWiesbadenPlatterParking::FrameToEastNorth(double U, double N)
{
	return FrameOrigin + FrameU * U + FrameN * N;
}

void AWiesbadenPlatterParking::GetGarageFront(double& OutN, double& OutU0, double& OutU1)
{
	// Nordostfront der Garagenzeile OSM 182281233.
	const FVector2D A = OsmToFrame(FVector2D(20.0, 7.8));
	const FVector2D B = OsmToFrame(FVector2D(32.8, 1.7));
	OutN = FMath::Min(A.Y, B.Y);
	OutU0 = FMath::Min(A.X, B.X);
	OutU1 = FMath::Max(A.X, B.X);
}

TArray<FWiesbadenPlatterSpace> AWiesbadenPlatterParking::BuildSpaces(double InEdgeOffsetM)
{
	// Rueckwaerts eingeparkt: die Nase zeigt quer zur Strasse in den Hof,
	// ausgeparkt wird vorwaerts in die Fahrgasse vor den Garagen.
	const double HeadingDeg = FMath::RadiansToDegrees(FMath::Atan2(-FrameN.Y, FrameN.X));
	const double CentreN = InEdgeOffsetM + SpaceSetbackM + SpaceLengthM * 0.5;
	TArray<FWiesbadenPlatterSpace> Spaces;
	Spaces.Reserve(SpaceCount);
	for (int32 Index = 0; Index < SpaceCount; ++Index)
	{
		FWiesbadenPlatterSpace Space;
		Space.EastNorthM = FrameToEastNorth(SpacesU0 + (Index + 0.5) * SpaceWidthM, CentreN);
		Space.HeadingDeg = static_cast<float>(HeadingDeg);
		Space.LengthM = static_cast<float>(SpaceLengthM);
		Space.WidthM = static_cast<float>(SpaceWidthM);
		Spaces.Add(Space);
	}
	return Spaces;
}

FWiesbadenPlatterSpace AWiesbadenPlatterParking::PlayerStartSpace(double InEdgeOffsetM)
{
	return BuildSpaces(InEdgeOffsetM)[5]; // vor dem zweiten Garagentor
}

void AWiesbadenPlatterParking::BeginPlay()
{
	Super::BeginPlay();
	Converter = NewObject<UGeoCoordinateConverter>(this);
	bool bGeoreferenced = false;
	for (TActorIterator<AWiesbadenWorldBuilder> It(GetWorld()); It; ++It)
	{
		bGeoreferenced = It->bUseWiesbadenOrigin
			? Converter->InitializeWithWiesbadenOrigin()
			: Converter->Initialize(It->CustomOrigin);
		break;
	}
	if (!bGeoreferenced)
	{
		Converter->InitializeWithWiesbadenOrigin();
	}
	Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	EnsureBuilt();
}

void AWiesbadenPlatterParking::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bBuilt)
	{
		EnsureBuilt();
	}
	else if (!bFinished)
	{
		TryFinish();
	}
}

void AWiesbadenPlatterParking::TryFinish()
{
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	FString Missing;
	const bool bReady = IsSurroundingReady(Missing);
	if (!bReady && Now - BuiltSeconds < 30.0)
	{
		if (Now >= NextWaitLogSeconds)
		{
			UE_LOG(LogWbPlatterParking, Log, TEXT("Platter-Hof: warte auf %s (%.0f s)."),
				*Missing, Now - BuiltSeconds);
			NextWaitLogSeconds = Now + 5.0;
		}
		return;
	}
	if (!bReady)
	{
		UE_LOG(LogWbPlatterParking, Warning,
			TEXT("Platter-Hof: %s nach 30 s noch nicht gestreamt - Tore/Rampe trotzdem."), *Missing);
	}
	// Beim ersten Bau (Bild 0) sind Gehweg und Fahrbahn oft noch nicht
	// gestreamt - die Vorderkante lag dann auf dem Gelaende, 25 cm unter dem
	// Gehweg. Jetzt, mit messbarem Bordstein, die Kanten neu abtasten.
	FitCourtPlane();
	BuildCourt();
	AddEntryRamp();
	AddGarageFront();
	bFinished = true;
	SetActorTickEnabled(false);
}

FVector AWiesbadenPlatterParking::WorldXY(const FVector2D& EastNorthM) const
{
	const FGeoCoordinate Coord(
		AddressLongitude + EastNorthM.X /
			UGeoCoordinateConverter::MetersPerDegreeLongitude(AddressLatitude),
		AddressLatitude + EastNorthM.Y /
			UGeoCoordinateConverter::MetersPerDegreeLatitude(AddressLatitude),
		0.0);
	return Converter->GeoToUnrealGround(Coord);
}

bool AWiesbadenPlatterParking::TerrainZ(const FVector& XY, double& OutZ) const
{
	UWorld* World = GetWorld();
	if (!World) { return false; }
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WbPlatterTerrain), true);
	Params.AddIgnoredActor(this);
	TArray<FHitResult> Hits;
	World->LineTraceMultiByChannel(Hits, FVector(XY.X, XY.Y, 100000.0),
		FVector(XY.X, XY.Y, -20000.0), ECC_WorldStatic, Params);
	for (const FHitResult& Hit : Hits)
	{
		if (!Hit.bStartPenetrating && Cast<ALandscapeProxy>(Hit.GetActor()))
		{
			OutZ = Hit.Location.Z;
			return true;
		}
	}
	return false;
}

bool AWiesbadenPlatterParking::TerrainMaxAround(double U, double N, double RadiusM,
	double& OutZ) const
{
	bool bAny = false;
	OutZ = -TNumericLimits<double>::Max();
	static const FVector2D Taps[] = { {0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1} };
	for (const FVector2D& Tap : Taps)
	{
		double Z;
		if (TerrainZ(WorldXY(FrameToEastNorth(U + Tap.X * RadiusM, N + Tap.Y * RadiusM)), Z))
		{
			OutZ = FMath::Max(OutZ, Z);
			bAny = true;
		}
	}
	return bAny;
}

bool AWiesbadenPlatterParking::GroundZ(const FVector& XY, double& OutZ) const
{
	UWorld* World = GetWorld();
	if (!World) { return false; }
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WbPlatterGround), true);
	Params.AddIgnoredActor(this);
	TArray<FHitResult> Hits;
	if (!World->LineTraceMultiByChannel(Hits, FVector(XY.X, XY.Y, 100000.0),
		FVector(XY.X, XY.Y, -20000.0), ECC_WorldStatic, Params))
	{
		return false;
	}
	const FHitResult* Terrain = nullptr;
	for (const FHitResult& Hit : Hits)
	{
		if (!Hit.bStartPenetrating && Cast<ALandscapeProxy>(Hit.GetActor()))
		{
			Terrain = &Hit;
			break;
		}
	}
	if (Terrain)
	{
		OutZ = Terrain->Location.Z;
		// Fahrbahn und Gehweg duerfen bis 80 cm ueber dem Gelaende liegen;
		// Gebaeudedaecher bleiben draussen.
		for (const FHitResult& Hit : Hits)
		{
			if (!Hit.bStartPenetrating && Hit.Location.Z > OutZ
				&& Hit.Location.Z - OutZ < 80.0)
			{
				OutZ = Hit.Location.Z;
				break;
			}
		}
		return true;
	}
	for (const FHitResult& Hit : Hits)
	{
		if (!Hit.bStartPenetrating)
		{
			OutZ = Hit.Location.Z;
			return true;
		}
	}
	return false;
}

void AWiesbadenPlatterParking::ReadRoadProfile()
{
	// Wo liegt die Gehweg-Hinterkante WIRKLICH? Der Bake baut Fahrbahn und
	// Gehweg um die Achse des Strassensegments; dieselben Zahlen stehen im
	// gebackenen Netz. Die Achse weicht bis ~0,4 m von der Sehne ab und
	// knickt am Ostende ab - darum zaehlt die groesste Auslenkung ueber die
	// ganze Hoflaenge, damit keine Bucht in den Gehweg ragt.
	const FRoadSegment* Best = nullptr;
	double BestDist = 60.0;
	double MaxAxisN = -TNumericLimits<double>::Max();
	TArray<FVector2D> BestSamples;
	auto ToFrame = [this](const FVector& World) -> FVector2D
	{
		const FGeoCoordinate Geo = Converter->UnrealToGeo(World);
		return OsmToFrame(FVector2D(
			(Geo.Longitude - AddressLongitude) * UGeoCoordinateConverter::MetersPerDegreeLongitude(AddressLatitude),
			(Geo.Latitude - AddressLatitude) * UGeoCoordinateConverter::MetersPerDegreeLatitude(AddressLatitude)));
	};
	for (TActorIterator<AWiesbadenWorldBuilder> It(GetWorld()); It; ++It)
	{
		for (const FRoadSegment& Segment : It->RoadNetwork.Segments)
		{
			if (Segment.bIsArea || !Segment.StreetName.Contains(TEXT("Platter")))
			{
				continue;
			}
			double SegMaxN = -TNumericLimits<double>::Max();
			double SegDist = TNumericLimits<double>::Max();
			TArray<FVector2D> SegSamples;
			for (int32 Index = 0; Index + 1 < Segment.Centerline.Num(); ++Index)
			{
				const FVector2D A = ToFrame(Segment.Centerline[Index]);
				const FVector2D B = ToFrame(Segment.Centerline[Index + 1]);
				// Nur der Abschnitt vor dem Hof zaehlt.
				for (int32 Step = 0; Step <= 8; ++Step)
				{
					const FVector2D P = FMath::Lerp(A, B, Step / 8.0);
					if (P.X < CourtU0 - 1.0 || P.X > CourtU1 + 1.0) { continue; }
					SegMaxN = FMath::Max(SegMaxN, P.Y);
					SegSamples.Add(P);
					SegDist = FMath::Min(SegDist, FMath::Abs(P.Y));
				}
			}
			if (SegDist < BestDist)
			{
				BestDist = SegDist;
				Best = &Segment;
				MaxAxisN = SegMaxN;
				BestSamples = MoveTemp(SegSamples);
			}
		}
	}
	if (!Best)
	{
		UE_LOG(LogWbPlatterParking, Warning,
			TEXT("Platter-Hof: kein gebackenes Platter-Strassensegment gefunden - Standardprofil %.1f m."),
			EdgeOffsetM);
		return;
	}
	const bool bSidewalk = Best->SidewalkType != EOSMSidewalkType::None
		&& Best->SidewalkType != EOSMSidewalkType::Separate;
	CarriagewayHalfM = Best->CarriagewayWidthCm * 0.005;
	SidewalkM = bSidewalk ? Best->SidewalkWidthCm * 0.01 : 0.0;
	BestSamples.Sort([](const FVector2D& A, const FVector2D& B) { return A.X < B.X; });
	AxisSamples = MoveTemp(BestSamples);
	// Die Buchtreihe ist gerade: sie beginnt hinter der am weitesten zum Hof
	// ausholenden Stelle der Gehwegkante.
	EdgeOffsetM = MaxAxisN + CarriagewayHalfM + SidewalkM;
	UE_LOG(LogWbPlatterParking, Log,
		TEXT("Platter-Hof: Strassenprofil aus dem Netz - Fahrbahn %.2f m, Gehweg %.2f m, Achse bis %.2f m zum Hof; Gehweg-Hinterkante bei %.2f m."),
		Best->CarriagewayWidthCm * 0.01, SidewalkM, MaxAxisN, EdgeOffsetM);
}

bool AWiesbadenPlatterParking::FindGarageWall(double U, double FloorZ, FVector& OutWallXY) const
{
	UWorld* World = GetWorld();
	if (!World) { return false; }
	double GarageN, GarageU0, GarageU1;
	GetGarageFront(GarageN, GarageU0, GarageU1);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WbPlatterGarageWall), true);
	Params.AddIgnoredActor(this);
	// Von weit draussen (6 m vor der OSM-Front) bis 4 m hinein - ein Strahl,
	// der IN der Wand beginnt, sieht deren Aussenseite nicht.
	const FVector Out = WorldXY(FrameToEastNorth(U, GarageN - 6.0));
	const FVector In = WorldXY(FrameToEastNorth(U, GarageN + 4.0));
	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, FVector(Out.X, Out.Y, FloorZ + 110.0),
		FVector(In.X, In.Y, FloorZ + 110.0), ECC_WorldStatic, Params)
		&& !Cast<ALandscapeProxy>(Hit.GetActor()))
	{
		OutWallXY = FVector(Hit.Location.X, Hit.Location.Y, 0.0);
		return true;
	}
	return false;
}

bool AWiesbadenPlatterParking::IsSurroundingReady(FString& OutMissing) const
{
	double GarageN, GarageU0, GarageU1;
	GetGarageFront(GarageN, GarageU0, GarageU1);
	double Terrain = 0.0;
	const double MidU = (GarageU0 + GarageU1) * 0.5;
	FVector Wall;
	if (!TerrainZ(WorldXY(FrameToEastNorth(MidU, GarageN - 1.0)), Terrain)
		|| !FindGarageWall(MidU, Terrain, Wall))
	{
		OutMissing = TEXT("Garagenwand");
		return false;
	}
	// Fahrbahndecke an der Zufahrt: ein Treffer ueber dem Gelaende, der nicht
	// das Landscape selbst ist.
	const double EntryU = (EntryU0 + EntryU1) * 0.5;
	double Road = 0.0, Walk = 0.0;
	const bool bRoad = GroundZ(WorldXY(FrameToEastNorth(EntryU, KerbN(EntryU) - 1.0)), Road);
	const bool bWalk = GroundZ(WorldXY(FrameToEastNorth(EntryU, KerbN(EntryU) + 0.5)), Walk);
	// Gestreamt ist die Strasse, wenn die Bordkante (Gehweg ueber Fahrbahn)
	// messbar ist - ein Vergleich mit dem Gelaende taugt nicht: die Fahrbahn
	// liegt hier UNTER dem Landscape.
	if (!bRoad || !bWalk || Walk - Road < 5.0 || Walk - Road > 30.0)
	{
		OutMissing = TEXT("Bordstein der Platter Strasse");
		return false;
	}
	return true;
}

double AWiesbadenPlatterParking::AxisN(double U) const
{
	if (AxisSamples.Num() == 0) { return EdgeOffsetM - CarriagewayHalfM - SidewalkM; }
	if (U <= AxisSamples[0].X) { return AxisSamples[0].Y; }
	for (int32 Index = 1; Index < AxisSamples.Num(); ++Index)
	{
		const FVector2D& A = AxisSamples[Index - 1];
		const FVector2D& B = AxisSamples[Index];
		if (U <= B.X)
		{
			const double Span = B.X - A.X;
			return Span > KINDA_SMALL_NUMBER ? FMath::Lerp(A.Y, B.Y, (U - A.X) / Span) : B.Y;
		}
	}
	return AxisSamples.Last().Y;
}

double AWiesbadenPlatterParking::SampleProfile(const TArray<FVector2D>& Profile,
	double U, double Fallback)
{
	if (Profile.Num() == 0) { return Fallback; }
	if (U <= Profile[0].X) { return Profile[0].Y; }
	for (int32 Index = 1; Index < Profile.Num(); ++Index)
	{
		if (U <= Profile[Index].X)
		{
			const FVector2D& A = Profile[Index - 1];
			const FVector2D& B = Profile[Index];
			return FMath::Lerp(A.Y, B.Y, (U - A.X) / FMath::Max(B.X - A.X, 0.01));
		}
	}
	return Profile.Last().Y;
}

void AWiesbadenPlatterParking::FitCourtPlane()
{
	// Der Hof liegt vorn BUENDIG an der Gehwegoberkante und hinten auf dem
	// Gelaende an den Garagen, dazwischen linear (Regelflaeche). Beide Kanten
	// werden je 1,5 m abgetastet und leicht geglaettet: eine Ausgleichsebene
	// (erste Fassung) und selbst eine Ausgleichsgerade je Kante liessen den
	// Hof an der Zufahrt 23-25 cm unter dem Gehweg enden - der Gehweg folgt
	// der gewoelbten Platter Strasse, keine Gerade.
	double GarageN, GarageU0, GarageU1;
	GetGarageFront(GarageN, GarageU0, GarageU1);
	CourtBackN = GarageN - WallGapM;
	TArray<FVector2D> Front, Back;
	int32 WalkHits = 0;
	for (double U = CourtU0; U <= CourtU1 + 0.01; U += 1.5)
	{
		double Z;
		const FVector Walk = WorldXY(FrameToEastNorth(U, EdgeN(U) - 0.4));
		// Sichtbare Oberkante des Gehwegs. KEIN Gelaendevergleich: der Mehrfach-
		// strahl endet am ersten BLOCKIERENDEN Treffer - ist der Gehweg gestreamt,
		// erreicht er das Landscape darunter nie, und jeder Filter "ueber dem
		// Gelaende" verwarf alle Proben (Hofkante blieb 25 cm unter dem Gehweg).
		if (SidewalkM > 0.5 && GroundZ(Walk, Z))
		{
			Front.Add(FVector2D(U, Z));
			++WalkHits;
		}
		else if (TerrainZ(WorldXY(FrameToEastNorth(U, EdgeN(U) + 0.3)), Z))
		{
			// Kein Gehweg getroffen: vorn auf das Gelaende an der Kante.
			Front.Add(FVector2D(U, Z));
		}
		if (TerrainZ(WorldXY(FrameToEastNorth(U, CourtBackN - 0.5)), Z))
		{
			Back.Add(FVector2D(U, Z));
		}
	}
	// 3er-Mittel gegen Einzelausreisser; die Enden bleiben unveraendert.
	auto Smooth = [](const TArray<FVector2D>& In)
	{
		TArray<FVector2D> Out = In;
		for (int32 I = 1; I + 1 < In.Num(); ++I)
		{
			Out[I].Y = (In[I - 1].Y + In[I].Y + In[I + 1].Y) / 3.0;
		}
		return Out;
	};
	FrontProfile = Smooth(Front);
	BackProfile = Smooth(Back);
	const double MidU = (CourtU0 + CourtU1) * 0.5;
	UE_LOG(LogWbPlatterParking, Log,
		TEXT("Platter-Hof: Vorderkante aus %d Proben (%d auf dem Gehweg), Hinterkante aus %d; Hofmitte vorn %.0f / hinten %.0f cm."),
		Front.Num(), WalkHits, Back.Num(), FrontZ(MidU), BackZ(MidU));
}

double AWiesbadenPlatterParking::CourtZ(double U, double N) const
{
	// Die Ebene; wo das Gelaende hoeher liegt, folgt der Belag ihm, statt
	// von Gras durchstossen zu werden. Gemessen wird im Umkreis eines halben
	// Rasterschritts: an einem Stuetzpunkt allein stach das Landscape
	// ZWISCHEN den Punkten als gruene Flecken durch den Hof.
	const double Edge = EdgeN(U);
	const double T = FMath::Clamp((N - Edge) / FMath::Max(CourtBackN - Edge, 0.5), 0.0, 1.0);
	const double Plane = FMath::Lerp(FrontZ(U), BackZ(U), T) + 1.0;
	double Terrain;
	if (TerrainMaxAround(U, N, 0.4, Terrain))
	{
		return FMath::Max(Plane, Terrain + 6.0);
	}
	return Plane;
}

void AWiesbadenPlatterParking::AddQuad(FMeshData& Mesh, double U0, double U1,
	TFunctionRef<double(double)> N0Of, TFunctionRef<double(double)> N1Of,
	double GridMetres, TFunctionRef<double(double, double)> SurfaceZ)
{
	const double Mid = (U0 + U1) * 0.5;
	const int32 UCount = FMath::Max(1, FMath::CeilToInt(FMath::Abs(U1 - U0) / GridMetres));
	const int32 NCount = FMath::Max(1, FMath::CeilToInt(FMath::Abs(N1Of(Mid) - N0Of(Mid)) / GridMetres));
	const int32 Base = Mesh.Vertices.Num();
	for (int32 IUx = 0; IUx <= UCount; ++IUx)
	{
		const double U = FMath::Lerp(U0, U1, double(IUx) / UCount);
		const double N0 = N0Of(U);
		const double N1 = N1Of(U);
		for (int32 INx = 0; INx <= NCount; ++INx)
		{
			const double N = FMath::Lerp(N0, N1, double(INx) / NCount);
			const FVector XY = WorldXY(FrameToEastNorth(U, N));
			Mesh.Vertices.Add(FVector(XY.X - AnchorWorld.X, XY.Y - AnchorWorld.Y,
				SurfaceZ(U, N) - AnchorWorld.Z));
			Mesh.Normals.Add(FVector::UpVector);
			Mesh.UVs.Add(FVector2D(U * 0.5, N * 0.5));
			Mesh.Colours.Add(FLinearColor::White);
		}
	}
	for (int32 IUx = 0; IUx < UCount; ++IUx)
	{
		for (int32 INx = 0; INx < NCount; ++INx)
		{
			const int32 I = Base + IUx * (NCount + 1) + INx;
			const int32 J = I + (NCount + 1);
			const bool bUp = FVector::CrossProduct(Mesh.Vertices[J] - Mesh.Vertices[I],
				Mesh.Vertices[I + 1] - Mesh.Vertices[I]).Z > 0.0;
			// ProceduralMesh rendert die von oben im UE-Linkshaendsystem im
			// Uhrzeigersinn laufenden Dreiecke. Die Gegenrichtung hat korrekte
			// Kollision, bleibt im Spielbild aber voellig unsichtbar.
			if (bUp)
			{
				Mesh.Triangles.Append({I, J + 1, J, I, I + 1, J + 1});
			}
			else
			{
				Mesh.Triangles.Append({I, J, J + 1, I, J + 1, I + 1});
			}
		}
	}
}

void AWiesbadenPlatterParking::AddLine(FMeshData& Mesh, double UA, double NA,
	double UB, double NB, double WidthCm)
{
	// Markierungen als schmales Band 3 cm ueber dem Hofbelag, alle 0,5 m
	// gestuetzt: mit nur vier Ecken ueber 5 m tauchte die Linie zwischen
	// ihren Enden unter den gewoelbten Belag und erschien gestrichelt.
	const FVector2D A(UA, NA);
	const FVector2D B(UB, NB);
	const FVector2D Along = (B - A).GetSafeNormal();
	const FVector2D Side = FVector2D(-Along.Y, Along.X) * (WidthCm / 200.0);
	const int32 Steps = FMath::Max(1, FMath::CeilToInt(FVector2D::Distance(A, B) / 0.5));
	const int32 Base = Mesh.Vertices.Num();
	for (int32 Step = 0; Step <= Steps; ++Step)
	{
		const FVector2D P = FMath::Lerp(A, B, double(Step) / Steps);
		for (const FVector2D& Corner : { P - Side, P + Side })
		{
			const FVector XY = WorldXY(FrameToEastNorth(Corner.X, Corner.Y));
			Mesh.Vertices.Add(FVector(XY.X - AnchorWorld.X, XY.Y - AnchorWorld.Y,
				CourtZ(Corner.X, Corner.Y) - AnchorWorld.Z + 3.0));
			Mesh.Normals.Add(FVector::UpVector);
			Mesh.UVs.Add(FVector2D::ZeroVector);
			Mesh.Colours.Add(FLinearColor::White);
		}
	}
	for (int32 Step = 0; Step < Steps; ++Step)
	{
		// I, I+1 = Querpaar am Anfang; I+2, I+3 = am Ende des Abschnitts.
		const int32 I = Base + Step * 2;
		const bool bUp = FVector::CrossProduct(Mesh.Vertices[I + 1] - Mesh.Vertices[I],
			Mesh.Vertices[I + 2] - Mesh.Vertices[I]).Z > 0.0;
		// Dieselbe Regel wie in AddQuad: zeigt (I, I+1, I+2) nach dem Kreuz-
		// produkt nach oben, wird umgekehrt gewickelt - sonst schaut das Band
		// nach unten und ist im Spielbild unsichtbar (so geschehen).
		if (bUp)
		{
			Mesh.Triangles.Append({I, I + 2, I + 1, I + 1, I + 2, I + 3});
		}
		else
		{
			Mesh.Triangles.Append({I, I + 1, I + 2, I + 1, I + 3, I + 2});
		}
	}
}

void AWiesbadenPlatterParking::AddGaragePart(const FVector& XY, float YawDeg,
	const FVector& SizeCm, double BaseZ, UMaterialInterface* Material)
{
	if (!Cube) { return; }
	UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this);
	Part->SetStaticMesh(Cube);
	Part->SetupAttachment(Root);
	Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Part->RegisterComponent();
	Part->SetWorldLocation(FVector(XY.X, XY.Y, BaseZ + SizeCm.Z * .5));
	Part->SetWorldRotation(FRotator(0.0f, YawDeg, 0.0f));
	Part->SetWorldScale3D(SizeCm / 100.0);
	if (Material) { Part->SetMaterial(0, Material); }
	GarageParts.Add(Part);
}

void AWiesbadenPlatterParking::AddGarageFront()
{
	// Vier Schwingtore in der Nordostfront der Garagenzeile, mit hellem
	// Rahmen. Die Tore werden an die WIRKLICH gebackene Wand gesetzt: die
	// erste Fassung stellte sie auf die OSM-Linie, die Wand stand davor, und
	// alle vier Tore steckten unsichtbar in der Garage.
	double GarageN, GarageU0, GarageU1;
	GetGarageFront(GarageN, GarageU0, GarageU1);
	UMaterialInterface* Door = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Game/Materials/City/M_WbLmSlate.M_WbLmSlate"));
	UMaterialInterface* Frame = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Game/Materials/City/M_WbLmWhite.M_WbLmWhite"));
	const double Bay = (GarageU1 - GarageU0) / GarageDoorCount;
	int32 OnWall = 0;
	for (int32 Index = 0; Index < GarageDoorCount; ++Index)
	{
		const double U = GarageU0 + (Index + 0.5) * Bay;
		const double FloorZ = CourtZ(U, GarageN - 1.0);
		const FVector Out = WorldXY(FrameToEastNorth(U, GarageN - 3.0));
		const FVector In = WorldXY(FrameToEastNorth(U, GarageN + 2.0));
		const FVector Outward = (Out - In).GetSafeNormal2D();
		const float Yaw = static_cast<float>((WorldXY(FrameToEastNorth(U + 1.0, GarageN))
			- WorldXY(FrameToEastNorth(U, GarageN))).Rotation().Yaw);
		FVector WallXY = WorldXY(FrameToEastNorth(U, GarageN));
		if (FindGarageWall(U, FloorZ, WallXY))
		{
			++OnWall;
		}
		// Torblatt 2,5 x 2,1 m, 3 cm vor der Wand; Zarge 5 cm davor.
		AddGaragePart(WallXY + Outward * 3.0, Yaw, FVector(250.0, 4.0, 210.0), FloorZ, Door);
		const FVector Along = FRotator(0.0f, Yaw, 0.0f).Vector();
		for (const double Side : { -1.0, 1.0 })
		{
			AddGaragePart(WallXY + Outward * 5.0 + Along * (Side * 130.0), Yaw,
				FVector(10.0, 6.0, 220.0), FloorZ, Frame);
		}
		AddGaragePart(WallXY + Outward * 5.0, Yaw, FVector(270.0, 6.0, 10.0), FloorZ + 210.0, Frame);
	}
	UE_LOG(LogWbPlatterParking, Log,
		TEXT("Platter-Hof: %d von %d Garagentoren an der gebackenen Wand ausgerichtet."),
		OnWall, GarageDoorCount);
}

void AWiesbadenPlatterParking::AddEntryRamp()
{
	// Querprofil an der Zufahrt protokollieren: Fahrbahn, Bordstein, Gehweg.
	{
		const double U = (EntryU0 + EntryU1) * 0.5;
		FString Profile;
		for (double N = KerbN(U) - 1.5; N <= EdgeN(U) + 0.8; N += 0.25)
		{
			double Z = 0.0;
			GroundZ(WorldXY(FrameToEastNorth(U, N)), Z);
			Profile += FString::Printf(TEXT(" %.2f:%.0f"), N, Z);
		}
		UE_LOG(LogWbPlatterParking, Log, TEXT("Platter-Hof: Querprofil N:Z bei U %.1f (Bord %.2f, Kante %.2f):%s"),
			U, KerbN(U), EdgeN(U), *Profile);
	}
	FMeshData Asphalt;
	auto EdgeOf = [this](double U) { return EdgeN(U); };
	// Zufahrt: Rampe von der Fahrbahn ueber den abgesenkten Bordstein und
	// den Gehweg bis an die Hofkante - stufenlos. Sie beginnt RampRunM vor
	// dem Bordstein auf der Fahrbahn, damit die Bordkante nirgends durchsticht.
	auto RoadZ = [this](double U)
	{
		double Z = FrontZ(U);
		GroundZ(WorldXY(FrameToEastNorth(U, KerbN(U) - RampRunM - 0.3)), Z);
		return Z + 1.0;
	};
	auto KerbTopZ = [this](double U)
	{
		double Z = FrontZ(U);
		GroundZ(WorldXY(FrameToEastNorth(U, KerbN(U) + 0.3)), Z);
		return Z + 1.5;
	};
	auto Ramp = [&](double U, double N)
	{
		const double Kerb = KerbN(U);
		const double KerbTop = KerbTopZ(U);
		double Z;
		if (N <= Kerb + 0.001)
		{
			const double T = FMath::Clamp((N - (Kerb - RampRunM)) / RampRunM, 0.0, 1.0);
			Z = FMath::Lerp(RoadZ(U), KerbTop, T);
		}
		else
		{
			const double Span = FMath::Max(EdgeN(U) - Kerb, 0.01);
			Z = FMath::Lerp(KerbTop, CourtZ(U, EdgeN(U)), FMath::Clamp((N - Kerb) / Span, 0.0, 1.0));
		}
		// Nie unter der gebackenen Gehwegoberflaeche - sonst verschwindet die
		// Rampe darunter (erste Fassung: im Bild keine Zufahrt sichtbar).
		double Below;
		if (N > Kerb + 0.05 && GroundZ(WorldXY(FrameToEastNorth(U, N)), Below))
		{
			Z = FMath::Max(Z, Below + 1.5);
		}
		return Z;
	};
	auto RampStart = [this](double U) { return KerbN(U) - RampRunM; };
	auto KerbOf = [this](double U) { return KerbN(U); };
	UE_LOG(LogWbPlatterParking, Log,
		TEXT("Platter-Hof: Zufahrt bei U %.1f - Fahrbahn Z %.1f, Bordstein oben Z %.1f, Hofkante Z %.1f."),
		EntryU0 + 3.0, RoadZ(EntryU0 + 3.0), KerbTopZ(EntryU0 + 3.0), CourtZ(EntryU0 + 3.0, EdgeN(EntryU0 + 3.0)));
	AddQuad(Asphalt, EntryU0, EntryU1, RampStart, KerbOf, 0.5, Ramp);
	if (SidewalkM > 0.05)
	{
		AddQuad(Asphalt, EntryU0, EntryU1, KerbOf, EdgeOf, 0.5, Ramp);
	}

	Pavement->CreateMeshSection_LinearColor(1, Asphalt.Vertices, Asphalt.Triangles,
		Asphalt.Normals, Asphalt.UVs, Asphalt.Colours, TArray<FProcMeshTangent>(), true);
	if (UMaterialInterface* Paving = Pavement->GetMaterial(0))
	{
		Pavement->SetMaterial(1, Paving);
	}
}

void AWiesbadenPlatterParking::BuildCourt()
{
	double GarageN, GarageU0, GarageU1;
	GetGarageFront(GarageN, GarageU0, GarageU1);
	const double CourtN1 = GarageN - WallGapM;

	FMeshData Asphalt;
	auto Court = [this](double U, double N) { return CourtZ(U, N); };
	auto EdgeOf = [this](double U) { return EdgeN(U); };
	auto BackOf = [CourtN1](double) { return CourtN1; };
	AddQuad(Asphalt, CourtU0, CourtU1, EdgeOf, BackOf, 0.75, Court);

	Pavement->CreateMeshSection_LinearColor(0, Asphalt.Vertices, Asphalt.Triangles,
		Asphalt.Normals, Asphalt.UVs, Asphalt.Colours, TArray<FProcMeshTangent>(), true);
	Pavement->SetCollisionProfileName(TEXT("BlockAll"));
	UMaterialInterface* Paving = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Game/Materials/City/MI_WbFahrbahn_Asphalt_Alt.MI_WbFahrbahn_Asphalt_Alt"));
	if (Paving) { Pavement->SetMaterial(0, Paving); }

	FMeshData Lines;
	// Buchttrennlinien quer zur Strasse.
	const double RowN0 = EdgeOffsetM + SpaceSetbackM;
	const double RowN1 = RowN0 + SpaceLengthM;
	for (int32 Index = 0; Index <= SpaceCount; ++Index)
	{
		const double U = SpacesU0 + Index * SpaceWidthM;
		AddLine(Lines, U, RowN0 + 0.3, U, RowN1, 12.0);
	}
	// Sperrflaeche: Rahmen und 45-Grad-Schraffur.
	AddLine(Lines, HatchU0, HatchN0, HatchU1, HatchN0, 12.0);
	AddLine(Lines, HatchU1, HatchN0, HatchU1, HatchN1, 12.0);
	AddLine(Lines, HatchU1, HatchN1, HatchU0, HatchN1, 12.0);
	AddLine(Lines, HatchU0, HatchN1, HatchU0, HatchN0, 12.0);
	for (double C = HatchU0 - (HatchN1 - HatchN0) + 1.0; C < HatchU1; C += 1.0)
	{
		// Linie U = C + (N - HatchN0), auf das Rechteck geklippt.
		const double NA = FMath::Max(HatchN0, HatchN0 + (HatchU0 - C));
		const double NB = FMath::Min(HatchN1, HatchN0 + (HatchU1 - C));
		if (NB - NA > 0.1)
		{
			AddLine(Lines, C + (NA - HatchN0), NA, C + (NB - HatchN0), NB, 20.0);
		}
	}
	Markings->CreateMeshSection_LinearColor(0, Lines.Vertices, Lines.Triangles,
		Lines.Normals, Lines.UVs, Lines.Colours, TArray<FProcMeshTangent>(), false);
	UMaterialInterface* Marking = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Game/Materials/City/M_WbLmWhite.M_WbLmWhite"));
	if (Marking) { Markings->SetMaterial(0, Marking); }

}

bool AWiesbadenPlatterParking::EnsureBuilt()
{
	if (bBuilt) { return true; }
	if (!Converter) { return false; }
	double GarageN, GarageU0, GarageU1;
	GetGarageFront(GarageN, GarageU0, GarageU1);
	const FVector AnchorXY = WorldXY(FrameToEastNorth((CourtU0 + CourtU1) * 0.5, GarageN));
	double AnchorZ = 0.0;
	if (!TerrainZ(AnchorXY, AnchorZ)) { return false; }
	AnchorWorld = FVector(AnchorXY.X, AnchorXY.Y, AnchorZ);
	SetActorLocation(AnchorWorld);

	ReadRoadProfile();
	FitCourtPlane();
	BuildCourt();

	const FWiesbadenPlatterSpace Start = PlayerStartSpace(EdgeOffsetM);
	const FVector2D StartFrame = OsmToFrame(Start.EastNorthM);
	const FVector StartXY = WorldXY(Start.EastNorthM);
	const FVector Nose = WorldXY(FrameToEastNorth(StartFrame.X, StartFrame.Y + 1.0)) - StartXY;
	PlayerStartWorld = FVector(StartXY.X, StartXY.Y, CourtZ(StartFrame.X, StartFrame.Y));
	PlayerStartRotation = FRotator(0.0f, Nose.Rotation().Yaw, 0.0f);
	bBuilt = true;
	BuiltSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	// Tick bleibt an: Tore und Rampe folgen, sobald Wand und Fahrbahn da sind.
	TryFinish();
	UE_LOG(LogWbPlatterParking, Log,
		TEXT("Platter 144: Garagenhof mit %d Toren und %d Stellplaetzen bei (%.0f, %.0f, %.0f) gebaut; Hof %.1f..%.1f m quer, Zufahrt ab Bordstein %.1f m; Start (%.0f, %.0f, %.0f) Gier %.0f."),
		GarageDoorCount, SpaceCount, AnchorWorld.X, AnchorWorld.Y, AnchorWorld.Z,
		EdgeOffsetM, CourtBackN, KerbN(EntryU0),
		PlayerStartWorld.X, PlayerStartWorld.Y, PlayerStartWorld.Z, PlayerStartRotation.Yaw);
	return true;
}

bool AWiesbadenPlatterParking::GetPlayerStart(FVector& OutLocation,
	FRotator& OutRotation) const
{
	if (!bBuilt) { return false; }
	OutLocation = PlayerStartWorld;
	OutRotation = PlayerStartRotation;
	return true;
}
