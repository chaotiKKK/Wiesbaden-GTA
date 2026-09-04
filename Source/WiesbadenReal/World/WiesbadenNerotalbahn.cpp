// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/WiesbadenNerotalbahn.h"
#include "World/WiesbadenRailTransport.h"

#include "WiesbadenReal.h"

#include "Engine/World.h"
#include "GIS/GeoCoordinateConverter.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// Streckenpunkte Nerotal -> Kranzplatz, Nord (oben) nach Sued (unten).
	// Der obere Terminus im Nerotal ist ergaenzt; der Rest folgt der
	// Taunusstrasse aus OpenStreetMap. Der Norden liegt hoeher: die Bahn
	// faehrt das Tal hinunter in die Stadt.
	struct FBahnLatLon { double Lat; double Lon; };

	const FBahnLatLon ROUTE[] = {
		{ 50.09160, 8.23560 },   // Nerotal-Terminus (ergaenzt)
		{ 50.0909427, 8.2364965 },
		{ 50.0907596, 8.2367996 },
		{ 50.0906581, 8.2369579 },
		{ 50.0905663, 8.2370996 },
		{ 50.0904245, 8.2373202 },
		{ 50.0903097, 8.2375058 },
		{ 50.0900824, 8.2378790 },
		{ 50.0899584, 8.2380827 },
		{ 50.0897739, 8.2383856 },
		{ 50.0896057, 8.2386560 },
		{ 50.0895116, 8.2388047 },
		{ 50.0892604, 8.2392021 },
		{ 50.0891397, 8.2393930 },
		{ 50.0890629, 8.2395114 },
		{ 50.0889464, 8.2396909 },
		{ 50.0888458, 8.2398459 },
		{ 50.0885945, 8.2402333 },
		{ 50.0881910, 8.2408552 },
		{ 50.0879844, 8.2411743 },
		{ 50.0875890, 8.2417868 },
		{ 50.0873894, 8.2420959 },
		{ 50.0872777, 8.2422690 },
		{ 50.0871476, 8.2424755 },
		{ 50.0868011, 8.2430177 },
		{ 50.0866997, 8.2431765 },
		{ 50.0865121, 8.2434880 },
		{ 50.0862916, 8.2438466 },
		{ 50.0861464, 8.2441546 },
		{ 50.0859790, 8.2443003 },   // Kranzplatz / Kochbrunnen
	};

	// Trassenbreiten (Meterspur: Schienenmitten 50 cm neben der Achse).
	constexpr float BedHalfWidthCm = 110.0f;
	constexpr float RailHalfWidthCm = 5.0f;
	constexpr float RailOffsetCm = 50.0f;
	constexpr float BedLiftCm = 2.0f;
	constexpr float RailLiftCm = 7.0f;
}

AWiesbadenNerotalbahn::AWiesbadenNerotalbahn()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	TrackMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("TrackMesh"));
	TrackMesh->SetupAttachment(Root);
	// Reines Schmuckband; das Gelaende darunter traegt die Kollision.
	TrackMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Dieselbe Vertexfarben-Trasse wie die Nerobergbahn: Pflasterbett dunkel,
	// Rillenschienen hell - drei Abschnitte, ein Material.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> TrackMat(
		TEXT("/Game/Materials/City/M_WbVertexFarbe.M_WbVertexFarbe"));
	if (TrackMat.Succeeded())
	{
		for (int32 Section = 0; Section < 3; ++Section)
		{
			TrackMesh->SetMaterial(Section, TrackMat.Object);
		}
	}
}

void AWiesbadenNerotalbahn::BeginPlay()
{
	Super::BeginPlay();
	BuildTrack();
	BuildTrackMesh();

	UE_LOG(LogWbStreaming, Log,
		TEXT("Nerotalbahn: Strecke %.0f m, Terminus bei (%.0f, %.0f)."),
		TotalLength / 100.0,
		Points.Num() ? Points[0].Position.X : 0.0,
		Points.Num() ? Points[0].Position.Y : 0.0);
}

