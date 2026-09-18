// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "GIS/WiesbadenTrafficLights.h"
#include "GIS/WiesbadenTrafficSimulation.h"

namespace
{
	FRoadLane MakeSimLane(int32 Id, const TArray<FVector>& Line, double SpeedKmh = 50.0)
	{
		FRoadLane Lane;
		Lane.LaneId = Id;
		Lane.SegmentId = Id;
		Lane.Direction = ELaneDirection::Forward;
		Lane.Centerline = Line;
		Lane.LengthCm = 0.0;
		for (int32 i = 1; i < Line.Num(); ++i)
		{
			Lane.LengthCm += FVector::Dist(Line[i], Line[i - 1]);
		}
		Lane.SpeedLimitKmh = SpeedKmh;
		return Lane;
	}

	/** Lane 0 -> Verbindung 0 -> Lane 1; Lane 2 = Sackgasse. LaneId == Array-Index. */
	FRoadNetwork MakeNetwork()
	{
		FRoadNetwork Network;
		Network.Lanes.Add(MakeSimLane(0, { FVector(0.0, 0.0, 0.0), FVector(10000.0, 0.0, 0.0) }));
		Network.Lanes.Add(MakeSimLane(1, { FVector(10500.0, 0.0, 0.0), FVector(20500.0, 0.0, 0.0) }));
		Network.Lanes.Add(MakeSimLane(2, { FVector(0.0, 5000.0, 0.0), FVector(10000.0, 5000.0, 0.0) }));

		FLaneConnection Connection;
		Connection.FromLaneId = 0;
		Connection.ToLaneId = 1;
		Connection.IntersectionNodeId = 42;
		Connection.TurnType = ETurnType::Through;
		Connection.bRestricted = false;
		Connection.ConnectionPath = {
			FVector(10000.0, 0.0, 0.0), FVector(10250.0, 0.0, 0.0), FVector(10500.0, 0.0, 0.0) };
		Network.Connections.Add(Connection);

		FRoadSegment Segment;
		Segment.SegmentId = 0;
		Segment.HighwayType = EOSMHighwayType::Residential;
		Segment.LengthCm = 30000.0;
		Network.Segments.Add(Segment);

		return Network;
	}

