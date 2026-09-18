// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
//
// Ein Fehlertyp, eine Testdatei: ein Laufindex ueber ein QUELL-Array, der
// spaeter gegen ein GEFILTERTES Array verwendet wird.
//
// Der Anlass war echt. FWiesbadenTrafficLightSystem::Initialize legte in
// ConnectionToLight den Laufindex ueber FRoadNetwork::Intersections ab,
// nachgeschlagen wurde er aber in Lights - dem gefilterten Array der
// Ampelkreuzungen. Nur 1073 von 20213 Kreuzungen sind Ampeln, der Index lag
// also fast immer daneben; jede Verbindung galt als gruen und kein Fahrzeug
// hielt je an Rot. Der damalige Test verdeckte das vollstaendig, weil sein
// Netz GENAU EINE Kreuzung hatte, die zugleich die Ampel war: dort sind beide
// Indizes 0.
//
// Daraus die Regel, nach der hier jeder Test gebaut ist: das gesuchte Element
// darf NIE an Index 0 liegen. Die Stellen unten sind heute richtig - diese
// Tests halten sie richtig.

#include "Misc/AutomationTest.h"

#include "GIS/WiesbadenPedestrianSimulation.h"
#include "GIS/WiesbadenRoadClearance.h"
#include "GIS/WiesbadenTrafficSimulation.h"

namespace
{
	FRoadSegment MakeIdxSegment(int32 Id, const FVector& Start, const FVector& End,
		EOSMSidewalkType Sidewalk)
	{
		FRoadSegment Segment;
		Segment.SegmentId = Id;
		Segment.HighwayType = EOSMHighwayType::Residential;
		Segment.Centerline = { Start, (Start + End) * 0.5, End };
		Segment.TrimmedCenterline = Segment.Centerline;
		Segment.LengthCm = FVector::Dist(Start, End);
		Segment.SidewalkType = Sidewalk;
		Segment.CarriagewayWidthCm = 650.0;
		Segment.SidewalkWidthCm = 200.0;
		return Segment;
	}

