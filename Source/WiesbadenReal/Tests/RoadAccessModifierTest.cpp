// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "GIS/RoadNetworkGenerator.h"

namespace
{
	FRoadNetwork MakeAccessNetwork()
	{
		FRoadNetwork Network;

		FRoadSegment Platter;
		Platter.SegmentId = 0;
		Platter.StreetName = TEXT("Platter Strasse");
		Platter.Centerline = { FVector(0.0, 0.0, 120.0), FVector(12000.0, 0.0, 180.0) };
		Platter.TrimmedCenterline = Platter.Centerline;
		Network.Segments.Add(Platter);

		FRoadSegment Parallel;
		Parallel.SegmentId = 1;
		Parallel.StreetName = TEXT("Nebenstrasse");
		Parallel.Centerline = { FVector(0.0, 5000.0, 120.0), FVector(12000.0, 5000.0, 180.0) };
		Parallel.TrimmedCenterline = Parallel.Centerline;
		Network.Segments.Add(Parallel);

		return Network;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadAccessModifierTest,
	"WiesbadenReal.GIS.RoadNetwork.AccessModifier",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FRoadAccessModifierTest::RunTest(const FString& Parameters)
{
	const FRoadNetwork Network = MakeAccessNetwork();

	FRoadAccessOverride Override;
	Override.bEnabled = true;
	Override.GarageEntranceWorldCm = FVector(4000.0, -2200.0, 0.0);
	Override.PedestrianEntranceWorldCm = FVector(7000.0, -2200.0, 0.0);
	Override.SearchRadiusCm = 3000.0;
	Override.GarageWidthCm = 700.0;
	Override.PedestrianWidthCm = 200.0;

	const FResolvedRoadAccess Access = URoadNetworkGenerator::ResolveRoadAccess(Network, Override);
	if (!TestTrue(TEXT("Zufahrt wird aufgeloest"), Access.IsValid()))
	{
		return false;
	}

	TestEqual(TEXT("Garage liegt auf dem naechsten Abschnitt"), Access.Garage.SegmentId, 0);
	TestEqual(TEXT("Portal liegt auf demselben Abschnitt"), Access.Pedestrian.SegmentId, 0);
	TestTrue(TEXT("Beide Ziele liegen auf der rechten Gehwegseite"),
		Access.Garage.SideSign < 0.0 && Access.Pedestrian.SideSign < 0.0);

	constexpr double Kerb = 12.0;
	TestEqual(TEXT("Die Garagenzufahrt ist bordsteinfrei"),
		URoadNetworkGenerator::GetRoadAccessKerbHeightCm(Access, 0, -1.0,
			FVector2D(4000.0, 0.0), Kerb), 0.0, 0.01);
	TestEqual(TEXT("Der Personeneingang ist bordsteinfrei"),
		URoadNetworkGenerator::GetRoadAccessKerbHeightCm(Access, 0, -1.0,
			FVector2D(7000.0, 0.0), Kerb), 0.0, 0.01);
	TestEqual(TEXT("Dazwischen bleibt der Bordstein erhalten"),
		URoadNetworkGenerator::GetRoadAccessKerbHeightCm(Access, 0, -1.0,
			FVector2D(5500.0, 0.0), Kerb), Kerb, 0.01);
	TestEqual(TEXT("Die Gegenseite bleibt unveraendert"),
		URoadNetworkGenerator::GetRoadAccessKerbHeightCm(Access, 0, 1.0,
			FVector2D(4000.0, 0.0), Kerb), Kerb, 0.01);
	TestEqual(TEXT("Ein paralleler Abschnitt bleibt unveraendert"),
		URoadNetworkGenerator::GetRoadAccessKerbHeightCm(Access, 1, -1.0,
			FVector2D(4000.0, 5000.0), Kerb), Kerb, 0.01);

	FRoadAccessOverride SeparateTargets = Override;
	SeparateTargets.PedestrianEntranceWorldCm.Y = 5000.0;
	const FResolvedRoadAccess SeparateAccess = URoadNetworkGenerator::ResolveRoadAccess(Network, SeparateTargets);
	TestFalse(TEXT("Ziele auf verschiedenen Abschnitten oeffnen keinen Bordstein"), SeparateAccess.IsValid());
	TestEqual(TEXT("Ohne gemeinsamen Zugang bleibt die Garage unveraendert"),
		URoadNetworkGenerator::GetRoadAccessKerbHeightCm(SeparateAccess, 0, -1.0,
			FVector2D(4000.0, 0.0), Kerb), Kerb, 0.01);

	return true;
}
