// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "GIS/WiesbadenTrafficSimulation.h"

/**
 * Abstand ueber die Kreuzung hinweg.
 *
 * Die Folgeregel arbeitet je Bahn - Spur fuer Spur, Verbindung fuer Verbindung.
 * Zwei Fahrzeuge auf VERSCHIEDENEN Bahnen desselben Knotens sah sie nie: jedes
 * hielt auf seiner eigenen Bahn brav Abstand, und trotzdem fuhren sie
 * ineinander. Im Probespiel waren das bis zu 105 ineinander steckende Paare je
 * Diagnose.
 *
 * Geprueft werden die datenreinen Entscheidungen - ohne Welt, ohne Netz.
 */
namespace
{
	using FSim = FWiesbadenTrafficSimulation;

	FLaneConnection MakeConn(int32 From, int32 To, const TArray<FVector>& Path)
	{
		FLaneConnection C;
		C.FromLaneId = From;
		C.ToLaneId = To;
		C.IntersectionNodeId = 1;
		C.ConnectionPath = Path;
		return C;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJunctionConflictTest,
	"WiesbadenReal.Traffic.Kreuzungskonflikt",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FJunctionConflictTest::RunTest(const FString& Parameters)
{
	// -- 1. Strecken-Schnitt ------------------------------------------------
	{
		// Kreuz: von West nach Ost und von Sued nach Nord.
		TestTrue(TEXT("Kreuzende Strecken schneiden sich"),
			FSim::SegmentsIntersect2D(
				FVector(-100.0, 0.0, 0.0), FVector(100.0, 0.0, 0.0),
				FVector(0.0, -100.0, 0.0), FVector(0.0, 100.0, 0.0)));

		// Parallel nebeneinander - das ist der Gegenverkehr derselben Achse.
		// Wuerde er als Konflikt gelten, haelt jede Hauptstrasse sich selbst an.
		TestFalse(TEXT("Parallele Strecken schneiden sich nicht"),
			FSim::SegmentsIntersect2D(
				FVector(-100.0, 0.0, 0.0), FVector(100.0, 0.0, 0.0),
				FVector(-100.0, 300.0, 0.0), FVector(100.0, 300.0, 0.0)));

		// Kollinear hintereinander: keine Kreuzung (das regelt der Folgeabstand).
		TestFalse(TEXT("Kollineare Strecken zaehlen nicht als Kreuzung"),
			FSim::SegmentsIntersect2D(
				FVector(0.0, 0.0, 0.0), FVector(100.0, 0.0, 0.0),
				FVector(50.0, 0.0, 0.0), FVector(200.0, 0.0, 0.0)));

		// Verlaengert wuerden sie sich treffen - die Strecken selbst nicht.
		TestFalse(TEXT("Schnittpunkt ausserhalb beider Strecken zaehlt nicht"),
			FSim::SegmentsIntersect2D(
				FVector(-100.0, 0.0, 0.0), FVector(-50.0, 0.0, 0.0),
				FVector(0.0, -100.0, 0.0), FVector(0.0, 100.0, 0.0)));

		// Die Hoehe bleibt bewusst aussen vor: sonst waere die Bruecke ueber der
		// Strasse ein Kreuzungskonflikt.
		TestTrue(TEXT("Hoehenversatz trennt (bewusst) nicht"),
			FSim::SegmentsIntersect2D(
				FVector(-100.0, 0.0, 0.0), FVector(100.0, 0.0, 0.0),
				FVector(0.0, -100.0, 5000.0), FVector(0.0, 100.0, 5000.0)));
	}

	// -- 2. Wann liegen sich zwei Verbindungen im Weg? ----------------------
	{
		const TArray<FVector> WestOst = { FVector(-500.0, 0.0, 0.0), FVector(500.0, 0.0, 0.0) };
		const TArray<FVector> SuedNord = { FVector(0.0, -500.0, 0.0), FVector(0.0, 500.0, 0.0) };

		// DER KERNFALL: zwei Zufahrten, deren Wege sich kreuzen.
		double ClearA = 0.0;
		double ClearB = 0.0;
		TestTrue(TEXT("Kreuzende Wege sind ein Konflikt"),
			FSim::FindConnectionConflict(MakeConn(0, 1, WestOst), MakeConn(2, 3, SuedNord),
				ClearA, ClearB));

		// Und WO: in beiden Faellen nach der halben Strecke (500 von 1000 cm).
		// Ohne diese Zahl muesste ein Fahrzeug seine ganze Verbindung sperren,
		// bis es sie verlassen hat - gemessen 56 % Steher statt 46 %.
		TestTrue(FString::Printf(TEXT("Konfliktpunkt auf A bei 500 cm (%.0f)"), ClearA),
			FMath::IsNearlyEqual(ClearA, 500.0, 1.0));
		TestTrue(FString::Printf(TEXT("Konfliktpunkt auf B bei 500 cm (%.0f)"), ClearB),
			FMath::IsNearlyEqual(ClearB, 500.0, 1.0));

		// AUS DERSELBEN SPUR: kein Konflikt. Die beiden faechern aus einer
		// Kolonne auf, dort haelt die Folgeregel sie auseinander. Wer sie sperrt,
		// laesst jede Kreuzung nur noch im Gaensemarsch abfliessen.
		TestFalse(TEXT("Gleiche Quellspur ist kein Konflikt"),
			FSim::DoConnectionsConflict(MakeConn(0, 1, WestOst), MakeConn(0, 3, SuedNord)));

		// IN DIESELBE SPUR: Konflikt, auch ohne Schnittpunkt - sie treffen sich
		// beim Einfaedeln.
		{
			const TArray<FVector> VonLinks = { FVector(-500.0, -300.0, 0.0), FVector(0.0, 0.0, 0.0) };
			const TArray<FVector> VonRechts = { FVector(-500.0, 300.0, 0.0), FVector(0.0, 0.0, 0.0) };
			double MergeA = 0.0;
			double MergeB = 0.0;
			TestTrue(TEXT("Gleiche Zielspur ist ein Konflikt"),
				FSim::FindConnectionConflict(MakeConn(0, 9, VonLinks), MakeConn(2, 9, VonRechts),
					MergeA, MergeB));
			// Frei wird es erst am Ende der jeweiligen Verbindung.
			TestTrue(TEXT("Beim Einfaedeln zaehlt das Ende der Verbindung"),
				MergeA > 500.0 && MergeB > 500.0);
		}

		// GEGENVERKEHR DERSELBEN ACHSE: parallele Wege, KEIN Konflikt. Das ist
		// die Gegenprobe - waere er einer, haelt jede Hauptachse sich selbst an.
		{
			const TArray<FVector> Hin = { FVector(-500.0, -150.0, 0.0), FVector(500.0, -150.0, 0.0) };
			const TArray<FVector> Zurueck = { FVector(500.0, 150.0, 0.0), FVector(-500.0, 150.0, 0.0) };
			TestFalse(TEXT("Gegenverkehr derselben Achse ist kein Konflikt"),
				FSim::DoConnectionsConflict(MakeConn(0, 1, Hin), MakeConn(2, 3, Zurueck)));
		}
	}

	// -- 3. Stecken zwei Fahrzeuge ineinander? ------------------------------
	//
	// Ein blosser Mittenabstand genuegt nicht: nebeneinander auf zwei Spuren
	// sind 3 m voellig in Ordnung, hintereinander sind 3 m eine Beruehrung.
	{
		constexpr double HalfLen = 207.0;
		constexpr double HalfWide = 77.0;
		const FVector NachX(1.0, 0.0, 0.0);
		const FVector NachY(0.0, 1.0, 0.0);

		TestTrue(TEXT("Hintereinander mit 3 m Mittenabstand: ineinander"),
			FSim::AreVehiclesOverlapping(FVector::ZeroVector, NachX,
				FVector(300.0, 0.0, 0.0), NachX, HalfLen, HalfWide));

		TestFalse(TEXT("Hintereinander mit 5 m Mittenabstand: frei"),
			FSim::AreVehiclesOverlapping(FVector::ZeroVector, NachX,
				FVector(500.0, 0.0, 0.0), NachX, HalfLen, HalfWide));

		// DIE GEGENPROBE zum naiven Mittenabstand: nebeneinander auf der
		// Nachbarspur, 3 m quer - voellig zu Recht kein Konflikt.
		TestFalse(TEXT("Nebeneinander mit 3 m Querabstand: frei"),
			FSim::AreVehiclesOverlapping(FVector::ZeroVector, NachX,
				FVector(0.0, 300.0, 0.0), NachX, HalfLen, HalfWide));

		// Quer zueinander im Kreuzungsmittelpunkt - genau das Bild aus dem
		// Probespiel.
		TestTrue(TEXT("Quer im selben Punkt: ineinander"),
			FSim::AreVehiclesOverlapping(FVector::ZeroVector, NachX,
				FVector(50.0, 50.0, 0.0), NachY, HalfLen, HalfWide));

		// Quer, aber weit genug auseinander.
		TestFalse(TEXT("Quer mit 6 m Abstand: frei"),
			FSim::AreVehiclesOverlapping(FVector::ZeroVector, NachX,
				FVector(600.0, 0.0, 0.0), NachY, HalfLen, HalfWide));

		// Die Hoehe zaehlt auch hier nicht.
		TestTrue(TEXT("Hoehenversatz trennt (bewusst) nicht"),
			FSim::AreVehiclesOverlapping(FVector::ZeroVector, NachX,
				FVector(100.0, 0.0, 10000.0), NachX, HalfLen, HalfWide));

		// Ohne Fahrtrichtung keine Aussage - lieber nichts melden als raten.
		TestFalse(TEXT("Ohne Fahrtrichtung: keine Meldung"),
			FSim::AreVehiclesOverlapping(FVector::ZeroVector, FVector::ZeroVector,
				FVector(10.0, 0.0, 0.0), NachX, HalfLen, HalfWide));
	}

	return true;
}