	FRoadLane MakeIdxLane(int32 Id, int32 SegmentId, const FVector& Start, const FVector& End,
		bool bBusLane)
	{
		FRoadLane Lane;
		Lane.LaneId = Id;
		Lane.SegmentId = SegmentId;
		Lane.Direction = ELaneDirection::Forward;
		Lane.Centerline = { Start, End };
		Lane.LengthCm = FVector::Dist(Start, End);
		Lane.SpeedLimitKmh = 50.0;
		Lane.bIsBusLane = bBusLane;
		return Lane;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFilteredIndexPedestrianTest,
	"WiesbadenReal.World.GefilterterIndexGehweg",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FFilteredIndexPedestrianTest::RunTest(const FString& Parameters)
{
	// WalkableSegmentIndices ist eine GEFILTERTE Liste: nur Abschnitte mit
	// Gehweg. Wer dort die Position in der gefilterten Liste mit dem
	// Segment-Index verwechselt, laesst Fussgaenger auf Strassen laufen, die
	// gar keinen Gehweg haben.
	//
	// Deshalb hat hier NUR das dritte Segment einen Gehweg - und es liegt
	// weit weg von den ersten beiden, damit die Position die Antwort verraet.
	FRoadNetwork Network;
	Network.Segments.Add(MakeIdxSegment(0,
		FVector(0.0, 0.0, 0.0), FVector(20000.0, 0.0, 0.0), EOSMSidewalkType::None));
	Network.Segments.Add(MakeIdxSegment(1,
		FVector(0.0, 5000.0, 0.0), FVector(20000.0, 5000.0, 0.0), EOSMSidewalkType::None));
	Network.Segments.Add(MakeIdxSegment(2,
		FVector(0.0, 40000.0, 0.0), FVector(20000.0, 40000.0, 0.0), EOSMSidewalkType::Both));

	FWiesbadenPedestrianSettings Settings;
	Settings.Density = 1.0f;
	Settings.TargetPedestriansInRadius = 30;
	Settings.SpawnRadiusMeters = 600.0;
	Settings.DespawnRadiusMeters = 1200.0;
	Settings.CityCentreCm = FVector2D::ZeroVector;
	Settings.MinOuterFraction = 1.0;      // keine Ausduennung im Test
	Settings.OuterFalloffPerRing = 0.0;

	FWiesbadenPedestrianSimulation Sim;
	Sim.Initialize(Network, Settings);
	Sim.SetObserverLocation(FVector(10000.0, 20000.0, 0.0));   // zwischen beiden Gruppen

	for (int32 Step = 0; Step < 60; ++Step)
	{
		Sim.Tick(0.1f);
	}

	TestTrue(TEXT("Es laufen ueberhaupt Fussgaenger"), Sim.GetPedestrianCount() > 0);

	TArray<FPlacedPedestrian> Placed;
	Sim.CollectPlaced(Placed);
	TestTrue(TEXT("Und sie werden platziert"), Placed.Num() > 0);

	// Alle muessen beim Gehweg-Abschnitt sein (Y um 40000), keiner bei den
	// gehweglosen (Y um 0 oder 5000).
	int32 OnSidewalkStreet = 0;
	int32 OnStreetWithoutSidewalk = 0;
	for (const FPlacedPedestrian& P : Placed)
	{
		if (FMath::Abs(P.Location.Y - 40000.0) < 8000.0)
		{
			++OnSidewalkStreet;
		}
		else if (P.Location.Y < 20000.0)
		{
			++OnStreetWithoutSidewalk;
		}
	}

	TestEqual(FString::Printf(
		TEXT("Kein Fussgaenger auf einer Strasse ohne Gehweg (%d gefunden)"),
		OnStreetWithoutSidewalk), OnStreetWithoutSidewalk, 0);
	TestTrue(FString::Printf(TEXT("%d Fussgaenger auf dem Gehweg-Abschnitt"), OnSidewalkStreet),
		OnSidewalkStreet > 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFilteredIndexSpawnLaneTest,
	"WiesbadenReal.World.GefilterterIndexSpur",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FFilteredIndexSpawnLaneTest::RunTest(const FString& Parameters)
{
	// SpawnLaneIds ist eine GEFILTERTE Liste: Busspuren fallen heraus. Wer
	// dort die Position in der gefilterten Liste als Spur-Id benutzt, setzt
	// Fahrzeuge auf die falsche Spur - und mit einer Busspur an Index 0
	// faellt genau das nicht auf, solange man nur eine Spur pruefst.
	//
	// Deshalb ist hier Spur 0 eine BUSSPUR und erst Spur 1 befahrbar.
	FRoadNetwork Network;
	Network.Segments.Add(MakeIdxSegment(0,
		FVector(0.0, 0.0, 0.0), FVector(30000.0, 0.0, 0.0), EOSMSidewalkType::None));

	Network.Lanes.Add(MakeIdxLane(0, 0,
		FVector(0.0, 300.0, 0.0), FVector(30000.0, 300.0, 0.0), /*bBusLane=*/true));
	Network.Lanes.Add(MakeIdxLane(1, 0,
		FVector(0.0, -300.0, 0.0), FVector(30000.0, -300.0, 0.0), /*bBusLane=*/false));
	Network.Lanes.Add(MakeIdxLane(2, 0,
		FVector(0.0, -900.0, 0.0), FVector(30000.0, -900.0, 0.0), /*bBusLane=*/false));

	FWiesbadenTrafficSettings Settings;
	Settings.TrafficDensity = 1.0f;
	Settings.MaxSpawnRatePerSecond = 20.0f;
	Settings.VehiclesPerLaneKm = 200.0;
	Settings.SpawnRadiusMeters = 600.0;
	Settings.DespawnRadiusMeters = 1200.0;
	Settings.MinGapCm = 700.0;
	Settings.MaxVehicles = 50;
	Settings.RandomSeed = 7;

	FWiesbadenTrafficSimulation Sim;
	Sim.Initialize(Network, Settings);
	Sim.SetObserverLocation(FVector(0.0, 0.0, 0.0));

	for (int32 Step = 0; Step < 40; ++Step)
	{
		Sim.Tick(0.1f);
	}

	TestTrue(TEXT("Es fahren ueberhaupt Fahrzeuge"), Sim.Vehicles.Num() > 0);

	int32 OnBusLane = 0;
	int32 InvalidLane = 0;
	for (const FTrafficVehicle& Vehicle : Sim.Vehicles)
	{
		if (!Network.Lanes.IsValidIndex(Vehicle.LaneId))
		{
			++InvalidLane;
			continue;
		}
		if (Network.Lanes[Vehicle.LaneId].bIsBusLane)
		{
			++OnBusLane;
		}
	}

	TestEqual(TEXT("Keine ungueltige Spur-Id"), InvalidLane, 0);
	TestEqual(FString::Printf(TEXT("Kein Fahrzeug auf der Busspur (%d gefunden)"), OnBusLane),
		OnBusLane, 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFilteredIndexClearanceTest,
	"WiesbadenReal.World.GefilterterIndexFreihaltung",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FFilteredIndexClearanceTest::RunTest(const FString& Parameters)
{
	// FWiesbadenRoadClearance::BuildAround uebernimmt nur Abschnitte im
	// Umkreis - also wieder eine gefilterte Auswahl. Wer hier danebengreift,
	// bekommt eine Freihaltung, die an der falschen Strasse haengt.
	//
	// Das Netz ist darauf gebaut: die gesuchte Strasse ist die DRITTE. Die
	// erste ist entartet (wird verworfen), die zweite liegt weit ausserhalb
	// des Umkreises. Eine Verwechslung von Auswahl-Position und Segment
	// trifft damit garantiert die falsche.
	FRoadNetwork Network;

	// Entartet: spannt einen Kasten ueber die halbe Welt und wird verworfen.
	Network.Segments.Add(MakeIdxSegment(0,
		FVector(-10000000.0, -10000000.0, 0.0), FVector(10000000.0, 10000000.0, 0.0),
		EOSMSidewalkType::None));

	// Weit weg - ausserhalb jedes geprueften Umkreises.
	Network.Segments.Add(MakeIdxSegment(1,
		FVector(0.0, 900000.0, 0.0), FVector(20000.0, 900000.0, 0.0), EOSMSidewalkType::None));

	// Die gesuchte Strasse.
	Network.Segments.Add(MakeIdxSegment(2,
		FVector(0.0, 0.0, 0.0), FVector(20000.0, 0.0, 0.0), EOSMSidewalkType::None));

	// -- 1. Voller Bau: die echte Strasse sperrt, der entartete Abschnitt
	//       poisoniert nichts. ---------------------------------------------
	{
		FWiesbadenRoadClearance Clearance;
		Clearance.Build(Network, /*ExtraMarginCm=*/100.0, /*bIncludeSidewalk=*/false);

		TestTrue(TEXT("Es sind Abschnitte eingetragen"), Clearance.GetSpanCount() > 0);
		TestTrue(TEXT("Punkt auf der Fahrbahn ist gesperrt"),
			Clearance.IsBlocked(FVector2D(10000.0, 0.0)));

		// Ohne den Deckel gegen entartete Abschnitte waere hier alles gesperrt.
		TestFalse(TEXT("Punkt 50 m neben der Fahrbahn ist frei"),
			Clearance.IsBlocked(FVector2D(10000.0, 5000.0)));
	}

	// -- 2. Umkreis-Bau: dieselbe Antwort, obwohl nur ein Teil des Netzes
	//       eingetragen wird. ----------------------------------------------
	{
		FWiesbadenRoadClearance Around;
		Around.BuildAround(Network, FVector2D(10000.0, 0.0), /*AreaRadiusCm=*/30000.0,
			/*ExtraMarginCm=*/100.0, /*bIncludeSidewalk=*/false);

		TestTrue(TEXT("Umkreis-Bau traegt die nahe Strasse ein"), Around.GetSpanCount() > 0);
		TestTrue(TEXT("Umkreis-Bau: Fahrbahn gesperrt"),
			Around.IsBlocked(FVector2D(10000.0, 0.0)));
		TestFalse(TEXT("Umkreis-Bau: daneben frei"),
			Around.IsBlocked(FVector2D(10000.0, 5000.0)));

		// Und die ferne Strasse ist NICHT mitgekommen - sonst waere der
		// Umkreis wirkungslos und der Index haenge am falschen Abschnitt.
		TestFalse(TEXT("Die ferne Strasse ist nicht im Umkreis-Index"),
			Around.IsBlocked(FVector2D(10000.0, 900000.0)));
	}

	// -- 3. Ein Umkreis, der NUR die ferne Strasse umfasst, sperrt auch nur
	//       diese. --------------------------------------------------------
	//
	// Die Gegenprobe zu 2.: greift die Auswahl an der falschen Stelle zu,
	// faellt genau hier der Unterschied auf.
	{
		FWiesbadenRoadClearance Far;
		Far.BuildAround(Network, FVector2D(10000.0, 900000.0), /*AreaRadiusCm=*/30000.0,
			/*ExtraMarginCm=*/100.0, /*bIncludeSidewalk=*/false);

		TestTrue(TEXT("Ferner Umkreis: die ferne Fahrbahn ist gesperrt"),
			Far.IsBlocked(FVector2D(10000.0, 900000.0)));
		TestFalse(TEXT("Ferner Umkreis: die nahe Fahrbahn ist NICHT gesperrt"),
			Far.IsBlocked(FVector2D(10000.0, 0.0)));
	}

	return true;
}
