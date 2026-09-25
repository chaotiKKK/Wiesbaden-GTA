// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Vehicles/WiesbadenTrafficCars.h"
#include "Misc/AutomationTest.h"

#include "GIS/WiesbadenTrafficSimulation.h"
#include "World/TrafficVehicleSpawnerComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVehicleBrakeLightTest,
	"WiesbadenReal.Vehicles.Bremslicht",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVehicleBrakeLightTest::RunTest(const FString& Parameters)
{
	using FSim = FWiesbadenTrafficSimulation;

	// -- 1. Stillstand trotz Fahrwunsch: die Kolonne vor der Ampel leuchtet. -
	TestTrue(TEXT("Steht und will fahren -> Bremslicht"),
		FSim::ShouldShowBrakeLight(0.0, 0.0, 1000.0, 0.05));

	// -- 2. Steht und WILL auch nicht fahren (geparkt/Feierabend): dunkel. ---
	TestFalse(TEXT("Steht ohne Fahrwunsch -> dunkel"),
		FSim::ShouldShowBrakeLight(0.0, 0.0, 0.0, 0.05));

	// -- 3. Gleichmaessige Fahrt: dunkel. -----------------------------------
	TestFalse(TEXT("Konstante Fahrt -> dunkel"),
		FSim::ShouldShowBrakeLight(1000.0, 1000.0, 1000.0, 0.05));

	// -- 4. Beschleunigen: dunkel. ------------------------------------------
	TestFalse(TEXT("Beschleunigen -> dunkel"),
		FSim::ShouldShowBrakeLight(800.0, 1000.0, 1200.0, 0.05));

	// -- 5. Kraeftige Verzoegerung: an. -------------------------------------
	// 1000 -> 900 cm/s in 0,05 s sind 2000 cm/s^2 - eine echte Bremsung.
	TestTrue(TEXT("Starke Verzoegerung -> Bremslicht"),
		FSim::ShouldShowBrakeLight(1000.0, 900.0, 1200.0, 0.05));

	// -- 6. Winziges Ausrollen: dunkel. -------------------------------------
	//
	// Der entscheidende Fall: die Schwelle ist eine VERZOEGERUNG, keine
	// Geschwindigkeitsdifferenz. 1000 -> 999 cm/s in 0,05 s sind 20 cm/s^2 -
	// das ist eine Kuppe, keine Bremsung. Ohne diese Unterscheidung flackerte
	// die ganze Stadt im Takt des Gelaendes.
	TestFalse(TEXT("Leichtes Ausrollen -> dunkel"),
		FSim::ShouldShowBrakeLight(1000.0, 999.0, 1200.0, 0.05));

	// -- 7. Dieselbe Differenz ueber ein LANGES Bild ist keine Bremsung. ----
	//
	// 1000 -> 900 in 1,0 s sind nur 100 cm/s^2. Haenge das Ergebnis an der
	// Differenz statt an der Verzoegerung, waeren beide Faelle gleich - und
	// das Bremslicht haette von der Bildrate abgehangen.
	TestFalse(TEXT("Gleiche Differenz, langes Bild -> keine Bremsung"),
		FSim::ShouldShowBrakeLight(1000.0, 900.0, 1200.0, 1.0));

	// -- 8. Entartetes Zeitintervall stuerzt nicht ab. ----------------------
	TestFalse(TEXT("DeltaZeit 0 -> keine Division, kein Bremslicht"),
		FSim::ShouldShowBrakeLight(1000.0, 500.0, 1200.0, 0.0));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVehicleIndicatorTest,
	"WiesbadenReal.Vehicles.Blinker",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVehicleIndicatorTest::RunTest(const FString& Parameters)
{
	using FSim = FWiesbadenTrafficSimulation;

	// -- 1. Richtung aus der Abbiegeart. ------------------------------------
	TestEqual(TEXT("Links abbiegen -> linker Blinker"),
		FSim::IndicatorForTurn(ETurnType::Left), EVehicleIndicator::Left);
	TestEqual(TEXT("Rechts abbiegen -> rechter Blinker"),
		FSim::IndicatorForTurn(ETurnType::Right), EVehicleIndicator::Right);
	TestEqual(TEXT("Geradeaus -> kein Blinker"),
		FSim::IndicatorForTurn(ETurnType::Through), EVehicleIndicator::None);
	TestEqual(TEXT("Wenden wird links angezeigt"),
		FSim::IndicatorForTurn(ETurnType::UTurn), EVehicleIndicator::Left);

	// -- 2. Der Takt: an und aus, rund zur Haelfte. -------------------------
	{
		int32 Lit = 0;
		constexpr int32 Steps = 600;
		constexpr double StepSeconds = 0.01;
		for (int32 i = 0; i < Steps; ++i)
		{
			if (FSim::IsIndicatorLit(7, i * StepSeconds))
			{
				++Lit;
			}
		}
		const double Share = static_cast<double>(Lit) / Steps;
		TestTrue(FString::Printf(TEXT("Blinker leuchtet %.0f %% der Zeit"), Share * 100.0),
			Share > 0.4 && Share < 0.6);
	}

	// -- 3. Die Phase haengt am Fahrzeug. -----------------------------------
	//
	// Ohne eigene Phase blinkt eine ganze Kreuzung im Gleichtakt wie eine
	// Lichterkette. Mindestens ein Zeitpunkt muss zwei Fahrzeuge
	// unterschiedlich zeigen.
	{
		bool bEverDifferent = false;
		for (int32 i = 0; i < 200 && !bEverDifferent; ++i)
		{
			const double T = i * 0.01;
			bEverDifferent = FSim::IsIndicatorLit(1, T) != FSim::IsIndicatorLit(2, T);
		}
		TestTrue(TEXT("Zwei Fahrzeuge blinken nicht im Gleichtakt"), bEverDifferent);
	}

	// -- 4. Reproduzierbar: gleiche Eingabe, gleiche Antwort. ---------------
	{
		bool bStable = true;
		for (int32 i = 0; i < 100; ++i)
		{
			const double T = i * 0.017;
			bStable = bStable && (FSim::IsIndicatorLit(42, T) == FSim::IsIndicatorLit(42, T));
		}
		TestTrue(TEXT("Derselbe Augenblick liefert denselben Zustand"), bStable);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVehicleLampPlacementTest,
	"WiesbadenReal.Vehicles.Lampensitz",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVehicleLampPlacementTest::RunTest(const FString& Parameters)
{
	// Lampen des Golf aus dem Katalog (Fahrzeugrahmen: +X vorn, links = -Y).
	const FWbTrafficCarType& Golf = WiesbadenTrafficCars::Types()[0];

	// Fahrzeug an einer bekannten Stelle, Blick nach +X.
	const FTransform Vehicle(FRotator::ZeroRotator, FVector(1000.0, 2000.0, 300.0));

	FTransform RearLeft, RearRight, FrontLeft, FrontRight;
	UTrafficVehicleSpawnerComponent::ComputeLampTransforms(Vehicle, Golf.TailLampCm, RearLeft, RearRight);
	UTrafficVehicleSpawnerComponent::ComputeLampTransforms(Vehicle, Golf.HeadLampCm, FrontLeft, FrontRight);

	// -- 1. Vorn ist vorn, hinten ist hinten; beide an den Enden. ------------
	TestTrue(TEXT("Die vorderen Lampen liegen vor den hinteren"),
		FrontLeft.GetLocation().X > RearLeft.GetLocation().X);
	const double RearOffset = Vehicle.GetLocation().X - RearLeft.GetLocation().X;
	TestTrue(FString::Printf(TEXT("Heckleuchte %.0f cm hinter der Mitte"), RearOffset),
		RearOffset > Golf.RearCm * 0.9);

	// -- 2. Links ist links: Unreal-Y zeigt nach RECHTS. Der linke Blinker
	// sass frueher bei +Y - ein Linksabbieger blinkte rechts.
	TestTrue(TEXT("linke Lampe bei -Y"), FrontLeft.GetLocation().Y < Vehicle.GetLocation().Y);
	TestTrue(TEXT("rechte Lampe bei +Y"), FrontRight.GetLocation().Y > Vehicle.GetLocation().Y);
	const double Spread = FrontRight.GetLocation().Y - FrontLeft.GetLocation().Y;
	TestTrue(FString::Printf(TEXT("Lampenabstand %.0f cm innerhalb der Karosserie"), Spread),
		Spread > 60.0 && Spread < Golf.BodyWidthCm);

	// -- 3. Lampenhoehe: ueber dem Boden, unter der Guertellinie. ------------
	const double LampHeight = RearLeft.GetLocation().Z - Vehicle.GetLocation().Z;
	TestTrue(FString::Printf(TEXT("Lampe %.0f cm ueber dem Boden"), LampHeight),
		LampHeight > 40.0 && LampHeight < 120.0);

	// -- 4. Die Lampe ist klein - ein Lichtpunkt, kein Kasten. --------------
	const FVector Scale = RearLeft.GetScale3D();
	TestTrue(FString::Printf(TEXT("Lampengroesse %.0f x %.0f cm"),
		Scale.X * 100.0, Scale.Y * 100.0),
		Scale.X * 100.0 < 40.0 && Scale.Y * 100.0 < 30.0);

	// -- 5. Gedreht bleibt gedreht: die Lampen fahren mit. ------------------
	const FTransform Turned(FRotator(0.0, 90.0, 0.0), FVector(1000.0, 2000.0, 300.0));
	FTransform TurnedRearLeft, TurnedRearRight;
	UTrafficVehicleSpawnerComponent::ComputeLampTransforms(Turned, Golf.TailLampCm, TurnedRearLeft, TurnedRearRight);

	// Bei 90 Grad Gierung zeigt "hinten" nach -Y statt nach -X.
	const double TurnedOffsetY = Vehicle.GetLocation().Y - TurnedRearLeft.GetLocation().Y;
	TestTrue(FString::Printf(TEXT("Gedreht liegt die Heckleuchte %.0f cm in -Y"), TurnedOffsetY),
		TurnedOffsetY > Golf.RearCm * 0.9);

	return true;
}
