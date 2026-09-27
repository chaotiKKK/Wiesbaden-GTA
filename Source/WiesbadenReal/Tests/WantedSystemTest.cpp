// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "NPC/WiesbadenWanted.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWantedSystemTest,
	"WiesbadenReal.Polizei.Wanted",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FWantedSystemTest::RunTest(const FString& Parameters)
{
	FWiesbadenWantedParams P;

	// Stufe 0 ohne Punkte, Stufe folgt aus dem Konto.
	TestEqual(TEXT("Start bei Stufe 0"), FWiesbadenWanted::LevelOf(P, 0.0), 0);
	TestEqual(TEXT("Schwelle 1"), FWiesbadenWanted::LevelOf(P, P.LevelThresholds[0] - 0.01), 0);
	TestEqual(TEXT("Stufe 1 exakt"), FWiesbadenWanted::LevelOf(P, P.LevelThresholds[0]), 1);
	TestEqual(TEXT("Stufe 3"), FWiesbadenWanted::LevelOf(P, P.LevelThresholds[2]), 3);
	TestEqual(TEXT("Stufe 6 exakt"), FWiesbadenWanted::LevelOf(P, P.LevelThresholds[5]), 6);
	TestTrue(TEXT("Stufe 6 gedeckelt"),
		FWiesbadenWanted::LevelOf(P, P.MaxPoints) <= 6);

	// Ein Schuss allein loest noch keine Stufe aus (25 < 20? Doch: 25 >= 20).
	{
		FWiesbadenWantedState S;
		S = FWiesbadenWanted::AddEvent(S, P, EWiesbadenCrimeEvent::ShotFired);
		TestEqual(TEXT("Ein Schuss = 25 Punkte"), S.Points, P.ShotFiredPoints);
		TestEqual(TEXT("Ein Schuss = Stufe 1"), S.Level, 1);
		TestEqual(TEXT("Grace startet neu"), S.SecondsSinceEvent, 0.0);
	}

	// Gewichtungs-Reihenfolge: Schuss < Passant < Fahrzeug < Beamter.
	{
		FWiesbadenWantedState A, B, C, D;
		A = FWiesbadenWanted::AddEvent(A, P, EWiesbadenCrimeEvent::ShotFired);
		B = FWiesbadenWanted::AddEvent(B, P, EWiesbadenCrimeEvent::PedestrianDowned);
		C = FWiesbadenWanted::AddEvent(C, P, EWiesbadenCrimeEvent::VehicleDestroyed);
		D = FWiesbadenWanted::AddEvent(D, P, EWiesbadenCrimeEvent::OfficerHit);
		TestTrue(TEXT("Schuss < Passant"), A.Points < B.Points);
		TestTrue(TEXT("Passant < Fahrzeug"), B.Points < C.Points);
		TestTrue(TEXT("Fahrzeug < Beamter"), C.Points < D.Points);
	}

	// Mehrere Passanten summieren sich; Konto deckelt am Maximum.
	{
		FWiesbadenWantedState S;
		for (int32 i = 0; i < 20; ++i)
		{
			S = FWiesbadenWanted::AddEvent(S, P, EWiesbadenCrimeEvent::PedestrianDowned);
		}
		TestEqual(TEXT("Konto am Maximum"), S.Points, P.MaxPoints);
	}

	// Abbau: erst nach der Grace, dann linear, Stufe folgt nach unten.
	{
		FWiesbadenWantedState S;
		S = FWiesbadenWanted::AddEvent(S, P, EWiesbadenCrimeEvent::VehicleDestroyed);
		const double VorGrace = S.Points;

		// Grace-Zeit unveranderter Punktstand (Stufe bleibt).
		S = FWiesbadenWanted::Step(S, P, P.DecayGraceSeconds - 1.0);
		TestEqual(TEXT("In der Grace kein Abbau"), S.Points, VorGrace);

		// Ein Tick knapp ueber der Grace: genau ein Delta Abbau.
		const double Knapp = P.DecayGraceSeconds + 0.1;
		S = FWiesbadenWanted::Step(S, P, Knapp);
		TestTrue(FString::Printf(TEXT("Nach Grace abgebaut (%.2f -> %.2f)"), VorGrace, S.Points),
			S.Points < VorGrace);
		TestEqual(TEXT("Ein Delta Abbaubetrag"), S.Points, VorGrace - P.DecayPointsPerSecond * Knapp);

		// Lange ohne Tat: Konto leer, Stufe 0.
		for (int32 i = 0; i < 400; ++i)
		{
			S = FWiesbadenWanted::Step(S, P, 1.0);
		}
		TestEqual(TEXT("Konto leer"), S.Points, 0.0);
		TestEqual(TEXT("Stufe zurueck auf 0"), S.Level, 0);
	}

	// Neue Tat setzt Grace zurueck: Abbau startet nicht zwischen Schuessen.
	{
		FWiesbadenWantedState S;
		S = FWiesbadenWanted::AddEvent(S, P, EWiesbadenCrimeEvent::ShotFired);
		S = FWiesbadenWanted::Step(S, P, P.DecayGraceSeconds - 0.5);
		S = FWiesbadenWanted::AddEvent(S, P, EWiesbadenCrimeEvent::ShotFired);
		TestEqual(TEXT("Grace zurueckgesetzt"), S.SecondsSinceEvent, 0.0);
		S = FWiesbadenWanted::Step(S, P, 1.0);
		TestTrue(TEXT("Kein Abbau frisch nach Tat"),
			FMath::IsNearlyEqual(S.Points,
				FMath::Min(2.0 * P.ShotFiredPoints, P.MaxPoints), 0.001));
	}

	return true;
}
