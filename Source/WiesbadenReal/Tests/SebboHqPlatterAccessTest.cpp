// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "GIS/GeoCoordinateConverter.h"
#include "GIS/RoadNetworkGenerator.h"
#include "World/SebboHqShape.h"
#include "World/SebboHqSite.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSebboHqPlatterAccessTest,
	"WiesbadenReal.World.SebboHq.PlatterAccess",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FSebboHqPlatterAccessTest::RunTest(const FString& Parameters)
{
	// Echte OSM-Knoten der beiden Strassen unmittelbar am Grundstueck. Der
	// bisherige Standort bindet an die Wolkenbruch; der beauftragte Zugang
	// muss beide Eingange mit der Platter Strasse verbinden.
	UGeoCoordinateConverter* Converter = NewObject<UGeoCoordinateConverter>();
	if (!TestTrue(TEXT("Georeferenz steht"), Converter->InitializeWithWiesbadenOrigin()))
	{
		return false;
	}

	FRoadNetwork Network;
	const auto AddPoint = [Converter](FRoadSegment& Segment, double Latitude, double Longitude)
	{
		Segment.Centerline.Add(Converter->GeoToUnrealGround(FGeoCoordinate(Longitude, Latitude, 0.0)));
	};
	FRoadSegment& Wolkenbruch = Network.Segments.AddDefaulted_GetRef();
	Wolkenbruch.SegmentId = 1;
	Wolkenbruch.StreetName = TEXT("Wolkenbruch");
	AddPoint(Wolkenbruch, 50.0939727, 8.2236246);
	AddPoint(Wolkenbruch, 50.0939462, 8.2238666);
	AddPoint(Wolkenbruch, 50.0939797, 8.2241662);
	AddPoint(Wolkenbruch, 50.0941545, 8.2244809);
	AddPoint(Wolkenbruch, 50.0942957, 8.2246200);

	FRoadSegment& PublicPath = Network.Segments.AddDefaulted_GetRef();
	PublicPath.SegmentId = 3;
	PublicPath.StreetName = TEXT("Oeffentlicher Fuss- und Radweg");
	AddPoint(PublicPath, 50.0934067, 8.2243124);
	AddPoint(PublicPath, 50.0934808, 8.2241554);
	AddPoint(PublicPath, 50.0935291, 8.2240353);
	AddPoint(PublicPath, 50.0935621, 8.2239347);
	AddPoint(PublicPath, 50.0936666, 8.2235723);
	AddPoint(PublicPath, 50.0937171, 8.2234200);
	for (FVector& Point : PublicPath.Centerline)
	{
		Point.Z = 10796.0; // Spielsonde Alkis23 am Kreuzungspunkt
	}

	FRoadSegment& Platter = Network.Segments.AddDefaulted_GetRef();
	Platter.SegmentId = 2;
	Platter.StreetName = TEXT("Platter Stra\u00dfe");
	AddPoint(Platter, 50.0936041, 8.2234027);
	AddPoint(Platter, 50.0935334, 8.2236377);
	AddPoint(Platter, 50.0934431, 8.2239421);
	AddPoint(Platter, 50.0933888, 8.2240915);
	AddPoint(Platter, 50.0933452, 8.2241948);
	AddPoint(Platter, 50.0932119, 8.2244691);
	Platter.CarriagewayWidthCm = 650.0;
	Platter.SidewalkWidthCm = 250.0;
	for (FVector& Point : Platter.Centerline)
	{
		Point.Z = 11013.0; // Spielsonde Alkis23 am Garagenanker
	}

	const FSebboHqDimensions D;
	const FSebboHqArrivalLayout Layout = SebboHq::BuildArrivalFacilities(D);
	const FVector Base = Converter->GeoToUnrealGround(SebboHqSite::Coordinate());
	const FRotator Heading = SebboHqSite::Heading();
	const auto ToWorld = [&Base, &Heading](const FVector& Local)
	{
		return Base + Heading.RotateVector(Local);
	};

	FRoadAccessOverride AccessRequest;
	AccessRequest.bEnabled = true;
	AccessRequest.SearchRadiusCm = 5000.0;
	AccessRequest.PreferredStreetName = TEXT("Platter Stra\u00dfe");
	AccessRequest.GarageEntranceWorldCm = ToWorld(Layout.GarageTarget.CenterCm);
	AccessRequest.PedestrianEntranceWorldCm = ToWorld(Layout.PedestrianTarget.CenterCm);
	const FResolvedRoadAccess Access = URoadNetworkGenerator::ResolveRoadAccess(Network, AccessRequest);
	if (!TestTrue(TEXT("Beide Eingaenge haben einen gemeinsamen Strassenzugang"), Access.IsValid()))
	{
		return false;
	}
	TestEqual(TEXT("Garagenzufahrt kommt von der Platter Strasse"), Access.Garage.SegmentId, 2);
	TestEqual(TEXT("Personeneingang kommt von der Platter Strasse"), Access.Pedestrian.SegmentId, 2);

	const double Half = D.FootprintCm * 0.5 + D.PodiumOversizeCm;
	double GarageEndX = Half;
	double FootpathEndX = Half;
	for (const FHqPart& Part : Layout.Parts)
	{
		if (Part.Material != EHqMaterial::Concrete || Part.SizeCm.Z > 20.0)
		{
			continue; // nur begeh-/befahrbare Decks, keine Widerlager
		}
		const double X = Part.CenterCm.X + Part.SizeCm.X * 0.5;
		const double Y0 = Part.CenterCm.Y - Part.SizeCm.Y * 0.5;
		const double Y1 = Part.CenterCm.Y + Part.SizeCm.Y * 0.5;
		if (Y0 < Layout.GarageTarget.CenterCm.Y && Y1 > Layout.GarageTarget.CenterCm.Y)
		{
			GarageEndX = FMath::Max(GarageEndX, X);
		}
		if (Y0 < Layout.PedestrianTarget.CenterCm.Y && Y1 > Layout.PedestrianTarget.CenterCm.Y)
		{
			FootpathEndX = FMath::Max(FootpathEndX, X);
		}
	}
	const FVector GarageApronEnd = ToWorld(FVector(GarageEndX, Layout.GarageTarget.CenterCm.Y, 0.0));
	const double ApronDistanceCm = FVector::Dist2D(GarageApronEnd, Access.Garage.RoadPointCm);
	TestTrue(*FString::Printf(TEXT("Garagendeck erreicht die Fahrbahnkante (%.0f cm zur Achse)"), ApronDistanceCm),
		ApronDistanceCm <= 350.0);

	const FVector PathEnd = ToWorld(FVector(FootpathEndX, Layout.PedestrianTarget.CenterCm.Y, 0.0));
	const double PathDistanceCm = FVector::Dist2D(PathEnd, Access.Pedestrian.RoadPointCm);
	const double SidewalkCenterCm = (Platter.CarriagewayWidthCm + Platter.SidewalkWidthCm) * 0.5;
	TestTrue(*FString::Printf(TEXT("Portalweg trifft den Platter-Gehweg (%.0f cm zur Achse)"), PathDistanceCm),
		FMath::Abs(PathDistanceCm - SidewalkCenterCm) <= 100.0);

	// Die beiden privaten Decks kreuzen den tieferen oeffentlichen Weg. Fuer
	// Fuss- und Radverkehr bleibt eine lichte Hoehe von mindestens 2,25 m.
	const double TowerBaseZ = Access.Garage.RoadPointCm.Z - SebboHq::GetAccessFloorCm(D);
	for (const double RouteY : { Layout.GarageTarget.CenterCm.Y, Layout.PedestrianTarget.CenterCm.Y })
	{
		FVector Crossing = FVector::ZeroVector;
		bool bCrossing = false;
		for (int32 i = 0; i + 1 < PublicPath.Centerline.Num(); ++i)
		{
			const FVector A = Heading.UnrotateVector(PublicPath.Centerline[i] - Base);
			const FVector B = Heading.UnrotateVector(PublicPath.Centerline[i + 1] - Base);
			if ((RouteY - A.Y) * (RouteY - B.Y) > 0.0 || FMath::IsNearlyEqual(A.Y, B.Y))
			{
				continue;
			}
			Crossing = FMath::Lerp(A, B, (RouteY - A.Y) / (B.Y - A.Y));
			bCrossing = true;
			break;
		}
		if (!TestTrue(TEXT("Der oeffentliche Weg kreuzt die private Verbindung"), bCrossing))
		{
			return false;
		}
		double ClearanceCm = TNumericLimits<double>::Max();
		for (const FHqPart& Part : Layout.Parts)
		{
			if (FMath::Abs(Part.CenterCm.X - Crossing.X) > Part.SizeCm.X * 0.5
				|| FMath::Abs(Part.CenterCm.Y - RouteY) > Part.SizeCm.Y * 0.5)
			{
				continue;
			}
			const double BottomCm = TowerBaseZ + Part.CenterCm.Z - Part.SizeCm.Z * 0.5;
			const double TopCm = TowerBaseZ + Part.CenterCm.Z + Part.SizeCm.Z * 0.5;
			if (TopCm > Crossing.Z)
			{
				ClearanceCm = FMath::Min(ClearanceCm, BottomCm - Crossing.Z);
			}
		}
		TestTrue(*FString::Printf(TEXT("Oeffentlicher Weg bleibt unter der Bruecke frei (%.0f cm)"), ClearanceCm),
			ClearanceCm >= 225.0 && ClearanceCm < TNumericLimits<double>::Max());
	}

	FRoadAccessOverride NearestRequest = AccessRequest;
	NearestRequest.PreferredStreetName.Empty();
	const FResolvedRoadAccess Nearest = URoadNetworkGenerator::ResolveRoadAccess(Network, NearestRequest);
	TestEqual(TEXT("Ohne Praeferenz bleibt der naechste oeffentliche Weg erreichbar"), Nearest.Garage.SegmentId, 3);

	FRoadAccessOverride MissingRequest = AccessRequest;
	MissingRequest.PreferredStreetName = TEXT("Nicht vorhanden");
	TestFalse(TEXT("Fehlender bevorzugter Strassenname faellt nicht still auf einen anderen Weg zurueck"),
		URoadNetworkGenerator::ResolveRoadAccess(Network, MissingRequest).IsValid());
	return true;
}
