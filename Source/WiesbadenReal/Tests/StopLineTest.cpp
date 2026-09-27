// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "GIS/WiesbadenTrafficSimulation.h"

/**
 * Haltelinie je Zufahrt (FWiesbadenTrafficSimulation::ComputeStopSetbackCm).
 *
 * Vorher hielt jede Zufahrt pauschal 350 cm vor ihrem Spurende. Am
 * Bahnhofsplatz lagen so je Diagnose 13-19 Fahrzeugpaare ineinander, ALLE an
 * Kreuzungen: der Wartende der einen Zufahrt stand im Wartebereich der
 * anderen oder im Weg der Abbieger. Die Haltelinie wandert jetzt so weit
 * zurueck, bis die Grundflaeche frei ist.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStopLineTest,
	"WiesbadenReal.Traffic.StopLines",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FStopLineTest::RunTest(const FString& Parameters)
{
	using FSim = FWiesbadenTrafficSimulation;
	// Zufahrt entlang +X, Spurende im Ursprung, 30 m lang.
	const TArray<FVector> Approach = { FVector(-3000.0, 0.0, 0.0), FVector(0.0, 0.0, 0.0) };
	constexpr double Length = 3000.0;
	constexpr double Base = 350.0, Max = 1500.0, Step = 50.0;
	constexpr double HalfL = 245.0, HalfW = 95.0;   // T6
	bool bResolved = false;

	// Nichts im Weg: Grundwert.
	TestEqual(TEXT("ohne Hindernis: 350 cm"),
		FSim::ComputeStopSetbackCm(Approach, Length, {}, Base, Max, Step, HalfL, HalfW, HalfW, bResolved), Base);
	TestTrue(TEXT("ohne Hindernis: geloest"), bResolved);

	// Querweg 1 m hinter dem Spurende: die Front (350 - 245 = 105 cm vor dem
	// Ende) bleibt davor - Grundwert.
	{
		const TArray<FSim::FStopObstacle> Obstacles = { { FVector(100.0, -2000.0, 0.0), FVector(100.0, 2000.0, 0.0) } };
		TestEqual(TEXT("Querweg hinter der Linie: 350 cm"),
			FSim::ComputeStopSetbackCm(Approach, Length, Obstacles, Base, Max, Step, HalfL, HalfW, HalfW, bResolved), Base);
	}

	// Querweg 3 m VOR dem Spurende (Spur reicht in den Knoten): zurueck, bis die
	// Front vor dem Streifen liegt - Mitte < -300 - 95 - 245 = -640 -> 650 cm.
	{
		const TArray<FSim::FStopObstacle> Obstacles = { { FVector(-300.0, -2000.0, 0.0), FVector(-300.0, 2000.0, 0.0) } };
		TestEqual(TEXT("Querweg ueber der Spur: 650 cm"),
			FSim::ComputeStopSetbackCm(Approach, Length, Obstacles, Base, Max, Step, HalfL, HalfW, HalfW, bResolved), 650.0);
		TestTrue(TEXT("Querweg ueber der Spur: geloest"), bResolved);
	}

	// Spitz zulaufender Nachbararm (30 Grad), der am Spurende auf 1 m heranreicht:
	// die Wartenden stuenden ineinander. Zurueck, bis der Abstand quer reicht.
	{
		const FVector End(0.0, 100.0, 0.0);
		const FVector Dir = FVector(-FMath::Cos(PI / 6.0), FMath::Sin(PI / 6.0), 0.0);
		const TArray<FSim::FStopObstacle> Obstacles = { { End + Dir * 1500.0, End } };
		const double Setback = FSim::ComputeStopSetbackCm(Approach, Length, Obstacles, Base, Max, Step, HalfL, HalfW, HalfW, bResolved);
		TestTrue(FString::Printf(TEXT("spitzer Nachbararm: zurueckversetzt (%.0f cm)"), Setback), Setback > Base && bResolved);
		// Gegenprobe: an der gefundenen Stelle frei, 50 cm weiter vorn nicht.
		const FVector Center(-Setback, 0.0, 0.0);
		const FVector Ahead(-(Setback - Step), 0.0, 0.0);
		const FVector Mid = 0.5 * (Obstacles[0].From + Obstacles[0].To);
		const double HalfPiece = 0.5 * FVector::Dist2D(Obstacles[0].From, Obstacles[0].To);
		TestFalse(TEXT("spitzer Nachbararm: an der Haltelinie frei"),
			FSim::AreBoxesOverlapping(Center, FVector::ForwardVector, HalfL, HalfW, Mid, -Dir, HalfPiece, HalfW));
		TestTrue(TEXT("spitzer Nachbararm: 50 cm weiter vorn im Weg"),
			FSim::AreBoxesOverlapping(Ahead, FVector::ForwardVector, HalfL, HalfW, Mid, -Dir, HalfPiece, HalfW));
	}

	// Zusammenfuehrung, die 30 m eng parallel laeuft: keine freie Stelle -
	// dann bleibt der Grundwert (weiter zurueck hilft nichts).
	{
		const TArray<FSim::FStopObstacle> Obstacles = { { FVector(-3000.0, 150.0, 0.0), FVector(0.0, 150.0, 0.0) } };
		TestEqual(TEXT("enge Zusammenfuehrung: Grundwert"),
			FSim::ComputeStopSetbackCm(Approach, Length, Obstacles, Base, Max, Step, HalfL, HalfW, HalfW, bResolved), Base);
		TestFalse(TEXT("enge Zusammenfuehrung: nicht geloest"), bResolved);
	}

	// Nie hinter den Spuranfang: eine 4 m kurze Zufahrt mit Querweg auf der Spur
	// findet keine freie Stelle.
	{
		const TArray<FVector> Short = { FVector(-400.0, 0.0, 0.0), FVector(0.0, 0.0, 0.0) };
		const TArray<FSim::FStopObstacle> Obstacles = { { FVector(-300.0, -2000.0, 0.0), FVector(-300.0, 2000.0, 0.0) } };
		const double Setback = FSim::ComputeStopSetbackCm(Short, 400.0, Obstacles, Base, Max, Step, HalfL, HalfW, HalfW, bResolved);
		TestTrue(FString::Printf(TEXT("kurze Zufahrt: hoechstens Spurlaenge (%.0f cm)"), Setback), Setback <= 400.0);
	}

	// Grundflaeche je Typ: Ursprung Radstandmitte, Mitte um (Front - Heck) / 2 verschoben.
	{
		FTrafficVehicle V;
		V.Location = FVector(1000.0, 0.0, 0.0);
		V.Forward = FVector::ForwardVector;
		V.TypeIndex = 3;   // Kaefer: vorn 196,6, hinten 214,5
		FVector C, F;
		double HL, HW;
		FSim::GetVehicleFootprint(V, /*bBody=*/false, C, F, HL, HW);
		TestTrue(FString::Printf(TEXT("Kaefer: Mitte %.1f cm hinter dem Ursprung"), 1000.0 - C.X), FMath::IsNearlyEqual(C.X, 1000.0 - 8.95, 0.5));
		TestTrue(FString::Printf(TEXT("Kaefer: halbe Laenge %.1f"), HL), FMath::IsNearlyEqual(HL, 205.55, 0.5));
	}
	return true;
}