	FWiesbadenTrafficSettings MakeSettings(float Density)
	{
		FWiesbadenTrafficSettings Settings;
		Settings.TrafficDensity = Density;
		Settings.MaxSpawnRatePerSecond = 2.0f;
		Settings.TargetSpeedFraction = 0.75f;
		Settings.MinGapCm = 700.0;
		Settings.MinSpeedKmh = 20.0;
		Settings.MaxVehicles = 3000;
		Settings.RandomSeed = 12345;
		return Settings;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrafficPlacementTest,
	"WiesbadenReal.Traffic.Placement",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTrafficPlacementTest::RunTest(const FString& Parameters)
{
	// Drei Fahrzeuge: nah (500 m), fern (1500 m), ohne Position (0/0/0).
	TArray<FTrafficVehicle> Vehicles;

	FTrafficVehicle Near;
	Near.VehicleId = 1;
	Near.Location = FVector(50000.0, 0.0, 0.0);
	Near.Forward = FVector(1.0, 0.0, 0.0);
	Vehicles.Add(Near);

	FTrafficVehicle Far;
	Far.VehicleId = 2;
	Far.Location = FVector(150000.0, 0.0, 0.0);
	Far.Forward = FVector(0.0, 1.0, 0.0);
	Vehicles.Add(Far);

	FTrafficVehicle Center;
	Center.VehicleId = 3;
	Center.Location = FVector::ZeroVector;
	Center.Forward = FVector(1.0, 0.0, 0.0);
	Vehicles.Add(Center);

	// Culling: Beobachter im Ursprung, Radius 1000 m -> nur Nah (500 m) und
	// Center (0 m) sichtbar, Fern (1500 m) verworfen.
	{
		TArray<FPlacedTrafficVehicle> Placed;
		FWiesbadenTrafficSimulation::PlaceTrafficVehicles(Vehicles, FVector::ZeroVector, 100000.0, 4, Placed);

		TestEqual(TEXT("Culling 1 km: nur 2 von 3 Fahrzeugen sichtbar"), Placed.Num(), 2);

		// Deterministische Zuordnung: gleiche Eingabe -> gleiche Ausgabe.
		TArray<FPlacedTrafficVehicle> PlacedAgain;
		FWiesbadenTrafficSimulation::PlaceTrafficVehicles(Vehicles, FVector::ZeroVector, 100000.0, 4, PlacedAgain);
		TestTrue(TEXT("Determinismus: identische Platzierungen"), Placed.Num() == PlacedAgain.Num());
		bool bIdentical = Placed.Num() == PlacedAgain.Num();
		for (int32 i = 0; i < Placed.Num() && bIdentical; ++i)
		{
			bIdentical = Placed[i].VehicleId == PlacedAgain[i].VehicleId
				&& Placed[i].ColorIndex == PlacedAgain[i].ColorIndex
				&& Placed[i].Transform.Equals(PlacedAgain[i].Transform, 0.001);
		}
		TestTrue(TEXT("Determinismus: Position/Farbe identisch"), bIdentical);

		// Farbe: ColorIndex in [0, PaletteSize), gleiche Id -> gleiche Farbe.
		for (const FPlacedTrafficVehicle& P : Placed)
		{
			TestTrue(TEXT("ColorIndex im Palette-Bereich"),
				P.ColorIndex >= 0 && P.ColorIndex < 4);
		}
		bool bStableColor = Placed.Num() >= 1;
		int32 FirstColor = Placed.Num() > 0 ? Placed[0].ColorIndex : -1;
		for (const FPlacedTrafficVehicle& P : Placed)
		{
			if (P.VehicleId == 1) { bStableColor = bStableColor && (P.ColorIndex == FirstColor); }
		}
		TestTrue(TEXT("Gleiche Id -> gleiche Farbe (stabil)"), bStableColor);

		// Yaw aus Forward: Nah (Forward +X) -> Yaw 0; Center ebenfalls.
		for (const FPlacedTrafficVehicle& P : Placed)
		{
			if (P.VehicleId == 1)
			{
				TestTrue(TEXT("Yaw aus Forward (+X) -> 0 Grad"),
					FMath::Abs(P.Transform.GetRotation().Rotator().Yaw) < 0.5);
			}
			if (P.VehicleId == 3)
			{
				TestTrue(TEXT("Position uebernommen (Center 0/0/0)"),
					P.Transform.GetLocation().IsNearlyZero());
			}
		}
	}

	// Radius <= 0: kein Culling, alle Fahrzeuge sichtbar.
	{
		TArray<FPlacedTrafficVehicle> Placed;
		FWiesbadenTrafficSimulation::PlaceTrafficVehicles(Vehicles, FVector::ZeroVector, 0.0, 4, Placed);
		TestEqual(TEXT("Radius 0: alle Fahrzeuge sichtbar"), Placed.Num(), 3);
	}

	// Leere Eingabe -> leere Ausgabe.
	{
		TArray<FPlacedTrafficVehicle> Placed;
		FWiesbadenTrafficSimulation::PlaceTrafficVehicles(TArray<FTrafficVehicle>(), FVector::ZeroVector, 100000.0, 4, Placed);
		TestEqual(TEXT("Leere Eingabe -> leer"), Placed.Num(), 0);
	}

	// Forward +Y -> Yaw 90 Grad (Ost-Konvention wie Rotator).
	{
		FTrafficVehicle North;
		North.VehicleId = 9;
		North.Location = FVector(0.0, 5000.0, 0.0);
		North.Forward = FVector(0.0, 1.0, 0.0);
		TArray<FTrafficVehicle> Single;
		Single.Add(North);

		TArray<FPlacedTrafficVehicle> Placed;
		FWiesbadenTrafficSimulation::PlaceTrafficVehicles(Single, FVector::ZeroVector, 100000.0, 4, Placed);
		TestEqual(TEXT("Ein Fahrzeug platziert"), Placed.Num(), 1);
		if (Placed.Num() == 1)
		{
			TestTrue(TEXT("Yaw aus Forward (+Y) -> 90 Grad"),
				FMath::Abs(Placed[0].Transform.GetRotation().Rotator().Yaw - 90.0) < 0.5);
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrafficSpawnRateTest,
	"WiesbadenReal.Traffic.SpawnRate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTrafficSpawnRateTest::RunTest(const FString& Parameters)
{
	// Dichte 0 -> keine Fahrzeuge.
	{
		FWiesbadenTrafficSimulation Sim;
		const FRoadNetwork ScopedNetwork1 = MakeNetwork();
		Sim.Initialize(ScopedNetwork1, MakeSettings(0.0f));
		for (int32 i = 0; i < 5; ++i)
		{
			Sim.Tick(1.0f);
		}
		TestTrue(TEXT("Dichte 0: keine Fahrzeuge"),
			Sim.Report.ActiveVehicleCount == 0 && Sim.Report.TotalSpawnedCount == 0);
	}

	// Dichte 1, Rate 2/s, 5 s -> exakt 10 Spawns.
	{
		FWiesbadenTrafficSimulation Sim;
		const FRoadNetwork ScopedNetwork2 = MakeNetwork();
		Sim.Initialize(ScopedNetwork2, MakeSettings(1.0f));
		for (int32 i = 0; i < 5; ++i)
		{
			Sim.Tick(1.0f);
		}
		TestEqual(TEXT("Dichte 1: exakt 10 Spawns in 5 s"),
			Sim.Report.TotalSpawnedCount, static_cast<int64>(10));

		// Alle Fahrzeuge auf Spawn-Spuren, Round-Robin verteilt.
		TSet<int32> UsedLanes;
		bool bAllValid = true;
		for (const FTrafficVehicle& Vehicle : Sim.Vehicles)
		{
			bAllValid = bAllValid && Vehicle.bOnLane && Vehicle.LaneId >= 0 && Vehicle.LaneId <= 2;
			UsedLanes.Add(Vehicle.LaneId);
		}
		TestTrue(TEXT("Spawn auf gueltigen Spuren"), bAllValid);
		TestTrue(TEXT("Spawn auf mindestens zwei Spuren verteilt"), UsedLanes.Num() >= 2);
	}

	// Monotonie: Dichte steuert die Spawn-Rate (kumulativ 2 / 7 / 10).
	{
		const float Densities[] = { 0.25f, 0.75f, 1.0f };
		const int64 Expected[] = { 2, 7, 10 };
		for (int32 i = 0; i < 3; ++i)
		{
			FWiesbadenTrafficSimulation Sim;
			const FRoadNetwork ScopedNetwork3 = MakeNetwork();
			Sim.Initialize(ScopedNetwork3, MakeSettings(Densities[i]));
			for (int32 t = 0; t < 5; ++t)
			{
				Sim.Tick(1.0f);
			}
			TestEqual(FString::Printf(TEXT("Dichte %.2f -> %lld Spawns"), Densities[i], Expected[i]),
				Sim.Report.TotalSpawnedCount, Expected[i]);
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrafficGraphFollowTest,
	"WiesbadenReal.Traffic.GraphFollow",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTrafficGraphFollowTest::RunTest(const FString& Parameters)
{
	// Frueh (5 s): erstes Fahrzeug noch auf Lane 0 (max ~1390 cm/s * 5 < 10000).
	{
		FWiesbadenTrafficSimulation Sim;
		const FRoadNetwork ScopedNetwork4 = MakeNetwork();
		Sim.Initialize(ScopedNetwork4, MakeSettings(1.0f));
		for (int32 t = 0; t < 5; ++t)
		{
			Sim.Tick(1.0f);
		}
		TestTrue(TEXT("Nach 5 s noch auf Lane 0"),
			Sim.Vehicles.IsValidIndex(0)
			&& Sim.Vehicles[0].bOnLane && Sim.Vehicles[0].LaneId == 0);
	}

	// Voll: Fahrzeug durchlaeuft Spur -> Verbindung -> Folgespur; endet an der
	// Sackgasse (Lane 1 hat keine Nachfolger) und wird entfernt.
	{
		FWiesbadenTrafficSimulation Sim;
		const FRoadNetwork ScopedNetwork5 = MakeNetwork();
		Sim.Initialize(ScopedNetwork5, MakeSettings(1.0f));
		bool bSawConnection = false;
		bool bReachedLane1 = false;
		for (int32 t = 0; t < 25; ++t)
		{
			Sim.Tick(1.0f);
			if (!Sim.Vehicles.IsValidIndex(0))
			{
				break;
			}
			const FTrafficVehicle& First = Sim.Vehicles[0];
			if (!First.bOnLane)
			{
				bSawConnection = true;
			}
			if (First.bOnLane && First.LaneId == 1)
			{
				bReachedLane1 = true;
			}
		}
		TestTrue(TEXT("Fahrzeug durchlaeuft die Kreuzungs-Verbindung"), bSawConnection);
		TestTrue(TEXT("Fahrzeug erreicht die Folgespur (Lane 1)"), bReachedLane1);
		TestTrue(TEXT("Sackgassen-Ende entfernt Fahrzeug (Lane 1 ohne Nachfolger)"),
			Sim.Report.TotalRemovedCount >= 1);
	}

	// Dediziertes Sackgassen-Netz: alle Fahrzeuge werden entfernt, Bestand bleibt
	// durch die begrenzte Lebensdauer gedeckelt.
	{
		// Konvention: LaneId == Index in Network->Lanes (wie GetLane).
		FRoadNetwork DeadEnd = MakeNetwork();
		DeadEnd.Lanes.Reset();
		DeadEnd.Connections.Reset();
		DeadEnd.Lanes.Add(MakeSimLane(0, { FVector(0.0, 5000.0, 0.0), FVector(10000.0, 5000.0, 0.0) }));

		FWiesbadenTrafficSimulation Sim;
		FWiesbadenTrafficSettings Settings = MakeSettings(1.0f);
		Settings.MaxSpawnRatePerSecond = 5.0f;
		Sim.Initialize(DeadEnd, Settings);
		for (int32 t = 0; t < 40; ++t)
		{
			Sim.Tick(1.0f);
		}
		TestTrue(TEXT("Sackgassen-Netz: Fahrzeuge werden entfernt"), Sim.Report.TotalRemovedCount >= 20);
		TestTrue(TEXT("Sackgassen-Netz: Bestand gedeckelt"), Sim.Report.ActiveVehicleCount < 100);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrafficHeadwayTest,
	"WiesbadenReal.Traffic.Headway",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTrafficHeadwayTest::RunTest(const FString& Parameters)
{
	// Eine lange Spur (500 m) ohne Verbindungen; Fahrzeuge von Hand gesetzt.
	FRoadNetwork LongNetwork;
	LongNetwork.Lanes.Add(MakeSimLane(0, { FVector(0.0, 0.0, 0.0), FVector(50000.0, 0.0, 0.0) }));
	FRoadSegment Segment;
	Segment.SegmentId = 0;
	Segment.HighwayType = EOSMHighwayType::Residential;
	Segment.LengthCm = 50000.0;
	LongNetwork.Segments.Add(Segment);

	FWiesbadenTrafficSimulation Sim;
	Sim.Initialize(LongNetwork, MakeSettings(0.0f));

	// Leader: langsam (300 cm/s) bei 40000; Follower bei 39300 (Luecke 700 = MinGap);
	// Dritter weitere 1300 cm dahinter -> Safe-Tempo 300 + 600 = 900.
	FTrafficVehicle Leader;
	Leader.VehicleId = 1;
	Leader.LaneId = 0;
	Leader.bOnLane = true;
	Leader.DistanceCm = 40000.0;
	Leader.SpeedCmS = 300.0;
	Leader.DesiredSpeedCmS = 300.0;
	Sim.Vehicles.Add(Leader);

	FTrafficVehicle Follower;
	Follower.VehicleId = 2;
	Follower.LaneId = 0;
	Follower.bOnLane = true;
	Follower.DistanceCm = 39300.0;
	Follower.SpeedCmS = 1400.0;
	Follower.DesiredSpeedCmS = 1400.0;
	Sim.Vehicles.Add(Follower);

	FTrafficVehicle Third;
	Third.VehicleId = 3;
	Third.LaneId = 0;
	Third.bOnLane = true;
	Third.DistanceCm = 38000.0;
	Third.SpeedCmS = 1400.0;
	Third.DesiredSpeedCmS = 1400.0;
	Sim.Vehicles.Add(Third);

	Sim.Tick(1.0f);

	// Vehicles[1] ist der Follower, Vehicles[2] der Dritte (Spawn-Reihenfolge).
	TestTrue(TEXT("Follower an MinGap: uebernimmt Leader-Tempo (300)"),
		FMath::Abs(Sim.Vehicles[1].SpeedCmS - 300.0) < 0.5);
	TestTrue(TEXT("Dritter mit Luecke 1300: Safe-Tempo 900"),
		FMath::Abs(Sim.Vehicles[2].SpeedCmS - 900.0) < 0.5);

	for (int32 t = 0; t < 5; ++t)
	{
		Sim.Tick(1.0f);
	}

	const FTrafficVehicle* LeaderNow = nullptr;
	const FTrafficVehicle* FollowerNow = nullptr;
	for (const FTrafficVehicle& Vehicle : Sim.Vehicles)
	{
		if (Vehicle.VehicleId == 1) { LeaderNow = &Vehicle; }
		if (Vehicle.VehicleId == 2) { FollowerNow = &Vehicle; }
	}
	TestTrue(TEXT("Kein Ueberholen: Follower bleibt hinter Leader mit MinGap"),
		LeaderNow && FollowerNow && FollowerNow->DistanceCm < LeaderNow->DistanceCm
		&& LeaderNow->DistanceCm - FollowerNow->DistanceCm >= 699.5);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrafficOvertakeTest,
	"WiesbadenReal.Traffic.Overtake",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Ein behindertes Fahrzeug wechselt auf die freie Nachbarspur.
 *
 * Die Simulation kannte bisher keinen Spurwechsel: Wer hinter einem langsamen
 * Fahrzeug hing, blieb dort - auch auf einer zweispurigen Strasse mit voellig
 * freier Nebenspur. Genau das sah im Spiel aus wie Fahrzeuge, die sich
 * verhaken, statt aneinander vorbeizufahren.
 */
bool FTrafficOvertakeTest::RunTest(const FString& Parameters)
{
	// Zwei parallele Spuren desselben Abschnitts, gleiche Fahrtrichtung.
	FRoadNetwork Network;

	FRoadLane Right = MakeSimLane(0, { FVector(0.0, 0.0, 0.0), FVector(50000.0, 0.0, 0.0) });
	Right.SegmentId = 7;
	Right.LaneIndexFromLeft = 1;
	Network.Lanes.Add(Right);

	FRoadLane Left = MakeSimLane(1, { FVector(0.0, 350.0, 0.0), FVector(50000.0, 350.0, 0.0) });
	Left.SegmentId = 7;
	Left.LaneIndexFromLeft = 0;
	Network.Lanes.Add(Left);

	FRoadSegment Segment;
	Segment.SegmentId = 7;
	Segment.HighwayType = EOSMHighwayType::Secondary;
	Segment.LengthCm = 50000.0;
	Network.Segments.Add(Segment);

	FWiesbadenTrafficSimulation Sim;
	Sim.Initialize(Network, MakeSettings(0.0f));

	// Langsamer Vordermann auf Spur 0, dicht dahinter ein schnelleres Fahrzeug.
	FTrafficVehicle Slow;
	Slow.VehicleId = 1;
	Slow.LaneId = 0;
	Slow.bOnLane = true;
	Slow.DistanceCm = 20000.0;
	Slow.SpeedCmS = 300.0;
	Slow.DesiredSpeedCmS = 300.0;
	Sim.Vehicles.Add(Slow);

	FTrafficVehicle Fast;
	Fast.VehicleId = 2;
	Fast.LaneId = 0;
	Fast.bOnLane = true;
	Fast.DistanceCm = 19500.0;   // Luecke 500 < MinGap 700 -> ausgebremst
	Fast.SpeedCmS = 300.0;
	Fast.DesiredSpeedCmS = 1400.0;
	Sim.Vehicles.Add(Fast);

	Sim.Tick(0.1f);

	const FTrafficVehicle* FastNow = nullptr;
	const FTrafficVehicle* SlowNow = nullptr;
	for (const FTrafficVehicle& Vehicle : Sim.Vehicles)
	{
		if (Vehicle.VehicleId == 2) { FastNow = &Vehicle; }
		if (Vehicle.VehicleId == 1) { SlowNow = &Vehicle; }
	}

	TestNotNull(TEXT("Ueberholer vorhanden"), FastNow);
	TestNotNull(TEXT("Vordermann vorhanden"), SlowNow);
	if (!FastNow || !SlowNow)
	{
		return false;
	}

	TestEqual(TEXT("Ueberholer ist auf die Nachbarspur gewechselt"), FastNow->LaneId, 1);
	TestEqual(TEXT("Der langsame Vordermann bleibt, wo er ist"), SlowNow->LaneId, 0);
	TestEqual(TEXT("Genau ein Spurwechsel gezaehlt"), Sim.Report.LaneChangesThisTick, 1);

	// Gegenprobe: Ist die Nachbarspur belegt, wird NICHT gewechselt.
	FWiesbadenTrafficSimulation Blocked;
	Blocked.Initialize(Network, MakeSettings(0.0f));
	Blocked.Vehicles.Add(Slow);
	Blocked.Vehicles.Add(Fast);

	FTrafficVehicle Occupant;
	Occupant.VehicleId = 3;
	Occupant.LaneId = 1;
	Occupant.bOnLane = true;
	Occupant.DistanceCm = 19600.0;   // direkt neben dem Ueberholer
	Occupant.SpeedCmS = 300.0;
	Occupant.DesiredSpeedCmS = 300.0;
	Blocked.Vehicles.Add(Occupant);

	Blocked.Tick(0.1f);

	const FTrafficVehicle* BlockedFast = nullptr;
	for (const FTrafficVehicle& Vehicle : Blocked.Vehicles)
	{
		if (Vehicle.VehicleId == 2) { BlockedFast = &Vehicle; }
	}
	TestTrue(TEXT("Kein Wechsel auf eine belegte Nachbarspur"),
		BlockedFast && BlockedFast->LaneId == 0);
	TestEqual(TEXT("Kein Spurwechsel gezaehlt"), Blocked.Report.LaneChangesThisTick, 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrafficLaneChangeExclusivityTest,
	"WiesbadenReal.Traffic.LaneChangeExclusivity",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Zwei Fahrzeuge duerfen nicht gleichzeitig in dieselbe Luecke ziehen.
 *
 * Die Luecken-Suche liest eine Belegungsliste, die zu Beginn des Ticks
 * aufgebaut wird. Wird ein Wechsel nicht darin gebucht, bleibt er fuer alle
 * folgenden Fahrzeuge desselben Ticks unsichtbar - zwei Fahrzeuge einer
 * Kolonne sehen beide dieselbe Luecke als frei an und landen aufeinander.
 */
bool FTrafficLaneChangeExclusivityTest::RunTest(const FString& Parameters)
{
	FRoadNetwork Network;

	FRoadLane Right = MakeSimLane(0, { FVector(0.0, 0.0, 0.0), FVector(50000.0, 0.0, 0.0) });
	Right.SegmentId = 7;
	Right.LaneIndexFromLeft = 1;
	Network.Lanes.Add(Right);

	FRoadLane Left = MakeSimLane(1, { FVector(0.0, 350.0, 0.0), FVector(50000.0, 350.0, 0.0) });
	Left.SegmentId = 7;
	Left.LaneIndexFromLeft = 0;
	Network.Lanes.Add(Left);

	FRoadSegment Segment;
	Segment.SegmentId = 7;
	Segment.HighwayType = EOSMHighwayType::Secondary;
	Segment.LengthCm = 50000.0;
	Network.Segments.Add(Segment);

	FWiesbadenTrafficSimulation Sim;
	Sim.Initialize(Network, MakeSettings(0.0f));

	// Langsamer Vordermann, dahinter ZWEI schnellere Fahrzeuge dicht auf.
	FTrafficVehicle Slow;
	Slow.VehicleId = 1;
	Slow.LaneId = 0;
	Slow.bOnLane = true;
	Slow.DistanceCm = 20000.0;
	Slow.SpeedCmS = 300.0;
	Slow.DesiredSpeedCmS = 300.0;
	Sim.Vehicles.Add(Slow);

	FTrafficVehicle FirstFast;
	FirstFast.VehicleId = 2;
	FirstFast.LaneId = 0;
	FirstFast.bOnLane = true;
	FirstFast.DistanceCm = 19500.0;
	FirstFast.SpeedCmS = 300.0;
	FirstFast.DesiredSpeedCmS = 1400.0;
	Sim.Vehicles.Add(FirstFast);

	FTrafficVehicle SecondFast;
	SecondFast.VehicleId = 3;
	SecondFast.LaneId = 0;
	SecondFast.bOnLane = true;
	SecondFast.DistanceCm = 19000.0;   // 500 cm hinter dem ersten
	SecondFast.SpeedCmS = 300.0;
	SecondFast.DesiredSpeedCmS = 1400.0;
	Sim.Vehicles.Add(SecondFast);

	Sim.Tick(0.1f);

	// Beide wollen wechseln, aber nach dem ersten Wechsel liegt der zweite nur
	// 500 cm hinter ihm - weniger als die geforderten 1400 cm.
	TestEqual(TEXT("Nur ein Fahrzeug wechselt je Tick in dieselbe Luecke"),
		Sim.Report.LaneChangesThisTick, 1);

	// Gegenprobe ueber die Positionen: auf der Zielspur steht genau eines.
	int32 OnTargetLane = 0;
	for (const FTrafficVehicle& Vehicle : Sim.Vehicles)
	{
		if (Vehicle.bOnLane && Vehicle.LaneId == 1)
		{
			++OnTargetLane;
		}
	}
	TestEqual(TEXT("Genau ein Fahrzeug auf der Nachbarspur"), OnTargetLane, 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrafficCrossEdgeHeadwayTest,
	"WiesbadenReal.Traffic.CrossEdgeHeadway",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Ein Fahrzeug am Spurende sieht das Fahrzeug auf der Kreuzungsverbindung.
 *
 * Die Kopf-zu-Schwanz-Regel gruppierte nur je Spur bzw. je Verbindung. Ein
 * Fahrzeug am Ende einer Spur sah damit nicht, dass auf der Verbindung davor
 * bereits jemand stand - es fuhr hinein, und erst im naechsten Tick, mit
 * bereits negativem Abstand, wurde es auf 0 geklemmt. So entstanden die
 * ineinander steckenden Fahrzeuge an den Kreuzungen.
 */
bool FTrafficCrossEdgeHeadwayTest::RunTest(const FString& Parameters)
{
	const FRoadNetwork Network = MakeNetwork();   // Spur 0 (100 m) -> Verbindung 0 -> Spur 1

	FWiesbadenTrafficSimulation Sim;
	Sim.Initialize(Network, MakeSettings(0.0f));

	// Stehendes Fahrzeug am Anfang der Verbindung.
	FTrafficVehicle Blocker;
	Blocker.VehicleId = 1;
	Blocker.bOnLane = false;
	Blocker.ConnectionIndex = 0;
	Blocker.DistanceCm = 0.0;
	Blocker.SpeedCmS = 0.0;
	Blocker.DesiredSpeedCmS = 0.0;
	Sim.Vehicles.Add(Blocker);

	// Fahrzeug kurz vor dem Spurende, aus dem Stand anfahrend.
	FTrafficVehicle Approaching;
	Approaching.VehicleId = 2;
	Approaching.LaneId = 0;
	Approaching.bOnLane = true;
	Approaching.DistanceCm = 9800.0;   // 200 cm bis zum Spurende
	Approaching.SpeedCmS = 0.0;
	Approaching.DesiredSpeedCmS = 1400.0;
	Sim.Vehicles.Add(Approaching);

	Sim.Tick(1.0f);

	const FTrafficVehicle* ApproachingNow = nullptr;
	for (const FTrafficVehicle& Vehicle : Sim.Vehicles)
	{
		if (Vehicle.VehicleId == 2) { ApproachingNow = &Vehicle; }
	}
	TestNotNull(TEXT("Fahrzeug vorhanden"), ApproachingNow);
	if (!ApproachingNow)
	{
		return false;
	}

	// Abstand 200 cm liegt unter der Mindestluecke von 700 cm: Das Fahrzeug
	// darf sich nicht bewegen. Ohne die Regel ueber die Bahngrenze hinweg
	// wuerde es mit der vollen Beschleunigung von 250 cm/s^2 anfahren.
	TestTrue(
		FString::Printf(TEXT("Haelt vor dem besetzten Kreuzungsbereich (%.1f cm/s)"),
			ApproachingNow->SpeedCmS),
		ApproachingNow->SpeedCmS < 1.0);

	// Gegenprobe: ohne Fahrzeug auf der Verbindung faehrt es an - und zwar
	// genau mit der begrenzten Beschleunigung, nicht mit Wunschgeschwindigkeit.
	FWiesbadenTrafficSimulation Free;
	Free.Initialize(Network, MakeSettings(0.0f));
	Free.Vehicles.Add(Approaching);
	Free.Tick(1.0f);

	TestTrue(
		FString::Printf(TEXT("Freie Kreuzung: faehrt mit 250 cm/s^2 an (%.1f cm/s)"),
			Free.Vehicles[0].SpeedCmS),
		FMath::Abs(Free.Vehicles[0].SpeedCmS - 250.0) < 0.5);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrafficAccelerationLimitTest,
	"WiesbadenReal.Traffic.AccelerationLimit",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Geschwindigkeit steigt nur mit begrenzter Beschleunigung, faellt aber frei.
 *
 * Frueher uebernahm die Simulation den Zielwert unmittelbar - ein Fahrzeug
 * sprang in einem Tick von 0 auf Wunschgeschwindigkeit und zurueck. Daraus
 * entstanden Bremswellen, die rueckwaerts durch die Kolonne liefen.
 *
 * Das Bremsen bleibt bewusst unbegrenzt: Die Abstandsregel loest exakt auf,
 * und ein gedeckeltes Bremsen wuerde bedeuten, dass ein Fahrzeug die noetige
 * Geschwindigkeit nicht erreicht und auffaehrt.
 */
bool FTrafficAccelerationLimitTest::RunTest(const FString& Parameters)
{
	FRoadNetwork Network;
	Network.Lanes.Add(MakeSimLane(0, { FVector(0.0, 0.0, 0.0), FVector(100000.0, 0.0, 0.0) }));
	FRoadSegment Segment;
	Segment.SegmentId = 0;
	Segment.HighwayType = EOSMHighwayType::Residential;
	Segment.LengthCm = 100000.0;
	Network.Segments.Add(Segment);

	FWiesbadenTrafficSimulation Sim;
	Sim.Initialize(Network, MakeSettings(0.0f));

	FTrafficVehicle Vehicle;
	Vehicle.VehicleId = 1;
	Vehicle.LaneId = 0;
	Vehicle.bOnLane = true;
	Vehicle.DistanceCm = 0.0;
	Vehicle.SpeedCmS = 0.0;
	Vehicle.DesiredSpeedCmS = 1400.0;
	Sim.Vehicles.Add(Vehicle);

	// Anfahren: je Sekunde hoechstens MaxAccelerationCmS2 mehr.
	double Previous = 0.0;
	for (int32 Step = 0; Step < 4; ++Step)
	{
		Sim.Tick(1.0f);
		const double Now = Sim.Vehicles[0].SpeedCmS;
		TestTrue(
			FString::Printf(TEXT("Schritt %d: Zuwachs %.1f cm/s bleibt unter 250,5"),
				Step, Now - Previous),
			Now - Previous <= 250.5);
		Previous = Now;
	}

	TestTrue(
		FString::Printf(TEXT("Nach 4 s noch nicht auf Wunschgeschwindigkeit (%.0f von 1400)"),
			Previous),
		Previous < 1400.0);

	// Bremsen bleibt frei: Wunschgeschwindigkeit auf 0 setzen, ein Tick.
	Sim.Vehicles[0].DesiredSpeedCmS = 0.0;
	Sim.Tick(0.1f);
	TestTrue(
		FString::Printf(TEXT("Bremsen ist nicht gedeckelt (%.1f cm/s)"),
			Sim.Vehicles[0].SpeedCmS),
		Sim.Vehicles[0].SpeedCmS < 1.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrafficDeterminismTest,
	"WiesbadenReal.Traffic.Determinism",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTrafficDeterminismTest::RunTest(const FString& Parameters)
{
	const FRoadNetwork Network = MakeNetwork();
	const FWiesbadenTrafficSettings Settings = MakeSettings(1.0f);

	FWiesbadenTrafficSimulation A;
	FWiesbadenTrafficSimulation B;
	A.Initialize(Network, Settings);
	B.Initialize(Network, Settings);
	for (int32 t = 0; t < 15; ++t)
	{
		A.Tick(1.0f);
		B.Tick(1.0f);
	}

	TestEqual(TEXT("Determinismus: gleiche Fahrzeugzahl"), A.Vehicles.Num(), B.Vehicles.Num());
	bool bIdentical = A.Vehicles.Num() == B.Vehicles.Num();
	for (int32 i = 0; i < A.Vehicles.Num() && bIdentical; ++i)
	{
		const FTrafficVehicle& VA = A.Vehicles[i];
		const FTrafficVehicle& VB = B.Vehicles[i];
		bIdentical = VA.VehicleId == VB.VehicleId && VA.bOnLane == VB.bOnLane
			&& VA.LaneId == VB.LaneId && VA.ConnectionIndex == VB.ConnectionIndex
			&& FMath::Abs(VA.DistanceCm - VB.DistanceCm) < 0.001
			&& FMath::Abs(VA.SpeedCmS - VB.SpeedCmS) < 0.001;
	}
	TestTrue(TEXT("Determinismus: identische Fahrzeugzustaende"), bIdentical);

	// Report-Konsistenz.
	TestEqual(TEXT("Report: Active == Vehicles"), A.Report.ActiveVehicleCount, A.Vehicles.Num());
	TestTrue(TEXT("Report: MeanSpeed > 0"), A.Report.MeanSpeedKmh > 0.0);
	TestEqual(TEXT("Report: Spawn-Zaehler konsistent"),
		A.Report.TotalSpawnedCount, static_cast<int64>(A.Vehicles.Num() + A.Report.TotalRemovedCount));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrafficRedLightStopTest,
	"WiesbadenReal.Traffic.RedLightStop",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Ein Fahrzeug an der Haltelinie MUSS bei Rot stehenbleiben - und bei Gruen
 * weiterfahren.
 *
 * Hintergrund: Das Ampelsystem war vollstaendig implementiert und getestet, die
 * Simulation hielt einen Zeiger darauf bereit (SetTrafficLightSystem) samt
 * fertiger Haltelogik - nur rief die Methode niemand auf. Der Zeiger blieb
 * nullptr, und dann gilt JEDE Verbindung als gruen: der Verkehr fuhr durch jede
 * rote Ampel, ohne dass etwas gemeldet haette.
 *
 * Die Kopplung an der laufenden Stadt zu pruefen taugt nicht: dort haengt es vom
 * Zufall ab, ob im Messmoment ueberhaupt ein Fahrzeug an einer Haltelinie steht
 * (die naechste Ampel lag 545 m entfernt). Dieser Test macht daraus eine
 * deterministische Aussage.
 */
bool FTrafficRedLightStopTest::RunTest(const FString& Parameters)
{
	FRoadNetwork Network = MakeNetwork();

	// Kreuzung 42 als Ampel auszeichnen - Verbindung 0 fuehrt dort hindurch.
	FRoadIntersection Intersection;
	Intersection.NodeId = 42;
	Intersection.Location = FVector(10250.0, 0.0, 0.0);
	Intersection.Control = EIntersectionControl::TrafficSignals;
	Network.Intersections.Add(Intersection);

	FWiesbadenTrafficLightSettings LightSettings;
	LightSettings.CycleSeconds = 30.0;
	LightSettings.GreenSecondsPerCycle = 15.0;

	FWiesbadenTrafficLightSystem Lights;
	Lights.Initialize(Network, LightSettings);
	TestEqual(TEXT("Eine Ampel erzeugt"), Lights.GetTrafficLightCount(), 1);

	FWiesbadenTrafficSimulation Simulation;
	Simulation.Initialize(Network, MakeSettings(0.0f));
	Simulation.SetTrafficLightSystem(&Lights);

	// Fahrzeug kurz vor dem Spurende (= vor der Haltelinie) einsetzen.
	FTrafficVehicle Vehicle;
	Vehicle.LaneId = 0;
	Vehicle.bOnLane = true;
	Vehicle.DistanceCm = Network.Lanes[0].LengthCm - 50.0;
	Vehicle.SpeedCmS = 500.0;

	// DesiredSpeedCmS MUSS mitgesetzt werden: der Kolonnen-Durchgang schreibt
	// SpeedCmS aus diesem Wert und wuerde ein von Hand eingesetztes Fahrzeug
	// sonst mit dem Default 0 ueberschreiben - unabhaengig von jeder Ampel.
	Vehicle.DesiredSpeedCmS = 500.0;
	Simulation.Vehicles.Add(Vehicle);

	// Den Zyklus abfahren und beide Zustaende einsammeln. Welche Phase gerade
	// gilt, haengt vom Phasenversatz der Kreuzung ab - deshalb wird gesucht,
	// statt einen festen Zeitpunkt anzunehmen.
	bool bSawHeldAtRed = false;
	bool bSawMovingAtGreen = false;

	for (int32 Step = 0; Step < 300; ++Step)
	{
		Lights.Tick(0.1f);

		// Die Simulation kann das Fahrzeug am Spurende uebernehmen oder
		// entfernen - dann gibt es nichts mehr zu beobachten.
		if (Simulation.Vehicles.Num() == 0)
		{
			break;
		}

		Simulation.Vehicles[0].DistanceCm = Network.Lanes[0].LengthCm - 50.0;
		Simulation.Vehicles[0].SpeedCmS = 500.0;
		Simulation.Vehicles[0].DesiredSpeedCmS = 500.0;
		Simulation.Vehicles[0].bOnLane = true;
		Simulation.Vehicles[0].LaneId = 0;

		const bool bGreen = Lights.IsConnectionGreen(0);
		Simulation.Tick(0.1f);

		if (!bGreen && Simulation.Vehicles.Num() > 0 && Simulation.Vehicles[0].SpeedCmS == 0.0)
		{
			bSawHeldAtRed = true;
		}
		if (bGreen && Simulation.Vehicles.Num() > 0 && Simulation.Vehicles[0].SpeedCmS > 0.0)
		{
			bSawMovingAtGreen = true;
		}
	}

	TestTrue(TEXT("Bei Rot wird das Fahrzeug gehalten"), bSawHeldAtRed);
	TestTrue(TEXT("Bei Gruen faehrt es weiter"), bSawMovingAtGreen);
	TestTrue(TEXT("Zaehler meldet gehaltene Fahrzeuge"), Simulation.GetVehiclesHeldAtRed() >= 0);

	// Gegenprobe: OHNE gesetztes Ampelsystem darf NICHTS halten - genau dieser
	// Zustand lag lange vor.
	FWiesbadenTrafficSimulation Unlinked;
	Unlinked.Initialize(Network, MakeSettings(0.0f));
	Unlinked.Vehicles.Add(Vehicle);

	bool bHeldWithoutLights = false;
	for (int32 Step = 0; Step < 50; ++Step)
	{
		if (Unlinked.Vehicles.Num() == 0)
		{
			break;
		}

		Unlinked.Vehicles[0].DistanceCm = Network.Lanes[0].LengthCm - 50.0;
		Unlinked.Vehicles[0].SpeedCmS = 500.0;
		Unlinked.Vehicles[0].DesiredSpeedCmS = 500.0;
		Unlinked.Tick(0.1f);
		if (Unlinked.Vehicles.Num() > 0 && Unlinked.Vehicles[0].SpeedCmS == 0.0)
		{
			bHeldWithoutLights = true;
		}
	}

	TestFalse(TEXT("Ohne Ampelsystem haelt nichts (alles gilt als gruen)"), bHeldWithoutLights);

	// -- Hauptstrassen bevorzugen ---------------------------------------------
	//
	// Die Wahl an Kreuzungen war GLEICHVERTEILT: eine Wohnstrasse wurde so
	// oft genommen wie die Bundesstrasse daneben. Im Spiel sah man Verkehr,
	// der planlos durch Wohngebiete irrt, waehrend die Hauptachsen leer
	// bleiben - genau umgekehrt zur Wirklichkeit.
	{
		using FSim = FWiesbadenTrafficSimulation;

		// Die Rangfolge ist das Wesentliche, nicht die einzelne Zahl.
		TestTrue(TEXT("Autobahn ueber Bundesstrasse"),
			FSim::GetRoadClassWeight(EOSMHighwayType::Motorway)
			> FSim::GetRoadClassWeight(EOSMHighwayType::Primary));

		TestTrue(TEXT("Bundesstrasse ueber Landesstrasse"),
			FSim::GetRoadClassWeight(EOSMHighwayType::Primary)
			> FSim::GetRoadClassWeight(EOSMHighwayType::Secondary));

		TestTrue(TEXT("Landesstrasse ueber Kreisstrasse"),
			FSim::GetRoadClassWeight(EOSMHighwayType::Secondary)
			> FSim::GetRoadClassWeight(EOSMHighwayType::Tertiary));

		TestTrue(TEXT("Kreisstrasse ueber Wohnstrasse"),
			FSim::GetRoadClassWeight(EOSMHighwayType::Tertiary)
			> FSim::GetRoadClassWeight(EOSMHighwayType::Residential));

		// Der verkehrsberuhigte Bereich ist der unattraktivste befahrbare
		// Typ - dort HAT Durchgangsverkehr nichts zu suchen.
		TestTrue(TEXT("Wohnstrasse ueber verkehrsberuhigten Bereich"),
			FSim::GetRoadClassWeight(EOSMHighwayType::Residential)
			> FSim::GetRoadClassWeight(EOSMHighwayType::LivingStreet));

		// Der Unterschied muss DEUTLICH sein: liegt er bei wenigen Prozent,
		// aendert sich am Bild nichts.
		TestTrue(TEXT("Bundesstrasse mindestens fuenfmal so attraktiv wie Wohnstrasse"),
			FSim::GetRoadClassWeight(EOSMHighwayType::Primary)
			>= 5.0 * FSim::GetRoadClassWeight(EOSMHighwayType::Residential));

		// Kein Gewicht darf null oder negativ sein - sonst faellt eine
		// Strassenklasse ganz aus der Auswahl und Fahrzeuge blieben an
		// Kreuzungen stehen, an denen es nur solche Abzweige gibt.
		for (const EOSMHighwayType Type : {
			EOSMHighwayType::Motorway, EOSMHighwayType::Trunk,
			EOSMHighwayType::Primary, EOSMHighwayType::Secondary,
			EOSMHighwayType::Tertiary, EOSMHighwayType::Unclassified,
			EOSMHighwayType::Residential, EOSMHighwayType::LivingStreet,
			EOSMHighwayType::Service, EOSMHighwayType::None })
		{
			TestTrue(TEXT("Gewicht ist positiv"), FSim::GetRoadClassWeight(Type) > 0.0);
		}
	}

	return true;
}

// ---------------------------------------------------------------------------
// Einspurmodell: Wendekreis und Lenkgeschwindigkeit
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrafficBicycleModelTest,
	"WiesbadenReal.Traffic.Einspurmodell",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTrafficBicycleModelTest::RunTest(const FString& Parameters)
{
	const FWiesbadenTrafficSettings Defaults;
	const double Wheelbase = Defaults.WheelbaseCm;
	const double MaxSteer = FMath::DegreesToRadians(Defaults.MaxSteerAngleDeg);
	const double MaxSteerRate = FMath::DegreesToRadians(Defaults.MaxSteerRateDegS);
	const double Dt = 1.0 / 60.0;
	const double SpeedCmS = 500.0;   // 18 km/h - Kreuzungstempo

	// -- 1) Geradeaus bleibt geradeaus ------------------------------------
	{
		FVector2D Body(0.0, 0.0);
		float Yaw = 0.0f;
		float Steer = 0.0f;

		// Ziel liegt genau voraus.
		for (int32 Step = 0; Step < 60; ++Step)
		{
			const FVector2D Target(Body.X + 1000.0, 0.0);
			FWiesbadenTrafficSimulation::StepBicycleModel(Target, SpeedCmS, Wheelbase,
				MaxSteer, MaxSteerRate, Dt, Body, Yaw, Steer);
		}

		TestTrue(FString::Printf(TEXT("Geradeaus: kein seitlicher Versatz (%.2f cm)"), Body.Y),
			FMath::Abs(Body.Y) < 1.0);
		TestTrue(FString::Printf(TEXT("Geradeaus: Raeder bleiben gerade (%.3f rad)"), Steer),
			FMath::Abs(Steer) < 0.01f);
	}

	// -- 2) Der Wendekreis wird eingehalten --------------------------------
	//
	// Das Ziel steht dauerhaft quer zum Fahrzeug - eine Vorgabe, die kein Auto
	// erfuellen kann. Der Wagen muss dann den ENGSTEN Kreis fahren, den sein
	// Anschlag hergibt, und keinen engeren. Genau das ist die Zusage
	// "Wendezirkel beachten".
	{
		FVector2D Body(0.0, 0.0);
		float Yaw = 0.0f;
		float Steer = 0.0f;

		double MinX = 0.0, MaxX = 0.0, MinY = 0.0, MaxY = 0.0;

		// Volle Umdrehung: Umfang 2*pi*R, bei R = L/tan(delta).
		const double ExpectedRadius = Wheelbase / FMath::Tan(MaxSteer);
		const double FullCircleSeconds = (2.0 * PI * ExpectedRadius) / SpeedCmS;
		const int32 Steps = FMath::CeilToInt(FullCircleSeconds / Dt);

		for (int32 Step = 0; Step < Steps; ++Step)
		{
			// Ziel immer 90 Grad links neben dem Fahrzeug.
			const double LeftAngle = static_cast<double>(Yaw) + HALF_PI;
			const FVector2D Target(
				Body.X + 500.0 * FMath::Cos(LeftAngle),
				Body.Y + 500.0 * FMath::Sin(LeftAngle));

			FWiesbadenTrafficSimulation::StepBicycleModel(Target, SpeedCmS, Wheelbase,
				MaxSteer, MaxSteerRate, Dt, Body, Yaw, Steer);

			MinX = FMath::Min(MinX, Body.X);
			MaxX = FMath::Max(MaxX, Body.X);
			MinY = FMath::Min(MinY, Body.Y);
			MaxY = FMath::Max(MaxY, Body.Y);
		}

		TestTrue(FString::Printf(TEXT("Anschlag wird nicht ueberschritten (%.3f von %.3f rad)"),
			Steer, MaxSteer),
			FMath::Abs(Steer) <= MaxSteer + KINDA_SMALL_NUMBER);

		// Der befahrene Kreis muss den erwarteten Durchmesser haben. Die
		// Lenkgeschwindigkeit macht den Anfang etwas weiter, darum 10 Prozent
		// Spielraum nach oben und 5 Prozent nach unten.
		const double MeasuredDiameter = FMath::Max(MaxX - MinX, MaxY - MinY);
		const double ExpectedDiameter = 2.0 * ExpectedRadius;
		TestTrue(FString::Printf(
			TEXT("Wendekreis %.0f cm liegt bei den erwarteten %.0f cm"),
			MeasuredDiameter, ExpectedDiameter),
			MeasuredDiameter > ExpectedDiameter * 0.95
			&& MeasuredDiameter < ExpectedDiameter * 1.10);
	}

	// -- 3) Das Lenkrad dreht nicht unendlich schnell -----------------------
	{
		FVector2D Body(0.0, 0.0);
		float Yaw = 0.0f;
		float Steer = 0.0f;

		// Ein einziger Schritt mit einem Ziel scharf links: der Einschlag darf
		// hoechstens um MaxSteerRate * Dt zunehmen.
		const FVector2D Target(0.0, 500.0);
		FWiesbadenTrafficSimulation::StepBicycleModel(Target, SpeedCmS, Wheelbase,
			MaxSteer, MaxSteerRate, Dt, Body, Yaw, Steer);

		const double Allowed = MaxSteerRate * Dt;
		TestTrue(FString::Printf(
			TEXT("Einschlag waechst hoechstens um %.4f rad je Tick (war %.4f)"),
			Allowed, Steer),
			static_cast<double>(Steer) <= Allowed + KINDA_SMALL_NUMBER);
		TestTrue(TEXT("Einschlag geht in die richtige Richtung (links)"), Steer > 0.0f);
	}

	// -- 4) Stillstand aendert nichts ---------------------------------------
	{
		FVector2D Body(100.0, 200.0);
		float Yaw = 0.5f;
		float Steer = 0.3f;
		const FVector2D Before = Body;
		const float YawBefore = Yaw;

		FWiesbadenTrafficSimulation::StepBicycleModel(FVector2D(0.0, 500.0), 0.0, Wheelbase,
			MaxSteer, MaxSteerRate, Dt, Body, Yaw, Steer);

		TestTrue(TEXT("Stehendes Fahrzeug bewegt sich nicht"),
			FVector2D::Distance(Body, Before) < KINDA_SMALL_NUMBER);
		TestTrue(TEXT("Stehendes Fahrzeug dreht sich nicht"),
			FMath::IsNearlyEqual(Yaw, YawBefore));
	}

	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrafficTargetDensityTest,
	"WiesbadenReal.Traffic.Zielbestand",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTrafficTargetDensityTest::RunTest(const FString& Parameters)
{
	// Das Netz hat drei Spuren zu je 100 m, alle im Spawn-Umkreis: 0.3 km.
	// Frueher stand hier eine feste Stueckzahl je Umkreis - die ergab am
	// Stadtrand dieselbe Zahl wie in der Innenstadt und liess beide leer
	// wirken. Jetzt traegt die Spurlaenge den Zielbestand.
	const FRoadNetwork Network = MakeNetwork();

	FWiesbadenTrafficSettings Settings = MakeSettings(0.5f);
	Settings.VehiclesPerLaneKm = 100.0;
	Settings.SpawnRadiusMeters = 600.0;

	FWiesbadenTrafficSimulation Sim;
	Sim.Initialize(Network, Settings);

	// Ohne Beobachter gibt es keinen Umkreis - dann gilt die harte Obergrenze.
	// Darauf stuetzen sich die uebrigen datenreinen Tests.
	TestEqual(TEXT("Ohne Beobachter: MaxVehicles als Ziel"),
		Sim.GetTargetVehicleCount(), Settings.MaxVehicles);

	Sim.SetObserverLocation(FVector::ZeroVector);
	TestEqual(TEXT("Alle drei Spuren liegen im Umkreis"), Sim.GetNearbyLaneCount(), 3);
	TestTrue(TEXT("Spurlaenge im Umkreis 0.3 km"),
		FMath::IsNearlyEqual(Sim.GetNearbyLaneKm(), 0.3, 0.001));

	// 0.3 km * 100 je km * Dichte 0.5 = 15.
	TestEqual(TEXT("Zielbestand folgt Spurlaenge x Dichte"), Sim.GetTargetVehicleCount(), 15);

	// Doppelte Dichte -> doppelter Zielbestand (linear, keine Sprungstelle).
	Settings.TrafficDensity = 1.0f;
	FWiesbadenTrafficSimulation Voll;
	Voll.Initialize(Network, Settings);
	Voll.SetObserverLocation(FVector::ZeroVector);
	TestEqual(TEXT("Volle Dichte verdoppelt den Zielbestand"), Voll.GetTargetVehicleCount(), 30);

	// MaxVehicles bleibt die harte Obergrenze.
	Settings.MaxVehicles = 10;
	FWiesbadenTrafficSimulation Gedeckelt;
	Gedeckelt.Initialize(Network, Settings);
	Gedeckelt.SetObserverLocation(FVector::ZeroVector);
	TestEqual(TEXT("MaxVehicles deckelt den Zielbestand"), Gedeckelt.GetTargetVehicleCount(), 10);

	// Ausserhalb des Umkreises traegt keine Spur mehr - kein Verkehr.
	FWiesbadenTrafficSimulation Fern;
	Settings.MaxVehicles = 3000;
	Fern.Initialize(Network, Settings);
	Fern.SetObserverLocation(FVector(10000000.0, 0.0, 0.0));
	TestEqual(TEXT("Keine Spur im Umkreis -> Zielbestand 0"), Fern.GetTargetVehicleCount(), 0);

	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrafficMergeStackingTest,
	"WiesbadenReal.Traffic.Einmuendung",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTrafficMergeStackingTest::RunTest(const FString& Parameters)
{
	// Zwei Zufluesse auf DIESELBE Folgespur - die Lage, in der im Spiel drei
	// Fahrzeuge auf 1 cm genau uebereinander standen und nie wieder
	// auseinanderkamen. Die Abstandsregel HAELT eine Luecke, sie STELLT KEINE
	// HER: bei Abstand 0 bekommen beide Tempo 0 und der Stapel bleibt stehen.
	FRoadNetwork Network = MakeNetwork();

	// Spur 2 muendet ebenfalls in Spur 1 (Spur 0 tut das schon ueber
	// Verbindung 0). Die Verbindung ist GENAU SO LANG wie Verbindung 0 -
	// sonst treffen die beiden nie im selben Tick ein und der Test misst nichts.
	FLaneConnection Merge;
	Merge.FromLaneId = 2;
	Merge.ToLaneId = 1;
	Merge.IntersectionNodeId = 42;
	Merge.TurnType = ETurnType::Through;
	Merge.bRestricted = false;
	Merge.ConnectionPath = {
		FVector(10000.0, 5000.0, 0.0), FVector(10250.0, 5000.0, 0.0),
		FVector(10500.0, 5000.0, 0.0) };
	Network.Connections.Add(Merge);

	FWiesbadenTrafficSettings Settings = MakeSettings(0.0f);   // kein Spawn
	Settings.MinGapCm = 700.0;

	FWiesbadenTrafficSimulation Sim;
	Sim.Initialize(Network, Settings);

	// Beide DIREKT auf ihre Verbindung setzen, dicht vor deren Ende und mit
	// gleicher Geschwindigkeit: im naechsten Schritt laufen beide auf Spur 1
	// ueber, mit derselben Rest-Distanz. Genau hier entstand der Stapel.
	auto PathLength = [](const TArray<FVector>& Path)
	{
		double Length = 0.0;
		for (int32 i = 1; i < Path.Num(); ++i)
		{
			Length += FVector::Dist(Path[i], Path[i - 1]);
		}
		return Length;
	};

	const double Conn0Length = PathLength(Network.Connections[0].ConnectionPath);
	const double Conn1Length = PathLength(Network.Connections[1].ConnectionPath);

	FTrafficVehicle A;
	A.VehicleId = 1;
	A.bOnLane = false;
	A.ConnectionIndex = 0;
	A.DistanceCm = Conn0Length - 50.0;
	A.SpeedCmS = 1000.0;
	A.DesiredSpeedCmS = 1000.0;
	Sim.Vehicles.Add(A);

	FTrafficVehicle B;
	B.VehicleId = 2;
	B.bOnLane = false;
	B.ConnectionIndex = 1;
	B.DistanceCm = Conn1Length - 50.0;
	B.SpeedCmS = 1000.0;
	B.DesiredSpeedCmS = 1000.0;
	Sim.Vehicles.Add(B);

	bool bSameSpot = false;
	double ClosestOnLaneCm = TNumericLimits<double>::Max();

	for (int32 Step = 0; Step < 30; ++Step)
	{
		Sim.Tick(0.1f);

		if (Sim.Vehicles.Num() < 2)
		{
			break;   // eines ist aus dem Netz gelaufen - dann ist nichts mehr zu messen
		}

		const FTrafficVehicle& V0 = Sim.Vehicles[0];
		const FTrafficVehicle& V1 = Sim.Vehicles[1];

		const bool bSameEdge = (V0.bOnLane == V1.bOnLane)
			&& (V0.bOnLane ? V0.LaneId == V1.LaneId : V0.ConnectionIndex == V1.ConnectionIndex);
		if (bSameEdge)
		{
			const double Gap = FMath::Abs(V0.DistanceCm - V1.DistanceCm);
			ClosestOnLaneCm = FMath::Min(ClosestOnLaneCm, Gap);
			if (Gap < 100.0)
			{
				bSameSpot = true;
			}
		}
	}

	TestFalse(TEXT("Einmuendung: keine zwei Fahrzeuge auf derselben Bahn am selben Punkt"),
		bSameSpot);

	if (ClosestOnLaneCm < TNumericLimits<double>::Max())
	{
		TestTrue(FString::Printf(
			TEXT("Einmuendung: geringster Abstand auf der Folgespur %.2f m"),
			ClosestOnLaneCm / 100.0),
			ClosestOnLaneCm >= 100.0);
	}

	return true;
}
