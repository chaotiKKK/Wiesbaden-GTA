// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/WiesbadenNerotal48.h"

#include "Engine/World.h"
#include "GIS/GeoCoordinateConverter.h"
#include "ProceduralMeshComponent.h"
#include "KismetProceduralMeshLibrary.h"
#include "UObject/ConstructorHelpers.h"
#include "WiesbadenReal.h"

namespace
{
	/**
	 * Der Grundriss aus OpenStreetMap, way 476086889.
	 *
	 * Sechzehn Punkte, im Umlaufsinn wie in den Daten. Sie stehen hier
	 * ausgeschrieben und werden nicht zur Laufzeit gesucht: Der Garten soll
	 * auch dann an der richtigen Stelle liegen, wenn die Stadt gerade erst
	 * streamt und der Gebaeudegenerator noch nichts geliefert hat.
	 */
	struct FGartenLatLon { double Lat; double Lon; };

	const FGartenLatLon HOUSE_FOOTPRINT[] = {
		{ 50.0936212, 8.2302861 },
		{ 50.0936349, 8.2302820 },
		{ 50.0936461, 8.2302592 },
		{ 50.0936427, 8.2302311 },
		{ 50.0936280, 8.2302163 },
		{ 50.0936190, 8.2302197 },
		{ 50.0936100, 8.2302324 },
		{ 50.0935291, 8.2301721 },
		{ 50.0935332, 8.2301589 },
		{ 50.0934949, 8.2301303 },
		{ 50.0934580, 8.2302508 },
		{ 50.0934272, 8.2302278 },
		{ 50.0934083, 8.2302893 },
		{ 50.0935913, 8.2304258 },
		{ 50.0936247, 8.2303168 },
		{ 50.0936141, 8.2303089 },
	};

	// Farben. Vertexfarben, damit ein einziges Material fuer alles genuegt.
	const FLinearColor COLOUR_TERRACE(0.62f, 0.56f, 0.46f);   // Sandsteinplatten
	const FLinearColor COLOUR_COPING(0.80f, 0.78f, 0.73f);    // heller Beckenrand
	const FLinearColor COLOUR_TILE(0.12f, 0.42f, 0.55f);      // Beckenfliesen
	const FLinearColor COLOUR_WATER(0.05f, 0.35f, 0.50f);     // Wasser
	const FLinearColor COLOUR_TRUNK(0.36f, 0.29f, 0.21f);     // Palmstamm
	const FLinearColor COLOUR_FROND(0.26f, 0.50f, 0.18f);     // Wedel - helleres Palmgruen (unter GI=None sonst nachschwarz)

	// Abschnittsnummern im ProceduralMesh.
	constexpr int32 SECTION_TERRACE = 0;
	constexpr int32 SECTION_POOL = 1;
	constexpr int32 SECTION_WATER = 2;
	constexpr int32 SECTION_TRUNK = 3;
	constexpr int32 SECTION_FROND = 4;
	constexpr int32 SECTION_COUNT = 5;

	/** Freibord: so weit steht das Wasser unter der Beckenkante. */
	constexpr double FREEBOARD_CM = 12.0;
}

AWiesbadenNerotal48::AWiesbadenNerotal48()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	GardenMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("GardenMesh"));
	GardenMesh->SetupAttachment(Root);

	// Kollision AN - anders als bei der Bahntrasse.
	//
	// Auf der Terrasse soll man stehen, ins Becken soll man fallen koennen.
	// Ohne Kollision liefe der Spieler ueber ein Bild.
	GardenMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	GardenMesh->SetCollisionObjectType(ECC_WorldStatic);
	GardenMesh->SetCollisionResponseToAllChannels(ECR_Block);

	// Materialien.
	//
	// Die Geometrie faerbt sich ueber VERTEXFARBEN - Fliese gegen
	// Sandstein gegen Palmwedel. Das frueher hier benutzte
	// BasicShapeMaterial wertet Vertexfarben ueberhaupt nicht aus; der
	// ganze Garten waere ein grauer Klotz geworden.
	//
	// Das Wasser braucht ein eigenes, durchscheinendes Material, sonst
	// waere der Pool eine blaue Platte und die Fliesen darunter umsonst
	// gebaut.
	{
		static ConstructorHelpers::FObjectFinder<UMaterialInterface> GardenMat(
			TEXT("/Game/Materials/City/M_WbVertexFarbe.M_WbVertexFarbe"));
		static ConstructorHelpers::FObjectFinder<UMaterialInterface> WaterMat(
			TEXT("/Game/Materials/City/M_WbVertexFarbeWasser.M_WbVertexFarbeWasser"));

		if (GardenMat.Succeeded())
		{
			for (int32 Section = 0; Section < SECTION_COUNT; ++Section)
			{
				GardenMesh->SetMaterial(Section, GardenMat.Object);
			}
		}
		if (WaterMat.Succeeded())
		{
			GardenMesh->SetMaterial(SECTION_WATER, WaterMat.Object);
		}
	}
}

