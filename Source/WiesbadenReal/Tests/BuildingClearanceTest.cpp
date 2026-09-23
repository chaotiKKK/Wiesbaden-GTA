// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "GIS/BuildingGenerator.h"
#include "GIS/WiesbadenBuildingClearance.h"

/**
 * Die Gebaeude-Freihaltung: haelt sie einen Abstellplatz wirklich frei?
 *
 * Der Anlass war ein gemessener Fehler: der zweite Hubschrauber stand 9,4 m
 * tief in "Platter Strasse 150". Geprueft wurde damals nur die Fahrbahn - und
 * wer der Fahrbahn ausweicht, weicht ins Blockinnere aus, wo die Haeuser
 * stehen. Diese Pruefung ist die Gegenrechnung dazu, und sie braucht keine
 * Welt: Grundrisse sind Zahlen.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWiesbadenBuildingClearanceTest,
	"WiesbadenReal.GIS.GebaeudeFreihaltung",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
	FGeneratedBuilding MacheGebaeude(
		double CenterX, double CenterY, double HalfX, double HalfY, double YawDeg)
	{
		FGeneratedBuilding B;
		B.FootprintCenterCm = FVector2D(CenterX, CenterY);
		B.FootprintExtentCm = FVector2D(HalfX, HalfY);
		B.FootprintYawDegrees = static_cast<float>(YawDeg);
		B.Bounds = FBox(
			FVector(CenterX - HalfX, CenterY - HalfY, 0.0),
			FVector(CenterX + HalfX, CenterY + HalfY, 1000.0));
		return B;
	}
}

bool FWiesbadenBuildingClearanceTest::RunTest(const FString& Parameters)
{
	using FClearance = FWiesbadenBuildingClearance;

	// -- Abstand zum gedrehten Rechteck (datenrein) --------------------------
	{
		const FVector2D Mitte(0.0, 0.0);
		const FVector2D Weite(1000.0, 500.0);   // 20 x 10 m

		TestEqual(TEXT("in der Mitte ist der Abstand 0"),
			FClearance::DistanceToRotatedBox2D(Mitte, Mitte, Weite, 0.0), 0.0);

		TestEqual(TEXT("auf der Kante ist er 0"),
			FClearance::DistanceToRotatedBox2D(
				FVector2D(1000.0, 0.0), Mitte, Weite, 0.0), 0.0);

		TestTrue(TEXT("3 m vor der langen Seite sind 3 m"),
			FMath::IsNearlyEqual(FClearance::DistanceToRotatedBox2D(
				FVector2D(0.0, 800.0), Mitte, Weite, 0.0), 300.0, 0.01));

		// Ueber Eck gemessen zaehlen beide Achsen - nicht nur die groessere.
		TestTrue(TEXT("ueber Eck gilt der Diagonalabstand"),
			FMath::IsNearlyEqual(FClearance::DistanceToRotatedBox2D(
				FVector2D(1300.0, 900.0), Mitte, Weite, 0.0),
				FMath::Sqrt(300.0 * 300.0 + 400.0 * 400.0), 0.01));

		// DIE DREHUNG IST DER PUNKT. Derselbe Punkt liegt am ungedrehten
		// Gebaeude weit draussen und am um 90 Grad gedrehten mitten drin -
		// genau dieser Unterschied geht verloren, wenn man die achsparallele
		// Bounds nimmt.
		TestTrue(TEXT("ungedreht liegt (0|800) draussen"),
			FClearance::DistanceToRotatedBox2D(
				FVector2D(0.0, 800.0), Mitte, Weite, 0.0) > 0.0);
		TestEqual(TEXT("um 90 Grad gedreht liegt derselbe Punkt drin"),
			FClearance::DistanceToRotatedBox2D(
				FVector2D(0.0, 800.0), Mitte, Weite, 90.0), 0.0);
	}

	// -- Der Index: findet er das Gebaeude, das im Weg steht? ----------------
	{
		TArray<FGeneratedBuilding> Gebaeude;
		// Ein Bau wie "Platter Strasse 150": 80 x 26 m, leicht gedreht.
		Gebaeude.Add(MacheGebaeude(0.0, 0.0, 4010.0, 1286.0, 17.0));

		FClearance Frei;
		Frei.BuildAround(Gebaeude, FVector2D::ZeroVector, /*AreaRadiusCm=*/20000.0);

		TestEqual(TEXT("das Gebaeude ist im Index"), Frei.GetFootprintCount(), 1);
		TestFalse(TEXT("der Index ist nicht leer"), Frei.IsEmpty());

		// Der gemessene Fehlstand: 9,36 / 5,23 m von der Mitte - mitten drin.
		TestTrue(TEXT("der gemeldete Fehlstand wird erkannt"),
			Frei.IsBlocked(FVector2D(936.0, 523.0), 730.0));

		// Weit genug weg ist frei - sonst waere die Pruefung wertlos, weil
		// nirgends mehr ein Platz bliebe.
		TestFalse(TEXT("200 m entfernt ist frei"),
			Frei.IsBlocked(FVector2D(20000.0, 20000.0), 730.0));

		// DER RUMPFKREIS ZAEHLT, nicht nur die Mitte. Ein Punkt 3 m neben der
		// Wand ist fuer einen 14-m-Hubschrauber belegt: die Mitte liegt
		// draussen, das Heck steckt in der Wand.
		const FVector2D NebenDerWand(0.0, 1286.0 + 300.0);
		const FVector2D Gedreht(
			NebenDerWand.X * FMath::Cos(FMath::DegreesToRadians(17.0))
				- NebenDerWand.Y * FMath::Sin(FMath::DegreesToRadians(17.0)),
			NebenDerWand.X * FMath::Sin(FMath::DegreesToRadians(17.0))
				+ NebenDerWand.Y * FMath::Cos(FMath::DegreesToRadians(17.0)));
		TestFalse(TEXT("als Punkt waere 3 m neben der Wand frei"),
			Frei.IsBlocked(Gedreht, 0.0));
		TestTrue(TEXT("mit 7 m Rumpfkreis ist es belegt"),
			Frei.IsBlocked(Gedreht, 730.0));
	}

	// -- Der Zuschlag wirkt --------------------------------------------------
	{
		TArray<FGeneratedBuilding> Gebaeude;
		Gebaeude.Add(MacheGebaeude(0.0, 0.0, 1000.0, 1000.0, 0.0));

		FClearance Ohne;
		Ohne.BuildAround(Gebaeude, FVector2D::ZeroVector, 20000.0, /*Margin=*/0.0);
		FClearance Mit;
		Mit.BuildAround(Gebaeude, FVector2D::ZeroVector, 20000.0, /*Margin=*/500.0);

		// 3 m vor der Wand: ohne Zuschlag frei, mit 5 m Zuschlag belegt.
		const FVector2D Punkt(0.0, 1300.0);
		TestFalse(TEXT("ohne Zuschlag frei"), Ohne.IsBlocked(Punkt, 0.0));
		TestTrue(TEXT("mit 5 m Zuschlag belegt"), Mit.IsBlocked(Punkt, 0.0));
	}

	// -- Rueckfall auf die achsparallele Box ---------------------------------
	//
	// Aeltere Backungen tragen den gedrehten Grundriss noch nicht. Dann lieber
	// etwas zu viel sperren als ein Gebaeude gar nicht zu kennen - denn genau
	// "kenne ich nicht" war der Fehler, der den Hubschrauber ins Haus stellte.
	{
		FGeneratedBuilding Alt;
		Alt.FootprintExtentCm = FVector2D::ZeroVector;   // kein gedrehter Grundriss
		Alt.Bounds = FBox(FVector(-1000.0, -1000.0, 0.0), FVector(1000.0, 1000.0, 900.0));

		TArray<FGeneratedBuilding> Gebaeude;
		Gebaeude.Add(Alt);

		FClearance Frei;
		Frei.BuildAround(Gebaeude, FVector2D::ZeroVector, 20000.0);

		TestEqual(TEXT("auch ohne gedrehten Grundriss im Index"),
			Frei.GetFootprintCount(), 1);
		TestTrue(TEXT("die Bounds sperren trotzdem"),
			Frei.IsBlocked(FVector2D(500.0, 500.0), 0.0));
	}

	// -- Ein Gebaeude ausserhalb des Umkreises kostet nichts -----------------
	//
	// Der Index wird um EINEN Punkt gebaut; die Karte traegt 104.458 Gebaeude.
	// Wer alle einliest, zahlt fuer eine einzige Frage den ganzen Katalog.
	{
		TArray<FGeneratedBuilding> Gebaeude;
		Gebaeude.Add(MacheGebaeude(0.0, 0.0, 500.0, 500.0, 0.0));
		Gebaeude.Add(MacheGebaeude(500000.0, 0.0, 500.0, 500.0, 0.0));   // 5 km weg

		FClearance Frei;
		Frei.BuildAround(Gebaeude, FVector2D::ZeroVector, /*AreaRadiusCm=*/10000.0);
		TestEqual(TEXT("nur das nahe Gebaeude ist im Index"),
			Frei.GetFootprintCount(), 1);
	}

	return true;
}
