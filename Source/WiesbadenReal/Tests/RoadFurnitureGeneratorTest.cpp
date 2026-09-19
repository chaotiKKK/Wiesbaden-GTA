// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "GIS/GeoCoordinateConverter.h"
#include "GIS/RoadFurnitureGenerator.h"

namespace
{
	FRoadSegment MakeSegment(int32 Id, int64 StartNode, int64 EndNode, const TArray<FVector>& Line, double SpeedKmh = 50.0)
	{
		FRoadSegment Segment;
		Segment.SegmentId = Id;
		Segment.StartNodeId = StartNode;
		Segment.EndNodeId = EndNode;
		Segment.Centerline = Line;
		Segment.TrimmedCenterline = Line;
		Segment.HighwayType = EOSMHighwayType::Residential;
		Segment.CarriagewayWidthCm = 650.0;
		Segment.MaxSpeedKmh = SpeedKmh;
		Segment.LengthCm = 0.0;
		for (int32 i = 1; i < Line.Num(); ++i)
		{
			Segment.LengthCm += FVector::Dist(Line[i], Line[i - 1]);
		}
		return Segment;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadFurnitureDerivedTest,
	"WiesbadenReal.GIS.RoadFurniture.Derived",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FRoadFurnitureDerivedTest::RunTest(const FString& Parameters)
{
	// Synthetische T-Kreuzung: drei 50-m-Arme, Kontrolle "Stopp", ein Arm Tempo 30.
	FRoadNetwork Network;
	Network.Segments.Add(MakeSegment(0, 1, 0, { FVector(-5000.0, 0.0, 0.0), FVector(0.0, 0.0, 0.0) }));
	Network.Segments.Add(MakeSegment(1, 2, 0, { FVector(5000.0, 0.0, 0.0), FVector(0.0, 0.0, 0.0) }));
	Network.Segments.Add(MakeSegment(2, 3, 0, { FVector(0.0, 5000.0, 0.0), FVector(0.0, 0.0, 0.0) }, /*SpeedKmh=*/30.0));

	FRoadIntersection Intersection;
	Intersection.NodeId = 0;
	Intersection.Location = FVector::ZeroVector;
	Intersection.Control = EIntersectionControl::Stop;
	Intersection.RadiusCm = 800.0;

	auto AddArm = [&Intersection](const FVector& Outward)
	{
		FIntersectionArm Arm;
		Arm.OutwardDirection = Outward;
		Arm.HalfWidthCm = 325.0;
		Arm.bIsSegmentStart = false;
		Arm.BearingDegrees = Outward.Rotation().Yaw;
		Intersection.Arms.Add(Arm);
	};
	AddArm(FVector(-1.0, 0.0, 0.0)); // West
	AddArm(FVector(1.0, 0.0, 0.0));  // Ost
	AddArm(FVector(0.0, 1.0, 0.0));  // +Y (Sued, Tempo 30)
	Network.Intersections.Add(Intersection);

	FFlatHeightSampler Sampler(0.0);
	FRoadFurnitureSettings Settings;
	FRoadFurnitureLayout Layout;

	URoadFurnitureGenerator* Generator = NewObject<URoadFurnitureGenerator>();
	const FRoadFurnitureReport Report = Generator->Generate(Network, nullptr, nullptr, &Sampler, Settings, Layout);

	TestTrue(TEXT("Pass erfolgreich"), Report.bSuccess);

	// Stopp-Schilder: je Arm eines, zusammen drei.
	int32 StopSigns = 0;
	int32 SpeedLimit30 = 0;
	for (const FSignInstance& Sign : Layout.Signs)
	{
		if (Sign.SignId == TEXT("206")) { ++StopSigns; }
		if (Sign.Sign.bSpeedLimit && Sign.Sign.SpeedLimitKmh == 30) { ++SpeedLimit30; }
	}
	TestEqual(TEXT("3 Stopp-Schilder"), StopSigns, 3);
	TestEqual(TEXT("1 Tempo-30-Schild"), SpeedLimit30, 1);

	// Leitpfosten: 50-m-Arme mit 50-m-Abstand -> 2 Positionen je Kante,
	// beidseitig -> 4 je Segment -> 12 gesamt.
	TestEqual(TEXT("12 Leitpfosten"), Layout.Delineators.Num(), 12);

	// Haltlinien: je Arm eine, zusammen drei. Zusaetzlich je 30-Zonen-Segment
	// eine "30" auf der Fahrbahn - alle drei Arme sind Wohnstrassen (bzw. Tempo
	// 30), also je Segment eine "30" (5000-cm-Segment, 5000-cm-Abstand).
	int32 StopLines = 0;
	int32 Zone30 = 0;
	for (const FMarkingInstance& Marking : Layout.Markings)
	{
		if (Marking.Kind == ERoadMarkingKind::StopLine) { ++StopLines; }
		else if (Marking.Kind == ERoadMarkingKind::SpeedZone30) { ++Zone30; }
	}
	TestEqual(TEXT("3 Haltlinien"), StopLines, 3);
	TestEqual(TEXT("3 Zone-30-Symbole"), Zone30, 3);

	TestTrue(TEXT("Report zaehlt Schilder"), Report.SignCount == Layout.Signs.Num());
	TestTrue(TEXT("Report zaehlt Leitpfosten"), Report.DelineatorCount == Layout.Delineators.Num());
	TestTrue(TEXT("Report zaehlt Markierungen"), Report.MarkingCount == Layout.Markings.Num());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadFurnitureZone30Test,
	"WiesbadenReal.GIS.RoadFurniture.Zone30",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FRoadFurnitureZone30Test::RunTest(const FString& Parameters)
{
	// Zwei gerade 100-m-Strassen ohne Kreuzung: eine Wohnstrasse (30-Zone, y=0)
	// und eine Hauptstrasse Tempo 50 (keine 30-Zone, y=3000).
	FRoadNetwork Network;
	FRoadSegment Wohn = MakeSegment(0, 1, 2,
		{ FVector(0.0, 0.0, 0.0), FVector(10000.0, 0.0, 0.0) }, /*SpeedKmh=*/30.0);
	FRoadSegment Haupt = MakeSegment(1, 3, 4,
		{ FVector(0.0, 3000.0, 0.0), FVector(10000.0, 3000.0, 0.0) }, /*SpeedKmh=*/50.0);
	Haupt.HighwayType = EOSMHighwayType::Primary;   // Durchgangsstrasse, keine 30-Zone
	Network.Segments.Add(Wohn);
	Network.Segments.Add(Haupt);

	FFlatHeightSampler Sampler(0.0);
	FRoadFurnitureSettings Settings;
	FRoadFurnitureLayout Layout;

	URoadFurnitureGenerator* Generator = NewObject<URoadFurnitureGenerator>();
	const FRoadFurnitureReport Report =
		Generator->Generate(Network, nullptr, nullptr, &Sampler, Settings, Layout);
	TestTrue(TEXT("Pass erfolgreich"), Report.bSuccess);

	int32 Zone30 = 0;
	bool bAllOnWohnstrasse = true;
	bool bAllAlongTravel = true;
	for (const FMarkingInstance& M : Layout.Markings)
	{
		if (M.Kind != ERoadMarkingKind::SpeedZone30)
		{
			continue;
		}
		++Zone30;
		// Alle "30" liegen auf der Wohnstrasse (y ~ 0), nicht auf der Hauptstrasse.
		if (FMath::Abs(M.Center.Y) > 100.0) { bAllOnWohnstrasse = false; }
		// Ausrichtung laengs zur Fahrtrichtung (+X).
		if (FMath::Abs(M.Direction.X) < 0.9) { bAllAlongTravel = false; }
	}
	// 100 m Laenge, 50 m Abstand, Start bei 25 m -> Positionen 25 m und 75 m.
	TestEqual(TEXT("2 Zone-30-Symbole auf der Wohnstrasse"), Zone30, 2);
	TestTrue(TEXT("Keine 30 auf der Tempo-50-Hauptstrasse"), bAllOnWohnstrasse);
	TestTrue(TEXT("30 laengs zur Fahrtrichtung ausgerichtet"), bAllAlongTravel);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadFurnitureExplicitSignTest,
	"WiesbadenReal.GIS.RoadFurniture.ExplicitSign",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FRoadFurnitureExplicitSignTest::RunTest(const FString& Parameters)
{
	// Ein OSM-Node mit explizitem traffic_sign=DE:206 am Wiesbaden-Origin.
	FOSMDataSet DataSet;
	FOSMNode Node;
	Node.Id = 999;
	Node.Location = FGeoCoordinate(8.2400, 50.0824, 0.0);
	Node.Tags.Add(TEXT("traffic_sign"), TEXT("DE:206"));
	DataSet.Nodes.Add(Node.Id, Node);

	UGeoCoordinateConverter* Converter = NewObject<UGeoCoordinateConverter>();
	if (!TestTrue(TEXT("Konverter initialisiert"), Converter->InitializeWithWiesbadenOrigin()))
	{
		return false;
	}

	// Minimales Netz (ein Segment), damit Generate nicht frueh abbricht.
	FRoadNetwork Network;
	Network.Segments.Add(MakeSegment(0, 1, 2, { FVector(-5000.0, 0.0, 0.0), FVector(5000.0, 0.0, 0.0) }));

	FFlatHeightSampler Sampler(0.0);
	FRoadFurnitureSettings Settings;
	FRoadFurnitureLayout Layout;

	URoadFurnitureGenerator* Generator = NewObject<URoadFurnitureGenerator>();
	const FRoadFurnitureReport Report = Generator->Generate(Network, &DataSet, Converter, &Sampler, Settings, Layout);

	TestTrue(TEXT("Pass erfolgreich"), Report.bSuccess);

	bool bFound = false;
	for (const FSignInstance& Sign : Layout.Signs)
	{
		if (Sign.SignId == TEXT("206") && Sign.SourceNodeId == 999)
		{
			bFound = true;
			break;
		}
	}
	TestTrue(TEXT("Explizites Node-Schild platziert"), bFound);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadFurniturePlaceholderSignTest,
	"WiesbadenReal.GIS.RoadFurniture.PlaceholderSign",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FRoadFurniturePlaceholderSignTest::RunTest(const FString& Parameters)
{
	// Ein OSM-Node mit traffic_sign=none ist kein explizites Schild. Er darf
	// deshalb die aus der Kreuzung abgeleiteten Stoppschilder nicht blockieren.
	FOSMDataSet DataSet;
	FOSMNode Node;
	Node.Id = 0;
	Node.Location = FGeoCoordinate(8.2400, 50.0824, 0.0);
	Node.Tags.Add(TEXT("traffic_sign"), TEXT("none"));
	DataSet.Nodes.Add(Node.Id, Node);

	UGeoCoordinateConverter* Converter = NewObject<UGeoCoordinateConverter>();
	if (!TestTrue(TEXT("Konverter initialisiert"), Converter->InitializeWithWiesbadenOrigin()))
	{
		return false;
	}

	FRoadNetwork Network;
	Network.Segments.Add(MakeSegment(0, 1, 0,
		{ FVector(-5000.0, 0.0, 0.0), FVector(0.0, 0.0, 0.0) }));
	Network.Segments.Add(MakeSegment(1, 2, 0,
		{ FVector(5000.0, 0.0, 0.0), FVector(0.0, 0.0, 0.0) }));
	Network.Segments.Add(MakeSegment(2, 3, 0,
		{ FVector(0.0, 5000.0, 0.0), FVector(0.0, 0.0, 0.0) }));

	FRoadIntersection Intersection;
	Intersection.NodeId = 0;
	Intersection.Location = FVector::ZeroVector;
	Intersection.Control = EIntersectionControl::Stop;
	Intersection.RadiusCm = 800.0;
	for (const FVector& Outward : { FVector(-1.0, 0.0, 0.0), FVector(1.0, 0.0, 0.0), FVector(0.0, 1.0, 0.0) })
	{
		FIntersectionArm Arm;
		Arm.OutwardDirection = Outward;
		Arm.HalfWidthCm = 325.0;
		Arm.bIsSegmentStart = false;
		Arm.BearingDegrees = Outward.Rotation().Yaw;
		Intersection.Arms.Add(Arm);
	}
	Network.Intersections.Add(Intersection);

	FFlatHeightSampler Sampler(0.0);
	FRoadFurnitureSettings Settings;
	FRoadFurnitureLayout Layout;
	URoadFurnitureGenerator* Generator = NewObject<URoadFurnitureGenerator>();
	const FRoadFurnitureReport Report =
		Generator->Generate(Network, &DataSet, Converter, &Sampler, Settings, Layout);

	TestTrue(TEXT("Pass erfolgreich"), Report.bSuccess);
	int32 StopSigns = 0;
	for (const FSignInstance& Sign : Layout.Signs)
	{
		if (Sign.SignId == TEXT("206"))
		{
			++StopSigns;
		}
	}
	TestEqual(TEXT("Placeholder blockiert keine 3 abgeleiteten Stoppschilder"), StopSigns, 3);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStreetLampSynthesisTest,
	"WiesbadenReal.GIS.RoadFurniture.StreetLampSynthesis",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Fehlende Strassenlaternen muessen ergaenzt werden - aber nur, wo keine steht.
 *
 * OSM hat fuer Wiesbaden 3.332 Leuchten erfasst, ein Bruchteil des Bestands.
 * Gemessen lag die naechste kartierte Laterne 915 m vom Startpunkt entfernt;
 * nachts war dort nichts zu sehen. Deutsche Ortsstrassen sind dagegen
 * praktisch durchgaengig beleuchtet.
 *
 * Geprueft wird beides: dass entlang einer unbeleuchteten Strasse ergaenzt wird
 * UND dass an einer bereits kartierten Leuchte KEINE zweite dazukommt. Ohne die
 * zweite Probe wuerde der Test auch ein blindes Zupflastern durchwinken.
 */
bool FStreetLampSynthesisTest::RunTest(const FString& Parameters)
{
	URoadFurnitureGenerator* Generator = NewObject<URoadFurnitureGenerator>();

	// Gerade Strasse, 300 m lang, entlang X.
	FRoadSegment Segment;
	Segment.SegmentId = 2;   // gerade -> erste Leuchte links
	Segment.HighwayType = EOSMHighwayType::Residential;
	Segment.CarriagewayWidthCm = 550.0;
	Segment.SidewalkWidthCm = 200.0;
	Segment.Centerline.Add(FVector(0.0, 0.0, 0.0));
	Segment.Centerline.Add(FVector(30000.0, 0.0, 0.0));

	FRoadNetwork Network;
	Network.Segments.Add(Segment);

	FRoadFurnitureSettings Settings;
	Settings.StreetLampSpacingCm = 3000.0;   // 30 m

	// -- Fall 1: keine kartierte Leuchte -----------------------------------
	{
		FRoadFurnitureLayout Layout;
		Generator->SynthesiseStreetLamps(Network, nullptr, Settings, Layout);

		// 300 m bei 30 m Abstand -> in der Groessenordnung von 10 Leuchten.
		TestTrue(
			FString::Printf(TEXT("Leuchten ergaenzt (%d)"), Layout.StreetLamps.Num()),
			Layout.StreetLamps.Num() >= 8 && Layout.StreetLamps.Num() <= 12);

		// Alle sind als ergaenzt gekennzeichnet.
		int32 FromOsm = 0;
		for (const FStreetLampInstance& Lamp : Layout.StreetLamps)
		{
			if (Lamp.NodeId != 0)
			{
				++FromOsm;
			}
		}
		TestEqual(TEXT("Ergaenzte Leuchten tragen keine OSM-Kennung"), FromOsm, 0);

		// Sie stehen NEBEN der Fahrbahn, nicht darauf.
		int32 OnCarriageway = 0;
		for (const FStreetLampInstance& Lamp : Layout.StreetLamps)
		{
			if (FMath::Abs(Lamp.Location.Y) < Segment.CarriagewayWidthCm * 0.5)
			{
				++OnCarriageway;
			}
		}
		TestEqual(TEXT("Keine Leuchte steht auf der Fahrbahn"), OnCarriageway, 0);

		// Beide Strassenseiten werden benutzt.
		bool bLeft = false;
		bool bRight = false;
		for (const FStreetLampInstance& Lamp : Layout.StreetLamps)
		{
			if (Lamp.Location.Y > 0.0) { bLeft = true; }
			if (Lamp.Location.Y < 0.0) { bRight = true; }
		}
		TestTrue(TEXT("Leuchten wechseln die Strassenseite"), bLeft && bRight);
	}

	// -- Fall 2: kartierte Leuchte am Anfang -------------------------------
	{
		FRoadFurnitureLayout Layout;

		FStreetLampInstance Mapped;
		Mapped.NodeId = 4711;
		Mapped.Location = FVector(0.0, 475.0, 0.0);   // dort, wo Fall 1 die erste setzte
		Layout.StreetLamps.Add(Mapped);

		Generator->SynthesiseStreetLamps(Network, nullptr, Settings, Layout);

		// Die kartierte Leuchte bleibt erhalten...
		int32 MappedCount = 0;
		for (const FStreetLampInstance& Lamp : Layout.StreetLamps)
		{
			if (Lamp.NodeId == 4711)
			{
				++MappedCount;
			}
		}
		TestEqual(TEXT("Kartierte Leuchte bleibt unangetastet"), MappedCount, 1);

		// ...und in ihrer Naehe kommt keine zweite dazu.
		int32 TooClose = 0;
		for (const FStreetLampInstance& Lamp : Layout.StreetLamps)
		{
			if (Lamp.NodeId == 4711)
			{
				continue;
			}
			if (FVector::Dist2D(Lamp.Location, Mapped.Location) < Settings.StreetLampSpacingCm * 0.7)
			{
				++TooClose;
			}
		}
		TestEqual(TEXT("Keine Leuchte doppelt neben einer kartierten"), TooClose, 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadFurnitureSurfaceHeightTest,
	"WiesbadenReal.GIS.RoadFurniture.SurfaceHeight",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Ausstattung steht AUF der Oberflaeche, nicht IM Boden.
 *
 * Die Strasse/der Gehweg liegt um RoadSurfaceOffsetCm (Fahrbahn) bzw.
 * RoadSurfaceOffsetCm + KerbHeightCm (Gehweg) UEBER dem Terrain. Frueher
 * sampelte die Ausstattung die rohe Terrainhoehe und sass damit um genau
 * diesen Betrag im Boden ("Ampeln/Schilder stecken im Boden"). Dieser Test
 * haelt fest, dass Schilder/Leitpfosten auf Gehweghoehe und Markierungen auf
 * Fahrbahnhoehe platziert werden.
 */
bool FRoadFurnitureSurfaceHeightTest::RunTest(const FString& Parameters)
{
	// Terrain auf konstanter, deutlich von 0 verschiedener Hoehe.
	constexpr double TerrainCm = 500.0;

	FRoadNetwork Network;
	Network.Segments.Add(MakeSegment(0, 1, 0, { FVector(-5000.0, 0.0, TerrainCm), FVector(0.0, 0.0, TerrainCm) }));
	Network.Segments.Add(MakeSegment(1, 2, 0, { FVector(5000.0, 0.0, TerrainCm), FVector(0.0, 0.0, TerrainCm) }));
	Network.Segments.Add(MakeSegment(2, 3, 0, { FVector(0.0, 5000.0, TerrainCm), FVector(0.0, 0.0, TerrainCm) }, 30.0));

	FRoadIntersection Intersection;
	Intersection.NodeId = 0;
	Intersection.Location = FVector(0.0, 0.0, TerrainCm);
	Intersection.Control = EIntersectionControl::Stop;
	Intersection.RadiusCm = 800.0;
	auto AddArm = [&Intersection](const FVector& Outward)
	{
		FIntersectionArm Arm;
		Arm.OutwardDirection = Outward;
		Arm.HalfWidthCm = 325.0;
		Arm.BearingDegrees = Outward.Rotation().Yaw;
		Intersection.Arms.Add(Arm);
	};
	AddArm(FVector(-1.0, 0.0, 0.0));
	AddArm(FVector(1.0, 0.0, 0.0));
	AddArm(FVector(0.0, 1.0, 0.0));
	Network.Intersections.Add(Intersection);

	FFlatHeightSampler Sampler(TerrainCm);
	FRoadFurnitureSettings Settings;   // Default: RoadSurfaceOffsetCm=20, KerbHeightCm=12
	FRoadFurnitureLayout Layout;

	URoadFurnitureGenerator* Generator = NewObject<URoadFurnitureGenerator>();
	const FRoadFurnitureReport Report = Generator->Generate(Network, nullptr, nullptr, &Sampler, Settings, Layout);
	TestTrue(TEXT("Pass erfolgreich"), Report.bSuccess);

	const double SidewalkZ = TerrainCm + Settings.RoadSurfaceOffsetCm + Settings.KerbHeightCm; // 532 (Fuss)
	const double RoadZ = TerrainCm + Settings.RoadSurfaceOffsetCm;                              // 520
	// Die Schild-TAFEL sitzt SignHeightAboveGroundCm ueber dem Gehweg-Fuss.
	const double SignPlateZ = SidewalkZ + Settings.SignHeightAboveGroundCm;

	TestTrue(TEXT("Es gibt Schilder zum Pruefen"), Layout.Signs.Num() > 0);
	for (const FSignInstance& Sign : Layout.Signs)
	{
		TestTrue(FString::Printf(TEXT("Schild-Tafel ueber Gehweg (%.1f statt %.1f)"), Sign.Location.Z, SignPlateZ),
			FMath::IsNearlyEqual(Sign.Location.Z, SignPlateZ, 0.5));
	}
	TestTrue(TEXT("Es gibt Leitpfosten zum Pruefen"), Layout.Delineators.Num() > 0);
	for (const FDelineatorInstance& D : Layout.Delineators)
	{
		TestTrue(FString::Printf(TEXT("Leitpfosten-Fuss auf Gehweghoehe (%.1f statt %.1f)"), D.Location.Z, SidewalkZ),
			FMath::IsNearlyEqual(D.Location.Z, SidewalkZ, 0.5));
	}
	TestTrue(TEXT("Es gibt Markierungen zum Pruefen"), Layout.Markings.Num() > 0);
	for (const FMarkingInstance& M : Layout.Markings)
	{
		TestTrue(FString::Printf(TEXT("Markierung auf Fahrbahnhoehe (%.1f statt %.1f)"), M.Center.Z, RoadZ),
			FMath::IsNearlyEqual(M.Center.Z, RoadZ, 0.5));
	}

	// Kernaussage: NICHTS sitzt auf der rohen Terrainhoehe (= im Boden).
	for (const FSignInstance& Sign : Layout.Signs)
	{
		TestTrue(TEXT("Schild NICHT auf roher Terrainhoehe (nicht im Boden)"),
			Sign.Location.Z > TerrainCm + 1.0);
	}

	return true;
}