FVector2D AWiesbadenNerotal48::ComputeFootprintCenter(const TArray<FVector2D>& Points)
{
	if (Points.Num() == 0)
	{
		return FVector2D::ZeroVector;
	}

	FVector2D Sum = FVector2D::ZeroVector;
	for (const FVector2D& Point : Points)
	{
		Sum += Point;
	}
	return Sum / static_cast<double>(Points.Num());
}

double AWiesbadenNerotal48::ComputeWaterLevelCm(double CopingTopCm, double FreeboardCm)
{
	return CopingTopCm - FMath::Max(FreeboardCm, 0.0);
}

void AWiesbadenNerotal48::BeginPlay()
{
	Super::BeginPlay();

	// Der erste Versuch geschieht sofort; steht das Gelaende noch nicht, wird
	// im Tick weiter probiert.
	if (ResolvePlacement())
	{
		BuildGarden();
		bBuilt = true;
	}
}

void AWiesbadenNerotal48::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bBuilt)
	{
		return;
	}

	// Nicht in jedem Bild erneut in den Boden tasten: Das Gelaende kommt ueber
	// das Streaming, und das braucht Sekunden, keine Millisekunden.
	RetrySeconds -= DeltaSeconds;
	if (RetrySeconds > 0.0f)
	{
		return;
	}
	RetrySeconds = 1.0f;

	if (ResolvePlacement())
	{
		BuildGarden();
		bBuilt = true;
	}
}

bool AWiesbadenNerotal48::ResolvePlacement()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	UGeoCoordinateConverter* Converter = NewObject<UGeoCoordinateConverter>(this);
	Converter->InitializeWithWiesbadenOrigin();

	TArray<FVector2D> Footprint;
	Footprint.Reserve(UE_ARRAY_COUNT(HOUSE_FOOTPRINT));
	for (const FGartenLatLon& Node : HOUSE_FOOTPRINT)
	{
		FGeoCoordinate Coord;
		Coord.Latitude = Node.Lat;
		Coord.Longitude = Node.Lon;
		Coord.Height = 0.0;
		const FVector World2D = Converter->GeoToUnrealGround(Coord);
		Footprint.Add(FVector2D(World2D.X, World2D.Y));
	}

	const FVector2D Center2D = ComputeFootprintCenter(Footprint);

	// Der Garten liegt in GardenBearingDeg vom Haus aus. Norden ist +X.
	const double BearingRad = FMath::DegreesToRadians(GardenBearingDeg);
	const FVector2D GardenDir(FMath::Cos(BearingRad), FMath::Sin(BearingRad));
	const FVector2D Garden2D = Center2D + GardenDir * GardenOffsetCm;

	// Gelaendehoehe abtasten - von WEIT oben.
	//
	// 1.000 m, nicht 200: Ein Trace, der in der Geometrie startet, meldet die
	// Flaeche nicht, in der er beginnt. Genau daran ist die Nerobergbahn im
	// Hang stecken geblieben, und das Nerotal liegt am selben Berg.
	constexpr double TraceTopCm = 100000.0;
	constexpr double TraceBottomCm = -20000.0;

	auto GroundZ = [&](const FVector2D& Point, double& OutZ) -> bool
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(WbNerotalBoden), true);
		Params.AddIgnoredActor(this);

		FHitResult Hit;
		const bool bHit = World->LineTraceSingleByChannel(
			Hit,
			FVector(Point.X, Point.Y, TraceTopCm),
			FVector(Point.X, Point.Y, TraceBottomCm),
			ECC_WorldStatic, Params);

		if (!bHit || Hit.bStartPenetrating)
		{
			return false;
		}
		OutZ = Hit.Location.Z;
		return true;
	};

	double HouseZ = 0.0;
	double GardenZ = 0.0;
	if (!GroundZ(Center2D, HouseZ) || !GroundZ(Garden2D, GardenZ))
	{
		return false;   // Gelaende noch nicht gestreamt.
	}

	HouseCenter = FVector(Center2D.X, Center2D.Y, HouseZ);
	PoolCenter = FVector(Garden2D.X, Garden2D.Y, GardenZ);

	// Der Actor selbst sitzt am Becken; alles Weitere ist relativ dazu.
	SetActorLocation(PoolCenter);

	UE_LOG(LogWbStreaming, Log,
		TEXT("Nerotal 48: Haus bei %.0f/%.0f (%.1f m), Garten bei %.0f/%.0f (%.1f m)."),
		HouseCenter.X, HouseCenter.Y, HouseCenter.Z / 100.0,
		PoolCenter.X, PoolCenter.Y, PoolCenter.Z / 100.0);

	return true;
}

