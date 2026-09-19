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


	// -- 10. Wie weit darf die KAROSSERIE von ihrer Bahn abweichen? ---------
	//
	// Im Probespiel standen Fahrzeuge sichtbar ineinander, ohne dass ihre
	// Sollpositionen einander nahe kamen: gemessen 23 von 27 Paaren betrafen
	// ausschliesslich die Karosserien, bei Seitenversaetzen bis 5 m. Die
	// grosszuegige Grenze war mit dem Spurwechsel begruendet - der passiert
	// aber im FAHREN, und im Stand bewegt das Einspurmodell die Karosserie
	// gar nicht mehr (Step = Tempo * Dt): wer mit Versatz zum Stehen kommt,
	// bleibt fuer immer neben seiner Spur stehen.
	{
		using FSim = FWiesbadenTrafficSimulation;
		FWiesbadenTrafficSettings S;
		S.MaxBodyDeviationCm = 400.0;
		S.DrivingBodyDeviationCm = 120.0;
		S.StandingBodyDeviationCm = 60.0;
		S.BodyDeviationFullSpeedCmS = 500.0;

		// DER FEHLERFALL: im Stand darf das Auto nicht neben seiner Spur stehen.
		TestTrue(FString::Printf(TEXT("Im Stand hoechstens %.0f cm (%.0f)"),
			S.StandingBodyDeviationCm,
			FSim::BodyDeviationLimitCm(S, 0.0, /*bChangingLane=*/false)),
			FMath::IsNearlyEqual(FSim::BodyDeviationLimitCm(S, 0.0, false),
				S.StandingBodyDeviationCm, 0.01));

		// Und das ist die Zahl, auf die es ankommt: die schmalste Spur im Netz
		// misst 275 cm, ein Fahrzeug 154 cm - es bleiben 60 cm Spiel je Seite.
		// Mehr, und das stehende Auto ragt in die Nachbarspur.
		TestTrue(TEXT("Im Stand bleibt das Auto in seiner Spur"),
			FSim::BodyDeviationLimitCm(S, 0.0, false) <= (275.0 - 154.0) * 0.5 + 0.01);

		// Im Fahren mehr Spiel - aber nicht die volle Spurwechsel-Grenze.
		const double Fahrt = FSim::BodyDeviationLimitCm(S, 2000.0, false);
		TestTrue(FString::Printf(TEXT("Im Fahren %.0f cm"), Fahrt),
			FMath::IsNearlyEqual(Fahrt, S.DrivingBodyDeviationCm, 0.01));
		TestTrue(TEXT("Im Fahren deutlich unter der Spurwechsel-Grenze"),
			Fahrt < S.MaxBodyDeviationCm * 0.5);

		// BEIM SPURWECHSEL gilt die grosse Grenze - dafuer ist sie da. Ohne
		// diesen Zweig haengt die Karosserie waehrend des Herueberziehens am
		// Sicherheitsnetz, und der Wechsel sieht aus wie ein Ruck.
		TestTrue(TEXT("Beim Spurwechsel gilt die volle Grenze"),
			FMath::IsNearlyEqual(FSim::BodyDeviationLimitCm(S, 2000.0, true),
				S.MaxBodyDeviationCm, 0.01));
		TestTrue(TEXT("Spurwechsel-Grenze traegt einen ganzen Spurversatz"),
			FSim::BodyDeviationLimitCm(S, 2000.0, true) > 325.0);

		// Dazwischen monoton: schneller heisst nie weniger Spiel.
		double Vorher = -1.0;
		bool bMonoton = true;
		for (double Tempo = 0.0; Tempo <= 3000.0; Tempo += 50.0)
		{
			const double Grenze = FSim::BodyDeviationLimitCm(S, Tempo, false);
			bMonoton = bMonoton && (Grenze >= Vorher - 0.001);
			Vorher = Grenze;
		}
		TestTrue(TEXT("Schneller heisst nie weniger Spiel"), bMonoton);

		// Unsinnige Einstellungen duerfen die Regel nicht umdrehen: steht die
		// Stand-Grenze ueber der Fahr-Grenze, gewinnt die Stand-Grenze als
		// Untergrenze - nie weniger als im Stand.
		FWiesbadenTrafficSettings Verdreht = S;
		Verdreht.DrivingBodyDeviationCm = 10.0;
		TestTrue(TEXT("Verdrehte Einstellung faellt nie unter die Stand-Grenze"),
			FSim::BodyDeviationLimitCm(Verdreht, 2000.0, false)
				>= Verdreht.StandingBodyDeviationCm - 0.01);
	}
	return true;
}
