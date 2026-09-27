// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "World/SebboHqShape.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSebboHqDachAufbautenTest,
	"WiesbadenReal.World.SebboHq.Dachaufbauten",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FSebboHqDachAufbautenTest::RunTest(const FString& Parameters)
{
	// Vertrag mit SebboHq::BuildDachaufbauten: Logo, Antennen,
	// Satellitenschuessel, Magazinstaender und vier Topfpflanzen stehen auf
	// Dach bzw. Krone, und nichts ragt in den Anflugkorridor des Landeplatzes.
	// Der Korridor ist der Grund, warum die Aufbauten hinter dem Kern stehen:
	// der Landeplatz liegt bei +X, die Silhouette mit Masten und Schuessel
	// gehoert nach -X (die Krone hatte einmal den halben Landeplatz
	// ueberdeckt, siehe HelipadApproach).
	const FSebboHqDimensions D;
	TArray<SebboHq::FSebboHqDachProp> Props;
	SebboHq::BuildDachaufbauten(D, Props);

	TestTrue(TEXT("Dachaufbauten sind vorhanden (Schuessel, Masten, Logo, "
		"Magazin, vier Kuebel)"), Props.Num() >= 9);

	const double DachZ = SebboHq::GetRoofHeightCm(D) + D.SlabCm;
	const double KroneZ = SebboHq::GetCoreTopHeightCm(D) + D.CrownHeightCm;
	const double KroneHalb = SebboHq::GetCrownHalfWidthCm(D);
	const double KernHalb = D.CoreCm * 0.5;
	const double RotorRadius = D.HelipadDiameterCm * 0.35;
	const double KorridorX = SebboHq::GetHelipadOffsetCm(D) - RotorRadius;
	const double DachHalb = D.FootprintCm * 0.5 + D.SlabCm;

	int32 AufDemDach = 0;
	int32 AufDerKrone = 0;
	int32 Kuebel = 0;
	int32 Staender = 0;
	double StaenderYaw = -999.0;
	for (const SebboHq::FSebboHqDachProp& Prop : Props)
	{
		TestTrue(TEXT("Assetpfad liegt unter /Game/SebboTower/Meshes"),
			Prop.MeshPfad.StartsWith(TEXT("/Game/SebboTower/Meshes/")));
		TestTrue(TEXT("Prop steht auf Dach- oder Kronehoehe"),
			FMath::IsNearlyEqual(Prop.PosCm.Z, DachZ, 1.0)
			|| FMath::IsNearlyEqual(Prop.PosCm.Z, KroneZ, 1.0));
		TestTrue(TEXT("Prop bleibt auf dem Dachgrundriss"),
			FMath::Abs(Prop.PosCm.X) + Prop.ExtentCm.X <= DachHalb
			&& FMath::Abs(Prop.PosCm.Y) + Prop.ExtentCm.Y <= DachHalb);
		// Anflugkorridor: der Rotor braucht um den Landeplatz herum freie
		// Luft - nichts darf von +X in die Zone ueber dem Platz ragen.
		TestTrue(TEXT("Anflugkorridor bleibt frei"),
			Prop.PosCm.X + Prop.ExtentCm.X < KorridorX);

		if (FMath::IsNearlyEqual(Prop.PosCm.Z, KroneZ, 1.0))
		{
			++AufDerKrone;
			TestTrue(TEXT("Krone-Prop steht innerhalb der Krone"),
				FMath::Abs(Prop.PosCm.X) + Prop.ExtentCm.X <= KroneHalb
				&& FMath::Abs(Prop.PosCm.Y) + Prop.ExtentCm.Y <= KroneHalb);
		}
		else
		{
			++AufDemDach;
			// Der Kern (Treppenhaus/Schacht) ragt aus dem Dach: auf der
			// Dachflaeche darf nichts in seinem Grundriss stehen.
			TestTrue(TEXT("Dach-Prop steht nicht im Kerngrundriss"),
				FMath::Abs(Prop.PosCm.X) - Prop.ExtentCm.X > KernHalb
				|| FMath::Abs(Prop.PosCm.Y) - Prop.ExtentCm.Y > KernHalb);
		}

		// --- Blattwerk und Magazin ------------------------------------------
		if (Prop.MeshPfad.EndsWith(TEXT("SM_WbSebboPflanze")))
		{
			++Kuebel;
			// AM RAND, nicht im Laufweg: die Pflanze ist ein Konvexhuelle-
			// Import ueber Kuebel und Blattwerk, mitten im Dach wuerde der
			// Spieler an einem Blatt haengen.
			TestTrue(TEXT("Kuebel steht am Dachrand (|Y| >= 12 m)"),
				FMath::Abs(Prop.PosCm.Y) >= 1200.0);
			TestTrue(TEXT("Kuebel ragt nicht in den Laufweg der Dachmitte"),
				FMath::Abs(Prop.PosCm.X) >= 300.0);
		}
		if (Prop.MeshPfad.EndsWith(TEXT("SM_WbSebboMagazin")))
		{
			++Staender;
			StaenderYaw = Prop.YawDeg;
		}
	}
	TestTrue(TEXT("Dachreklame steht auf der Krone"), AufDerKrone >= 1);
	TestTrue(TEXT("Schuessel, Masten, Kuebel und Magazin stehen auf der "
		"Dachflaeche"), AufDemDach >= 8);
	TestEqual(TEXT("Es stehen vier Topfpflanzen am Dachrand"), Kuebel, 4);
	TestEqual(TEXT("Es steht ein Magazinstaender"), Staender, 1);
	// Das Cover ist aufgemalte Grafik: es muss zur Strasse zeigen, nicht
	// seitlich weggewandt (Yaw 0 = +X).
	TestTrue(TEXT("Der Magazinstaender zeigt sein Cover zur Strasse (+X)"),
		FMath::IsNearlyZero(StaenderYaw, 0.01));

	return true;
}
