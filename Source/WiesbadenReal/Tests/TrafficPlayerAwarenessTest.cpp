// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "GIS/WiesbadenTrafficSimulation.h"

/**
 * Ruecksicht des Verkehrs auf das Spielerfahrzeug.
 *
 * Ohne diese Regel faehrt der Verkehr stur seine Spur ab. In Verbindung mit
 * den Kollisionskoerpern waere das schlimmer als gar keine Reaktion: die
 * Fahrzeuge wuerden den Spieler vor sich herschieben, statt ihn nur zu
 * ignorieren.
 *
 * Geprueft wird die reine Entscheidungsfunktion - ohne Welt, ohne Netz.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrafficPlayerAwarenessTest,
	"WiesbadenReal.Traffic.PlayerAwareness",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTrafficPlayerAwarenessTest::RunTest(const FString& Parameters)
{
	// Fahrzeug im Ursprung, faehrt nach +X mit 1000 cm/s (36 km/h).
	const FVector VehicleLoc = FVector::ZeroVector;
	const FVector Forward = FVector(1.0, 0.0, 0.0);
	constexpr double Speed = 1000.0;

	constexpr double Corridor = 160.0;      // halbe Fahrschlauchbreite
	constexpr double Reaction = 2500.0;     // 25 m Reaktionsweg
	constexpr double MinGap = 700.0;        // 7 m Mindestabstand
	constexpr double HalfLen = 207.0;       // halbe Fahrzeuglaenge
	constexpr double Decel = 400.0;      // 4 m/s^2 komfortable Verzoegerung

	auto Compute = [&](const FVector& ObstacleLoc)
	{
		return FWiesbadenTrafficSimulation::ComputeObstacleAwareSpeed(
			Speed, VehicleLoc, Forward, ObstacleLoc, HalfLen, Corridor, Reaction, MinGap, Decel);
	};

	// -- 1. Freie Fahrt -----------------------------------------------------
	// Hindernis weit voraus, jenseits des Reaktionswegs: keine Aenderung.
	TestEqual(TEXT("Hindernis ausserhalb des Reaktionswegs: unveraendert"),
		Compute(FVector(10000.0, 0.0, 0.0)), Speed);

	// -- 2. Hindernis HINTER dem Fahrzeug -----------------------------------
	// Wer hinter mir ist, geht mich nichts an - sonst bremste jedes Fahrzeug,
	// sobald der Spieler es ueberholt hat.
	TestEqual(TEXT("Hindernis dahinter: unveraendert"),
		Compute(FVector(-1000.0, 0.0, 0.0)), Speed);

	// -- 3. Hindernis auf der Nachbarspur -----------------------------------
	// Ohne Querpruefung wuerde der Gegenverkehr bremsen, sobald der Spieler
	// ihm entgegenkommt - die Strasse waere sofort verstopft.
	TestEqual(TEXT("Hindernis seitlich ausserhalb des Fahrschlauchs: unveraendert"),
		Compute(FVector(1000.0, 400.0, 0.0)), Speed);

	// -- 4. Hindernis direkt voraus -> bremsen ------------------------------
	{
		// 10 m voraus: freier Weg = 1000 - 207 - 700 = 93 cm.
		// Bremsweg-Modell: v = sqrt(2 * 400 * 93) = rund 273 cm/s.
		const double Braked = Compute(FVector(1000.0, 0.0, 0.0));
		TestTrue(TEXT("Hindernis voraus laesst bremsen"), Braked < Speed);
		TestTrue(TEXT("Geschwindigkeit bleibt nicht negativ"), Braked >= 0.0);
	}

	// -- 5. Sehr nah -> Stillstand ------------------------------------------
	{
		// Innerhalb des Mindestabstands muss die Geschwindigkeit auf 0 gehen,
		// sonst faehrt der Verkehr in den Spieler hinein.
		const double Stopped = Compute(FVector(HalfLen + 100.0, 0.0, 0.0));
		TestEqual(TEXT("Innerhalb des Mindestabstands: Stillstand"), Stopped, 0.0);
	}

	// -- 6. Monotonie -------------------------------------------------------
	// Je naeher das Hindernis, desto langsamer - ohne Sprung.
	{
		double Previous = Speed + 1.0;
		bool bMonotone = true;

		for (double Distance = 2400.0; Distance >= 300.0; Distance -= 100.0)
		{
			const double Current = Compute(FVector(Distance, 0.0, 0.0));
			if (Current > Previous + 0.001)
			{
				bMonotone = false;
				break;
			}
			Previous = Current;
		}

		TestTrue(TEXT("Naeher heisst nie schneller"), bMonotone);
	}

	// -- 7. Bremsen erhoeht nie die Geschwindigkeit -------------------------
	// Die Funktion darf ausschliesslich begrenzen. Ein langsames Fahrzeug vor
	// einem weit entfernten Hindernis darf nicht beschleunigt werden.
	{
		const double Slow = 200.0;
		const double Result = FWiesbadenTrafficSimulation::ComputeObstacleAwareSpeed(
			Slow, VehicleLoc, Forward, FVector(2000.0, 0.0, 0.0),
			HalfLen, Corridor, Reaction, MinGap, Decel);
		TestTrue(TEXT("Funktion begrenzt nur, beschleunigt nie"), Result <= Slow);
	}

	// -- 8. Hoehenunterschied ignorieren ------------------------------------
	// Ein Fahrzeug 113 m hoeher auf einer Bruecke ist kein Hindernis fuer den
	// Verkehr darunter. Da horizontal gemessen wird, wuerde es faelschlich
	// bremsen - das ist der bewusst in Kauf genommene Preis dafuer, dass am
	// Hang nicht faelschlich ignoriert wird. Der Test haelt das Verhalten
	// fest, damit es eine bewusste Entscheidung bleibt.
	{
		const double OnBridge = Compute(FVector(1000.0, 0.0, 11300.0));
		TestTrue(TEXT("Hoehenversatz wird (bewusst) nicht beruecksichtigt"), OnBridge < Speed);
	}

	// -- 9. Randfaelle ------------------------------------------------------
	{
		// Verzoegerung 0 waere ein Fahrzeug ohne Bremsen - die Funktion darf
		// dann nicht durch null teilen, sondern laesst die Geschwindigkeit stehen.
		const double ZeroDecel = FWiesbadenTrafficSimulation::ComputeObstacleAwareSpeed(
			Speed, VehicleLoc, Forward, FVector(1000.0, 0.0, 0.0),
			HalfLen, Corridor, Reaction, MinGap, 0.0);
		TestEqual(TEXT("Verzoegerung 0: unveraendert"), ZeroDecel, Speed);

		// Fahrzeug ohne Richtung (Nullvektor) darf nicht abstuerzen.
		const double NoForward = FWiesbadenTrafficSimulation::ComputeObstacleAwareSpeed(
			Speed, VehicleLoc, FVector::ZeroVector, FVector(1000.0, 0.0, 0.0),
			HalfLen, Corridor, Reaction, MinGap, Decel);
		TestEqual(TEXT("Ohne Fahrtrichtung: unveraendert"), NoForward, Speed);
	}

	return true;
}
