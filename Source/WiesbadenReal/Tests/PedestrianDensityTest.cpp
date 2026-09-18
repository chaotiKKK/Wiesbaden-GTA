// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "GIS/WiesbadenPedestrianSimulation.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPedestrianDensityTest,
	"WiesbadenReal.World.Fussgaengerdichte",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPedestrianDensityTest::RunTest(const FString& Parameters)
{
	using FSim = FWiesbadenPedestrianSimulation;

	FWiesbadenPedestrianSettings Settings;
	Settings.CityCentreCm = FVector2D::ZeroVector;
	Settings.FalloffRingMeters = 900.0;
	Settings.OuterFalloffPerRing = 0.20;
	Settings.MinOuterFraction = 0.05;

	// -- 1. Die Innenstadt bleibt unangetastet. -----------------------------
	//
	// Der erste Ring ist die Innenstadt; wuerde schon dort ausgeduennt, waere
	// die Fussgaengerzone leerer als der Stadtrand.
	TestTrue(TEXT("Im Zentrum voller Anteil"),
		FMath::IsNearlyEqual(FSim::ComputeOuterFraction(FVector2D::ZeroVector, Settings), 1.0, 0.001));
	TestTrue(TEXT("Knapp innerhalb des ersten Rings noch voll"),
		FMath::IsNearlyEqual(
			FSim::ComputeOuterFraction(FVector2D(89900.0, 0.0), Settings), 1.0, 0.001));

	// -- 2. Je Ring nach aussen ein Fuenftel weniger. -----------------------
	TestTrue(TEXT("Zweiter Ring: 0,8"),
		FMath::IsNearlyEqual(
			FSim::ComputeOuterFraction(FVector2D(100000.0, 0.0), Settings), 0.8, 0.001));
	TestTrue(TEXT("Dritter Ring: 0,64"),
		FMath::IsNearlyEqual(
			FSim::ComputeOuterFraction(FVector2D(190000.0, 0.0), Settings), 0.64, 0.001));

	// -- 3. Die Ringe sind Stufen, keine stetige Kurve. ---------------------
	//
	// Innerhalb eines Viertels soll die Dichte gleich bleiben und nicht bei
	// jedem Schritt springen.
	TestTrue(TEXT("Innerhalb eines Rings bleibt der Anteil gleich"),
		FMath::IsNearlyEqual(
			FSim::ComputeOuterFraction(FVector2D(100000.0, 0.0), Settings),
			FSim::ComputeOuterFraction(FVector2D(179000.0, 0.0), Settings), 0.001));

	// -- 4. Untergrenze: der Stadtrand wird nicht menschenleer. -------------
	TestTrue(TEXT("Weit draussen greift die Untergrenze"),
		FMath::IsNearlyEqual(
			FSim::ComputeOuterFraction(FVector2D(5000000.0, 0.0), Settings),
			Settings.MinOuterFraction, 0.001));

	// -- 5. Die Richtung ist egal, nur der Abstand zaehlt. ------------------
	TestTrue(TEXT("Gleicher Abstand, andere Richtung -> gleicher Anteil"),
		FMath::IsNearlyEqual(
			FSim::ComputeOuterFraction(FVector2D(0.0, -190000.0), Settings),
			FSim::ComputeOuterFraction(FVector2D(190000.0, 0.0), Settings), 0.001));

	// -- 6. Die Zielzahl folgt Dichte UND Ausduennung. ----------------------
	//
	// Ohne Beobachter steht der Ort auf dem Ursprung, also volle Innenstadt.
	{
		FSim Sim;
		FWiesbadenPedestrianSettings S = Settings;
		S.TargetPedestriansInRadius = 200;
		S.Density = 1.0f;

		FRoadNetwork Empty;
		Sim.Initialize(Empty, S);
		TestEqual(TEXT("Volle Dichte im Zentrum -> volle Zielzahl"),
			Sim.GetTargetPedestrianCount(), 200);

		S.Density = 0.5f;
		Sim.Initialize(Empty, S);
		TestEqual(TEXT("Halbe Dichte halbiert die Zielzahl"),
			Sim.GetTargetPedestrianCount(), 100);

		S.Density = 0.0f;
		Sim.Initialize(Empty, S);
		TestEqual(TEXT("Dichte 0 schaltet die Fussgaenger ab"),
			Sim.GetTargetPedestrianCount(), 0);
	}

	// -- 7. Der Wert ist deutlich hoeher als die alten 70. ------------------
	//
	// Gemessen am Kaiser-Friedrich-Ring: 70 ergaben 18,7 Personen je
	// Kilometer Gehweg, also eine alle 53 m - das las sich als leerer Gehweg.
	{
		FWiesbadenPedestrianSettings Standard;
		TestTrue(FString::Printf(TEXT("Zielzahl %d traegt einen belebten Gehweg"),
			Standard.TargetPedestriansInRadius),
			Standard.TargetPedestriansInRadius >= 150);
	}

	return true;
}