void AWiesbadenNerotalbahn::BuildTrack()
{
	UGeoCoordinateConverter* Converter = NewObject<UGeoCoordinateConverter>(this);
	Converter->InitializeWithWiesbadenOrigin();

	Points.Reset();
	FVector Previous = FVector::ZeroVector;
	double Arc = 0.0;
	for (const FBahnLatLon& Node : ROUTE)
	{
		FGeoCoordinate Coord;
		Coord.Latitude = Node.Lat;
		Coord.Longitude = Node.Lon;
		Coord.Height = 0.0;
		const FVector World = Converter->GeoToUnrealGround(Coord);

		// Doppelpunkte ausduennen - die OSM-Stuetzpunkte liegen teils unter
		// einem Meter auseinander.
		if (Points.Num() > 0 && FVector::Dist2D(World, Previous) < 400.0)
		{
			continue;
		}

		FTrackPoint Point;
		Point.Position = World;
		if (Points.Num() > 0)
		{
			Arc += FVector::Dist2D(World, Previous);
		}
		Point.ArcLength = Arc;
		Points.Add(Point);
		Previous = World;
	}
	TotalLength = Arc;

	// Rueckfallprofil bis zur Gelaendeabtastung: eine flache Linie auf Hoehe
	// null. Sichtbar erst, sobald das Gelaende gestreamt ist - dann wird sie
	// durch die echten Hoehen ersetzt.
	for (FTrackPoint& Point : Points)
	{
		Point.Position.Z = 0.0;
	}
}

bool AWiesbadenNerotalbahn::ResolveHeights()
{
	UWorld* World = GetWorld();
	if (!World || Points.Num() < 2)
	{
		return false;
	}

	bool bResolvedAny = false;
	bool bAllResolved = true;

	for (FTrackPoint& Point : Points)
	{
		if (Point.bHeightResolved)
		{
			continue;
		}

		// Von WEIT oben nach unten - ein Trace, der in der Geometrie startet,
		// meldet die Startflaeche nicht (dieselbe Falle wie bei der
		// Nerobergbahn). 1.000 m liegen ueber jedem Punkt im Stadtgebiet.
		constexpr double TraceTopCm = 100000.0;
		constexpr double TraceBottomCm = -20000.0;

		const FVector Start(Point.Position.X, Point.Position.Y, TraceTopCm);
		const FVector End(Point.Position.X, Point.Position.Y, TraceBottomCm);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(WbNerotalHoehe), true);
		Params.AddIgnoredActor(this);

		FHitResult Hit;
		const bool bHit = World->LineTraceSingleByChannel(
			Hit, Start, End, ECC_WorldStatic, Params);

		if (bHit && !Hit.bStartPenetrating)
		{
			Point.Position.Z = Hit.Location.Z;
			Point.bHeightResolved = true;
			bResolvedAny = true;
		}
		else
		{
			bAllResolved = false;
		}
	}

	if (bAllResolved && !bHeightsFinal)
	{
		bHeightsFinal = true;

		TArray<double> ArcLengths;
		TArray<double> TerrainHeights;
		ArcLengths.Reserve(Points.Num());
		TerrainHeights.Reserve(Points.Num());
		for (const FTrackPoint& Point : Points)
		{
			ArcLengths.Add(Point.ArcLength);
			TerrainHeights.Add(Point.Position.Z);
		}

		// Der korrigierte Profil-Pfad: die Enden RUHEN auf dem Terrain (kein
		// kuenstliches Anheben), dazwischen ein steigungsbegrenztes Profil.
		double StartZ = 0.0;
		double EndZ = 0.0;
		WiesbadenRailTransport::StationRailEndpoints(
			TerrainHeights[0], TerrainHeights.Last(), RailClearanceCm, StartZ, EndZ);

		TArray<FWiesbadenRailProfilePoint> Profile;
		if (WiesbadenRailTransport::BuildConstrainedGradeProfile(
			ArcLengths, TerrainHeights, StartZ, EndZ,
			RailClearanceCm, MaxGrade, Profile))
		{
			for (int32 Index = 0; Index < Points.Num(); ++Index)
			{
				Points[Index].Position.Z = Profile[Index].RailZCm;
			}
		}

		BuildTrackMesh();

		double MinZ = TNumericLimits<double>::Max();
		double MaxZ = TNumericLimits<double>::Lowest();
		for (const FTrackPoint& Point : Points)
		{
			MinZ = FMath::Min(MinZ, Point.Position.Z);
			MaxZ = FMath::Max(MaxZ, Point.Position.Z);
		}
		UE_LOG(LogWbStreaming, Log,
			TEXT("Nerotalbahn: Gelaendehoehen uebernommen, Trasse neu gebaut. ")
			TEXT("Hoehe %.0f bis %.0f m ueber Null, Gefaelle %.0f m."),
			MinZ / 100.0, MaxZ / 100.0, (MaxZ - MinZ) / 100.0);
	}

	return bResolvedAny;
}