void AWiesbadenNerotal48::AddBox(const FVector& Center, const FVector& HalfSize,
	const FLinearColor& Colour,
	TArray<FVector>& Vertices, TArray<int32>& Triangles, TArray<FVector>& Normals,
	TArray<FVector2D>& UVs, TArray<FLinearColor>& Colours) const
{
	// Sechs Seiten mit je eigenen Ecken - gemeinsame Ecken haetten keine
	// eindeutige Normale, und der Quader saehe rundgelutscht aus.
	static const FVector FaceNormals[6] = {
		FVector(0, 0, 1), FVector(0, 0, -1),
		FVector(1, 0, 0), FVector(-1, 0, 0),
		FVector(0, 1, 0), FVector(0, -1, 0)
	};

	// Je Seite die vier Ecken als Vorzeichen der Halbmasse.
	static const FVector FaceCorners[6][4] = {
		{ {-1,-1, 1}, { 1,-1, 1}, { 1, 1, 1}, {-1, 1, 1} },   // oben
		{ {-1, 1,-1}, { 1, 1,-1}, { 1,-1,-1}, {-1,-1,-1} },   // unten
		{ { 1,-1,-1}, { 1, 1,-1}, { 1, 1, 1}, { 1,-1, 1} },   // +X
		{ {-1, 1,-1}, {-1,-1,-1}, {-1,-1, 1}, {-1, 1, 1} },   // -X
		{ { 1, 1,-1}, {-1, 1,-1}, {-1, 1, 1}, { 1, 1, 1} },   // +Y
		{ {-1,-1,-1}, { 1,-1,-1}, { 1,-1, 1}, {-1,-1, 1} },   // -Y
	};

	for (int32 Face = 0; Face < 6; ++Face)
	{
		const int32 Base = Vertices.Num();
		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			const FVector& Sign = FaceCorners[Face][Corner];
			Vertices.Add(Center + FVector(
				Sign.X * HalfSize.X, Sign.Y * HalfSize.Y, Sign.Z * HalfSize.Z));
			Normals.Add(FaceNormals[Face]);
			Colours.Add(Colour);
		}

		// UV in Metern, damit gekachelte Materialien ueberall gleich gross
		// wirken - unabhaengig davon, wie gross die Flaeche ist.
		const double U = (Face < 2 ? HalfSize.X : (Face < 4 ? HalfSize.Y : HalfSize.X)) / 50.0;
		const double V = (Face < 2 ? HalfSize.Y : HalfSize.Z) / 50.0;
		UVs.Add(FVector2D(0, 0));
		UVs.Add(FVector2D(U, 0));
		UVs.Add(FVector2D(U, V));
		UVs.Add(FVector2D(0, V));

		Triangles.Append({ Base, Base + 1, Base + 2, Base, Base + 2, Base + 3 });
	}
}

