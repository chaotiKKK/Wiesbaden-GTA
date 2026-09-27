// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "World/SebboHqShape.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSebboHqInnenausbauTest,
	"WiesbadenReal.World.SebboHq.Innenausbau",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FSebboHqInnenausbauTest::RunTest(const FString& Parameters)
{
	// Vertrag mit SebboHq::BuildInnenausbau: jedes Geschoss ist eingerichtet
	// (Deckenleuchte, Tuersfluegel am Kern, Polstermoebel), und der KERN bleibt
	// frei - Treppenweg (Lauf/Podest) und Aufzugsschacht erhalten nichts
	// Kollidierendes. Die Figurprobe laeuft dort ihre 57 Wegpunkte, und
	// Steckenbleiben zaehlt als Fehlschlag.
	const FSebboHqDimensions D;
	TArray<FHqPart> Teile;
	SebboHq::BuildInnenausbau(D, Teile);

	TestTrue(TEXT("Der Innenausbau hat Bauteile"), Teile.Num() > 100);

	for (int32 Floor = 0; Floor < D.FloorCount; ++Floor)
	{
		const double Z0 = Floor * D.FloorHeightCm;
		const double Zboden = Z0 + D.SlabCm;
		const double Z1 = Z0 + D.FloorHeightCm;

		int32 TeileImGeschoss = 0;
		bool bLeuchte = false;
		bool bTuerfluegel = false;
		bool bPolster = false;
		for (const FHqPart& Teil : Teile)
		{
			if (Teil.Floor != Floor)
			{
				continue;
			}
			++TeileImGeschoss;

			const double MinZ = Teil.CenterCm.Z - Teil.SizeCm.Z * 0.5;
			const double MaxZ = Teil.CenterCm.Z + Teil.SizeCm.Z * 0.5;

			if (Teil.Material == EHqMaterial::Lamp)
			{
				bLeuchte = bLeuchte || (MaxZ <= Z1 + 1.0 && Teil.CenterCm.Z >= Z1 - 20.0);
				TestFalse(TEXT("Eine Leuchte blockt nie"), Teil.bCollision);
				TestTrue(TEXT("Leuchten sind flache Panels"), Teil.SizeCm.Z <= 15.0);
			}
			if (Teil.Material == EHqMaterial::Wood && !Teil.bCollision
				&& Teil.CenterCm.X < -350.0 && Teil.SizeCm.Z >= 250.0)
			{
				bTuerfluegel = true;
			}
			bPolster = bPolster || Teil.Material == EHqMaterial::Fabric;

			// Nichts wachst durch die Decke des Geschosses.
			TestTrue(*FString::Printf(TEXT("Geschoss %d: Bauteil unter der Decke"), Floor),
				MaxZ <= Z1 + 1.0);

			if (Teil.bCollision)
			{
				// Kollidierende Moebel stehen auf dem Geschossboden - und
				// bleiben unter der Kopffreiheit von 2,40 m.
				TestTrue(*FString::Printf(TEXT("Geschoss %d: Moebel auf dem Boden"), Floor),
					MinZ >= Zboden - 3.0);
				TestTrue(*FString::Printf(TEXT("Geschoss %d: Kopffreiheit"), Floor),
					MaxZ <= Zboden + 240.0);

				// Der Kern ist heilig: Treppenweg und Schacht bleiben frei.
				// Die Kernquadrat-Pruefung ist absichtlich grosszuegig
				// (+-500 statt +-450): 50 cm Gang vor den Tueren gehoeren
				// auch noch zum Weg.
				const bool bImKern =
					Teil.CenterCm.X + Teil.SizeCm.X * 0.5 > -500.0 &&
					Teil.CenterCm.X - Teil.SizeCm.X * 0.5 < 500.0 &&
					Teil.CenterCm.Y + Teil.SizeCm.Y * 0.5 > -500.0 &&
					Teil.CenterCm.Y - Teil.SizeCm.Y * 0.5 < 500.0;
				TestFalse(*FString::Printf(TEXT("Geschoss %d: nichts Kollidierendes im Kern"), Floor),
					bImKern);
			}
		}

		TestTrue(*FString::Printf(TEXT("Geschoss %d ist eingerichtet"), Floor), TeileImGeschoss >= 20);
		TestTrue(*FString::Printf(TEXT("Geschoss %d hat eine Deckenleuchte"), Floor), bLeuchte);
		TestTrue(*FString::Printf(TEXT("Geschoss %d hat einen Tuersfluegel am Kern"), Floor), bTuerfluegel);
		TestTrue(*FString::Printf(TEXT("Geschoss %d hat Polstermoebel"), Floor), bPolster);
	}

	// Das Erdgeschoss traegt Zufahrt und Portal: im Fahrbereich der Garage
	// (Y -1300..-700) und auf dem Portalweg (Y -1520..-1340) steht ab der
	// Hallenmitte (X > 550) nichts von der Moeblierung.
	for (const FHqPart& Teil : Teile)
	{
		if (Teil.Floor != 0 || !Teil.bCollision)
		{
			continue;
		}
		const bool bZufahrtsweg =
			Teil.CenterCm.X + Teil.SizeCm.X * 0.5 > 550.0 &&
			Teil.CenterCm.Y + Teil.SizeCm.Y * 0.5 > -1550.0 &&
			Teil.CenterCm.Y - Teil.SizeCm.Y * 0.5 < -650.0;
		TestFalse(TEXT("Erdgeschoss: Zufahrt und Portal bleiben moebelfrei"), bZufahrtsweg);
	}

	return true;
}