void AWiesbadenNerotalbahn::BuildTrackMesh()
{
	if (!TrackMesh)
	{
		return;
	}
	TrackMesh->ClearAllMeshSections();

	// Drei Baender: Pflasterbett (dunkelgrau) und zwei Rillenschienen (hell).
	struct FRibbon { float HalfWidth; float Offset; float Lift; FLinearColor Colour; };
	const FRibbon Ribbons[] = {
		{ BedHalfWidthCm, 0.0f, BedLiftCm, FLinearColor(0.16f, 0.16f, 0.17f) },   // Bett
		{ RailHalfWidthCm, -RailOffsetCm, RailLiftCm, FLinearColor(0.42f, 0.42f, 0.40f) },
		{ RailHalfWidthCm, +RailOffsetCm, RailLiftCm, FLinearColor(0.42f, 0.42f, 0.40f) },
	};

	int32 Section = 0;
	for (const FRibbon& Ribbon : Ribbons)
	{
		TArray<FVector> Vertices;
		TArray<int32> Triangles;
		TArray<FVector> Normals;
		TArray<FVector2D> UV0;
		TArray<FLinearColor> Colours;

		for (int32 i = 0; i < Points.Num(); ++i)
		{
			const FVector& P = Points[i].Position;
			FVector Tangent = FVector::ForwardVector;
			if (i + 1 < Points.Num())
			{
				Tangent = (Points[i + 1].Position - P).GetSafeNormal();
			}
			else if (i > 0)
			{
				Tangent = (P - Points[i - 1].Position).GetSafeNormal();
			}
			const FVector Right = FVector::CrossProduct(Tangent, FVector::UpVector).GetSafeNormal();
			const FVector Centre = P + Right * Ribbon.Offset + FVector(0, 0, Ribbon.Lift);

			Vertices.Add(Centre - Right * Ribbon.HalfWidth);
			Vertices.Add(Centre + Right * Ribbon.HalfWidth);
			Normals.Add(FVector::UpVector);
			Normals.Add(FVector::UpVector);
			const float V = static_cast<float>(Points[i].ArcLength / 100.0);
			UV0.Add(FVector2D(0.0f, V));
			UV0.Add(FVector2D(1.0f, V));
			Colours.Add(Ribbon.Colour);
			Colours.Add(Ribbon.Colour);

			if (i > 0)
			{
				const int32 Base = (i - 1) * 2;
				Triangles.Append({ Base, Base + 2, Base + 1,
								   Base + 1, Base + 2, Base + 3 });
			}
		}

		TrackMesh->CreateMeshSection_LinearColor(
			Section++, Vertices, Triangles, Normals, UV0, Colours,
			TArray<FProcMeshTangent>(), /*bCreateCollision=*/false);
	}
}

void AWiesbadenNerotalbahn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bHeightsFinal)
	{
		HeightRetryRemaining -= DeltaSeconds;
		if (HeightRetryRemaining <= 0.0f)
		{
			HeightRetryRemaining = 2.0f;
			ResolveHeights();
		}
	}
}