void AWiesbadenNerotal48::AddPalm(const FVector& Base, double HeightCm, double YawRad,
	TArray<FVector>& TrunkVerts, TArray<int32>& TrunkTris,
	TArray<FVector>& TrunkNormals, TArray<FVector2D>& TrunkUVs,
	TArray<FLinearColor>& TrunkColours,
	TArray<FVector>& FrondVerts, TArray<int32>& FrondTris,
	TArray<FVector>& FrondNormals, TArray<FVector2D>& FrondUVs,
	TArray<FLinearColor>& FrondColours) const
{
	// -- Stamm: Ringe uebereinander, nach oben duenner und leicht geneigt ---
	//
	// Eine Palme steht nie ganz gerade. Die Neigung ist klein, aber ohne sie
	// sehen sechs Palmen nebeneinander aus wie sechs Masten.
	constexpr int32 Rings = 9;
	constexpr int32 Sides = 8;
	const double LeanCm = HeightCm * 0.06;
	const double LeanDir = YawRad + PI * 0.5;

	for (int32 Ring = 0; Ring <= Rings; ++Ring)
	{
		const double T = static_cast<double>(Ring) / Rings;
		const double RingZ = T * HeightCm;
		const double Radius = FMath::Lerp(18.0, 11.0, T);
		const double Offset = LeanCm * T * T;   // unten senkrecht, oben geneigt

		const FVector RingCenter = Base + FVector(
			FMath::Cos(LeanDir) * Offset,
			FMath::Sin(LeanDir) * Offset,
			RingZ);

		for (int32 Side = 0; Side < Sides; ++Side)
		{
			const double Angle = 2.0 * PI * Side / Sides;
			const FVector Out(FMath::Cos(Angle), FMath::Sin(Angle), 0.0);
			TrunkVerts.Add(RingCenter + Out * Radius);
			TrunkNormals.Add(Out);
			TrunkUVs.Add(FVector2D(static_cast<double>(Side) / Sides, T * HeightCm / 100.0));
			TrunkColours.Add(COLOUR_TRUNK);
		}
	}

	const int32 TrunkBase = TrunkVerts.Num() - (Rings + 1) * Sides;
	for (int32 Ring = 0; Ring < Rings; ++Ring)
	{
		for (int32 Side = 0; Side < Sides; ++Side)
		{
			const int32 A = TrunkBase + Ring * Sides + Side;
			const int32 B = TrunkBase + Ring * Sides + (Side + 1) % Sides;
			const int32 C = A + Sides;
			const int32 D = B + Sides;
			TrunkTris.Append({ A, C, B, B, C, D });
		}
	}

	// -- Wedel: flache Blaetter, die sich nach aussen neigen ----------------
	const FVector Crown = Base + FVector(
		FMath::Cos(LeanDir) * LeanCm,
		FMath::Sin(LeanDir) * LeanCm,
		HeightCm);

	constexpr int32 FrondCount = 9;
	constexpr double FrondLengthCm = 230.0;
	constexpr double FrondHalfWidthCm = 26.0;

	for (int32 Frond = 0; Frond < FrondCount; ++Frond)
	{
		const double Angle = YawRad + 2.0 * PI * Frond / FrondCount;
		const FVector Out(FMath::Cos(Angle), FMath::Sin(Angle), 0.0);
		const FVector Side = FVector::CrossProduct(Out, FVector::UpVector).GetSafeNormal();

		// Der Wedel haengt: erst aufwaerts, dann ueber die Spitze abwaerts.
		// Drei Stuetzpunkte genuegen fuer diese Biegung.
		const FVector P0 = Crown;
		const FVector P1 = Crown + Out * (FrondLengthCm * 0.55) + FVector(0, 0, 45.0);
		const FVector P2 = Crown + Out * FrondLengthCm + FVector(0, 0, -55.0);

		const int32 Base0 = FrondVerts.Num();

		// Blattflaeche als zwei Streifen: schmal am Ansatz, breit in der
		// Mitte, spitz am Ende.
		const double Widths[3] = { 8.0, FrondHalfWidthCm, 3.0 };
		const FVector Points[3] = { P0, P1, P2 };
		for (int32 i = 0; i < 3; ++i)
		{
			FrondVerts.Add(Points[i] - Side * Widths[i]);
			FrondVerts.Add(Points[i] + Side * Widths[i]);
			// Normalen NICHT fix setzen - sie werden am Sektionsende aus der
			// Geometrie abgeleitet (CalculateTangentsForMesh), sonst schattiert
			// der steile, haengende Wedel falsch und wirkt schwarz.
			for (int32 k = 0; k < 2; ++k)
			{
				FrondColours.Add(COLOUR_FROND);
			}
			FrondUVs.Add(FVector2D(0.0, static_cast<double>(i) * 0.5));
			FrondUVs.Add(FVector2D(1.0, static_cast<double>(i) * 0.5));
		}

		for (int32 i = 0; i < 2; ++i)
		{
			const int32 A = Base0 + i * 2;
			// Nur EINE Windung: die Rueckseite zeichnet der zweiseitige Shader
			// (two_sided) mit gespiegelter Normale selbst. Zwei deckungsgleiche
			// Windungen mit fixer UpVector-Normale ueberlagerten sich (Z-Fighting),
			// und auf der gespiegelten Seite wurde aus UpVector eine Abwaertsnormale
			// -> der Wedel wirkte schwarz.
			FrondTris.Append({ A, A + 2, A + 1, A + 1, A + 2, A + 3 });
		}
	}
}

