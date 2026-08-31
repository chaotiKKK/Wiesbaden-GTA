// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "World/WiesbadenNerotal48.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Nerotal 48: Grundrissmittelpunkt und Wasserspiegel.
 *
 * Beides ist datenrein und laesst sich ohne Welt pruefen - anders als das
 * Abtasten der Gelaendehoehe, das eine gestreamte Stadt braucht.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNerotal48GeometryTest,
	"WiesbadenReal.World.Nerotal48",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FNerotal48GeometryTest::RunTest(const FString& Parameters)
{
	// -- Mittelpunkt --------------------------------------------------------
	{
		// Ein Quadrat: der Mittelpunkt liegt in der Mitte.
		const TArray<FVector2D> Square = {
			{ 0.0, 0.0 }, { 100.0, 0.0 }, { 100.0, 100.0 }, { 0.0, 100.0 }
		};
		const FVector2D Center = AWiesbadenNerotal48::ComputeFootprintCenter(Square);
		TestTrue(FString::Printf(TEXT("Quadratmitte bei 50/50 (war %.1f/%.1f)"),
			Center.X, Center.Y),
			FMath::IsNearlyEqual(Center.X, 50.0) && FMath::IsNearlyEqual(Center.Y, 50.0));
	}

	{
		// Verschobenes Rechteck - der Mittelpunkt wandert mit.
		const TArray<FVector2D> Shifted = {
			{ -1000.0, 500.0 }, { -800.0, 500.0 }, { -800.0, 900.0 }, { -1000.0, 900.0 }
		};
		const FVector2D Center = AWiesbadenNerotal48::ComputeFootprintCenter(Shifted);
		TestTrue(TEXT("Verschobenes Rechteck: Mitte wandert mit"),
			FMath::IsNearlyEqual(Center.X, -900.0) && FMath::IsNearlyEqual(Center.Y, 700.0));
	}

	{
		// Leere Liste darf nicht abstuerzen.
		const FVector2D Center = AWiesbadenNerotal48::ComputeFootprintCenter(TArray<FVector2D>());
		TestTrue(TEXT("Leerer Grundriss liefert den Ursprung"), Center.IsNearlyZero());
	}

	// -- Wasserspiegel ------------------------------------------------------
	//
	// Das Wasser muss UNTER der Beckenkante stehen. Steht es genau auf der
	// Kante, fehlt der Schatten an der Innenwand, und aus dem gefuellten
	// Becken wird eine blaue Flaeche auf dem Rasen.
	{
		const double Level = AWiesbadenNerotal48::ComputeWaterLevelCm(14.0, 12.0);
		TestTrue(FString::Printf(TEXT("Wasser 12 cm unter der Kante (%.1f)"), Level),
			FMath::IsNearlyEqual(Level, 2.0));
		TestTrue(TEXT("Wasser liegt unter der Kante"), Level < 14.0);
	}

	{
		// Negatives Freibord waere ein Ueberlauf - es wird auf null geklemmt,
		// nicht ueber die Kante gehoben.
		const double Level = AWiesbadenNerotal48::ComputeWaterLevelCm(14.0, -5.0);
		TestTrue(TEXT("Negatives Freibord hebt das Wasser nicht ueber die Kante"),
			Level <= 14.0);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
