// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "GIS/WiesbadenTrafficSimulation.h"

/**
 * Fahrbild des Stadtverkehrs (25.09.): die Sollposition bremst vorausschauend
 * und fahrbar, Spurwechsel sind ein weicher S-Bogen.
 *
 * Vorher loeste die Abstandsregel nur den naechsten Tick exakt - die
 * Sollposition bremste in EINEM Tick von 50 auf 0, die Physik-Karosserie
 * schwang nach und wurde vom Sicherheitsnetz quer zurueckgezogen (gemessen am
 * Bahnhofsplatz: 38-41 cm seitliches Nachziehen je Fahrzeug-Sekunde).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDrivingSmoothnessTest,
	"WiesbadenReal.Traffic.Fahrbild",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDrivingSmoothnessTest::RunTest(const FString& Parameters)
{
	using FSim = FWiesbadenTrafficSimulation;
	constexpr double MinGap = 700.0, B = 350.0, Tau = 0.6;

	// -- Gipps: Stillstand, Gleichgewicht ---------------------------------------
	TestEqual(TEXT("Stand auf Mindestabstand: 0"), FSim::SafeFollowSpeedCmS(MinGap, MinGap, 0.0, 0.0, B, Tau), 0.0);
	TestEqual(TEXT("zu dicht: 0"), FSim::SafeFollowSpeedCmS(MinGap - 100.0, MinGap, 500.0, 0.0, B, Tau), 0.0);
	{
		const double V = 1389.0;   // 50 km/h
		const double Gap = MinGap + 1.5 * V * Tau;
		const double Safe = FSim::SafeFollowSpeedCmS(Gap, MinGap, V, V, B, Tau);
		TestTrue(FString::Printf(TEXT("Gleichgewicht bei %.0f cm Abstand: %.0f ~ %.0f cm/s"), Gap, Safe, V),
			FMath::Abs(Safe - V) < 0.02 * V);
	}

	// -- Auf ein stehendes Auto zufahren: fahrbar bremsen, nicht auffahren -------
	{
		const double Dt = 1.0 / 60.0;
		double Pos = 0.0, V = 1389.0, MaxDecel = 0.0;
		const double LeaderPos = 6000.0;   // steht 60 m voraus
		for (int32 Step = 0; Step < 60 * 20; ++Step)
		{
			const double Gap = LeaderPos - Pos;
			const double Exact = FMath::Max(0.0, (Gap - MinGap) / Dt);
			const double NewV = FMath::Min(FMath::Min(1389.0, Exact),
				FSim::SafeFollowSpeedCmS(Gap, MinGap, V, 0.0, B, Tau));
			MaxDecel = FMath::Max(MaxDecel, (V - NewV) / Dt);
			V = NewV;
			Pos += V * Dt;
		}
		TestTrue(FString::Printf(TEXT("haelt hinter dem Stehenden (Abstand %.0f cm)"), 6000.0 - Pos),
			6000.0 - Pos >= MinGap - 1.0 && V < 1.0);
		TestTrue(FString::Printf(TEXT("fahrbar gebremst: hoechstens %.0f cm/s2"), MaxDecel), MaxDecel <= 1.3 * B);
	}

	// -- Weicher Spurwechsel ------------------------------------------------------
	{
		constexpr double Start = 350.0, T = 3.5;
		TestEqual(TEXT("Beginn: voller Versatz"), FSim::LaneShiftOffsetCm(Start, 0.0, T), Start);
		TestEqual(TEXT("Ende: kein Versatz"), FSim::LaneShiftOffsetCm(Start, T, T), 0.0);
		double Prev = Start, MaxLat = 0.0;
		bool bMonotone = true;
		for (double t = 0.01; t <= T; t += 0.01)
		{
			const double O = FSim::LaneShiftOffsetCm(Start, t, T);
			bMonotone = bMonotone && O <= Prev + 1e-9;
			MaxLat = FMath::Max(MaxLat, (Prev - O) / 0.01);
			Prev = O;
		}
		TestTrue(TEXT("Versatz nimmt stetig ab"), bMonotone);
		TestTrue(FString::Printf(TEXT("Quertempo hoechstens %.0f cm/s (1,875 * Versatz / Dauer)"), MaxLat),
			MaxLat <= 1.9 * Start / T);
	}

	// -- Zu eng nebeneinander ist auch ein Konflikt ------------------------------
	{
		FLaneConnection PathA, PathB;
		PathA.FromLaneId = 1; PathA.ToLaneId = 3;
		PathB.FromLaneId = 2; PathB.ToLaneId = 4;
		PathA.ConnectionPath = { FVector(0, 0, 0), FVector(1000, 0, 0) };
		PathB.ConnectionPath = { FVector(0, 130, 0), FVector(1000, 130, 0) };   // 1,3 m daneben
		double ClearA = 0.0, ClearB = 0.0;
		TestTrue(TEXT("1,3 m nebeneinander: Konflikt"), FSim::FindPathProximity(PathA, PathB, 185.0, ClearA, ClearB));
		TestTrue(FString::Printf(TEXT("frei erst am Ende (%.0f / %.0f cm)"), ClearA, ClearB),
			FMath::IsNearlyEqual(ClearA, 1000.0, 1.0) && FMath::IsNearlyEqual(ClearB, 1000.0, 1.0));
		PathB.ConnectionPath = { FVector(0, 350, 0), FVector(1000, 350, 0) };   // eine Spurbreite daneben
		TestFalse(TEXT("3,5 m nebeneinander: frei"), FSim::FindPathProximity(PathA, PathB, 185.0, ClearA, ClearB));
		PathB.ConnectionPath = { FVector(0, 130, 0), FVector(1000, 130, 0) };
		PathB.FromLaneId = 1;
		TestFalse(TEXT("aus derselben Spur: regelt die Folgeregel"), FSim::FindPathProximity(PathA, PathB, 185.0, ClearA, ClearB));
	}

	// -- Projektion auf eine Polylinie ------------------------------------------
	{
		const TArray<FVector> L = { FVector(0, 0, 0), FVector(1000, 0, 0), FVector(1000, 1000, 0) };
		TestTrue(TEXT("neben dem ersten Stueck"),
			FMath::IsNearlyEqual(FSim::ProjectOntoPolylineCm(L, FVector(400, 300, 0)), 400.0, 0.5));
		TestTrue(TEXT("neben dem zweiten Stueck"),
			FMath::IsNearlyEqual(FSim::ProjectOntoPolylineCm(L, FVector(1300, 600, 0)), 1600.0, 0.5));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBodyOnPathTest,
	"WiesbadenReal.Traffic.KarosserieAufDerBahn",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Die Karosserie bleibt auf der Fahrlinie - auch in einer engen Abbiegung -
 * und steht danach gerade in ihrer Spur. Vorher lenkte sie frei nach, schnitt
 * die Kurve, wurde quer zurueckgezogen und blieb schraeg stehen.
 */
