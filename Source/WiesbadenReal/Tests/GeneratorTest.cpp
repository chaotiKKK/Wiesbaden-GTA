// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "GIS/BuildingGenerator.h"
#include "GIS/CityPrompt.h"
#include "GIS/GeoCoordinateConverter.h"
#include "GIS/IHeightSampler.h"
#include "GIS/OSMDataParser.h"
#include "GIS/PolygonUtils.h"
#include "GIS/WiesbadenTrafficLights.h"
#include "GIS/RoadNetworkGenerator.h"
#include "GIS/RoadTypeLibrary.h"
#include "GIS/TerrainGenerator.h"

namespace
{
	/** Initialisierter Konverter mit dem Projekt-Origin. */
	UGeoCoordinateConverter* NewWiesbadenConverter()
	{
		UGeoCoordinateConverter* Converter = NewObject<UGeoCoordinateConverter>();
		Converter->InitializeWithWiesbadenOrigin();
		return Converter;
	}

	/** Way mit Highway-Tag und einer Node-Liste. */
	FOSMWay MakeRoad(FOSMId Id, const TArray<FOSMId>& NodeIds, const FString& Name)
	{
		FOSMWay Way;
		Way.Id = Id;
		Way.NodeIds = NodeIds;
		Way.Tags.Add(TEXT("highway"), TEXT("residential"));
		Way.Tags.Add(TEXT("name"), Name);
		return Way;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadNetworkGeneratorTest,
	"WiesbadenReal.GIS.RoadNetworkGenerator.Generate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FRoadNetworkGeneratorTest::RunTest(const FString& Parameters)
{
	UGeoCoordinateConverter* Converter = NewWiesbadenConverter();
	if (!TestTrue(TEXT("Konverter initialisiert"), Converter != nullptr && Converter->IsInitialized()))
	{
		return false;
	}

	URoadTypeLibrary* TypeLibrary = NewObject<URoadTypeLibrary>();
	TypeLibrary->ApplyBuiltInDefaults();

	// Drei Wohnstrassen treffen sich in Node 1 -> eine T-Kreuzung mit 3 Armen.
	FOSMDataSet DataSet;
	DataSet.Nodes.Add(1, FOSMNode(1, 8.2400, 50.0824)); // Mitte
	DataSet.Nodes.Add(2, FOSMNode(2, 8.2410, 50.0824)); // Osten
	DataSet.Nodes.Add(3, FOSMNode(3, 8.2400, 50.0830)); // Norden
	DataSet.Nodes.Add(4, FOSMNode(4, 8.2390, 50.0824)); // Westen

	DataSet.Ways.Add(100, MakeRoad(100, { 2, 1 }, TEXT("Oststrasse")));
	DataSet.Ways.Add(101, MakeRoad(101, { 3, 1 }, TEXT("Nordstrasse")));
	DataSet.Ways.Add(102, MakeRoad(102, { 4, 1 }, TEXT("Weststrasse")));

	URoadNetworkGenerator* Generator = NewObject<URoadNetworkGenerator>();
	FRoadNetwork Network;
	FRoadMeshData MeshData;
	FRoadGenerationSettings Settings; // Defaults

	const FRoadGenerationReport Report = Generator->Generate(
		DataSet, Converter, TypeLibrary, /*HeightSampler=*/nullptr, Settings, Network, &MeshData);

	TestTrue(TEXT("Strassennetz erfolgreich"), Report.bSuccess);
	TestTrue(TEXT("Fehlermeldung leer"), Report.ErrorMessage.IsEmpty());
	TestEqual(TEXT("3 Ways verarbeitet"), Report.ProcessedWayCount, 3);
	TestEqual(TEXT("3 Segmente"), Report.SegmentCount, 3);
	TestEqual(TEXT("1 Kreuzung"), Report.IntersectionCount, 1);
	TestEqual(TEXT("6 Spuren (3x2)"), Report.LaneCount, 6);
	TestTrue(TEXT("Verbindungen vorhanden"), Report.ConnectionCount > 0);

	TestTrue(TEXT("Netz nicht leer"), !Network.IsEmpty());
	TestEqual(TEXT("Netz-Segmente"), Network.Segments.Num(), 3);
	TestEqual(TEXT("Netz-Kreuzungen"), Network.Intersections.Num(), 1);
	TestTrue(TEXT("Netz-Spuren"), Network.Lanes.Num() >= 6);
	TestTrue(TEXT("Befahrbare Laenge > 0"), Network.GetTotalDrivableLengthKm() > 0.0);

	TestTrue(TEXT("Mesh-Vertices"), Report.VertexCount > 0);
	TestTrue(TEXT("Mesh-Dreiecke"), Report.TriangleCount > 0);
	TestTrue(TEXT("Mesh-Daten gefuellt"), MeshData.GetTotalTriangleCount() > 0);

	// Segmente tragen die erwarteten Kennwerte (Wohnstrassen-Default: 50 km/h).
	for (const FRoadSegment& Segment : Network.Segments)
	{
		TestTrue(TEXT("Segment-Name gesetzt"), !Segment.StreetName.IsEmpty());
		TestTrue(TEXT("Segment drivable"), FOSMTagParser::IsDrivable(Segment.HighwayType));
		TestTrue(TEXT("Segment hat Centerline"), Segment.Centerline.Num() >= 2);
		TestTrue(TEXT("Segment-Tempo plausibel"), Segment.MaxSpeedKmh > 0.0 && Segment.MaxSpeedKmh < 200.0);
	}

	// Jede Spur hat eine gueltige Sollbahn.
	for (const FRoadLane& Lane : Network.Lanes)
	{
		TestTrue(TEXT("Spur gueltig"), Lane.IsValid());
		TestTrue(TEXT("Spur-Laenge > 0"), Lane.LengthCm > 0.0);
	}

	return true;
}

namespace
{
	/** Gelaende mit konstanter Neigung - fuer Nahtpruefungen am Hang. */
	class FSlopedHeightSampler final : public IHeightSampler
	{
	public:
		FSlopedHeightSampler(double InBaseCm, double InSlopeX, double InSlopeY)
			: BaseCm(InBaseCm), SlopeX(InSlopeX), SlopeY(InSlopeY)
		{
		}

		virtual double SampleHeightCm(const FVector2D& WorldXY) const override
		{
			return BaseCm + WorldXY.X * SlopeX + WorldXY.Y * SlopeY;
		}

		virtual bool HasValidData() const override { return true; }

	private:
		double BaseCm;
		double SlopeX;
		double SlopeY;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShortSegmentSurvivesTrimTest,
	"WiesbadenReal.GIS.RoadNetworkGenerator.ShortSegmentSurvivesTrim",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Ein kurzes Stueck zwischen zwei dicht benachbarten Kreuzungen muss eine
 * Fahrbahnflaeche behalten.
 *
 * Segmente werden an jedem Kreuzungsende gekuerzt, damit sich Fahrbahn und
 * Kreuzungsflaeche nicht ueberlagern. Waren beide Kuerzungen zusammen laenger
 * als das Segment, wurde es frueher ersatzlos verworfen - mit der Begruendung,
 * die Kreuzungsflaechen wuerden die Luecke schliessen.
 *
 * Das tun sie nicht. Jede Flaeche reicht nur so weit wie ihre eigene Kuerzung;
 * dazwischen blieb blanker Boden. Im Spiel sah das aus, als sei die Strasse
 * abgerissen: Fahrbahn und beide Gehwege enden in einer sauberen Querkante.
 *
 * Der Test stellt zwei Kreuzungen rund 12 m auseinander - kuerzer als die
 * Summe der beiden Kuerzungen, die bei fast gegenlaeufigen Armen anfaellt.
 */
bool FShortSegmentSurvivesTrimTest::RunTest(const FString& Parameters)
{
	UGeoCoordinateConverter* Converter = NewWiesbadenConverter();
	if (!TestTrue(TEXT("Konverter initialisiert"), Converter != nullptr && Converter->IsInitialized()))
	{
		return false;
	}

	URoadTypeLibrary* TypeLibrary = NewObject<URoadTypeLibrary>();
	TypeLibrary->ApplyBuiltInDefaults();

	// Auf 50,08 Grad Nord entspricht ein Laengengrad rund 71,5 km.
	// 0,00017 Grad sind damit etwa 12 m.
	constexpr double ShortStepDeg = 0.00017;

	FOSMDataSet DataSet;
	DataSet.Nodes.Add(1, FOSMNode(1, 8.2400, 50.0824));                    // Kreuzung A
	DataSet.Nodes.Add(2, FOSMNode(2, 8.2400 + ShortStepDeg, 50.0824));     // Kreuzung B
	DataSet.Nodes.Add(3, FOSMNode(3, 8.2380, 50.0824));                    // Westarm an A
	DataSet.Nodes.Add(4, FOSMNode(4, 8.2400, 50.0832));                    // Nordarm an A
	DataSet.Nodes.Add(5, FOSMNode(5, 8.2420, 50.0824));                    // Ostarm an B
	DataSet.Nodes.Add(6, FOSMNode(6, 8.2400 + ShortStepDeg, 50.0816));     // Suedarm an B

	DataSet.Ways.Add(100, MakeRoad(100, { 3, 1 }, TEXT("Weststrasse")));
	DataSet.Ways.Add(101, MakeRoad(101, { 4, 1 }, TEXT("Nordstrasse")));
	DataSet.Ways.Add(102, MakeRoad(102, { 1, 2 }, TEXT("Kurzes Stueck")));
	DataSet.Ways.Add(103, MakeRoad(103, { 2, 5 }, TEXT("Oststrasse")));
	DataSet.Ways.Add(104, MakeRoad(104, { 6, 2 }, TEXT("Suedstrasse")));

	URoadNetworkGenerator* Generator = NewObject<URoadNetworkGenerator>();
	FRoadNetwork Network;
	FRoadGenerationSettings Settings;

	const FRoadGenerationReport Report = Generator->Generate(
		DataSet, Converter, TypeLibrary, nullptr, Settings, Network, nullptr);

	TestTrue(TEXT("Strassennetz erfolgreich"), Report.bSuccess);
	TestEqual(TEXT("Zwei Kreuzungen"), Network.Intersections.Num(), 2);

	// KEIN Segment darf ohne Fahrbahnflaeche dastehen - auch das kurze nicht.
	int32 SegmentsWithoutSurface = 0;
	const FRoadSegment* ShortSegment = nullptr;

	for (const FRoadSegment& Segment : Network.Segments)
	{
		if (Segment.TrimmedCenterline.Num() < 2)
		{
			++SegmentsWithoutSurface;
		}
		if (Segment.StreetName == TEXT("Kurzes Stueck"))
		{
			ShortSegment = &Segment;
		}
	}

	TestEqual(TEXT("Kein Segment ohne Fahrbahnflaeche"), SegmentsWithoutSurface, 0);

	if (!TestTrue(TEXT("Kurzes Stueck gefunden"), ShortSegment != nullptr))
	{
		return false;
	}

	TestTrue(TEXT("Kurzes Stueck hat eine Fahrbahnflaeche"),
		ShortSegment->TrimmedCenterline.Num() >= 2);

	// Gegenprobe zur Aussagekraft: Auf einem LANGEN Arm muss weiterhin gekuerzt
	// werden. Ohne diese Probe wuerde der Test auch dann bestehen, wenn die
	// Kuerzung insgesamt abgeschaltet waere - und dann laegen Fahrbahn und
	// Kreuzungsflaeche wieder uebereinander.
	const FRoadSegment* LongSegment = nullptr;
	for (const FRoadSegment& Segment : Network.Segments)
	{
		if (Segment.StreetName == TEXT("Oststrasse"))
		{
			LongSegment = &Segment;
			break;
		}
	}

	if (TestTrue(TEXT("Langer Arm gefunden"), LongSegment != nullptr))
	{
		auto PolylineLength = [](const TArray<FVector>& Points)
		{
			double Sum = 0.0;
			for (int32 Index = 1; Index < Points.Num(); ++Index)
			{
				Sum += FVector::Dist2D(Points[Index - 1], Points[Index]);
			}
			return Sum;
		};

		const double FullCm = PolylineLength(LongSegment->Centerline);
		const double TrimmedCm = PolylineLength(LongSegment->TrimmedCenterline);

		TestTrue(
			FString::Printf(TEXT("Langer Arm wird weiterhin gekuerzt (%.0f cm von %.0f cm)"),
				TrimmedCm, FullCm),
			TrimmedCm < FullCm - 100.0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIntersectionSignalTest,
	"WiesbadenReal.GIS.RoadNetworkGenerator.IntersectionSignals",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Eine Ampel VOR der Kreuzung macht die Kreuzung signalgesteuert.
 *
 * In OSM sitzt highway=traffic_signals fast immer auf einem Knoten der
 * zufuehrenden Strasse - dort, wo der Mast steht - und nicht auf dem
 * Kreuzungsknoten selbst. Wurde nur der Kreuzungsknoten geprueft, blieben von
 * rund 2300 Signalknoten in Wiesbaden 27 Ampeln uebrig, und im Spiel hielt kein
 * Fahrzeug jemals an Rot.
 *
 * Geprueft wird beides: dass eine nahe Ampel greift UND dass eine weit
 * entfernte es nicht tut - sonst waere jede Kreuzung der Stadt signalgesteuert,
 * was genauso falsch waere.
 */
bool FIntersectionSignalTest::RunTest(const FString& Parameters)
{
	UGeoCoordinateConverter* Converter = NewWiesbadenConverter();
	if (!TestTrue(TEXT("Konverter initialisiert"), Converter != nullptr && Converter->IsInitialized()))
	{
		return false;
	}

	URoadTypeLibrary* TypeLibrary = NewObject<URoadTypeLibrary>();
	TypeLibrary->ApplyBuiltInDefaults();

	// Baut eine T-Kreuzung an Knoten 1, mit einem Signalknoten auf der
	// Oststrasse in SignalLongitude.
	auto BuildWithSignalAt = [&](double SignalLongitude, FRoadNetwork& OutNetwork)
	{
		FOSMDataSet DataSet;
		DataSet.Nodes.Add(1, FOSMNode(1, 8.2400, 50.0824)); // Kreuzung
		DataSet.Nodes.Add(2, FOSMNode(2, 8.2420, 50.0824)); // Osten
		DataSet.Nodes.Add(3, FOSMNode(3, 8.2400, 50.0832)); // Norden
		DataSet.Nodes.Add(4, FOSMNode(4, 8.2380, 50.0824)); // Westen

		FOSMNode SignalNode(6, SignalLongitude, 50.0824);
		SignalNode.Tags.Add(TEXT("highway"), TEXT("traffic_signals"));
		DataSet.Nodes.Add(6, SignalNode);

		// Der Signalknoten liegt AUF der Oststrasse, zwischen Knoten 2 und 1.
		DataSet.Ways.Add(100, MakeRoad(100, { 2, 6, 1 }, TEXT("Oststrasse")));
		DataSet.Ways.Add(101, MakeRoad(101, { 3, 1 }, TEXT("Nordstrasse")));
		DataSet.Ways.Add(102, MakeRoad(102, { 4, 1 }, TEXT("Weststrasse")));

		URoadNetworkGenerator* Generator = NewObject<URoadNetworkGenerator>();
		FRoadGenerationSettings Settings;

		return Generator->Generate(
			DataSet, Converter, TypeLibrary, nullptr, Settings, OutNetwork, nullptr);
	};

	// Auf 50,08 Grad Nord entspricht ein Laengengrad rund 71,5 km.
	// 0,00028 Grad sind damit etwa 20 m - ein typischer Mastabstand.
	{
		FRoadNetwork Network;
		const FRoadGenerationReport Report = BuildWithSignalAt(8.24028, Network);

		TestTrue(TEXT("Strassennetz erfolgreich (nah)"), Report.bSuccess);
		if (!TestEqual(TEXT("Eine Kreuzung (nah)"), Network.Intersections.Num(), 1))
		{
			return false;
		}

		TestTrue(TEXT("Ampel 20 m vor der Kreuzung steuert sie"),
			Network.Intersections[0].Control == EIntersectionControl::TrafficSignals);
	}

	// Gegenprobe: 0,0018 Grad sind rund 130 m - weit ausserhalb des
	// Suchradius von 40 m. Ohne diese Probe wuerde der Test auch ein
	// "alles ist signalgesteuert" durchwinken.
	{
		FRoadNetwork Network;
		const FRoadGenerationReport Report = BuildWithSignalAt(8.2418, Network);

		TestTrue(TEXT("Strassennetz erfolgreich (fern)"), Report.bSuccess);
		if (!TestEqual(TEXT("Eine Kreuzung (fern)"), Network.Intersections.Num(), 1))
		{
			return false;
		}

		TestTrue(TEXT("Ampel 130 m entfernt steuert die Kreuzung NICHT"),
			Network.Intersections[0].Control != EIntersectionControl::TrafficSignals);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIntersectionCoversRibbonEndsTest,
	"WiesbadenReal.GIS.RoadNetworkGenerator.IntersectionCoversRibbonEnds",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Die Kreuzungsflaeche muss die Enden ALLER Fahrbahnbaender ueberdecken.
 *
 * Der Umriss entstand frueher aus "Mitte + Auswaertsrichtung * Kuerzungslaenge",
 * also unter der Annahme, jede Strasse verlasse die Kreuzung GERADLINIG entlang
 * ihrer Anfangstangente. Bei einer gekruemmten Zufahrt endet das Band seitlich
 * versetzt, und dazwischen klaffte Wiese. In der gebauten Stadt lagen 1.274 von
 * 4.754 Bandenden (27 %) ausserhalb der Flaeche, im Extremfall 5,2 m daneben.
 *
 * Der Test faehrt deshalb bewusst eine KRUMME Zufahrt; auf geraden Armen waere
 * der Fehler unsichtbar.
 */
bool FIntersectionCoversRibbonEndsTest::RunTest(const FString& Parameters)
{
	UGeoCoordinateConverter* Converter = NewWiesbadenConverter();
	if (!TestTrue(TEXT("Konverter initialisiert"), Converter != nullptr && Converter->IsInitialized()))
	{
		return false;
	}

	URoadTypeLibrary* TypeLibrary = NewObject<URoadTypeLibrary>();
	TypeLibrary->ApplyBuiltInDefaults();

	FOSMDataSet DataSet;
	DataSet.Nodes.Add(1, FOSMNode(1, 8.2400, 50.0824));   // Kreuzung
	DataSet.Nodes.Add(2, FOSMNode(2, 8.2420, 50.0824));   // Ostarm, gerade
	DataSet.Nodes.Add(3, FOSMNode(3, 8.2400, 50.0834));   // Nordarm, gerade

	// Westarm mit deutlicher Kruemmung: er laeuft nach Suedwesten und biegt
	// erst kurz vor der Kreuzung nach Osten ein.
	DataSet.Nodes.Add(4, FOSMNode(4, 8.2378, 50.0812));
	DataSet.Nodes.Add(5, FOSMNode(5, 8.2384, 50.0816));
	DataSet.Nodes.Add(6, FOSMNode(6, 8.2390, 50.0820));
	DataSet.Nodes.Add(7, FOSMNode(7, 8.2395, 50.0823));

	DataSet.Ways.Add(100, MakeRoad(100, { 2, 1 }, TEXT("Oststrasse")));
	DataSet.Ways.Add(101, MakeRoad(101, { 3, 1 }, TEXT("Nordstrasse")));
	DataSet.Ways.Add(102, MakeRoad(102, { 4, 5, 6, 7, 1 }, TEXT("Kurvenstrasse")));

	URoadNetworkGenerator* Generator = NewObject<URoadNetworkGenerator>();
	FRoadNetwork Network;
	FRoadGenerationSettings Settings;

	const FRoadGenerationReport Report = Generator->Generate(
		DataSet, Converter, TypeLibrary, nullptr, Settings, Network, nullptr);

	TestTrue(TEXT("Strassennetz erfolgreich"), Report.bSuccess);
	if (!TestEqual(TEXT("Eine Kreuzung"), Network.Intersections.Num(), 1))
	{
		return false;
	}

	const FRoadIntersection& Intersection = Network.Intersections[0];
	if (!TestTrue(TEXT("Kreuzungsflaeche vorhanden"), Intersection.Polygon.Num() >= 3))
	{
		return false;
	}

	// Punkt-in-Polygon (Strahlverfahren) in 2D.
	auto IsInside = [](const FVector2D& P, const TArray<FVector>& Polygon)
	{
		bool bInside = false;
		const int32 N = Polygon.Num();
		for (int32 i = 0, j = N - 1; i < N; j = i++)
		{
			const double Xi = Polygon[i].X, Yi = Polygon[i].Y;
			const double Xj = Polygon[j].X, Yj = Polygon[j].Y;
			if ((Yi > P.Y) != (Yj > P.Y))
			{
				const double XCross = (Xj - Xi) * (P.Y - Yi) / (Yj - Yi) + Xi;
				if (P.X < XCross)
				{
					bInside = !bInside;
				}
			}
		}
		return bInside;
	};

	int32 EndsOutside = 0;
	int32 EndsChecked = 0;

	for (const FIntersectionArm& Arm : Intersection.Arms)
	{
		if (!Network.Segments.IsValidIndex(Arm.SegmentId))
		{
			continue;
		}

		const TArray<FVector>& Trimmed = Network.Segments[Arm.SegmentId].TrimmedCenterline;
		if (Trimmed.Num() < 2)
		{
			continue;
		}

		const FVector& End = Arm.bIsSegmentStart ? Trimmed[0] : Trimmed.Last();
		++EndsChecked;

		// Ein Punkt GENAU auf dem Rand ist zulaessig - die Bandkante liegt
		// bauartbedingt dort. Deshalb ein Stueck in Richtung Mitte pruefen.
		const FVector2D Inward = (FVector2D(Intersection.Location.X, Intersection.Location.Y)
			- FVector2D(End.X, End.Y)).GetSafeNormal();
		const FVector2D Probe = FVector2D(End.X, End.Y) + Inward * 10.0;

		if (!IsInside(Probe, Intersection.Polygon))
		{
			++EndsOutside;
		}
	}

	TestTrue(TEXT("Alle drei Arme geprueft"), EndsChecked == 3);
	TestEqual(
		FString::Printf(TEXT("Jedes Bandende liegt in der Kreuzungsflaeche (%d von %d ausserhalb)"),
			EndsOutside, EndsChecked),
		EndsOutside, 0);

	// Gegenprobe zur Aussagekraft: Die Zufahrt MUSS wirklich krumm sein, sonst
	// prueft der Test den Fehlerfall gar nicht. Die Anfangstangente des
	// Kurvenarms und seine Richtung am Kreuzungsende muessen sich deutlich
	// unterscheiden.
	const FRoadSegment* Curved = nullptr;
	for (const FRoadSegment& Segment : Network.Segments)
	{
		if (Segment.StreetName == TEXT("Kurvenstrasse"))
		{
			Curved = &Segment;
			break;
		}
	}

	if (TestTrue(TEXT("Kurvenstrasse gefunden"), Curved != nullptr) && Curved->Centerline.Num() >= 3)
	{
		const FVector StartDir = (Curved->Centerline[1] - Curved->Centerline[0]).GetSafeNormal2D();
		const FVector EndDir = (Curved->Centerline.Last()
			- Curved->Centerline[Curved->Centerline.Num() - 2]).GetSafeNormal2D();
		const double AngleDeg = FMath::RadiansToDegrees(
			FMath::Acos(FMath::Clamp(FVector::DotProduct(StartDir, EndDir), -1.0, 1.0)));

		TestTrue(
			FString::Printf(TEXT("Zufahrt ist wirklich gekruemmt (%.0f Grad)"), AngleDeg),
			AngleDeg > 10.0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIntersectionSeamTest,
	"WiesbadenReal.GIS.RoadNetworkGenerator.IntersectionSeam",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Die Ecken der Kreuzungsflaeche muessen auf der Hoehe der angrenzenden
 * Fahrbahnenden liegen.
 *
 * Beide Flaechen stossen an denselben XY-Punkten aneinander: die konvexe Huelle
 * der Kreuzung wird aus genau den Eckpunkten der gekuerzten Fahrbahnenden
 * gebildet. Ihre Hoehen entstanden aber nach verschiedenen Regeln - das Band
 * quer flach aus seiner Mittellinie, die Kreuzungsecke durch Abtasten des
 * Gelaendes an der Ecke selbst. Am Querhang klaffen beide auseinander, und an
 * jeder Kreuzung stand eine sichtbare Stufe.
 *
 * Der Test faehrt deshalb ein GENEIGTES Gelaende; auf ebenem Grund waere der
 * Fehler unsichtbar.
 */
bool FIntersectionSeamTest::RunTest(const FString& Parameters)
{
	UGeoCoordinateConverter* Converter = NewWiesbadenConverter();
	if (!TestTrue(TEXT("Konverter initialisiert"), Converter != nullptr && Converter->IsInitialized()))
	{
		return false;
	}

	URoadTypeLibrary* TypeLibrary = NewObject<URoadTypeLibrary>();
	TypeLibrary->ApplyBuiltInDefaults();

	// Vierarmige Kreuzung.
	FOSMDataSet DataSet;
	DataSet.Nodes.Add(1, FOSMNode(1, 8.2400, 50.0824)); // Mitte
	DataSet.Nodes.Add(2, FOSMNode(2, 8.2412, 50.0824)); // Osten
	DataSet.Nodes.Add(3, FOSMNode(3, 8.2400, 50.0832)); // Norden
	DataSet.Nodes.Add(4, FOSMNode(4, 8.2388, 50.0824)); // Westen
	DataSet.Nodes.Add(5, FOSMNode(5, 8.2400, 50.0816)); // Sueden

	DataSet.Ways.Add(100, MakeRoad(100, { 2, 1 }, TEXT("Oststrasse")));
	DataSet.Ways.Add(101, MakeRoad(101, { 3, 1 }, TEXT("Nordstrasse")));
	DataSet.Ways.Add(102, MakeRoad(102, { 4, 1 }, TEXT("Weststrasse")));
	DataSet.Ways.Add(103, MakeRoad(103, { 5, 1 }, TEXT("Suedstrasse")));

	// 6 % Neigung in beiden Achsen - eine Kreuzung am Hang, wie in Wiesbaden
	// die Regel und nicht die Ausnahme.
	const FSlopedHeightSampler Sampler(11000.0, 0.06, 0.06);

	URoadNetworkGenerator* Generator = NewObject<URoadNetworkGenerator>();
	FRoadNetwork Network;
	FRoadGenerationSettings Settings;

	const FRoadGenerationReport Report = Generator->Generate(
		DataSet, Converter, TypeLibrary, &Sampler, Settings, Network, nullptr);

	TestTrue(TEXT("Strassennetz erfolgreich"), Report.bSuccess);
	if (!TestEqual(TEXT("Eine Kreuzung"), Network.Intersections.Num(), 1))
	{
		return false;
	}

	const FRoadIntersection& Intersection = Network.Intersections[0];
	TestTrue(TEXT("Kreuzungsflaeche hat Ecken"), Intersection.Polygon.Num() >= 3);

	// Alle Hoehen der angrenzenden Fahrbahnenden einsammeln.
	TArray<double> ArmEndHeights;
	for (const FRoadSegment& Segment : Network.Segments)
	{
		if (Segment.TrimmedCenterline.Num() >= 2)
		{
			ArmEndHeights.Add(Segment.TrimmedCenterline[0].Z);
			ArmEndHeights.Add(Segment.TrimmedCenterline.Last().Z);
		}
	}

	if (!TestTrue(TEXT("Fahrbahnenden vorhanden"), ArmEndHeights.Num() > 0))
	{
		return false;
	}

	// Jede Kreuzungsecke muss zu IRGENDEINEM Fahrbahnende passen - naemlich zu
	// dem, an das sie stoesst. Die Toleranz ist bewusst eng: 5 cm sind im Spiel
	// noch nicht als Stufe zu erkennen, 26 cm (der gemessene Fehler bei 8 %
	// Querneigung) sehr wohl.
	constexpr double ToleranceCm = 5.0;

	int32 MismatchedCorners = 0;
	double WorstMismatchCm = 0.0;

	for (const FVector& Corner : Intersection.Polygon)
	{
		double BestDeltaCm = TNumericLimits<double>::Max();
		for (const double ArmHeight : ArmEndHeights)
		{
			BestDeltaCm = FMath::Min(BestDeltaCm, FMath::Abs(Corner.Z - ArmHeight));
		}

		if (BestDeltaCm > ToleranceCm)
		{
			++MismatchedCorners;
		}
		WorstMismatchCm = FMath::Max(WorstMismatchCm, BestDeltaCm);
	}

	TestEqual(
		FString::Printf(
			TEXT("Jede Kreuzungsecke sitzt auf einem Fahrbahnende (groesster Versatz %.1f cm)"),
			WorstMismatchCm),
		MismatchedCorners, 0);

	// Gegenprobe zur Aussagekraft: Auf diesem Hang MUESSEN die Ecken
	// unterschiedlich hoch liegen. Waeren sie alle gleich, wuerde der Test auch
	// eine flach eingezogene Kreuzungsplatte durchwinken.
	double MinCornerZ = TNumericLimits<double>::Max();
	double MaxCornerZ = TNumericLimits<double>::Lowest();
	for (const FVector& Corner : Intersection.Polygon)
	{
		MinCornerZ = FMath::Min(MinCornerZ, Corner.Z);
		MaxCornerZ = FMath::Max(MaxCornerZ, Corner.Z);
	}

	TestTrue(
		FString::Printf(TEXT("Kreuzung folgt dem Hang (Spanne %.1f cm)"), MaxCornerZ - MinCornerZ),
		(MaxCornerZ - MinCornerZ) > ToleranceCm);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadTurnClassificationTest,
	"WiesbadenReal.GIS.RoadNetworkGenerator.Turns",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FRoadTurnClassificationTest::RunTest(const FString& Parameters)
{
	const FVector East(1.0, 0.0, 0.0);
	const FVector West(-1.0, 0.0, 0.0);
	const FVector South(0.0, 1.0, 0.0);
	const FVector North(0.0, -1.0, 0.0);

	TestTrue(TEXT("Geradeaus -> Through"), URoadNetworkGenerator::ClassifyTurn(East, East) == ETurnType::Through);
	TestTrue(TEXT("Gegenrichtung -> UTurn"), URoadNetworkGenerator::ClassifyTurn(East, West) == ETurnType::UTurn);
	TestTrue(TEXT("Ost->Sued -> Right"), URoadNetworkGenerator::ClassifyTurn(East, South) == ETurnType::Right);
	TestTrue(TEXT("Ost->Nord -> Left"), URoadNetworkGenerator::ClassifyTurn(East, North) == ETurnType::Left);

	const uint8 Left = URoadNetworkGenerator::ParseTurnIndication(TEXT("left"));
	TestTrue(TEXT("turn left"), (Left & static_cast<uint8>(ETurnIndication::Left)) != 0);

	const uint8 ThroughRight = URoadNetworkGenerator::ParseTurnIndication(TEXT("through;right"));
	TestTrue(TEXT("turn through;right -> Through"),
		(ThroughRight & static_cast<uint8>(ETurnIndication::Through)) != 0);
	TestTrue(TEXT("turn through;right -> Right"),
		(ThroughRight & static_cast<uint8>(ETurnIndication::Right)) != 0);

	const uint8 Reverse = URoadNetworkGenerator::ParseTurnIndication(TEXT("reverse"));
	TestTrue(TEXT("turn reverse -> UTurn"), (Reverse & static_cast<uint8>(ETurnIndication::UTurn)) != 0);

	const uint8 Garbage = URoadNetworkGenerator::ParseTurnIndication(TEXT("garbage"));
	TestTrue(TEXT("turn unbekannt -> Through"),
		(Garbage & static_cast<uint8>(ETurnIndication::Through)) != 0);

	const uint8 Empty = URoadNetworkGenerator::ParseTurnIndication(TEXT(""));
	TestTrue(TEXT("turn leer -> None"), Empty == static_cast<uint8>(ETurnIndication::None));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBuildingGeneratorTest,
	"WiesbadenReal.GIS.BuildingGenerator.Generate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBuildingGeneratorTest::RunTest(const FString& Parameters)
{
	UGeoCoordinateConverter* Converter = NewWiesbadenConverter();
	if (!TestTrue(TEXT("Konverter initialisiert"), Converter != nullptr && Converter->IsInitialized()))
	{
		return false;
	}

	// Geschlossener Grundriss (Quadrat ~7 m x ~11 m) mit 4 Geschossen.
	FOSMDataSet DataSet;
	DataSet.Nodes.Add(1, FOSMNode(1, 8.2400, 50.0824));
	DataSet.Nodes.Add(2, FOSMNode(2, 8.2401, 50.0824));
	DataSet.Nodes.Add(3, FOSMNode(3, 8.2401, 50.0825));
	DataSet.Nodes.Add(4, FOSMNode(4, 8.2400, 50.0825));

	FOSMWay Building;
	Building.Id = 1;
	Building.NodeIds = { 1, 2, 3, 4, 1 };
	Building.Tags.Add(TEXT("building"), TEXT("yes"));
	Building.Tags.Add(TEXT("building:levels"), TEXT("4"));
	DataSet.Ways.Add(1, Building);

	UBuildingGenerator* Generator = NewObject<UBuildingGenerator>();
	TArray<FGeneratedBuilding> Buildings;
	FBuildingMeshData MeshData;
	FBuildingGenerationSettings Settings; // Defaults

	const FBuildingGenerationReport Report = Generator->Generate(
		DataSet, Converter, /*HeightSampler=*/nullptr, Settings, Buildings, &MeshData);

	TestTrue(TEXT("Gebaeude erfolgreich"), Report.bSuccess);
	TestEqual(TEXT("1 Gebaeude"), Report.BuildingCount, 1);
	TestEqual(TEXT("Gebaeude-Array"), Buildings.Num(), 1);
	TestTrue(TEXT("Mesh-Vertices"), Report.VertexCount > 0);
	TestTrue(TEXT("Mesh-Dreiecke"), Report.TriangleCount > 0);

	if (Buildings.Num() == 1)
	{
		const FGeneratedBuilding& B = Buildings[0];
		TestEqual(TEXT("LevelCount == 4"), B.LevelCount, 4);
		TestTrue(TEXT("Typ Generic"), B.BuildingType == EOSMBuildingType::Generic);
		TestTrue(TEXT("Hoehe ~1280 cm (4 x 3.2 m)"), FMath::IsNearlyEqual(B.HeightCm, 1280.0, 1.0));
		TestTrue(TEXT("Grundflaeche > 0"), B.FootprintAreaSqm > 0.0);
		TestTrue(TEXT("Bounds gueltig"), B.Bounds.IsValid > 0);
		TestTrue(TEXT("Zentroid ungleich Null"), !B.Centroid.IsNearlyZero());
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerrainGeneratorTest,
	"WiesbadenReal.GIS.TerrainGenerator.Generate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTerrainGeneratorTest::RunTest(const FString& Parameters)
{
	UGeoCoordinateConverter* Converter = NewWiesbadenConverter();
	if (!TestTrue(TEXT("Konverter initialisiert"), Converter != nullptr && Converter->IsInitialized()))
	{
		return false;
	}

	// Konstantes 120-m-DEM mit Bezugshoehe 120 m -> Tile ueberall 0 cm.
	FHeightmapRaster Dem;
	Dem.Width = 3;
	Dem.Height = 3;
	Dem.MinLongitude = 8.20;
	Dem.MinLatitude = 50.05;
	Dem.CellSizeX = 0.04;
	Dem.CellSizeY = 0.03;
	Dem.NoDataValue = -9999.0f;
	Dem.VerticalReferenceMeters = 120.0;
	Dem.Samples = { 120.0f, 120.0f, 120.0f, 120.0f, 120.0f, 120.0f, 120.0f, 120.0f, 120.0f };

	UTerrainGenerator* Generator = NewObject<UTerrainGenerator>();
	FTerrainGenerationSettings Settings;
	Settings.GridSize = 16;

	FTerrainTile Tile;
	const FTerrainGenerationReport Report = Generator->Generate(Dem, *Converter, Settings, Tile);

	TestTrue(TEXT("Terrain erfolgreich"), Report.bSuccess);
	TestTrue(TEXT("Tile gueltig"), Tile.IsValid());
	TestEqual(TEXT("GridSize == 16"), Report.GridSize, 16);
	TestEqual(TEXT("Heights == 16*16"), Tile.HeightsCm.Num(), 16 * 16);
	TestTrue(TEXT("Weltbreite > 0"), Report.WorldWidthMeters > 0.0);
	TestTrue(TEXT("Min-Hoehe ~0"), FMath::IsNearlyEqual(Report.MinHeightCm, 0.0f, 1.0f));
	TestTrue(TEXT("Max-Hoehe ~0"), FMath::IsNearlyEqual(Report.MaxHeightCm, 0.0f, 1.0f));

	// Ein ungueltiges DEM wird abgelehnt. Der Generator loggt dabei bewusst
	// einen Fehler (erwartet).
	AddExpectedErrorPlain(TEXT("Ungueltiges DEM-Raster"), EAutomationExpectedErrorFlags::Contains, 1);
	FHeightmapRaster Invalid;
	Invalid.Width = 1;
	Invalid.Height = 1;
	FTerrainTile Unused;
	const FTerrainGenerationReport InvalidReport = Generator->Generate(Invalid, *Converter, Settings, Unused);
	TestTrue(TEXT("Ungueltiges DEM schlaegt fehl"), !InvalidReport.bSuccess);

	// Flatten auf leerem Netz/Datensatz aendert nichts.
	FRoadNetwork EmptyNetwork;
	const int32 RoadFlattened = Generator->FlattenUnderRoads(EmptyNetwork, Settings, Tile);
	TestEqual(TEXT("Flatten leeres Netz -> 0"), RoadFlattened, 0);

	FOSMDataSet EmptyDataSet;
	const int32 BuildingFlattened = Generator->FlattenUnderBuildings(EmptyDataSet, *Converter, Settings, Tile);
	TestEqual(TEXT("Flatten leerer Datensatz -> 0"), BuildingFlattened, 0);

	// Zuschnitt auf die OSM-Ausdehnung: mit CropBounds deckt das Tile nur den
	// (vergroesserten) Crop ab statt des kompletten DEM-Rasters - sonst ist die
	// Stadt ein winziger Fleck in einem 1-Grad-SRTM-Tile (~111 km) und die
	// Aufloesung verschwendet.
	{
		FGeoBounds Crop(8.22, 50.06, 8.26, 50.10);
		FTerrainTile Cropped;
		const FTerrainGenerationReport CropReport =
			Generator->Generate(Dem, *Converter, Settings, Cropped, &Crop);
		TestTrue(TEXT("Crop: Erfolg"), CropReport.bSuccess);
		TestTrue(TEXT("Crop: Tile gueltig"), Cropped.IsValid());
		// Crop (0.04x0.04 Grad + Rand) ist deutlich kleiner als das DEM
		// (0.08x0.06 Grad): die Weltbreite muss unter 90 % der vollen liegen.
		TestTrue(TEXT("Crop: Weltbreite < volle Weltbreite"),
			CropReport.WorldWidthMeters < Report.WorldWidthMeters * 0.9);

		// Crop ausserhalb des DEMs -> Fallback auf volle Ausdehnung (kein
		// Fehler, kein leeres Tile).
		FGeoBounds Outside(9.0, 51.0, 9.5, 51.5);
		FTerrainTile Fallback;
		const FTerrainGenerationReport FallbackReport =
			Generator->Generate(Dem, *Converter, Settings, Fallback, &Outside);
		TestTrue(TEXT("Crop ausserhalb: Erfolg (Fallback)"), FallbackReport.bSuccess);
		TestTrue(TEXT("Crop ausserhalb: volle Weltbreite"),
			FMath::IsNearlyEqual(FallbackReport.WorldWidthMeters, Report.WorldWidthMeters, 1.0));
	}

	// Crop-Rand konfigurierbar: groesserer CropMarginMeters -> breiteres Tile.
	{
		FGeoBounds Crop(8.22, 50.06, 8.26, 50.10);

		FTerrainGenerationSettings Narrow = Settings;
		Narrow.CropMarginMeters = 0.0;
		FTerrainGenerationSettings Wide = Settings;
		Wide.CropMarginMeters = 2000.0;

		FTerrainTile NarrowTile;
		FTerrainTile WideTile;
		const FTerrainGenerationReport NarrowReport =
			Generator->Generate(Dem, *Converter, Narrow, NarrowTile, &Crop);
		const FTerrainGenerationReport WideReport =
			Generator->Generate(Dem, *Converter, Wide, WideTile, &Crop);

		TestTrue(TEXT("Crop-Margin 0: Erfolg"), NarrowReport.bSuccess);
		TestTrue(TEXT("Crop-Margin 2000: Erfolg"), WideReport.bSuccess);
		TestTrue(TEXT("Crop-Margin: grosser Rand -> deutlich breiteres Tile"),
			WideReport.WorldWidthMeters > NarrowReport.WorldWidthMeters + 1000.0);
	}

	return true;
}

/**
 * Terrain-Qualitaetskontrolle (CheckTerrainQuality): Warnt im BuildCity-
 * Abschluss, wenn das Tile deutlich groesser als die OSM-Ausdehnung ist
 * (Verdacht auf fehlenden Crop) oder die Hoehenspanne unplausibel gross/klein
 * ist. Datenrein und headless testbar.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerrainQualityCheckTest,
	"WiesbadenReal.GIS.TerrainGenerator.QualityCheck",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTerrainQualityCheckTest::RunTest(const FString& Parameters)
{
	UGeoCoordinateConverter* Converter = NewWiesbadenConverter();
	if (!TestTrue(TEXT("Konverter initialisiert"), Converter != nullptr && Converter->IsInitialized()))
	{
		return false;
	}

	// Normaler Fall: Tile entspricht OSM-Ausdehnung + Rand -> keine Warnung.
	{
		FTerrainGenerationReport Report;
		Report.bSuccess = true;
		Report.WorldWidthMeters = 12400.0; // ~12 km Tile (OSM ~11.6 km + 200-m-Rand)
		Report.MinHeightCm = 100.0f;
		Report.MaxHeightCm = 50000.0f;     // 500 m Span (plausibel fuer Wiesbaden)

		const FGeoBounds Osm(8.20, 50.05, 8.30, 50.12);
		const FTerrainQualityReport Q =
			UTerrainGenerator::CheckTerrainQuality(Report, Osm, *Converter);
		TestTrue(TEXT("Normal: keine Tile-Warnung"), !Q.bWarnTileTooLarge);
		TestTrue(TEXT("Normal: keine Hoehen-Warnung"), !Q.bWarnHeightRangeSuspicious);
		TestTrue(TEXT("Normal: leere Meldung"), Q.WarningMessage.IsEmpty());
	}

	// Fehlender Crop: Tile deutlich groesser als die OSM-Ausdehnung
	// (1-Grad-SRTM-Tile ~111 km statt ~12 km) -> Tile-Warnung.
	{
		FTerrainGenerationReport Report;
		Report.bSuccess = true;
		Report.WorldWidthMeters = 111000.0;
		Report.MinHeightCm = 100.0f;
		Report.MaxHeightCm = 50000.0f;

		const FGeoBounds Osm(8.20, 50.05, 8.30, 50.12);
		const FTerrainQualityReport Q =
			UTerrainGenerator::CheckTerrainQuality(Report, Osm, *Converter);
		TestTrue(TEXT("Crop fehlt: Tile-Warnung"), Q.bWarnTileTooLarge);
		TestTrue(TEXT("Crop fehlt: Meldung nicht leer"), !Q.WarningMessage.IsEmpty());
	}

	// Konfigurierbare Schwelle: dasselbe 2x-Tile warnt beim Default-Faktor
	// 2.0, nicht aber bei einem per Prompt gesetzten Faktor 4.0.
	{
		FTerrainGenerationReport Report;
		Report.bSuccess = true;
		Report.WorldWidthMeters = 24000.0; // ~2x OSM-Ausdehnung (~11.6 km)
		Report.MinHeightCm = 100.0f;
		Report.MaxHeightCm = 50000.0f;

		const FGeoBounds Osm(8.20, 50.05, 8.30, 50.12);
		const FTerrainQualityReport Strict =
			UTerrainGenerator::CheckTerrainQuality(Report, Osm, *Converter);
		const FTerrainQualityReport Loose =
			UTerrainGenerator::CheckTerrainQuality(Report, Osm, *Converter, 4.0, 1.0, 3000.0);
		TestTrue(TEXT("Faktor 2.0: Tile-Warnung"), Strict.bWarnTileTooLarge);
		TestTrue(TEXT("Faktor 4.0: keine Tile-Warnung"), !Loose.bWarnTileTooLarge);
	}

	// Hoehenspanne unplausibel klein (flaches/fehlendes DEM).
	{
		FTerrainGenerationReport Report;
		Report.bSuccess = true;
		Report.WorldWidthMeters = 12400.0;
		Report.MinHeightCm = 100.0f;
		Report.MaxHeightCm = 150.0f; // 0.5 m Span

		const FGeoBounds Osm(8.20, 50.05, 8.30, 50.12);
		const FTerrainQualityReport Q =
			UTerrainGenerator::CheckTerrainQuality(Report, Osm, *Converter);
		TestTrue(TEXT("Span klein: Hoehen-Warnung"), Q.bWarnHeightRangeSuspicious);
	}

	// Hoehenspanne unplausibel gross (NoData-Spikes im Raster).
	{
		FTerrainGenerationReport Report;
		Report.bSuccess = true;
		Report.WorldWidthMeters = 12400.0;
		Report.MinHeightCm = 0.0f;
		Report.MaxHeightCm = 800000.0f; // 8000 m Span

		const FGeoBounds Osm(8.20, 50.05, 8.30, 50.12);
		const FTerrainQualityReport Q =
			UTerrainGenerator::CheckTerrainQuality(Report, Osm, *Converter);
		TestTrue(TEXT("Span gross: Hoehen-Warnung"), Q.bWarnHeightRangeSuspicious);
	}

	// Ungueltige OSM-Bounds: Tile-Vergleich wird uebersprungen (kein
	// False-Positive, wenn keine Ausdehnung bekannt ist).
	{
		FTerrainGenerationReport Report;
		Report.bSuccess = true;
		Report.WorldWidthMeters = 111000.0;
		Report.MinHeightCm = 100.0f;
		Report.MaxHeightCm = 50000.0f;

		const FTerrainQualityReport Q =
			UTerrainGenerator::CheckTerrainQuality(Report, FGeoBounds(), *Converter);
		TestTrue(TEXT("Keine Bounds: keine Tile-Warnung"), !Q.bWarnTileTooLarge);
		TestTrue(TEXT("Keine Bounds: keine Hoehen-Warnung"), !Q.bWarnHeightRangeSuspicious);
	}

	// -- AppendToStatus: Warnung in den Status-String einhaengen ------------
	// Ohne Warnung bleibt der Basis-Status unveraendert.
	{
		const FTerrainQualityReport Q; // leere Meldung
		const FString Base = TEXT("OSM geparst: 10 Wege; 3 Kreuzungen");
		TestEqual(TEXT("AppendToStatus: leer -> unveraendert"), Q.AppendToStatus(Base), Base);
	}

	// Mit Warnung wird "; Terrain-Warnung: <Meldung>" angehaengt.
	{
		FTerrainQualityReport Q;
		Q.bWarnTileTooLarge = true;
		Q.WarningMessage = TEXT("Terrain-Tile deutlich groesser als die OSM-Ausdehnung");
		const FString Base = TEXT("OSM geparst: 10 Wege; 3 Kreuzungen");
		TestEqual(TEXT("AppendToStatus: Warnung angehaengt"),
			Q.AppendToStatus(Base),
			Base + TEXT("; Terrain-Warnung: Terrain-Tile deutlich groesser als die OSM-Ausdehnung"));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerrainTileHeightmapTest,
	"WiesbadenReal.GIS.TerrainGenerator.ToUEHeightmap",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTerrainTileHeightmapTest::RunTest(const FString& Parameters)
{
	FTerrainTile Tile;
	Tile.GridSize = 2;
	Tile.WorldMinXY = FVector2D::ZeroVector;
	Tile.CellSizeCm = 100.0;
	Tile.HeightsCm = { 0.0f, 100.0f, 100.0f, 200.0f };

	TestTrue(TEXT("Tile gueltig"), Tile.IsValid());

	const TArray<uint16> Map = Tile.ToUEHeightmap(0.0f, 200.0f);
	TestEqual(TEXT("Heightmap-Groesse == 4"), Map.Num(), 4);
	if (Map.Num() == 4)
	{
		TestEqual(TEXT("Wert 0 cm -> 0"), static_cast<int32>(Map[0]), 0);
		TestEqual(TEXT("Wert 100 cm -> 32768"), static_cast<int32>(Map[1]), 32768);
		TestEqual(TEXT("Wert 100 cm -> 32768"), static_cast<int32>(Map[2]), 32768);
		TestEqual(TEXT("Wert 200 cm -> 65535"), static_cast<int32>(Map[3]), 65535);
	}

	// Ungueltiges Tile liefert eine leere Heightmap.
	FTerrainTile Invalid;
	TestTrue(TEXT("Ungueltiges Tile -> leere Map"), Invalid.ToUEHeightmap(0.0f, 100.0f).IsEmpty());

	return true;
}

/**
 * Landscape-Hoehencodierung (EncodeLandscapeHeightCm): UE interpretiert einen
 * uint16-Heightmap-Wert als (value - 32768) / 128 * ZScale in cm. Die
 * Codierung muss HeightCm 1:1 auf worldZ abbilden - ein invertierter Bruch
 * (hier frueher als 61 %-Stauchung sichtbar) wuerde das Relief verfaelschen.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerrainLandscapeEncodingTest,
	"WiesbadenReal.GIS.TerrainGenerator.LandscapeEncoding",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTerrainLandscapeEncodingTest::RunTest(const FString& Parameters)
{
	// ZScale 100 (Projekt-Default): 10000 cm -> value 32768 + 10000 * 128/100.
	const uint16 V100 = FTerrainTile::EncodeLandscapeHeightCm(10000.0, 100.0);
	TestEqual(TEXT("ZScale 100: Wert exakt"), static_cast<int32>(V100), 45568);
	TestTrue(TEXT("ZScale 100: Roundtrip worldZ == cm"),
		FMath::IsNearlyEqual((static_cast<double>(V100) - 32768.0) / 128.0 * 100.0, 10000.0, 1.0));

	// ZScale 1: 1 cm -> 128 Schritte.
	const uint16 V1 = FTerrainTile::EncodeLandscapeHeightCm(1.0, 1.0);
	TestEqual(TEXT("ZScale 1: 1 cm -> 128 Schritte"), static_cast<int32>(V1), 32768 + 128);
	TestTrue(TEXT("ZScale 1: Roundtrip"),
		FMath::IsNearlyEqual(static_cast<double>(V1) - 32768.0, 128.0, 1e-6));

	// Randwerte werden geklemmt (kein uint16-Ueberlauf).
	TestEqual(TEXT("Clamp unten"),
		static_cast<int32>(FTerrainTile::EncodeLandscapeHeightCm(-100000.0, 100.0)), 0);
	TestEqual(TEXT("Clamp oben"),
		static_cast<int32>(FTerrainTile::EncodeLandscapeHeightCm(100000.0, 100.0)), 65535);

	// Degenerierte ZScale -> neutraler Wert (0 cm), kein Crash.
	TestEqual(TEXT("ZScale 0 -> neutral"),
		static_cast<int32>(FTerrainTile::EncodeLandscapeHeightCm(100.0, 0.0)), 32768);

	return true;
}

/**
 * Darstellbarer Hoehenbereich einer Landscape-Heightmap.
 *
 * Anlass: 1.612 von 22.227 Kreuzungen schwebten ueber 10 m in der Luft, und
 * das Gelaende meldete an jeder einzelnen denselben Wert 25.599 cm. Das war
 * kein Zufall, sondern die Obergrenze der Codierung bei ZScale 100 - der
 * Taunuskamm wurde zu einer waagerechten Platte.
 *
 * Der Test haelt den Zusammenhang fest, damit die Klemmung nie wieder
 * unbemerkt zuschlaegt: Sie ist als Schutz vor Ueberlauf richtig, aber die
 * ZScale muss so gewaehlt sein, dass sie fuer echte Hoehen nie greift.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerrainHeightRangeTest,
	"WiesbadenReal.GIS.TerrainGenerator.Hoehenbereich",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTerrainHeightRangeTest::RunTest(const FString& Parameters)
{
	// Darstellbar ist |HeightCm| <= ZScale * 32767 / 128.
	auto MaxRepresentableCm = [](double ZScale) { return ZScale * 32767.0 / 128.0; };

	// -- 1) Die Grenze liegt genau dort, wo die Messung sie gefunden hat ----
	const double LimitAt100 = MaxRepresentableCm(100.0);
	TestTrue(FString::Printf(TEXT("ZScale 100 reicht bis %.1f cm"), LimitAt100),
		FMath::IsNearlyEqual(LimitAt100, 25599.2, 0.5));

	const uint16 AtLimit = FTerrainTile::EncodeLandscapeHeightCm(LimitAt100, 100.0);
	TestEqual(TEXT("Genau an der Grenze noch kein Anschlag"),
		static_cast<int32>(AtLimit), 65535);

	// -- 2) Der Taunus passt bei ZScale 100 NICHT hinein --------------------
	//
	// Die Hohe Wurzel misst 618 m, die Bezugshoehe des Projekts liegt bei
	// 117 m - zu codieren sind also 501 m.
	constexpr double HoheWurzelCm = (618.0 - 117.0) * 100.0;
	TestTrue(TEXT("Hohe Wurzel liegt ueber der Grenze von ZScale 100"),
		HoheWurzelCm > LimitAt100);

	const uint16 Clipped = FTerrainTile::EncodeLandscapeHeightCm(HoheWurzelCm, 100.0);
	TestEqual(TEXT("ZScale 100 klemmt die Hohe Wurzel ab"),
		static_cast<int32>(Clipped), 65535);

	// Und die geklemmte Hoehe ist der Wert, der im Bericht stand.
	const double ClippedWorldZ = (static_cast<double>(Clipped) - 32768.0) / 128.0 * 100.0;
	TestTrue(FString::Printf(
		TEXT("Geklemmt ergibt %.0f cm - der gemessene Wert 25.599"), ClippedWorldZ),
		FMath::IsNearlyEqual(ClippedWorldZ, 25599.2, 1.0));

	// -- 3) Mit der neuen Vorgabe passt sie ---------------------------------
	constexpr double ProjectZScale = 300.0;
	TestTrue(FString::Printf(TEXT("ZScale %.0f reicht bis %.0f m"),
		ProjectZScale, MaxRepresentableCm(ProjectZScale) / 100.0),
		MaxRepresentableCm(ProjectZScale) > HoheWurzelCm);

	const uint16 Fits = FTerrainTile::EncodeLandscapeHeightCm(HoheWurzelCm, ProjectZScale);
	TestTrue(TEXT("Kein Anschlag mehr"), Fits < 65535);

	const double RoundTrip = (static_cast<double>(Fits) - 32768.0) / 128.0 * ProjectZScale;
	TestTrue(FString::Printf(
		TEXT("Hoehe kommt unveraendert zurueck (%.0f statt %.0f cm)"),
		RoundTrip, HoheWurzelCm),
		FMath::IsNearlyEqual(RoundTrip, HoheWurzelCm, ProjectZScale / 128.0 + 0.01));

	// -- 4) Der Preis der groesseren Reichweite ist tragbar ------------------
	//
	// Die Stufenhoehe ist ZScale/128. Bei 300 sind das 2,3 cm - das
	// Quellmaterial SRTM liefert ganze Meter, der Verlust ist also keiner.
	const double StepCm = ProjectZScale / 128.0;
	TestTrue(FString::Printf(TEXT("Stufenhoehe %.2f cm bleibt weit unter 1 m"), StepCm),
		StepCm < 10.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadTypeLibraryConfigTest,
	"WiesbadenReal.GIS.RoadTypeLibrary.Config",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FRoadTypeLibraryConfigTest::RunTest(const FString& Parameters)
{
	// Der Standardpfad liegt wie beim Verkehrszeichen-Katalog unter Content/Config.
	const FString ConfigPath = URoadTypeLibrary::GetDefaultConfigPath();
	TestTrue(TEXT("Standardpfad zeigt auf Content/Config"),
		ConfigPath.Contains(TEXT("/Content/Config/WiesbadenRoadTypes.json")));

	URoadTypeLibrary* Library = NewObject<URoadTypeLibrary>();

	// Die eingecheckte JSON wird geladen und ueberschreibt die Code-Defaults
	// mit denselben RASt-06/RAA-Werten.
	TestTrue(TEXT("RoadTypes-JSON aus Content laedt"), Library->LoadFromJsonFile(ConfigPath));
	const FRoadTypeDefinition& Primary = Library->GetDefinition(EOSMHighwayType::Primary);
	TestEqual(TEXT("Primary LaneWidth 3.5"), Primary.LaneWidthMeters, 3.5);
	TestEqual(TEXT("Primary 2 Spuren/Richtung"), Primary.DefaultLanesPerDirection, 2);

	// Oberflaechen-Override: Fussgaengerzonen sind gepflastert.
	TestTrue(TEXT("Pedestrian Pflaster"),
		Library->GetDefinition(EOSMHighwayType::Pedestrian).DefaultSurface == EOSMSurfaceType::PavingStones);

	// Fehlende Datei: Defaults bleiben nutzbar, Load meldet false.
	URoadTypeLibrary* FallbackLibrary = NewObject<URoadTypeLibrary>();
	TestFalse(TEXT("Fehlende Datei -> false"),
		FallbackLibrary->LoadFromJsonFile(TEXT("/nonexistent/WiesbadenRoadTypes.json")));
	TestEqual(TEXT("Residential-Default bleibt gesetzt"),
		FallbackLibrary->GetDefinition(EOSMHighwayType::Residential).DefaultMaxSpeedKmh, 30.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBuildingFacadeOverrideTest,
	"WiesbadenReal.GIS.Buildings.FacadeOverride",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBuildingFacadeOverrideTest::RunTest(const FString& Parameters)
{
	TArray<FString> OverrideAddresses;
	OverrideAddresses.Add(TEXT("Mainzer Strasse 129"));

	// Treffer (case-insensitive) liefert die kanonische Schreibweise als Key,
	// damit die Render-Seite exakt in AddressFacadeMaterials nachschlagen kann.
	TestEqual(TEXT("Override Key Mainzer Strasse 129"),
		UBuildingGenerator::ResolveFacadeOverrideKey(TEXT("mainzer strasse 129"), OverrideAddresses),
		TEXT("Mainzer Strasse 129"));

	// Nicht konfigurierte Adresse bekommt keinen eigenen Abschnitt.
	TestTrue(TEXT("Ohne Treffer -> leer"),
		UBuildingGenerator::ResolveFacadeOverrideKey(TEXT("Wilhelmstrasse 1"), OverrideAddresses).IsEmpty());

	// Leere Adresse wird nie gematcht.
	TestTrue(TEXT("Leere Adresse -> leer"),
		UBuildingGenerator::ResolveFacadeOverrideKey(TEXT(""), OverrideAddresses).IsEmpty());

	// Leere Liste aendert nichts.
	TArray<FString> Empty;
	TestTrue(TEXT("Leere Liste -> leer"),
		UBuildingGenerator::ResolveFacadeOverrideKey(TEXT("Mainzer Strasse 129"), Empty).IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBuildingAddressNormalizationTest,
	"WiesbadenReal.GIS.Buildings.AddressNormalization",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBuildingAddressNormalizationTest::RunTest(const FString& Parameters)
{
	// OSM liefert "Mainzer Stra\u00DFe 129" (mit sz), die Konfiguration ist
	// ASCII ("Mainzer Strasse 129"). Normalisierung gleicht beide Seiten an:
	// lowercase, sz->ss, ae/oe/ue-Transliteration der Umlaute.
	TArray<FString> OverrideAddresses;
	OverrideAddresses.Add(TEXT("Mainzer Strasse 129"));

	TestEqual(TEXT("OSM sz trifft ASCII-Konfig"),
		UBuildingGenerator::ResolveFacadeOverrideKey(TEXT("Mainzer Stra\u00DFe 129"), OverrideAddresses),
		TEXT("Mainzer Strasse 129"));

	// Umgekehrt: Konfiguration mit sz, OSM in ASCII-Schreibweise. Der
	// kanonische Key (Konfig-Schreibweise) muss unveraendert zurueckkommen.
	TArray<FString> SzConfig;
	SzConfig.Add(TEXT("Mainzer Stra\u00DFe 129"));
	TestEqual(TEXT("ASCII-OSM trifft sz-Konfig"),
		UBuildingGenerator::ResolveFacadeOverrideKey(TEXT("Mainzer Strasse 129"), SzConfig),
		TEXT("Mainzer Stra\u00DFe 129"));

	// Umlaut-Transliteration: OSM "M\u00FCllerstra\u00DFe" trifft "Muellerstrasse".
	TArray<FString> UmlautOverrides;
	UmlautOverrides.Add(TEXT("Muellerstrasse 5"));
	TestEqual(TEXT("Umlaut+sz-OSM trifft ae/ss-Konfig"),
		UBuildingGenerator::ResolveFacadeOverrideKey(TEXT("M\u00FCllerstra\u00DFe 5"), UmlautOverrides),
		TEXT("Muellerstrasse 5"));

	// Grossbuchstaben-Umlaute: ToLower passiert vor der Transliteration.
	TArray<FString> UpperOverrides;
	UpperOverrides.Add(TEXT("Gruenberger Strasse 7"));
	TestEqual(TEXT("Grossbuchstaben-Umlaute normalisiert"),
		UBuildingGenerator::ResolveFacadeOverrideKey(TEXT("GR\u00DCNBERGER Stra\u00DFe 7"), UpperOverrides),
		TEXT("Gruenberger Strasse 7"));

	// Normalisierung erzeugt keine Fehltreffer auf anderen Strassen.
	TestTrue(TEXT("Andere Strasse bleibt ohne Treffer"),
		UBuildingGenerator::ResolveFacadeOverrideKey(TEXT("Wilhelmstra\u00DFe 1"), OverrideAddresses).IsEmpty());

	// Normalizer direkt: lowercase + sz/ae/oe/ue-Transliteration.
	TestEqual(TEXT("NormalizeAddressForMatch 'M\u00FCller Stra\u00DFe'"),
		UBuildingGenerator::NormalizeAddressForMatch(TEXT("M\u00FCller Stra\u00DFe")),
		TEXT("mueller strasse"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBuildingPromptOverridesTest,
	"WiesbadenReal.GIS.Buildings.PromptOverrides",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBuildingPromptOverridesTest::RunTest(const FString& Parameters)
{
	// -- Stil: deterministische gewichtete Auswahl (Seed = OSM-Id) ----------
	TMap<int32, float> EmptyWeights;
	TestEqual(TEXT("Leere Gewichte -> -1"),
		UBuildingGenerator::SelectPromptVariant(EmptyWeights, TEXT("123")), -1);
	TestTrue(TEXT("Leere Gewichte -> leerer Stil-Key"),
		UBuildingGenerator::ResolvePromptStyleKey(EmptyWeights, TEXT("123")).IsEmpty());

	TMap<int32, float> Weights;
	Weights.Add(1, 2.0f);
	Weights.Add(5, 1.0f);

	const int32 First = UBuildingGenerator::SelectPromptVariant(Weights, TEXT("42"));
	TestTrue(TEXT("Nur erlaubte Varianten (1,5)"), First == 1 || First == 5);
	TestEqual(TEXT("Deterministisch je Seed"),
		UBuildingGenerator::SelectPromptVariant(Weights, TEXT("42")), First);
	const FString StyleKey = UBuildingGenerator::ResolvePromptStyleKey(Weights, TEXT("42"));
	TestTrue(TEXT("Stil-Key-Praefix PromptStyle:"), StyleKey.StartsWith(TEXT("PromptStyle:")));
	TestTrue(TEXT("Stil-Key-Endung aus GetFacadeStyleNames"),
		CityPromptParser::GetFacadeStyleNames().Contains(StyleKey.RightChop(12)));

	// Gewichte verteilen ueber viele Gebaeude ~ im Verhaeltnis (Backstein 2 :
	// Fachwerk 1). Seeds "0".."299" (OSM-Ids als String) - wie der node-Port.
	int32 CountBackstein = 0;
	int32 CountFachwerk = 0;
	for (int32 i = 0; i < 300; ++i)
	{
		const int32 Variant = UBuildingGenerator::SelectPromptVariant(Weights, FString::FromInt(i));
		if (Variant == 1) { ++CountBackstein; }
		else if (Variant == 5) { ++CountFachwerk; }
	}
	TestTrue(TEXT("Gewichte verteilen ~2:1"), CountBackstein > CountFachwerk);
	TestTrue(TEXT("Verteilung plausibel (Backstein 150..250)"),
		CountBackstein >= 150 && CountBackstein <= 250);

	// -- Landmarken: OSM-Name gegen prompt-erkannte Landmarken ---------------
	TArray<FString> Landmarks;
	Landmarks.Add(TEXT("Kurhaus"));
	Landmarks.Add(TEXT("Marktkirche"));

	TestEqual(TEXT("OSM-Name mit Zusatz trifft Landmarke"),
		UBuildingGenerator::ResolvePromptLandmarkKey(TEXT("Kurhaus Wiesbaden"), Landmarks),
		TEXT("PromptLandmark:Kurhaus"));
	TestEqual(TEXT("Exakter Treffer"),
		UBuildingGenerator::ResolvePromptLandmarkKey(TEXT("Marktkirche"), Landmarks),
		TEXT("PromptLandmark:Marktkirche"));
	TestTrue(TEXT("Wortgrenze schuetzt vor Teilwort"),
		UBuildingGenerator::ResolvePromptLandmarkKey(TEXT("Kurhausstrasse 5"), Landmarks).IsEmpty());
	TestTrue(TEXT("Kein Match -> leer"),
		UBuildingGenerator::ResolvePromptLandmarkKey(TEXT("Rathaus"), Landmarks).IsEmpty());
	TestTrue(TEXT("Leerer Name -> leer"),
		UBuildingGenerator::ResolvePromptLandmarkKey(TEXT(""), Landmarks).IsEmpty());
	TestTrue(TEXT("Leere Liste -> leer"),
		UBuildingGenerator::ResolvePromptLandmarkKey(TEXT("Kurhaus"), TArray<FString>()).IsEmpty());

	// Umlaut-Normalisierung (ue -> ue): OSM "Gr\u00FCneburg" trifft "Grueneburg".
	TArray<FString> UmlautLandmarks;
	UmlautLandmarks.Add(TEXT("Grueneburg"));
	TestEqual(TEXT("Umlaut-Normalisierung"),
		UBuildingGenerator::ResolvePromptLandmarkKey(TEXT("Gr\u00FCneburg"), UmlautLandmarks),
		TEXT("PromptLandmark:Grueneburg"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBuildingGeneratorCancelTest,
	"WiesbadenReal.GIS.Buildings.Cancel",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBuildingGeneratorCancelTest::RunTest(const FString& Parameters)
{
	UGeoCoordinateConverter* Converter = NewWiesbadenConverter();
	if (!TestTrue(TEXT("Konverter initialisiert"), Converter != nullptr && Converter->IsInitialized()))
	{
		return false;
	}

	// Viele kleine Gebaeude, damit der Abbruch mitten im Lauf beobachtbar ist.
	FOSMDataSet DataSet;
	const int32 BuildingCount = 200;
	for (int32 i = 0; i < BuildingCount; ++i)
	{
		const double BaseLon = 8.24 + i * 0.0002;
		const FOSMId N0 = i * 4 + 1;
		DataSet.Nodes.Add(N0, FOSMNode(N0, BaseLon, 50.08));
		DataSet.Nodes.Add(N0 + 1, FOSMNode(N0 + 1, BaseLon + 0.0001, 50.08));
		DataSet.Nodes.Add(N0 + 2, FOSMNode(N0 + 2, BaseLon + 0.0001, 50.0801));
		DataSet.Nodes.Add(N0 + 3, FOSMNode(N0 + 3, BaseLon, 50.0801));

		FOSMWay Building;
		Building.Id = i + 1;
		Building.NodeIds = { N0, N0 + 1, N0 + 2, N0 + 3, N0 };
		Building.Tags.Add(TEXT("building"), TEXT("yes"));
		DataSet.Ways.Add(Building.Id, Building);
	}

	UBuildingGenerator* Generator = NewObject<UBuildingGenerator>();
	FBuildingGenerationSettings Settings; // Defaults

	// Referenz: voller Lauf ohne Abbruch -> alle 200 Gebaeude.
	TArray<FGeneratedBuilding> FullBuildings;
	FBuildingMeshData FullMesh;
	const FBuildingGenerationReport FullReport = Generator->Generate(
		DataSet, Converter, /*HeightSampler=*/nullptr, Settings, FullBuildings, &FullMesh);
	TestTrue(TEXT("Voll-Lauf erfolgreich"), FullReport.bSuccess);
	TestEqual(TEXT("Alle Gebaeude erzeugt"), FullBuildings.Num(), BuildingCount);

	// Sofortiger Abbruch: der Callback liefert schon beim ersten Poll true.
	TArray<FGeneratedBuilding> CancelledBuildings;
	FBuildingMeshData CancelledMesh;
	int32 CancelPolls = 0;
	const FBuildingGenerationReport CancelledReport = Generator->Generate(
		DataSet, Converter, /*HeightSampler=*/nullptr, Settings, CancelledBuildings, &CancelledMesh,
		[&CancelPolls]()
		{
			++CancelPolls;
			return true;
		});

	TestTrue(TEXT("Abbruch erkannt"), CancelledReport.bCancelled);
	TestTrue(TEXT("Abbruch nicht als Erfolg gemeldet"), !CancelledReport.bSuccess);
	TestTrue(TEXT("Callback wurde gepollt"), CancelPolls > 0);
	TestTrue(TEXT("Vorzeitig gestoppt (deutlich weniger Gebaeude)"),
		CancelledBuildings.Num() < BuildingCount);

	// Abbruch nach 100 Polls: ungefaehr die Haelfte ist gebaut.
	TArray<FGeneratedBuilding> PartialBuildings;
	FBuildingMeshData PartialMesh;
	int32 Polls = 0;
	const FBuildingGenerationReport PartialReport = Generator->Generate(
		DataSet, Converter, /*HeightSampler=*/nullptr, Settings, PartialBuildings, &PartialMesh,
		[&Polls]()
		{
			return ++Polls >= 100;
		});

	TestTrue(TEXT("Teil-Abbruch erkannt"), PartialReport.bCancelled);
	TestTrue(TEXT("Teil-Abbruch gestoppt"), PartialBuildings.Num() < BuildingCount);
	TestTrue(TEXT("Teil-Abbruch hat schon gebaut"), PartialBuildings.Num() > 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerrainBridgeDoesNotFlattenTest,
	"WiesbadenReal.GIS.TerrainGenerator.BridgeDoesNotFlatten",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Eine Bruecke darf das Gelaende nicht anheben.
 *
 * FlattenUnderRoads lief ueber ALLE Segmente ohne Ebenen-Filter. Eine Bruecke
 * 13 m ueber Grund hat das Gelaende damit auf Brueckenhoehe gezogen - und die
 * ebenerdige Strasse, die darunter durchfuehrt, im Erdreich begraben. Im Spiel
 * war das als fehlendes Strassensegment zu sehen, obwohl die Geometrie da war.
 *
 * Der Test baut genau diese Situation: eine ebenerdige Strasse und quer
 * darueber eine Bruecke in 13 m Hoehe. Danach muss die ebenerdige Fahrbahn
 * ueber Grund liegen.
 */
bool FTerrainBridgeDoesNotFlattenTest::RunTest(const FString& Parameters)
{
	FTerrainTile Tile;
	Tile.GridSize = 64;
	Tile.CellSizeCm = 500.0;
	Tile.WorldMinXY = FVector2D(0.0, 0.0);
	Tile.HeightsCm.Init(0.0f, Tile.GridSize * Tile.GridSize);

	constexpr double RoadSurfaceOffsetCm = 20.0;
	constexpr double BridgeHeightCm = 1300.0;
	const double CrossY = 20.0 * Tile.CellSizeCm;

	// Ebenerdige Strasse in X-Richtung auf flachem Gelaende (Hoehe 0).
	FRoadSegment Ground;
	Ground.SegmentId = 1;
	Ground.HighwayType = EOSMHighwayType::Residential;
	Ground.CarriagewayWidthCm = 650.0;
	for (int32 Step = 10; Step < 30; ++Step)
	{
		Ground.Centerline.Add(FVector(Step * Tile.CellSizeCm, CrossY, RoadSurfaceOffsetCm));
	}

	// Bruecke quer darueber, 13 m hoch.
	FRoadSegment Bridge;
	Bridge.SegmentId = 2;
	Bridge.HighwayType = EOSMHighwayType::Secondary;
	Bridge.CarriagewayWidthCm = 900.0;
	Bridge.bIsBridge = true;
	Bridge.Layer = 1;
	for (int32 Step = 10; Step < 30; ++Step)
	{
		Bridge.Centerline.Add(FVector(20.0 * Tile.CellSizeCm, Step * Tile.CellSizeCm, BridgeHeightCm));
	}

	FRoadNetwork Network;
	Network.Segments.Add(Ground);
	Network.Segments.Add(Bridge);

	UTerrainGenerator* Generator = NewObject<UTerrainGenerator>();
	FTerrainGenerationSettings Settings;
	Settings.GridSize = Tile.GridSize;

	Generator->FlattenUnderRoads(Network, Settings, Tile);

	// Kernaussage: Kein Punkt der ebenerdigen Fahrbahn liegt unter dem Gelaende.
	int32 BuriedPoints = 0;
	double WorstBurialCm = 0.0;
	for (const FVector& Point : Ground.Centerline)
	{
		const float TerrainZ = Tile.SampleHeightBilinearCm(FVector2D(Point.X, Point.Y));
		const double Burial = TerrainZ - Point.Z;
		if (Burial > 0.0)
		{
			++BuriedPoints;
			WorstBurialCm = FMath::Max(WorstBurialCm, Burial);
		}
	}

	TestEqual(
		FString::Printf(TEXT("Bruecke begraebt die Strasse darunter nicht (schlimmster Fall %.0f cm)"),
			WorstBurialCm),
		BuriedPoints, 0);

	// Gegenprobe: Das Gelaende unter der Bruecke bleibt auf Ausgangshoehe.
	const float UnderBridgeZ = Tile.SampleHeightBilinearCm(
		FVector2D(20.0 * Tile.CellSizeCm, 25.0 * Tile.CellSizeCm));
	TestTrue(
		FString::Printf(TEXT("Gelaende unter der Bruecke bleibt unten (%.0f cm statt %.0f cm)"),
			UnderBridgeZ, BridgeHeightCm),
		UnderBridgeZ < BridgeHeightCm * 0.5);

	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerrainRoadFlattenClearanceTest,
	"WiesbadenReal.GIS.TerrainGenerator.RoadFlattenClearance",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Nach der Einebnung MUSS das Gelaende unter der Fahrbahn-Mittellinie liegen.
 *
 * Hintergrund: Der RoadNetworkGenerator legt die Mittellinie bereits um
 * RoadSurfaceOffsetCm ueber das Terrain (Point.Z = SampleHeightCm + Offset).
 * FlattenUnderRoads hat diesen Wert frueher unveraendert als Gelaendehoehe
 * geschrieben - Gelaende und Fahrbahndecke wurden damit koplanar.
 *
 * Der Abstand wird BEIDSEITIG geprueft, und das ist der eigentliche Punkt
 * dieses Tests. Er verlangte lange nur eine Untergrenze. Damit blieb er gruen,
 * waehrend Fahrbahnversatz und Gelaende-Aushub schrittweise auf 30 und 45 cm
 * wuchsen und die Fahrbahn im Spiel gemessen 91,6 cm ueber dem Boden schwebte -
 * an jeder Kante sichtbar aufgerissen. Eine Kennzahl ohne Obergrenze deckt
 * genau die Haelfte der moeglichen Fehler ab.
 *
 * Der Test faehrt bewusst eine STEIGENDE Strasse, weil dort sowohl das
 * Versinken als auch das Abheben am staerksten zuschlaegt.
 */
bool FTerrainRoadFlattenClearanceTest::RunTest(const FString& Parameters)
{
	FTerrainTile Tile;
	Tile.GridSize = 64;
	Tile.CellSizeCm = 500.0;
	Tile.WorldMinXY = FVector2D(0.0, 0.0);
	Tile.HeightsCm.Init(0.0f, Tile.GridSize * Tile.GridSize);

	// Gelaende als Rampe: 10 % Steigung in X-Richtung.
	for (int32 Y = 0; Y < Tile.GridSize; ++Y)
	{
		for (int32 X = 0; X < Tile.GridSize; ++X)
		{
			Tile.HeightsCm[Y * Tile.GridSize + X] = static_cast<float>(X * Tile.CellSizeCm * 0.10);
		}
	}

	TestTrue(TEXT("Tile gueltig"), Tile.IsValid());

	// Strasse laengs der Rampe. Die Mittellinie sitzt - wie im Generator - um
	// den Fahrbahnversatz ueber dem Gelaende.
	constexpr double RoadSurfaceOffsetCm = 20.0;   // wie FRoadNetworkSettings

	FRoadSegment Segment;
	Segment.SegmentId = 1;
	Segment.HighwayType = EOSMHighwayType::Residential;
	Segment.CarriagewayWidthCm = 650.0;

	for (int32 Step = 4; Step < 40; ++Step)
	{
		const double WorldX = Step * Tile.CellSizeCm;
		const double TerrainZ = WorldX * 0.10;
		Segment.Centerline.Add(FVector(WorldX, 20.0 * Tile.CellSizeCm, TerrainZ + RoadSurfaceOffsetCm));
	}

	FRoadNetwork Network;
	Network.Segments.Add(Segment);

	UTerrainGenerator* Generator = NewObject<UTerrainGenerator>();
	FTerrainGenerationSettings Settings;
	Settings.GridSize = Tile.GridSize;

	const int32 Modified = Generator->FlattenUnderRoads(Network, Settings, Tile);
	TestTrue(TEXT("Einebnung hat Zellen veraendert"), Modified > 0);

	// Kernaussage: An JEDEM Mittellinienpunkt muss das Gelaende unter der
	// Fahrbahndecke bleiben - aber eben auch nicht beliebig weit darunter.
	//
	// Untergrenze: bei zu wenig Freiraum interpoliert das Landscape mit 7,81 m
	// je Quad zwischen den Gitterpunkten ueber die Fahrbahn hinweg.
	// Obergrenze: ein Bordstein ist 12 cm hoch. Steht die Fahrbahn deutlich
	// weiter ueber dem Gelaende, klafft an ihrer Kante eine Luecke, durch die
	// man unter die Strasse sieht.
	constexpr double MinClearanceCm = 8.0;
	constexpr double MaxClearanceCm = 40.0;

	int32 BuriedPoints = 0;
	int32 TooClosePoints = 0;
	int32 FloatingPoints = 0;
	double WorstOverlapCm = 0.0;
	double WorstClearanceCm = 0.0;

	for (const FVector& Point : Segment.Centerline)
	{
		const int32 X = FMath::RoundToInt((Point.X - Tile.WorldMinXY.X) / Tile.CellSizeCm);
		const int32 Y = FMath::RoundToInt((Point.Y - Tile.WorldMinXY.Y) / Tile.CellSizeCm);
		if (X < 0 || Y < 0 || X >= Tile.GridSize || Y >= Tile.GridSize)
		{
			continue;
		}

		const double TerrainZ = Tile.HeightsCm[Y * Tile.GridSize + X];
		const double ClearanceCm = Point.Z - TerrainZ;

		if (ClearanceCm <= 0.0)
		{
			++BuriedPoints;
			WorstOverlapCm = FMath::Max(WorstOverlapCm, -ClearanceCm);
		}
		else if (ClearanceCm < MinClearanceCm)
		{
			++TooClosePoints;
		}
		else if (ClearanceCm > MaxClearanceCm)
		{
			++FloatingPoints;
		}

		WorstClearanceCm = FMath::Max(WorstClearanceCm, ClearanceCm);
	}

	TestEqual(TEXT("Kein Mittellinienpunkt liegt unter dem Gelaende"), BuriedPoints, 0);
	TestTrue(TEXT("Keine Ueberlappung"), WorstOverlapCm <= 0.0);
	TestEqual(TEXT("Ueberall mindestens 8 cm Abstand zum Gelaende"), TooClosePoints, 0);
	TestEqual(TEXT("Nirgends mehr als 40 cm ueber dem Gelaende"), FloatingPoints, 0);
	TestTrue(
		FString::Printf(TEXT("Groesste Bodenfreiheit %.1f cm bleibt unter %.0f cm"),
			WorstClearanceCm, MaxClearanceCm),
		WorstClearanceCm <= MaxClearanceCm);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRibbonWindingTest,
	"WiesbadenReal.GIS.PolygonUtils.RibbonWinding",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Jedes Dreieck eines Fahrbahn-Bandes MUSS mit der Vorderseite nach oben zeigen.
 *
 * Hintergrund: Die Wicklung stand auf (0,2,1)/(1,2,3) und damit falsch herum.
 * Fahrbahn, Gehweg und Markierungen (alle drei nutzen BuildRibbonMesh) wurden
 * von oben durch Backface-Culling entfernt - im Spiel lag zwischen den
 * Haeuserblocks nur Wiese. Der Fehler war extrem zaeh zu finden, weil JEDE
 * datenseitige Pruefung sauber war: Abschnitte, Vertices, Materialien,
 * Sichtbarkeitsflags, Bounds, Kantenlaengen, sogar die Vertex-Normalen - die
 * stehen naemlich fest auf FVector::UpVector und sehen deshalb korrekt aus.
 * Fuer das Culling zaehlt aber ausschliesslich die Reihenfolge der Indizes.
 *
 * Sichtbar wurde es erst, als das Fahrbahnmaterial beidseitig geschaltet wurde
 * und die Strasse schlagartig erschien.
 */
bool FRibbonWindingTest::RunTest(const FString& Parameters)
{
	// Gerade Achse in +X-Richtung, damit die Erwartung eindeutig ist.
	TArray<FVector2D> Centerline;
	for (int32 Index = 0; Index < 5; ++Index)
	{
		Centerline.Add(FVector2D(Index * 1000.0, 0.0));
	}

	TArray<FVector2D> Vertices;
	TArray<int32> Indices;
	TArray<FVector2D> UVs;

	TestTrue(TEXT("Band erzeugt"),
		FPolygonUtils::BuildRibbonMesh(Centerline, 650.0, Vertices, Indices, UVs));
	TestTrue(TEXT("Mindestens ein Dreieck"), Indices.Num() >= 3);

	int32 FacingDown = 0;
	for (int32 Tri = 0; Tri + 2 < Indices.Num(); Tri += 3)
	{
		// Das Band ist flach; die Hoehe kommt spaeter aus der Achse. Fuer die
		// Wicklung genuegt die Ebene Z = 0.
		const FVector A(Vertices[Indices[Tri + 0]], 0.0);
		const FVector B(Vertices[Indices[Tri + 1]], 0.0);
		const FVector C(Vertices[Indices[Tri + 2]], 0.0);

		// Unreal arbeitet linkshaendig: die Vorderseite ergibt sich aus
		// CrossProduct(C - A, B - A), NICHT aus (B - A, C - A). Genau diese
		// Verwechslung liess eine fruehere Pruefung die falsche Wicklung als
		// unauffaellig durchgehen.
		if (FVector::CrossProduct(C - A, B - A).Z <= 0.0)
		{
			++FacingDown;
		}
	}

	TestEqual(TEXT("Kein Dreieck zeigt nach unten"), FacingDown, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMinimumAreaBoxTest,
	"WiesbadenReal.GIS.PolygonUtils.MinimumAreaBox",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Flaechenminimale gedrehte Box eines Grundrisses.
 *
 * Hintergrund: Fuer die Gebaeude-Kollision wurde zunaechst die achsparallele
 * Bounding-Box benutzt. Gemessen an 107 Wiesbadener Gebaeuden deckte die im
 * Mittel das 2,12-fache des Grundrisses ab (schlimmster Fall 3,81-fach) und
 * haette als unsichtbare Wand bis auf die Fahrbahn gereicht. Die gedrehte Box
 * ist fuer rechteckige Grundrisse exakt - und Gebaeude sind ueberwiegend
 * rechteckig.
 *
 * Der Test faehrt daher ein um 30 Grad GEDREHTES Rechteck: genau dort versagt
 * die achsparallele Variante.
 */
bool FMinimumAreaBoxTest::RunTest(const FString& Parameters)
{
	// Rechteck 20 x 8 m, um 30 Grad gedreht, Mittelpunkt (500, 300).
	const double HalfLength = 1000.0;
	const double HalfWidth = 400.0;
	const double AngleRad = FMath::DegreesToRadians(30.0);
	const FVector2D Center(500.0, 300.0);

	const FVector2D AxisX(FMath::Cos(AngleRad), FMath::Sin(AngleRad));
	const FVector2D AxisY(-AxisX.Y, AxisX.X);

	TArray<FVector2D> Ring;
	Ring.Add(Center - AxisX * HalfLength - AxisY * HalfWidth);
	Ring.Add(Center + AxisX * HalfLength - AxisY * HalfWidth);
	Ring.Add(Center + AxisX * HalfLength + AxisY * HalfWidth);
	Ring.Add(Center - AxisX * HalfLength + AxisY * HalfWidth);

	FVector2D BoxCenter = FVector2D::ZeroVector;
	FVector2D BoxExtent = FVector2D::ZeroVector;
	double BoxYaw = 0.0;

	TestTrue(TEXT("Box berechnet"),
		FPolygonUtils::ComputeMinimumAreaBox2D(Ring, BoxCenter, BoxExtent, BoxYaw));

	TestTrue(TEXT("Mittelpunkt getroffen"), FVector2D::Distance(BoxCenter, Center) < 1.0);

	// Die Halbmasse muss dem Rechteck entsprechen - in irgendeiner Achsenlage.
	const bool bExtentMatches =
		(FMath::IsNearlyEqual(BoxExtent.X, HalfLength, 1.0) && FMath::IsNearlyEqual(BoxExtent.Y, HalfWidth, 1.0))
		|| (FMath::IsNearlyEqual(BoxExtent.X, HalfWidth, 1.0) && FMath::IsNearlyEqual(BoxExtent.Y, HalfLength, 1.0));
	TestTrue(TEXT("Halbmasse entspricht dem Rechteck"), bExtentMatches);

	// Kernaussage: Die Flaeche der gedrehten Box ist die des Grundrisses. Die
	// achsparallele Box waere hier deutlich groesser.
	const double BoxAreaSqm = (BoxExtent.X * 2.0 / 100.0) * (BoxExtent.Y * 2.0 / 100.0);
	const double RingAreaSqm = (HalfLength * 2.0 / 100.0) * (HalfWidth * 2.0 / 100.0);
	TestTrue(TEXT("Flaeche entspricht dem Grundriss"),
		FMath::IsNearlyEqual(BoxAreaSqm, RingAreaSqm, 0.5));

	const FBox2D AxisAligned = FPolygonUtils::ComputeBounds2D(Ring);
	const FVector2D AxisSize = AxisAligned.GetSize();
	const double AxisAreaSqm = (AxisSize.X / 100.0) * (AxisSize.Y / 100.0);
	TestTrue(TEXT("Achsparallele Box waere deutlich groesser"), AxisAreaSqm > BoxAreaSqm * 1.4);

	// Entartete Eingaben duerfen keine Box liefern - ein Koerper ohne
	// Ausdehnung waere eine unsichtbare Wand an falscher Stelle.
	TArray<FVector2D> TooFew;
	TooFew.Add(FVector2D::ZeroVector);
	TooFew.Add(FVector2D(100.0, 0.0));
	TestFalse(TEXT("Zwei Punkte ergeben keine Box"),
		FPolygonUtils::ComputeMinimumAreaBox2D(TooFew, BoxCenter, BoxExtent, BoxYaw));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIntersectionTrafficSignalTest,
	"WiesbadenReal.GIS.RoadNetwork.TrafficSignalControl",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Eine Kreuzung auf einem highway=traffic_signals-Knoten MUSS als Ampel gelten.
 *
 * Hintergrund: Der Stadt-Build meldet "20213 Kreuzungen (0 Ampeln)", obwohl die
 * OSM-Quelle 2310 Ampelknoten enthaelt und davon 1299 von mindestens zwei
 * Strassen geteilt werden (mit Tools/check_traffic_signals.mjs an den Daten
 * nachgezaehlt). Ohne Ampeln laeuft die gesamte Ampel-Steuerung ins Leere -
 * der Verkehr faehrt ueber jede Kreuzung durch.
 *
 * Dieser Test trennt die Datenfrage von der Codefrage: Er baut den Fall
 * kuenstlich nach. Schlaegt er fehl, liegt es am Generator; besteht er, liegt
 * es am Datenfluss davor.
 */
bool FIntersectionTrafficSignalTest::RunTest(const FString& Parameters)
{
	UGeoCoordinateConverter* Converter = NewWiesbadenConverter();
	if (!TestTrue(TEXT("Konverter initialisiert"), Converter != nullptr && Converter->IsInitialized()))
	{
		return false;
	}

	URoadTypeLibrary* TypeLibrary = NewObject<URoadTypeLibrary>();
	TypeLibrary->ApplyBuiltInDefaults();

	// Kreuzungsknoten 1 traegt die Ampel; vier Arme treffen dort zusammen.
	FOSMDataSet DataSet;
	FOSMNode Signal(1, 8.2400, 50.0824);
	Signal.Tags.Add(FName(TEXT("highway")), TEXT("traffic_signals"));
	DataSet.Nodes.Add(1, Signal);

	DataSet.Nodes.Add(2, FOSMNode(2, 8.2410, 50.0824));
	DataSet.Nodes.Add(3, FOSMNode(3, 8.2400, 50.0830));
	DataSet.Nodes.Add(4, FOSMNode(4, 8.2390, 50.0824));
	DataSet.Nodes.Add(5, FOSMNode(5, 8.2400, 50.0818));

	DataSet.Ways.Add(100, MakeRoad(100, { 2, 1 }, TEXT("Oststrasse")));
	DataSet.Ways.Add(101, MakeRoad(101, { 3, 1 }, TEXT("Nordstrasse")));
	DataSet.Ways.Add(102, MakeRoad(102, { 4, 1 }, TEXT("Weststrasse")));
	DataSet.Ways.Add(103, MakeRoad(103, { 5, 1 }, TEXT("Suedstrasse")));

	URoadNetworkGenerator* Generator = NewObject<URoadNetworkGenerator>();
	FRoadNetwork Network;
	FRoadGenerationSettings Settings;

	const FRoadGenerationReport Report = Generator->Generate(
		DataSet, Converter, TypeLibrary, /*HeightSampler=*/nullptr, Settings, Network, /*MeshData=*/nullptr);

	TestTrue(TEXT("Netz erzeugt"), Report.bSuccess);
	TestEqual(TEXT("Eine Kreuzung"), Network.Intersections.Num(), 1);

	if (Network.Intersections.Num() == 1)
	{
		TestEqual(TEXT("Kreuzung ist eine Ampel"),
			static_cast<int32>(Network.Intersections[0].Control),
			static_cast<int32>(EIntersectionControl::TrafficSignals));
	}

	// Und die Ampel-Steuerung muss daraus tatsaechlich eine Ampel bauen.
	FWiesbadenTrafficLightSystem Lights;
	FWiesbadenTrafficLightSettings LightSettings;
	Lights.Initialize(Network, LightSettings);

	TestEqual(TEXT("Ampel-System kennt eine Ampel"), Lights.GetTrafficLightCount(), 1);
	TestTrue(TEXT("Ampel sitzt am richtigen Knoten"), Lights.HasTrafficLightAt(1));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRealOsmTrafficSignalTest,
	"WiesbadenReal.GIS.RoadNetwork.RealOsmTrafficSignals",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Zaehlt Ampelknoten in der ECHTEN OSM-Datei.
 *
 * Hintergrund: Der Stadt-Build meldet "20213 Kreuzungen (0 Ampeln)". Der
 * synthetische Test `TrafficSignalControl` besteht, der Generator ist also in
 * Ordnung - der Fehler muss im Datenfluss liegen. Dieser Test trennt die
 * beiden Stufen: Kommen die Tags ueberhaupt aus dem Parser heraus?
 *
 * Ueberspringt sich selbst, wenn die Datei fehlt (sie liegt nicht im Repo).
 */
bool FRealOsmTrafficSignalTest::RunTest(const FString& Parameters)
{
	const FString Path = FPaths::ProjectDir() / TEXT("Data/Raw/OSM/wiesbaden.osm.json");
	if (!FPaths::FileExists(Path))
	{
		AddInfo(TEXT("OSM-Datei nicht vorhanden - Test uebersprungen."));
		return true;
	}

	UOSMDataParser* Parser = NewObject<UOSMDataParser>();
	FOSMDataSet DataSet;
	const FOSMParseResult Result = Parser->ParseFile(Path, DataSet);

	TestTrue(TEXT("OSM geparst"), Result.bSuccess);

	int32 SignalNodes = 0;
	for (const TPair<FOSMId, FOSMNode>& Pair : DataSet.Nodes)
	{
		if (Pair.Value.IsTrafficSignal())
		{
			++SignalNodes;
		}
	}

	// Wie oft wird ein Knoten von befahrbaren Ways benutzt? Nur Knoten mit
	// mindestens zwei Ways werden im Generator zu Segment-Endpunkten.
	TMap<FOSMId, int32> Usage;
	for (const TPair<FOSMId, FOSMWay>& Pair : DataSet.Ways)
	{
		if (!Pair.Value.HasTag(TEXT("highway")))
		{
			continue;
		}
		for (const FOSMId NodeId : Pair.Value.NodeIds)
		{
			Usage.FindOrAdd(NodeId) += 1;
		}
	}

	int32 SignalsOnSharedNodes = 0;
	for (const TPair<FOSMId, FOSMNode>& Pair : DataSet.Nodes)
	{
		if (Pair.Value.IsTrafficSignal() && Usage.FindRef(Pair.Key) >= 2)
		{
			++SignalsOnSharedNodes;
		}
	}

	AddInfo(FString::Printf(
		TEXT("Ampelknoten im Datensatz: %d, davon auf Knoten mit >= 2 Strassen: %d"),
		SignalNodes, SignalsOnSharedNodes));

	// Kernaussage: Die Tags MUESSEN aus dem Parser herauskommen. Sind sie hier
	// schon weg, liegt es am Parser - nicht am Kreuzungs-Code.
	TestTrue(TEXT("Der Parser liefert Ampelknoten"), SignalNodes > 0);
	TestTrue(TEXT("Ampeln liegen auf geteilten Knoten"), SignalsOnSharedNodes > 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBuildingHeightVariationTest,
	"WiesbadenReal.GIS.Buildings.DefaultHeightVariation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Streuung der Standard-Gebaeudehoehe.
 *
 * Hintergrund: 99.284 der 117.425 Wiesbadener ALKIS-Grundrisse sind
 * building=residential, nur 12.181 tragen eine Geschosszahl und ganze 210 eine
 * explizite Hoehe. Alle uebrigen bekamen denselben Typ-Default - die Stadt sah
 * aus wie eine gleichfoermige Mauer.
 *
 * Die Streuung nutzt bewusst nur vorhandene Groessen: Lage zum Zentrum (der
 * Weltursprung IST das Wiesbadener Zentrum), Grundrissflaeche und eine aus der
 * SourceId abgeleitete Streuung. Nichts davon ist erfunden, und der Build
 * bleibt reproduzierbar.
 */
bool FBuildingHeightVariationTest::RunTest(const FString& Parameters)
{
	constexpr double MetersPerLevel = 3.0;
	constexpr double BaseHeight = 3.0 * MetersPerLevel;   // Typ-Default "residential"

	// -- Reproduzierbarkeit -------------------------------------------------
	const double A = UBuildingGenerator::VaryDefaultHeightMeters(BaseHeight, 150.0, 100000.0, 4711, MetersPerLevel);
	const double B = UBuildingGenerator::VaryDefaultHeightMeters(BaseHeight, 150.0, 100000.0, 4711, MetersPerLevel);
	TestEqual(TEXT("Gleiche Eingaben ergeben gleiche Hoehe"), A, B);

	// -- Lage zum Zentrum ---------------------------------------------------
	// Innenstadt (Gruenderzeit, 4-5 Geschosse) gegen Vorort (2-3).
	const double Centre = UBuildingGenerator::VaryDefaultHeightMeters(BaseHeight, 150.0, 0.0, 99, MetersPerLevel);
	const double Suburb = UBuildingGenerator::VaryDefaultHeightMeters(BaseHeight, 150.0, 600000.0, 99, MetersPerLevel);
	TestTrue(TEXT("Innenstadt hoeher als Vorort"), Centre > Suburb);

	// -- Grundrissgroesse ---------------------------------------------------
	// Reihenhaus gegen Mehrfamilienblock, sonst identisch.
	const double Small = UBuildingGenerator::VaryDefaultHeightMeters(BaseHeight, 60.0, 200000.0, 7, MetersPerLevel);
	const double Large = UBuildingGenerator::VaryDefaultHeightMeters(BaseHeight, 700.0, 200000.0, 7, MetersPerLevel);
	TestTrue(TEXT("Grosser Grundriss hoeher als kleiner"), Large > Small);

	// -- Es MUSS streuen ----------------------------------------------------
	// Der eigentliche Zweck: eine Strassenzeile darf nicht eine Hoehe haben.
	TSet<int32> DistinctLevels;
	for (int64 Id = 1; Id <= 40; ++Id)
	{
		const double H = UBuildingGenerator::VaryDefaultHeightMeters(BaseHeight, 150.0, 150000.0, Id, MetersPerLevel);
		DistinctLevels.Add(FMath::RoundToInt32(H / MetersPerLevel * 4.0));   // Viertelgeschoss-Raster
	}
	TestTrue(TEXT("40 Gebaeude ergeben mehrere verschiedene Hoehen"), DistinctLevels.Num() >= 5);

	// -- Grenzen ------------------------------------------------------------
	// Kein Turm im Wohngebiet, kein Gebaeude ohne Geschoss.
	for (int64 Id = 1; Id <= 200; ++Id)
	{
		const double H = UBuildingGenerator::VaryDefaultHeightMeters(BaseHeight, 900.0, 0.0, Id, MetersPerLevel);
		TestTrue(TEXT("Hoechstens 8 Geschosse"), H <= 8.0 * MetersPerLevel + KINDA_SMALL_NUMBER);
		TestTrue(TEXT("Mindestens 1 Geschoss"), H >= MetersPerLevel - KINDA_SMALL_NUMBER);
	}

	// Ungueltige Eingaben unveraendert durchreichen.
	TestEqual(TEXT("Hoehe 0 bleibt 0"),
		UBuildingGenerator::VaryDefaultHeightMeters(0.0, 150.0, 0.0, 1, MetersPerLevel), 0.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPathWidthTest,
	"WiesbadenReal.GIS.RoadTypes.PathWidth",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Fuss-, Rad- und Wirtschaftswege duerfen keine zwei Fahrtrichtungen bekommen.
 *
 * Hintergrund: Die Spurzuweisung vergab pauschal 1+1 Spuren, auch fuer Wege.
 * Mit der Typbreite als SPURbreite kam ein Fussweg damit auf 3,60 m statt
 * 1,80 m - er sah im Spiel aus wie eine Fahrbahn. Am Startpunkt gemessen:
 * "Typ Footway, Fahrbahn 3.60 m, 1+1 Spuren". Betroffen waren 14.318
 * Fusswege, 10.089 Pfade und 9.267 Wirtschaftswege der OSM-Daten.
 *
 * Eine Wohnstrasse braucht die zwei Richtungen dagegen weiterhin.
 */
bool FPathWidthTest::RunTest(const FString& Parameters)
{
	URoadTypeLibrary* Library = NewObject<URoadTypeLibrary>();
	Library->ApplyBuiltInDefaults();

	FOSMWay Way;
	Way.Id = 1;

	auto WidthOf = [&](EOSMHighwayType Type)
	{
		int32 Forward = 0;
		int32 Backward = 0;
		Library->ResolveLaneCounts(Way, Type, EOSMOnewayType::No, Forward, Backward);
		return Library->ResolveCarriagewayWidthMeters(Way, Type, Forward, Backward);
	};

	auto LanesOf = [&](EOSMHighwayType Type)
	{
		int32 Forward = 0;
		int32 Backward = 0;
		Library->ResolveLaneCounts(Way, Type, EOSMOnewayType::No, Forward, Backward);
		return Forward + Backward;
	};

	// Wege: genau eine Spur, Breite = Typbreite.
	TestEqual(TEXT("Fussweg hat eine Spur"), LanesOf(EOSMHighwayType::Footway), 1);
	TestEqual(TEXT("Radweg hat eine Spur"), LanesOf(EOSMHighwayType::Cycleway), 1);
	TestEqual(TEXT("Pfad hat eine Spur"), LanesOf(EOSMHighwayType::Path), 1);
	TestEqual(TEXT("Wirtschaftsweg hat eine Spur"), LanesOf(EOSMHighwayType::Track), 1);

	// Ein Fussweg muss schmaler sein als ein Auto breit ist (Kaefer 1,55 m) -
	// sonst wirkt er wie eine Fahrbahn.
	const double FootwayWidth = WidthOf(EOSMHighwayType::Footway);
	TestTrue(TEXT("Fussweg schmaler als 2,5 m"), FootwayWidth < 2.5);
	TestTrue(TEXT("Fussweg breiter als 1 m"), FootwayWidth > 1.0);

	// Gegenprobe: Eine Wohnstrasse braucht beide Richtungen und muss deutlich
	// breiter sein als ein Fussweg.
	TestEqual(TEXT("Wohnstrasse hat zwei Spuren"), LanesOf(EOSMHighwayType::Residential), 2);
	TestTrue(TEXT("Wohnstrasse breiter als Fussweg"),
		WidthOf(EOSMHighwayType::Residential) > FootwayWidth * 2.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBendFillerTest,
	"WiesbadenReal.GIS.RoadNetworkGenerator.BendFiller",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Wo zwei Strassen mit einem Knick aneinanderstossen, darf keine Fuge bleiben.
 *
 * An einem Knoten mit nur ZWEI Armen entsteht keine Kreuzungsflaeche und es
 * wird nicht gekuerzt - die Baender stossen direkt aneinander. Ihre
 * Endquerschnitte stehen aber senkrecht zur jeweils EIGENEN Fahrtrichtung.
 * Knickt die Strasse dort ab, klafft aussen am Bogen ein Keil.
 *
 * In der gebauten Stadt gemessen: 1.508 solcher Knicke ueber 15 Grad, Keil im
 * Median 136,9 cm, maximal 33,7 m. Das ist der haeufigste sichtbare Riss im
 * Netz.
 */
bool FBendFillerTest::RunTest(const FString& Parameters)
{
	UGeoCoordinateConverter* Converter = NewWiesbadenConverter();
	if (!TestTrue(TEXT("Konverter initialisiert"), Converter != nullptr && Converter->IsInitialized()))
	{
		return false;
	}

	URoadTypeLibrary* TypeLibrary = NewObject<URoadTypeLibrary>();
	TypeLibrary->ApplyBuiltInDefaults();

	// Zwei Wege, die an Knoten 2 im rechten Winkel aneinanderstossen. Genau ein
	// solcher Knoten hat zwei Arme und bekommt keine Kreuzungsflaeche.
	FOSMDataSet DataSet;
	DataSet.Nodes.Add(1, FOSMNode(1, 8.2380, 50.0824));
	DataSet.Nodes.Add(2, FOSMNode(2, 8.2400, 50.0824));   // Knickpunkt
	DataSet.Nodes.Add(3, FOSMNode(3, 8.2400, 50.0840));

	DataSet.Ways.Add(100, MakeRoad(100, { 1, 2 }, TEXT("Weststrasse")));
	DataSet.Ways.Add(101, MakeRoad(101, { 2, 3 }, TEXT("Nordstrasse")));

	URoadNetworkGenerator* Generator = NewObject<URoadNetworkGenerator>();
	FRoadNetwork Network;
	FRoadMeshData MeshData;
	FRoadGenerationSettings Settings;

	const FRoadGenerationReport Report = Generator->Generate(
		DataSet, Converter, TypeLibrary, nullptr, Settings, Network, &MeshData);

	TestTrue(TEXT("Strassennetz erfolgreich"), Report.bSuccess);
	TestEqual(TEXT("Zwei Segmente"), Network.Segments.Num(), 2);

	// Gegenprobe zur Aussagekraft: Der Knoten darf KEINE Kreuzung sein, sonst
	// prueft der Test den Fehlerfall gar nicht.
	TestEqual(TEXT("Keine Kreuzung am Knickpunkt"), Network.Intersections.Num(), 0);

	// Eine Fuellflaeche muss entstanden sein. Sie landet im Kreuzungs-Kanal.
	int32 FillerTriangles = 0;
	for (const FRoadMeshSection& Section : MeshData.Sections)
	{
		if (Section.Channel == ERoadMeshChannel::Intersection)
		{
			FillerTriangles += Section.Triangles.Num() / 3;
		}
	}

	TestTrue(
		FString::Printf(TEXT("Fuellflaeche am Knick erzeugt (%d Dreiecke)"), FillerTriangles),
		FillerTriangles > 0);

	// Sie muss den Knickpunkt selbst ueberdecken - dort liegt der Keil.
	const FVector Corner = Converter->GeoToUnrealGround(DataSet.Nodes[2].Location);

	bool bCoversCorner = false;
	for (const FRoadMeshSection& Section : MeshData.Sections)
	{
		if (Section.Channel != ERoadMeshChannel::Intersection)
		{
			continue;
		}
		for (const FVector& Vertex : Section.Vertices)
		{
			if (FVector::Dist2D(Vertex, Corner) < 600.0)
			{
				bCoversCorner = true;
				break;
			}
		}
	}

	TestTrue(TEXT("Fuellflaeche liegt am Knickpunkt"), bCoversCorner);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadEmbankmentTest,
	"WiesbadenReal.GIS.RoadNetworkGenerator.Embankment",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Steht die Fahrbahn ueber dem Gelaende, muss eine Boeschung die Kante schliessen.
 *
 * Das Gelaende hat 7,81 m Rasterweite, die Fahrbahn ist rund 7 m breit. Am Hang
 * traegt die Einebnung nur ein bis zwei Rasterpunkte; daneben faellt der Boden
 * weg, und die Fahrbahn steht als Damm mit freier Kante darueber. Gemessen
 * liegen 90 Prozent der Fahrbahn unter 37 cm ueber Grund, das oberste Prozent
 * aber bis 28 m - im Spiel als abgeschnittene, schwebende Strasse zu sehen.
 *
 * Geprueft wird beides: dass am Hang eine Boeschung entsteht UND dass auf
 * ebenem Grund KEINE entsteht. Ohne die zweite Probe wuerde der Test auch ein
 * blindes Anbauen an jede Strasse durchwinken.
 */
bool FRoadEmbankmentTest::RunTest(const FString& Parameters)
{
	UGeoCoordinateConverter* Converter = NewWiesbadenConverter();
	if (!TestTrue(TEXT("Konverter initialisiert"), Converter != nullptr && Converter->IsInitialized()))
	{
		return false;
	}

	URoadTypeLibrary* TypeLibrary = NewObject<URoadTypeLibrary>();
	TypeLibrary->ApplyBuiltInDefaults();

	FOSMDataSet DataSet;
	DataSet.Nodes.Add(1, FOSMNode(1, 8.2400, 50.0824));
	DataSet.Nodes.Add(2, FOSMNode(2, 8.2430, 50.0824));
	DataSet.Ways.Add(100, MakeRoad(100, { 1, 2 }, TEXT("Hangstrasse")));

	auto CountGroundTriangles = [](const FRoadMeshData& MeshData)
	{
		int32 Count = 0;
		for (const FRoadMeshSection& Section : MeshData.Sections)
		{
			if (Section.Surface == EOSMSurfaceType::Ground)
			{
				Count += Section.Triangles.Num();
			}
		}
		return Count;
	};

	// -- Fall 1: Querhang. Das Gelaende faellt seitlich weg. ----------------
	{
		// Neigung quer zur Strasse: Die Strasse laeuft in X, das Gelaende
		// faellt in Y. Ihre Mittellinie liegt damit ueber dem Gelaende an
		// beiden Raendern.
		const FSlopedHeightSampler CrossSlope(11000.0, 0.0, 0.35);

		FRoadNetwork Network;
		FRoadMeshData MeshData;
		FRoadGenerationSettings Settings;

		URoadNetworkGenerator* Generator = NewObject<URoadNetworkGenerator>();
		const FRoadGenerationReport Report = Generator->Generate(
			DataSet, Converter, TypeLibrary, &CrossSlope, Settings, Network, &MeshData);

		TestTrue(TEXT("Strassennetz erfolgreich (Hang)"), Report.bSuccess);
		TestTrue(
			FString::Printf(TEXT("Am Hang entsteht eine Boeschung (%d Dreiecke)"),
				CountGroundTriangles(MeshData)),
			CountGroundTriangles(MeshData) > 0);
	}

	// -- Fall 1b: Gelaende UEBER der Fahrbahn. --------------------------
	//
	// Der wichtigste Fall: Liegt der Boden hoeher als die Strasse - gemessen
	// bei 10 Prozent der Vertices, bis 2,3 m - darf die Boeschung NICHT nach
	// oben kippen. Sonst steht sie als Erdwand ueber der Fahrbahn und
	// verdeckt sie. An Kreuzungen, wo Arme verschiedener Hoehe zusammenlaufen,
	// tritt das besonders haeufig auf.
	{
		// Gelaende steil ansteigend in X, die Strasse laeuft ebenfalls in X -
		// die Mittellinie folgt zwar, an den Raendern weicht es aber ab.
		const FSlopedHeightSampler Rising(11000.0, 0.45, 0.45);

		FRoadNetwork Network;
		FRoadMeshData MeshData;
		FRoadGenerationSettings Settings;

		URoadNetworkGenerator* Generator = NewObject<URoadNetworkGenerator>();
		const FRoadGenerationReport Report = Generator->Generate(
			DataSet, Converter, TypeLibrary, &Rising, Settings, Network, &MeshData);

		TestTrue(TEXT("Strassennetz erfolgreich (Gelaende hoeher)"), Report.bSuccess);

		// Kein Boeschungs-Vertex darf ueber der Fahrbahnhoehe seines
		// Segments liegen. Als Bezug genuegt die hoechste Stelle der
		// Mittellinie - die Boeschung gehoert immer darunter.
		double HighestRoadZ = -TNumericLimits<double>::Max();
		for (const FRoadSegment& Segment : Network.Segments)
		{
			for (const FVector& Point : Segment.Centerline)
			{
				HighestRoadZ = FMath::Max(HighestRoadZ, Point.Z);
			}
		}

		int32 AboveRoad = 0;
		double WorstAboveCm = 0.0;
		for (const FRoadMeshSection& Section : MeshData.Sections)
		{
			if (Section.Surface != EOSMSurfaceType::Ground)
			{
				continue;
			}
			for (const FVector& Vertex : Section.Vertices)
			{
				if (Vertex.Z > HighestRoadZ + 1.0)
				{
					++AboveRoad;
					WorstAboveCm = FMath::Max(WorstAboveCm, Vertex.Z - HighestRoadZ);
				}
			}
		}

		TestEqual(
			FString::Printf(TEXT("Keine Boeschung ueber der Fahrbahn (%d Vertices, bis %.0f cm)"),
				AboveRoad, WorstAboveCm),
			AboveRoad, 0);
	}

	// -- Fall 2: ebener Grund. Keine Boeschung noetig. ---------------------
	{
		const FFlatHeightSampler Flat(11000.0);

		FRoadNetwork Network;
		FRoadMeshData MeshData;
		FRoadGenerationSettings Settings;

		URoadNetworkGenerator* Generator = NewObject<URoadNetworkGenerator>();
		const FRoadGenerationReport Report = Generator->Generate(
			DataSet, Converter, TypeLibrary, &Flat, Settings, Network, &MeshData);

		TestTrue(TEXT("Strassennetz erfolgreich (eben)"), Report.bSuccess);
		TestEqual(
			FString::Printf(TEXT("Auf ebenem Grund keine Boeschung (%d Dreiecke)"),
				CountGroundTriangles(MeshData)),
			CountGroundTriangles(MeshData), 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLooseEndSnapTest,
	"WiesbadenReal.GIS.RoadNetworkGenerator.LooseEndSnap",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Zwei Strassen, die sich beruehren, aber keinen gemeinsamen Knoten teilen,
 * muessen verbunden werden.
 *
 * In OSM entsteht eine Verbindung ueber einen GEMEINSAMEN Knoten. Fehlt der,
 * enden beide Wege im Nichts - im Spiel klafft dort Wiese, und es sieht aus,
 * als fehle die Kreuzung. Gemessen liegen 1.175 von 4.000 freien Enden unter
 * 15 m an einem fremden Ende, im Median 7,8 m.
 *
 * Geprueft wird beides: dass aufeinander zulaufende Enden verbunden werden UND
 * dass zwei PARALLELE Sackgassen es nicht werden - die sind keine Kreuzung,
 * auch wenn sie dicht beieinanderliegen.
 */
bool FLooseEndSnapTest::RunTest(const FString& Parameters)
{
	UGeoCoordinateConverter* Converter = NewWiesbadenConverter();
	if (!TestTrue(TEXT("Konverter initialisiert"), Converter != nullptr && Converter->IsInitialized()))
	{
		return false;
	}

	URoadTypeLibrary* TypeLibrary = NewObject<URoadTypeLibrary>();
	TypeLibrary->ApplyBuiltInDefaults();

	// Auf 50,08 Grad Nord sind 0,00007 Grad Laenge rund 5 m.
	constexpr double GapDeg = 0.00007;

	// -- Fall 1: zwei Strassen laufen aufeinander zu, 5 m Luecke ----------
	{
		FOSMDataSet DataSet;
		DataSet.Nodes.Add(1, FOSMNode(1, 8.2380, 50.0824));
		DataSet.Nodes.Add(2, FOSMNode(2, 8.2400, 50.0824));               // Ende West
		DataSet.Nodes.Add(3, FOSMNode(3, 8.2400 + GapDeg, 50.0824));      // Anfang Ost
		DataSet.Nodes.Add(4, FOSMNode(4, 8.2420, 50.0824));

		DataSet.Ways.Add(100, MakeRoad(100, { 1, 2 }, TEXT("Weststrasse")));
		DataSet.Ways.Add(101, MakeRoad(101, { 3, 4 }, TEXT("Oststrasse")));

		URoadNetworkGenerator* Generator = NewObject<URoadNetworkGenerator>();
		FRoadNetwork Network;
		FRoadGenerationSettings Settings;

		const FRoadGenerationReport Report = Generator->Generate(
			DataSet, Converter, TypeLibrary, nullptr, Settings, Network, nullptr);

		TestTrue(TEXT("Strassennetz erfolgreich"), Report.bSuccess);
		if (!TestEqual(TEXT("Zwei Segmente"), Network.Segments.Num(), 2))
		{
			return false;
		}

		// Die beiden Enden muessen jetzt denselben Knoten tragen...
		TestEqual(TEXT("Enden teilen einen Knoten"),
			Network.Segments[0].EndNodeId, Network.Segments[1].StartNodeId);

		// ...und geometrisch zusammenfallen.
		const FVector WestEnd = Network.Segments[0].Centerline.Last();
		const FVector EastStart = Network.Segments[1].Centerline[0];
		TestTrue(
			FString::Printf(TEXT("Enden fallen zusammen (%.1f cm Abstand)"),
				FVector::Dist2D(WestEnd, EastStart)),
			FVector::Dist2D(WestEnd, EastStart) < 1.0);
	}

	// -- Fall 2: zwei PARALLELE Sackgassen, ebenfalls 5 m auseinander ----
	//
	// Gegenprobe: Ohne sie wuerde der Test auch ein blindes Zusammenziehen
	// aller nahen Enden durchwinken - und dann verbaende die Stadt lauter
	// Strassen, die nichts miteinander zu tun haben.
	{
		FOSMDataSet DataSet;
		DataSet.Nodes.Add(1, FOSMNode(1, 8.2380, 50.0824));
		DataSet.Nodes.Add(2, FOSMNode(2, 8.2400, 50.0824));               // Ende der ersten
		DataSet.Nodes.Add(3, FOSMNode(3, 8.2380, 50.0824 + GapDeg));      // parallel daneben
		DataSet.Nodes.Add(4, FOSMNode(4, 8.2400, 50.0824 + GapDeg));      // Ende der zweiten

		DataSet.Ways.Add(100, MakeRoad(100, { 1, 2 }, TEXT("Sackgasse A")));
		DataSet.Ways.Add(101, MakeRoad(101, { 3, 4 }, TEXT("Sackgasse B")));

		URoadNetworkGenerator* Generator = NewObject<URoadNetworkGenerator>();
		FRoadNetwork Network;
		FRoadGenerationSettings Settings;

		const FRoadGenerationReport Report = Generator->Generate(
			DataSet, Converter, TypeLibrary, nullptr, Settings, Network, nullptr);

		TestTrue(TEXT("Strassennetz erfolgreich (parallel)"), Report.bSuccess);
		if (!TestEqual(TEXT("Zwei Segmente (parallel)"), Network.Segments.Num(), 2))
		{
			return false;
		}

		// Beide Enden zeigen in DIESELBE Richtung, laufen also nicht
		// aufeinander zu - sie duerfen nicht verbunden werden.
		TestTrue(TEXT("Parallele Sackgassen bleiben getrennt"),
			Network.Segments[0].EndNodeId != Network.Segments[1].EndNodeId);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJunctionSidewalkRingTest,
	"WiesbadenReal.GIS.RoadNetworkGenerator.JunctionSidewalkRing",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Der Gehweg einer Kreuzung darf die Fahrbahn NICHT ueberdecken.
 *
 * Er entstand zuerst als gefuellte Huelle, mit der Begruendung, die
 * Fahrbahnplatte werde davon nicht verdeckt, "weil sie kleiner ist". Das ist
 * genau verkehrt: Die Gehwegflaeche ist GROESSER und liegt eine Bordsteinhoehe
 * HOEHER - sie deckt die Fahrbahn damit vollstaendig zu. An allen Kreuzungen
 * mit Gehweg lag Beton ueber dem Asphalt.
 *
 * Geprueft wird beides: dass die Mitte frei bleibt UND dass ueberhaupt ein
 * Gehweg entsteht - ein Ring, der gar nichts erzeugt, waere genauso falsch.
 */
bool FJunctionSidewalkRingTest::RunTest(const FString& Parameters)
{
	UGeoCoordinateConverter* Converter = NewWiesbadenConverter();
	if (!TestTrue(TEXT("Konverter initialisiert"), Converter != nullptr && Converter->IsInitialized()))
	{
		return false;
	}

	URoadTypeLibrary* TypeLibrary = NewObject<URoadTypeLibrary>();
	TypeLibrary->ApplyBuiltInDefaults();

	FOSMDataSet DataSet;
	DataSet.Nodes.Add(1, FOSMNode(1, 8.2400, 50.0824));
	DataSet.Nodes.Add(2, FOSMNode(2, 8.2420, 50.0824));
	DataSet.Nodes.Add(3, FOSMNode(3, 8.2400, 50.0834));
	DataSet.Nodes.Add(4, FOSMNode(4, 8.2380, 50.0824));
	DataSet.Nodes.Add(5, FOSMNode(5, 8.2400, 50.0814));

	DataSet.Ways.Add(100, MakeRoad(100, { 2, 1 }, TEXT("Oststrasse")));
	DataSet.Ways.Add(101, MakeRoad(101, { 3, 1 }, TEXT("Nordstrasse")));
	DataSet.Ways.Add(102, MakeRoad(102, { 4, 1 }, TEXT("Weststrasse")));
	DataSet.Ways.Add(103, MakeRoad(103, { 5, 1 }, TEXT("Suedstrasse")));

	URoadNetworkGenerator* Generator = NewObject<URoadNetworkGenerator>();
	FRoadNetwork Network;
	FRoadMeshData MeshData;
	FRoadGenerationSettings Settings;

	const FRoadGenerationReport Report = Generator->Generate(
		DataSet, Converter, TypeLibrary, nullptr, Settings, Network, &MeshData);

	TestTrue(TEXT("Strassennetz erfolgreich"), Report.bSuccess);
	if (!TestEqual(TEXT("Eine Kreuzung"), Network.Intersections.Num(), 1))
	{
		return false;
	}

	const FVector2D Centre(
		Network.Intersections[0].Location.X,
		Network.Intersections[0].Location.Y);

	// Liegt der Punkt im Dreieck? (Vorzeichen der drei Kreuzprodukte)
	auto InsideTriangle = [](const FVector2D& P,
		const FVector& A, const FVector& B, const FVector& C)
	{
		auto Cross = [](const FVector2D& U, const FVector2D& V)
		{
			return U.X * V.Y - U.Y * V.X;
		};

		const FVector2D A2(A.X, A.Y), B2(B.X, B.Y), C2(C.X, C.Y);
		const double D1 = Cross(B2 - A2, P - A2);
		const double D2 = Cross(C2 - B2, P - B2);
		const double D3 = Cross(A2 - C2, P - C2);

		const bool bAnyNegative = (D1 < 0.0) || (D2 < 0.0) || (D3 < 0.0);
		const bool bAnyPositive = (D1 > 0.0) || (D2 > 0.0) || (D3 > 0.0);
		return !(bAnyNegative && bAnyPositive);
	};

	int32 SidewalkTriangles = 0;
	int32 CoveringCentre = 0;

	for (const FRoadMeshSection& Section : MeshData.Sections)
	{
		if (Section.Channel != ERoadMeshChannel::Sidewalk)
		{
			continue;
		}

		for (int32 Index = 0; Index + 2 < Section.Triangles.Num(); Index += 3)
		{
			const FVector& A = Section.Vertices[Section.Triangles[Index + 0]];
			const FVector& B = Section.Vertices[Section.Triangles[Index + 1]];
			const FVector& C = Section.Vertices[Section.Triangles[Index + 2]];

			++SidewalkTriangles;
			if (InsideTriangle(Centre, A, B, C))
			{
				++CoveringCentre;
			}
		}
	}

	TestTrue(
		FString::Printf(TEXT("Es entsteht ueberhaupt Gehweg (%d Dreiecke)"), SidewalkTriangles),
		SidewalkTriangles > 0);

	TestEqual(
		FString::Printf(TEXT("Kein Gehweg-Dreieck ueberdeckt die Kreuzungsmitte (%d von %d)"),
			CoveringCentre, SidewalkTriangles),
		CoveringCentre, 0);

	return true;
}