void AWiesbadenNerotal48::BuildGarden()
{
	// Alles relativ zum Actor (der am Becken sitzt).
	const double HalfLength = PoolLengthCm * 0.5;
	const double HalfWidth = PoolWidthCm * 0.5;
	constexpr double CopingWidthCm = 45.0;      // Breite des Beckenrands
	constexpr double CopingHeightCm = 14.0;     // Hoehe ueber der Terrasse
	constexpr double TerraceThicknessCm = 12.0;

	// -- Terrasse -----------------------------------------------------------
	TArray<FVector> TV; TArray<int32> TT; TArray<FVector> TN;
	TArray<FVector2D> TU; TArray<FLinearColor> TC;

	const double TerraceHalfX = HalfLength + CopingWidthCm + 260.0;
	const double TerraceHalfY = HalfWidth + CopingWidthCm + 200.0;
	AddBox(FVector(0, 0, -TerraceThicknessCm * 0.5),
		FVector(TerraceHalfX, TerraceHalfY, TerraceThicknessCm * 0.5),
		COLOUR_TERRACE, TV, TT, TN, TU, TC);

	// -- Becken: vier Waende und ein Boden ----------------------------------
	//
	// Als Waende, nicht als ausgehoehlter Block: Ein Loch im Netz braucht
	// entweder eine Boolesche Verknuepfung oder vier Seiten. Vier Seiten sind
	// genauer und kosten weniger.
	TArray<FVector> PV; TArray<int32> PT; TArray<FVector> PN;
	TArray<FVector2D> PU; TArray<FLinearColor> PC;

	constexpr double WallThicknessCm = 20.0;

	// Boden
	AddBox(FVector(0, 0, -PoolDepthCm - WallThicknessCm * 0.5),
		FVector(HalfLength + WallThicknessCm, HalfWidth + WallThicknessCm, WallThicknessCm * 0.5),
		COLOUR_TILE, PV, PT, PN, PU, PC);

	// Laengswaende
	for (const double SideY : { -1.0, 1.0 })
	{
		AddBox(
			FVector(0, SideY * (HalfWidth + WallThicknessCm * 0.5), -PoolDepthCm * 0.5),
			FVector(HalfLength + WallThicknessCm, WallThicknessCm * 0.5, PoolDepthCm * 0.5),
			COLOUR_TILE, PV, PT, PN, PU, PC);
	}
	// Stirnwaende
	for (const double SideX : { -1.0, 1.0 })
	{
		AddBox(
			FVector(SideX * (HalfLength + WallThicknessCm * 0.5), 0, -PoolDepthCm * 0.5),
			FVector(WallThicknessCm * 0.5, HalfWidth, PoolDepthCm * 0.5),
			COLOUR_TILE, PV, PT, PN, PU, PC);
	}

	// Beckenrand ringsum
	for (const double SideY : { -1.0, 1.0 })
	{
		AddBox(
			FVector(0, SideY * (HalfWidth + WallThicknessCm + CopingWidthCm * 0.5),
				CopingHeightCm * 0.5),
			FVector(HalfLength + WallThicknessCm + CopingWidthCm, CopingWidthCm * 0.5,
				CopingHeightCm * 0.5),
			COLOUR_COPING, PV, PT, PN, PU, PC);
	}
	for (const double SideX : { -1.0, 1.0 })
	{
		AddBox(
			FVector(SideX * (HalfLength + WallThicknessCm + CopingWidthCm * 0.5), 0,
				CopingHeightCm * 0.5),
			FVector(CopingWidthCm * 0.5, HalfWidth + WallThicknessCm, CopingHeightCm * 0.5),
			COLOUR_COPING, PV, PT, PN, PU, PC);
	}

	// -- Wasserspiegel ------------------------------------------------------
	TArray<FVector> WV; TArray<int32> WT; TArray<FVector> WN;
	TArray<FVector2D> WU; TArray<FLinearColor> WC;

	const double WaterZ = ComputeWaterLevelCm(CopingHeightCm, FREEBOARD_CM);
	{
		const int32 Base = WV.Num();
		WV.Add(FVector(-HalfLength, -HalfWidth, WaterZ));
		WV.Add(FVector(HalfLength, -HalfWidth, WaterZ));
		WV.Add(FVector(HalfLength, HalfWidth, WaterZ));
		WV.Add(FVector(-HalfLength, HalfWidth, WaterZ));
		for (int32 i = 0; i < 4; ++i)
		{
			WN.Add(FVector::UpVector);
			WC.Add(COLOUR_WATER);
		}
		WU.Add(FVector2D(0, 0));
		WU.Add(FVector2D(PoolLengthCm / 100.0, 0));
		WU.Add(FVector2D(PoolLengthCm / 100.0, PoolWidthCm / 100.0));
		WU.Add(FVector2D(0, PoolWidthCm / 100.0));
		WT.Append({ Base, Base + 1, Base + 2, Base, Base + 2, Base + 3 });
	}

	// -- Palmen -------------------------------------------------------------
	TArray<FVector> TrV; TArray<int32> TrT; TArray<FVector> TrN;
	TArray<FVector2D> TrU; TArray<FLinearColor> TrC;
	TArray<FVector> FrV; TArray<int32> FrT; TArray<FVector> FrN;
	TArray<FVector2D> FrU; TArray<FLinearColor> FrC;

	for (int32 i = 0; i < PalmCount; ++i)
	{
		// Rings um die Terrasse verteilt, aber nicht auf einem Kreis: ein
		// gleichmaessiger Kreis sieht gepflanzt aus, nicht gewachsen. Der
		// Versatz kommt aus dem Index, bleibt also bei jedem Start gleich.
		const double Angle = 2.0 * PI * i / FMath::Max(PalmCount, 1);
		const double Wobble = 0.35 * FMath::Sin(i * 2.399963);   // goldener Winkel
		const double RadiusX = TerraceHalfX + 110.0 + 90.0 * FMath::Cos(i * 1.7);
		const double RadiusY = TerraceHalfY + 110.0 + 70.0 * FMath::Sin(i * 2.3);

		const FVector Base(
			FMath::Cos(Angle + Wobble) * RadiusX,
			FMath::Sin(Angle + Wobble) * RadiusY,
			-TerraceThicknessCm);

		const double Height = 420.0 + 110.0 * FMath::Sin(i * 1.1);
		AddPalm(Base, Height, Angle + Wobble,
			TrV, TrT, TrN, TrU, TrC,
			FrV, FrT, FrN, FrU, FrC);
	}

	// -- Abschnitte setzen --------------------------------------------------
	GardenMesh->ClearAllMeshSections();

	GardenMesh->CreateMeshSection_LinearColor(SECTION_TERRACE, TV, TT, TN, TU, TC,
		TArray<FProcMeshTangent>(), /*bCreateCollision=*/true);
	GardenMesh->CreateMeshSection_LinearColor(SECTION_POOL, PV, PT, PN, PU, PC,
		TArray<FProcMeshTangent>(), /*bCreateCollision=*/true);

	// Wasser OHNE Kollision: man soll hineinfallen, nicht darauf stehen.
	GardenMesh->CreateMeshSection_LinearColor(SECTION_WATER, WV, WT, WN, WU, WC,
		TArray<FProcMeshTangent>(), /*bCreateCollision=*/false);

	// Staemme mit, Wedel ohne Kollision - gegen einen Stamm laeuft man,
	// durch Blaetter geht man hindurch.
	GardenMesh->CreateMeshSection_LinearColor(SECTION_TRUNK, TrV, TrT, TrN, TrU, TrC,
		TArray<FProcMeshTangent>(), /*bCreateCollision=*/true);
	// Wedel-Normalen aus der Geometrie ableiten (statt fixem UpVector): steile,
	// haengende Blaetter schattieren so korrekt statt schwarz.
	TArray<FProcMeshTangent> FrTang;
	UKismetProceduralMeshLibrary::CalculateTangentsForMesh(FrV, FrT, FrU, FrN, FrTang);
	GardenMesh->CreateMeshSection_LinearColor(SECTION_FROND, FrV, FrT, FrN, FrU, FrC,
		FrTang, /*bCreateCollision=*/false);

	UE_LOG(LogWbStreaming, Log,
		TEXT("Nerotal 48: Garten gebaut - Becken %.1f x %.1f x %.1f m, %d Palmen, ")
		TEXT("Wasserspiegel %.0f cm unter der Kante."),
		PoolLengthCm / 100.0, PoolWidthCm / 100.0, PoolDepthCm / 100.0,
		PalmCount, CopingHeightCm - WaterZ);
}