bool FBodyOnPathTest::RunTest(const FString& Parameters)
{
	// Spur 0 nach +X, Viertelkreis (Radius 800 cm) nach +Y, Spur 1 nach +Y.
	auto MakeLane = [](int32 Id, const TArray<FVector>& Line)
	{
		FRoadLane Lane;
		Lane.LaneId = Id;
		Lane.SegmentId = Id;
		Lane.Centerline = Line;
		for (int32 i = 1; i < Line.Num(); ++i)
		{
			Lane.LengthCm += FVector::Dist(Line[i], Line[i - 1]);
		}
		return Lane;
	};
	FRoadNetwork Network;
	Network.Lanes.Add(MakeLane(0, { FVector(0, 0, 0), FVector(10000, 0, 0) }));
	Network.Lanes.Add(MakeLane(1, { FVector(10800, 800, 0), FVector(10800, 20800, 0) }));
	FLaneConnection Turn;
	Turn.FromLaneId = 0;
	Turn.ToLaneId = 1;
	Turn.IntersectionNodeId = 5;
	Turn.TurnType = ETurnType::Left;
	TArray<FVector> Arc;
	for (int32 i = 0; i <= 16; ++i)
	{
		const double A = -HALF_PI + HALF_PI * i / 16.0;
		Arc.Add(FVector(10000.0 + 800.0 * FMath::Cos(A), 800.0 + 800.0 * FMath::Sin(A), 0.0));
	}
	Turn.ConnectionPath = Arc;
	Network.Connections.Add(Turn);
	for (int32 i = 0; i < 2; ++i)
	{
		FRoadSegment Segment;
		Segment.SegmentId = i;
		Segment.HighwayType = EOSMHighwayType::Residential;
		Segment.LengthCm = 20000.0;
		Network.Segments.Add(Segment);
	}

	FWiesbadenTrafficSettings Settings;
	Settings.TrafficDensity = 0.0f;
	Settings.bAllowLaneChange = false;
	FWiesbadenTrafficSimulation Sim;
	Sim.Initialize(Network, Settings);

	FTrafficVehicle V;
	V.VehicleId = 1;
	V.TypeIndex = 0;   // Golf
	V.LaneId = 0;
	V.bOnLane = true;
	V.DistanceCm = 7000.0;
	V.SpeedCmS = 600.0;       // ~22 km/h
	V.DesiredSpeedCmS = 600.0;
	Sim.Vehicles.Add(V);

	// Die ganze Fahrlinie als eine Polylinie, zum Messen des Abstands.
	TArray<FVector> Line = { FVector(0, 0, 0) };
	Line.Append(Arc);
	Line.Add(FVector(10800, 20800, 0));
	const auto DistanceToLine = [&Line](const FVector& P)
	{
		double Best = TNumericLimits<double>::Max();
		for (int32 i = 0; i + 1 < Line.Num(); ++i)
		{
			Best = FMath::Min(Best, FMath::PointDistToSegment(FVector(P.X, P.Y, 0), Line[i], Line[i + 1]));
		}
		return Best;
	};

	double MaxOff = 0.0;
	bool bSawTurn = false;
	for (int32 Step = 0; Step < 60 * 10 && Sim.Vehicles.Num() > 0; ++Step)
	{
		Sim.Vehicles[0].DesiredSpeedCmS = 600.0;
		Sim.Tick(1.0f / 60.0f);
		const FTrafficVehicle& Now = Sim.Vehicles[0];
		if (Now.bBodyInitialized)
		{
			MaxOff = FMath::Max(MaxOff, DistanceToLine(Now.BodyLocation));
		}
		bSawTurn = bSawTurn || !Now.bOnLane;
	}
	TestTrue(TEXT("durch die Abbiegung gefahren"), bSawTurn);
	TestTrue(FString::Printf(TEXT("Karosserie auf der Linie: hoechstens %.0f cm daneben"), MaxOff), MaxOff < 30.0);
	if (Sim.Vehicles.Num() > 0)
	{
		const FTrafficVehicle& End = Sim.Vehicles[0];
		const double YawDeg = FMath::RadiansToDegrees(End.BodyYawRad);
		TestTrue(FString::Printf(TEXT("nach der Kurve gerade in der Spur (%.1f Grad, Soll 90)"), YawDeg),
			End.bOnLane && End.LaneId == 1 && FMath::Abs(FMath::FindDeltaAngleDegrees(YawDeg, 90.0)) < 3.0);
		TestTrue(FString::Printf(TEXT("Physik faehrt mit (%.0f cm/s)"), End.BodySpeedCmS), End.BodySpeedCmS > 300.0);
	}
	return true;
}
