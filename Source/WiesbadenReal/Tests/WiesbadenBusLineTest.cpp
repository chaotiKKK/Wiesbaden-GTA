// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "World/WiesbadenBusLine.h"

namespace
{
	WiesbadenBusLine::FBusRoute MakeRoute()
	{
		WiesbadenBusLine::FBusRoute R;
		R.StopArcCm = { 0.0, 1000.0, 3000.0 };
		R.TotalLengthCm = 3000.0;
		return R;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWiesbadenBusLineTest,
	"WiesbadenReal.Traffic.BusLine",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FWiesbadenBusLineTest::RunTest(const FString& Parameters)
{
	using namespace WiesbadenBusLine;
	const FBusRoute R = MakeRoute();
	const double v = 500.0, dwell = 2.0, term = 4.0;
	// Phasen: Hin seg0(2s) dwell1(2s) seg1(4s) termA(4s), Rueck seg(4s) dwell1(2s) seg(2s) termB(4s) = 24 s.

	TestTrue(TEXT("Rundfahrtdauer 24 s"),
		FMath::IsNearlyEqual(RoundTripSeconds(R, v, dwell, term), 24.0, 0.01));

	FBusState s0 = EvaluateRoundTrip(0.0, R, v, dwell, term);
	TestTrue(TEXT("t=0 an Halte 0, vorwaerts, faehrt"),
		FMath::IsNearlyEqual(s0.ArcLengthCm, 0.0, 1.0) && s0.bForward && !s0.bDwelling);

	TestTrue(TEXT("t=1 Mitte Segment 0 (arc 500)"),
		FMath::IsNearlyEqual(EvaluateRoundTrip(1.0, R, v, dwell, term).ArcLengthCm, 500.0, 1.0));

	FBusState s3 = EvaluateRoundTrip(3.0, R, v, dwell, term);
	TestTrue(TEXT("t=3 verweilt an Halte 1"),
		FMath::IsNearlyEqual(s3.ArcLengthCm, 1000.0, 1.0) && s3.bDwelling);

	TestTrue(TEXT("t=6 Mitte Segment 1 (arc 2000)"),
		FMath::IsNearlyEqual(EvaluateRoundTrip(6.0, R, v, dwell, term).ArcLengthCm, 2000.0, 1.0));

	FBusState s10 = EvaluateRoundTrip(10.0, R, v, dwell, term);
	TestTrue(TEXT("t=10 verweilt am Terminus"),
		FMath::IsNearlyEqual(s10.ArcLengthCm, 3000.0, 1.0) && s10.bDwelling && s10.bForward);

	FBusState s14 = EvaluateRoundTrip(14.0, R, v, dwell, term);
	TestTrue(TEXT("t=14 Rueckfahrt (arc 2000, rueckwaerts)"),
		FMath::IsNearlyEqual(s14.ArcLengthCm, 2000.0, 1.0) && !s14.bForward);

	FBusState a = EvaluateRoundTrip(5.0, R, v, dwell, term);
	FBusState b = EvaluateRoundTrip(29.0, R, v, dwell, term);
	TestTrue(TEXT("periodisch: t und t+24 identisch"),
		FMath::IsNearlyEqual(a.ArcLengthCm, b.ArcLengthCm, 0.01) && a.bForward == b.bForward);

	bool bInBounds = true;
	for (int32 i = 0; i <= 96; ++i)
	{
		const double arc = EvaluateRoundTrip(i * 0.5, R, v, dwell, term).ArcLengthCm;
		bInBounds = bInBounds && arc >= -0.5 && arc <= 3000.5;
	}
	TestTrue(TEXT("Bogenlaenge stets in [0, Gesamtlaenge]"), bInBounds);

	// --- Echter Fahrplan: ActiveRuns ---
	{
		FBusSchedule Sched;
		Sched.DepartureSeconds = { 0.0, 100.0, 200.0 };
		Sched.DaySeconds = 1000.0;
		const double RT = 120.0;   // Rundfahrtdauer
		TArray<FBusRun> Runs;

		ActiveRuns(250.0, Sched, RT, Runs);
		TestEqual(TEXT("t=250: genau 1 Kurs unterwegs"), Runs.Num(), 1);
		if (Runs.Num() == 1)
		{
			TestTrue(TEXT("t=250: Kurs 'Abfahrt 200' seit 50 s, Index 2"),
				FMath::IsNearlyEqual(Runs[0].Elapsed, 50.0, 0.01) && Runs[0].Index == 2);
		}

		ActiveRuns(210.0, Sched, RT, Runs);
		TestEqual(TEXT("t=210: 2 Kurse gleichzeitig (dichter Takt)"), Runs.Num(), 2);

		ActiveRuns(700.0, Sched, RT, Runs);
		TestEqual(TEXT("t=700: Betriebspause -> kein Kurs"), Runs.Num(), 0);

		// Mitternacht: Abfahrt kurz vor Tagesende ist im Folgetag noch unterwegs.
		FBusSchedule Night;
		Night.DepartureSeconds = { 950.0 };
		Night.DaySeconds = 1000.0;
		ActiveRuns(1010.0, Night, RT, Runs);
		TestEqual(TEXT("Mitternacht: 1 Kurs vom Vortag noch unterwegs"), Runs.Num(), 1);
		if (Runs.Num() == 1)
		{
			TestTrue(TEXT("Mitternacht: seit 60 s unterwegs"),
				FMath::IsNearlyEqual(Runs[0].Elapsed, 60.0, 0.01));
		}

		FBusSchedule Empty;
		ActiveRuns(100.0, Empty, RT, Runs);
		TestEqual(TEXT("leerer Fahrplan -> kein Kurs"), Runs.Num(), 0);
	}

	// --- Haltebucht: BayFactor ---
	{
		const TArray<double> Stops = { 0.0, 1000.0, 3000.0 };
		const double Zone = 500.0;
		TestTrue(TEXT("Bay: an der Halte -> 1"),
			FMath::IsNearlyEqual(BayFactor(0.0, false, Stops, Zone), 1.0, 0.001));
		TestTrue(TEXT("Bay: Verweilen -> 1 (egal wo)"),
			FMath::IsNearlyEqual(BayFactor(500.0, true, Stops, Zone), 1.0, 0.001));
		TestTrue(TEXT("Bay: halbe Zone -> 0.5 (smoothstep)"),
			FMath::IsNearlyEqual(BayFactor(250.0, false, Stops, Zone), 0.5, 0.001));
		TestTrue(TEXT("Bay: ab Zone -> 0"),
			FMath::IsNearlyEqual(BayFactor(500.0, false, Stops, Zone), 0.0, 0.001));
		TestTrue(TEXT("Bay: monoton fallend mit Abstand"),
			BayFactor(100.0, false, Stops, Zone) > BayFactor(300.0, false, Stops, Zone));
		TestTrue(TEXT("Bay: kurz vor Halte 1000 wieder hoch"),
			BayFactor(950.0, false, Stops, Zone) > 0.5);
	}

	// --- Abfahrtsmonitor: SecondsToStop + NextDepartures ---
	{
		// Route {0,1000,3000}, v=500, dwell=2.
		TestTrue(TEXT("SecondsToStop(0) = 0"),
			FMath::IsNearlyEqual(SecondsToStop(R, v, dwell, 0), 0.0, 0.01));
		TestTrue(TEXT("SecondsToStop(1) = 2 (Fahrt 0->1)"),
			FMath::IsNearlyEqual(SecondsToStop(R, v, dwell, 1), 2.0, 0.01));
		TestTrue(TEXT("SecondsToStop(2) = 8 (2 + Halt + 4)"),
			FMath::IsNearlyEqual(SecondsToStop(R, v, dwell, 2), 8.0, 0.01));

		FBusSchedule Sched; Sched.DepartureSeconds = { 0.0, 600.0, 1200.0 }; Sched.DaySeconds = 86400.0;
		TArray<double> Until;
		NextDepartures(100.0, Sched, 0.0, 2, Until);
		TestEqual(TEXT("2 naechste Abfahrten am Terminus"), Until.Num(), 2);
		if (Until.Num() == 2)
		{
			TestTrue(TEXT("naechste in 500 s"), FMath::IsNearlyEqual(Until[0], 500.0, 0.01));
			TestTrue(TEXT("uebernaechste in 1100 s"), FMath::IsNearlyEqual(Until[1], 1100.0, 0.01));
		}
		NextDepartures(20.0, Sched, 50.0, 1, Until);
		TestTrue(TEXT("Zwischenhalte (Offset 50): Durchfahrt in 30 s"),
			Until.Num() == 1 && FMath::IsNearlyEqual(Until[0], 30.0, 0.01));
		NextDepartures(0.0, Sched, 0.0, 3, Until);
		TestTrue(TEXT("Restzeiten aufsteigend sortiert"),
			Until.Num() == 3 && Until[0] <= Until[1] && Until[1] <= Until[2]);
	}

	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWiesbadenBusNextStopTest,
	"WiesbadenReal.Traffic.BusNextStop",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

// Naechste anzusagende Halte (Halteansagen). Halte 100 m auseinander, damit der
// 50-cm-Eps (nur um "an einer Halte -> naechste ansagen" abzudecken) vernachlaessigbar ist.
bool FWiesbadenBusNextStopTest::RunTest(const FString& Parameters)
{
	using WiesbadenBusLine::NextStopIndex;
	const TArray<double> Stops = { 0.0, 10000.0, 20000.0, 30000.0 };   // 4 Halte, 100 m Abstand

	// Hinfahrt: an/zwischen Halten immer die naechste voraus.
	TestEqual(TEXT("hin, an Halt 0 -> 1"), NextStopIndex(0.0, true, Stops), 1);
	TestEqual(TEXT("hin, zwischen 0 und 1 -> 1"), NextStopIndex(5000.0, true, Stops), 1);
	TestEqual(TEXT("hin, an Halt 1 -> 2"), NextStopIndex(10000.0, true, Stops), 2);
	TestEqual(TEXT("hin, kurz vor 3 -> 3"), NextStopIndex(29000.0, true, Stops), 3);
	TestEqual(TEXT("hin, am Terminus -> Endhalte"), NextStopIndex(30000.0, true, Stops), 3);

	// Rueckfahrt: die naechste ist die Halte mit kleinerem Bogen.
	TestEqual(TEXT("rueck, am Terminus 3 -> 2"), NextStopIndex(30000.0, false, Stops), 2);
	TestEqual(TEXT("rueck, zwischen 2 und 1 -> 1"), NextStopIndex(15000.0, false, Stops), 1);
	TestEqual(TEXT("rueck, an Halt 0 -> 0"), NextStopIndex(0.0, false, Stops), 0);

	// Entartet: < 2 Halte -> keine Ansage.
	const TArray<double> One = { 5000.0 };
	TestEqual(TEXT("eine Halte -> -1"), NextStopIndex(0.0, true, One), -1);

	return true;
}
