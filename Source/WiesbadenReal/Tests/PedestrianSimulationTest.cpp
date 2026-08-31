// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "GIS/WiesbadenPedestrianSimulation.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPedestrianSimulationTest,
	"WiesbadenReal.World.PedestrianSimulation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Die datenreinen Teile der Fussgaenger-Simulation.
 *
 * Geprueft wird vor allem, dass die Figuren auf dem GEHWEG landen und nicht auf
 * der Fahrbahn - ein Vorzeichenfehler beim seitlichen Versatz faellt im Spiel
 * kaum auf (Fussgaenger auf der Strasse wirken nur "etwas daneben"), ist aber
 * genau der Fehler, den man nicht sehen will.
 */
bool FPedestrianSimulationTest::RunTest(const FString& Parameters)
{
	// -- Seitlicher Versatz auf den Gehweg -----------------------------------
	{
		// Fahrbahn 6,5 m, Gehweg 2,5 m: Bordstein bei 3,25 m, Gehwegmitte bei 4,5 m.
		const double Offset = FWiesbadenPedestrianSimulation::ComputeSidewalkCenterOffsetCm(650.0, 250.0);
		TestTrue(TEXT("Gehwegmitte bei 4,5 m"), FMath::IsNearlyEqual(Offset, 450.0, 0.1));

		// Der Versatz muss IMMER ausserhalb der Fahrbahn liegen, sonst laufen
		// die Figuren auf der Strasse.
		TestTrue(TEXT("Versatz liegt hinter der Bordsteinkante"), Offset > 650.0 * 0.5);

		// Ohne Gehwegbreite bleibt die Bordsteinkante der Bezug.
		TestTrue(TEXT("Ohne Gehwegbreite = halbe Fahrbahn"),
			FMath::IsNearlyEqual(FWiesbadenPedestrianSimulation::ComputeSidewalkCenterOffsetCm(650.0, 0.0), 325.0, 0.1));

		// Negative Eingaben duerfen den Versatz nicht ins Negative ziehen.
		TestTrue(TEXT("Negative Breite wird geklemmt"),
			FWiesbadenPedestrianSimulation::ComputeSidewalkCenterOffsetCm(-100.0, -100.0) >= 0.0);
	}

	// -- Gehweg-Seiten aus dem OSM-Typ ---------------------------------------
	{
		using FSim = FWiesbadenPedestrianSimulation;

		TestTrue(TEXT("Both: rechts"), FSim::HasSidewalkOnSide(EOSMSidewalkType::Both, true));
		TestTrue(TEXT("Both: links"), FSim::HasSidewalkOnSide(EOSMSidewalkType::Both, false));

		TestTrue(TEXT("Left: nur links"),
			!FSim::HasSidewalkOnSide(EOSMSidewalkType::Left, true)
			&& FSim::HasSidewalkOnSide(EOSMSidewalkType::Left, false));

		TestTrue(TEXT("Right: nur rechts"),
			FSim::HasSidewalkOnSide(EOSMSidewalkType::Right, true)
			&& !FSim::HasSidewalkOnSide(EOSMSidewalkType::Right, false));

		TestTrue(TEXT("None: keine Seite"),
			!FSim::HasSidewalkOnSide(EOSMSidewalkType::None, true)
			&& !FSim::HasSidewalkOnSide(EOSMSidewalkType::None, false));

		// "Separate" ist als eigener Weg gemappt und wird ueber sein eigenes
		// Segment bedient - an dieser Achse darf niemand laufen, sonst stehen
		// die Figuren doppelt.
		TestTrue(TEXT("Separate: keine Seite an dieser Achse"),
			!FSim::HasSidewalkOnSide(EOSMSidewalkType::Separate, true)
			&& !FSim::HasSidewalkOnSide(EOSMSidewalkType::Separate, false));
	}

	// -- Bewegung und Umkehr am Segmentende ----------------------------------
	{
		constexpr double Length = 1000.0;

		// Normaler Schritt vorwaerts.
		bool bForward = true;
		double Distance = FWiesbadenPedestrianSimulation::AdvanceAlongSegment(
			100.0, 200.0, Length, 1.0f, bForward);
		TestTrue(TEXT("Vorwaerts 2 m"), FMath::IsNearlyEqual(Distance, 300.0, 0.1));
		TestTrue(TEXT("Richtung unveraendert"), bForward);

		// Am oberen Ende wird umgekehrt statt ueberzulaufen.
		bForward = true;
		Distance = FWiesbadenPedestrianSimulation::AdvanceAlongSegment(
			950.0, 200.0, Length, 1.0f, bForward);
		TestTrue(TEXT("Am Ende gedreht"), !bForward);
		TestTrue(TEXT("Innerhalb des Segments geblieben"), Distance >= 0.0 && Distance <= Length);
		TestTrue(TEXT("Am Ende gespiegelt"), FMath::IsNearlyEqual(Distance, 850.0, 0.1));

		// Am unteren Ende ebenso.
		bForward = false;
		Distance = FWiesbadenPedestrianSimulation::AdvanceAlongSegment(
			50.0, 200.0, Length, 1.0f, bForward);
		TestTrue(TEXT("Am Anfang gedreht"), bForward);
		TestTrue(TEXT("Am Anfang gespiegelt"), FMath::IsNearlyEqual(Distance, 150.0, 0.1));

		// Entartetes Segment darf nicht in die Irre laufen.
		bForward = true;
		TestTrue(TEXT("Nulllaenge -> 0"),
			FWiesbadenPedestrianSimulation::AdvanceAlongSegment(0.0, 200.0, 0.0, 1.0f, bForward) == 0.0);
	}

	// -- Schrittphase --------------------------------------------------------
	{
		// Die Phase muss stets im Bereich 0..1 bleiben, auch nach langer Strecke.
		for (double Distance = 0.0; Distance < 5000.0; Distance += 137.0)
		{
			const float Phase = FWiesbadenPedestrianSimulation::ComputeStridePhase(Distance, 75.0, 0.3f);
			if (Phase < 0.0f || Phase >= 1.0f)
			{
				AddError(FString::Printf(TEXT("Schrittphase ausserhalb 0..1 bei %.0f cm: %.3f"), Distance, Phase));
				break;
			}
		}

		// Ein voller Schritt bringt dieselbe Phase zurueck.
		const float PhaseA = FWiesbadenPedestrianSimulation::ComputeStridePhase(0.0, 75.0, 0.0f);
		const float PhaseB = FWiesbadenPedestrianSimulation::ComputeStridePhase(75.0, 75.0, 0.0f);
		TestTrue(TEXT("Nach einem Schritt gleiche Phase"), FMath::IsNearlyEqual(PhaseA, PhaseB, 1e-4f));

		// Der Versatz sorgt dafuer, dass nicht alle im Gleichschritt laufen.
		const float Offset = FWiesbadenPedestrianSimulation::ComputeStridePhase(0.0, 75.0, 0.5f);
		TestTrue(TEXT("Versatz verschiebt die Phase"), !FMath::IsNearlyEqual(PhaseA, Offset, 1e-3f));

		TestTrue(TEXT("Schrittlaenge 0 -> 0"),
			FWiesbadenPedestrianSimulation::ComputeStridePhase(500.0, 0.0, 0.0f) == 0.0f);
	}

	// -- Ausduennung nach aussen ---------------------------------------------
	//
	// Die Dichte galt bisher UEBERALL gleich: am Nordfriedhof und auf der
	// Platter Strasse Richtung Taunusstein liefen so viele Menschen herum wie
	// in der Fussgaengerzone.
	{
		using FSim = FWiesbadenPedestrianSimulation;

		FWiesbadenPedestrianSettings S;
		S.CityCentreCm = FVector2D(0.0, 0.0);
		S.FalloffRingMeters = 900.0;
		S.OuterFalloffPerRing = 0.20;
		S.MinOuterFraction = 0.05;

		// Innenstadt: voller Anteil.
		TestTrue(TEXT("Im Zentrum volle Dichte"),
			FMath::IsNearlyEqual(FSim::ComputeOuterFraction(FVector2D(0.0, 0.0), S), 1.0, 1e-6));

		// Noch im ersten Ring (unter 900 m): unveraendert.
		TestTrue(TEXT("Innerhalb des ersten Rings unveraendert"),
			FMath::IsNearlyEqual(FSim::ComputeOuterFraction(FVector2D(80000.0, 0.0), S), 1.0, 1e-6));

		// Zweiter Ring: 20 Prozent weniger.
		TestTrue(TEXT("Zweiter Ring 80 Prozent"),
			FMath::IsNearlyEqual(FSim::ComputeOuterFraction(FVector2D(100000.0, 0.0), S), 0.8, 1e-6));

		// Dritter Ring: 0,8 * 0,8.
		TestTrue(TEXT("Dritter Ring 64 Prozent"),
			FMath::IsNearlyEqual(FSim::ComputeOuterFraction(FVector2D(190000.0, 0.0), S), 0.64, 1e-6));

		// Muss FALLEND sein - sonst waere die ganze Regel wirkungslos.
		double Previous = 1.1;
		for (double Metres = 0.0; Metres < 12000.0; Metres += 450.0)
		{
			const double F = FSim::ComputeOuterFraction(FVector2D(Metres * 100.0, 0.0), S);
			if (F > Previous + 1e-9)
			{
				AddError(FString::Printf(TEXT("Anteil steigt bei %.0f m: %.3f nach %.3f"),
					Metres, Previous, F));
				break;
			}
			Previous = F;
		}

		// Weit draussen greift die Untergrenze - der Stadtrand soll nicht
		// voellig menschenleer wirken.
		TestTrue(TEXT("Untergrenze greift"),
			FMath::IsNearlyEqual(
				FSim::ComputeOuterFraction(FVector2D(5000000.0, 0.0), S), 0.05, 1e-6));

		// Ohne Ausduennung bleibt alles wie vorher.
		FWiesbadenPedestrianSettings Off = S;
		Off.OuterFalloffPerRing = 0.0;
		TestTrue(TEXT("Ausduennung abschaltbar"),
			FMath::IsNearlyEqual(
				FSim::ComputeOuterFraction(FVector2D(500000.0, 0.0), Off), 1.0, 1e-6));
	}

	// -- Saegehieb faellt Fussgaenger ----------------------------------------
	//
	// Der Hieb der Kettensaege konnte einen Fussgaenger PRINZIPIELL nicht
	// treffen: die Figuren werden als Instanzen einer Komponente gezeichnet,
	// die ausdruecklich keine Kollision traegt (PedestrianSpawnerComponent:
	// SetCollisionEnabled(NoCollision)). Ein Kugel-Sweep auf ECC_Pawn fand
	// deshalb nichts - Klang und Bewegung liefen, die Wirkung fehlte
	// vollstaendig, und im Spiel sah es aus wie ein Zielfehler.
	//
	// Geprueft wird der Weg, der das ersetzt: StrikeNear arbeitet auf den
	// Simulationsdaten, nicht auf Kollisionskoerpern.
	{
		FWiesbadenPedestrian Walker;
		TestTrue(TEXT("Frisch erzeugt laeuft er"), Walker.DownSeconds == 0.0f);

		// Ohne Strassennetz gibt es keine Fussgaenger - StrikeNear darf dann
		// nicht abstuerzen, sondern meldet schlicht null Treffer.
		FWiesbadenPedestrianSimulation Empty;
		const int32 None = Empty.StrikeNear(FVector::ZeroVector, 500.0, 10.0f);
		TestEqual(TEXT("Ohne Fussgaenger keine Treffer"), None, 0);
	}

	return true;
}
