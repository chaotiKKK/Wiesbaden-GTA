// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "World/WiesbadenHealthReport.h"

/**
 * Editorloser Logik-Test der EVIDENZ-GEWICHTETEN Verdikte des Health-Reports.
 *
 * FWiesbadenHealthReport ist der einzige Owner dieser Entscheidungen (Ampel-
 * Verdikt + Zeichnen-Defekt-Praedikate); sowohl die Inline-Diagnosen als auch
 * Warnings() lesen sie. Genau diese Logik hier gegen konstruierte Zahlen
 * festnageln - ohne Welt, ohne Editor, deterministisch.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHealthReportVerdictTest,
	"WiesbadenReal.World.HealthReportVerdict",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FHealthReportVerdictTest::RunTest(const FString& Parameters)
{
	using EVerdict = EWiesbadenTrafficLightVerdict;

	// Baut einen Ampel-Report aus den drei entscheidenden Zahlen.
	auto MakeLights = [](int32 Count, int32 Held, int32 Approaching)
	{
		FWiesbadenHealthReport R;
		R.TrafficLightCount = Count;
		R.VehiclesHeldAtRed = Held;
		R.VehiclesApproachingSignal = Approaching;
		return R;
	};

	// -- Ampel-Verdikt: die Drei-Wege-Entscheidung --------------------------
	// Keine Ampeln im Netz -> kein Urteil, egal was die Fahr-Zahlen sagen.
	TestTrue(TEXT("Keine Ampeln -> Inconclusive"),
		MakeLights(/*Count*/0, /*Held*/0, /*Approaching*/0).TrafficLightVerdict() == EVerdict::Inconclusive);
	TestTrue(TEXT("Keine Ampeln schlaegt sogar Halte-Evidenz -> Inconclusive"),
		MakeLights(0, 5, 50).TrafficLightVerdict() == EVerdict::Inconclusive);

	// Halte-Ereignisse an Rot belegt -> Kopplung wirkt (schlaegt alles andere).
	TestTrue(TEXT("Halten belegt -> Effective"),
		MakeLights(1073, 1, 0).TrafficLightVerdict() == EVerdict::Effective);
	TestTrue(TEXT("Halten schlaegt viele Anfahrten -> Effective, nicht Broken"),
		MakeLights(1073, 3, 500).TrafficLightVerdict() == EVerdict::Effective);

	// Genug Anfahrten (>=20) OHNE ein einziges Halten -> echter Defekt.
	TestTrue(TEXT("20 Anfahrten, 0 Halte -> Broken (Grenzfall unten)"),
		MakeLights(1073, 0, 20).TrafficLightVerdict() == EVerdict::Broken);
	TestTrue(TEXT("Viele Anfahrten, 0 Halte -> Broken"),
		MakeLights(1073, 0, 250).TrafficLightVerdict() == EVerdict::Broken);

	// Zu wenige Anfahrten (<20) ohne Halten -> kein Urteil (statistisch normal,
	// nur ~5% der Kreuzungen sind Ampeln). bStreamingComplete ist hier default
	// FALSE, daher greift das verkehrsunabhaengige "nie rot"-Signal NICHT - das
	// bildet die Vor-Streaming-Phase ab, in der noch kein Urteil moeglich ist.
	TestTrue(TEXT("19 Anfahrten, 0 Halte -> Inconclusive (knapp unter Schwelle)"),
		MakeLights(1073, 0, 19).TrafficLightVerdict() == EVerdict::Inconclusive);
	TestTrue(TEXT("0 Anfahrten, 0 Halte -> Inconclusive"),
		MakeLights(1073, 0, 0).TrafficLightVerdict() == EVerdict::Inconclusive);

	// -- EHRLICHES, verkehrsUNABHAENGIGES Signal fuer den "nicht verdrahtet"-Bug -
	// Bei fertiger Stadt (bStreamingComplete) mit Ampeln entscheidet, ob je eine
	// signalisierte Verbindung rot war. So wird der historische Bug
	// (SetTrafficLightSystem nie gerufen) als DEFEKT erkannt, statt als
	// "unschluessig" durchzurutschen - ganz OHNE dass 20 Fahrzeuge anfahren muessen.
	auto MakeStreamed = [](int32 Count, int32 Held, int32 Approaching, bool EverRed)
	{
		FWiesbadenHealthReport R;
		R.bStreamingComplete = true;
		R.TrafficLightCount = Count;
		R.VehiclesHeldAtRed = Held;
		R.VehiclesApproachingSignal = Approaching;
		R.bSignalizedConnectionEverRed = EverRed;
		return R;
	};

	TestTrue(TEXT("Fertige Stadt, Ampeln, NIE rot, kein Verkehr -> Broken (nicht verdrahtet)"),
		MakeStreamed(1073, 0, 0, /*EverRed*/false).TrafficLightVerdict() == EVerdict::Broken);
	TestTrue(TEXT("Fertige Stadt, Ampeln, NIE rot, wenige Anfahrten -> Broken statt Inconclusive"),
		MakeStreamed(1073, 0, 5, /*EverRed*/false).TrafficLightVerdict() == EVerdict::Broken);
	TestTrue(TEXT("Fertige Stadt, Ampeln, schon rot gewesen, kein Verkehr -> Inconclusive (gekoppelt, nur wenig Verkehr)"),
		MakeStreamed(1073, 0, 0, /*EverRed*/true).TrafficLightVerdict() == EVerdict::Inconclusive);
	TestTrue(TEXT("Halten schlaegt das 'nie rot'-Signal nicht aus dem Tritt -> Effective"),
		MakeStreamed(1073, 2, 0, /*EverRed*/true).TrafficLightVerdict() == EVerdict::Effective);
	TestTrue(TEXT("Keine Ampeln, fertige Stadt, nie rot -> Inconclusive (kein Fehlalarm ohne Ampeln)"),
		MakeStreamed(0, 0, 0, /*EverRed*/false).TrafficLightVerdict() == EVerdict::Inconclusive);

	// -- Verkehr-Zeichnen-Defekt: simuliert, aber keiner gezeichnet ---------
	{
		FWiesbadenHealthReport R;
		R.ActiveVehicles = 55; R.TrafficVehiclesVisible = 0;
		TestTrue(TEXT("Verkehr simuliert, 0 gezeichnet -> Defekt"), R.HasTrafficDrawDefect());
		R.TrafficVehiclesVisible = 55;
		TestFalse(TEXT("Verkehr simuliert und gezeichnet -> kein Defekt"), R.HasTrafficDrawDefect());
		R.ActiveVehicles = 0; R.TrafficVehiclesVisible = 0;
		TestFalse(TEXT("Kein Verkehr simuliert -> kein Defekt (0/0 ist normal)"), R.HasTrafficDrawDefect());
	}

	// -- Fussgaenger-Zeichnen-Defekt ----------------------------------------
	{
		FWiesbadenHealthReport R;
		R.PedestriansSimulated = 56; R.PedestriansDrawn = 0;
		TestTrue(TEXT("Fussgaenger simuliert, 0 gezeichnet -> Defekt"), R.HasPedestrianDrawDefect());
		R.PedestriansDrawn = 56;
		TestFalse(TEXT("Fussgaenger simuliert und gezeichnet -> kein Defekt"), R.HasPedestrianDrawDefect());
		R.PedestriansSimulated = 0; R.PedestriansDrawn = 0;
		TestFalse(TEXT("Keine Fussgaenger simuliert -> kein Defekt"), R.HasPedestrianDrawDefect());
	}

	// -- Material-Zeichnen-Defekt: Abschnitte ohne Material -----------------
	{
		FWiesbadenHealthReport R;
		R.PerfMeshSectionsTotal = 506; R.PerfMeshSectionsWithoutMaterial = 3;
		TestTrue(TEXT("Abschnitte ohne Material -> Defekt (Schachbrett)"), R.HasMaterialDrawDefect());
		R.PerfMeshSectionsWithoutMaterial = 0;
		TestFalse(TEXT("Alle Abschnitte mit Material -> kein Defekt"), R.HasMaterialDrawDefect());
	}

	// -- Beleuchtung: flacher Zenit-Stand (Re-Bake-Rueckfall) ---------------
	// Der abgestimmte Streiflicht-Stand ist Pitch ~-24; ~-88 heisst flach im
	// Zenit. Die Schwelle -60 trennt beide, ohne den mittleren Stand -42 oder
	// das Ziel -24 faelschlich zu melden.
	{
		FWiesbadenHealthReport R;
		R.bLightingPresent = true;
		R.SunPitchDegrees = -24.0f;
		TestFalse(TEXT("Streiflicht -24 -> kein Zenit-Defekt"), R.HasFlatZenithLighting());
		R.SunPitchDegrees = -42.0f;
		TestFalse(TEXT("Mittlerer Stand -42 -> kein Zenit-Defekt"), R.HasFlatZenithLighting());
		R.SunPitchDegrees = -60.0f;
		TestTrue(TEXT("Genau -60 -> flach (Grenzfall, <= Schwelle)"), R.HasFlatZenithLighting());
		R.SunPitchDegrees = -88.0f;
		TestTrue(TEXT("Flacher Zenit -88 -> Defekt"), R.HasFlatZenithLighting());
		R.bLightingPresent = false;
		TestFalse(TEXT("Kein Licht geladen -> kein Fehlalarm trotz Pitch -88"), R.HasFlatZenithLighting());
	}

	return true;
}
