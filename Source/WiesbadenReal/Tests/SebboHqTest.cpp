// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "World/SebboHqShape.h"

namespace
{
	/** Umschliessende Box aller Bauteile (oertliche Koordinaten). */
	FBox PartsBounds(const TArray<FHqPart>& Parts)
	{
		FBox Box(ForceInit);
		for (const FHqPart& Part : Parts)
		{
			const FVector Half = Part.SizeCm * 0.5;
			Box += FBox(Part.CenterCm - Half, Part.CenterCm + Half);
		}
		return Box;
	}

	int32 CountMaterial(const TArray<FHqPart>& Parts, EHqMaterial Material)
	{
		int32 N = 0;
		for (const FHqPart& Part : Parts)
		{
			N += Part.Material == Material ? 1 : 0;
		}
		return N;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSebboHqShellTest,
	"WiesbadenReal.World.SebboHq.Shell",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FSebboHqShellTest::RunTest(const FString& Parameters)
{
	const FSebboHqDimensions D;

	// --- Die Entscheidung vom 2026-09-20 steht in den Vorgaben --------------
	TestEqual(TEXT("15 Geschosse"), D.FloorCount, 15);
	TestEqual(TEXT("60 m hoch"), SebboHq::GetRoofHeightCm(D), 6000.0, 1.0);
	TestEqual(TEXT("30 m Grundflaeche"), D.FootprintCm, 3000.0, 1.0);

	TArray<FHqPart> Teile;
	SebboHq::BuildShell(D, Teile);
	TestTrue(TEXT("Die Huelle hat Bauteile"), Teile.Num() > 0);

	// --- Nichts steckt im Boden, die Hoehe stimmt ---------------------------
	const FBox Bounds = PartsBounds(Teile);
	TestTrue(TEXT("Der Turm steht auf dem Fusspunkt, nicht darunter"), Bounds.Min.Z >= -1.0);
	TestTrue(TEXT("Er beruehrt den Boden"), Bounds.Min.Z <= 1.0);
	TestTrue(TEXT("Krone und Mast ueberragen die Attika"),
		Bounds.Max.Z > SebboHq::GetRoofHeightCm(D));
	TestTrue(TEXT("Aber er bleibt unter 80 m - sonst ist es nicht mehr die Entscheidung"),
		Bounds.Max.Z <= 8000.0);

	// --- Je Geschoss eine Decke und ein Glasband ----------------------------
	//
	// Daran liest man von aussen die Geschosszahl ab; fehlt ein Band, sieht
	// der Turm aus wie ein Monolith und der Innenausbau (Stufe 2) findet
	// keine Bezugshoehen.
	for (int32 Floor = 0; Floor < D.FloorCount; ++Floor)
	{
		int32 Decken = 0;
		int32 Glas = 0;
		for (const FHqPart& Teil : Teile)
		{
			if (Teil.Floor != Floor)
			{
				continue;
			}
			Decken += Teil.Material == EHqMaterial::Concrete ? 1 : 0;
			Glas += Teil.Material == EHqMaterial::Glass ? 1 : 0;
		}
		TestEqual(*FString::Printf(TEXT("Geschoss %d hat eine Decke"), Floor), Decken, 1);
		TestEqual(*FString::Printf(TEXT("Geschoss %d hat ein Glasband"), Floor), Glas, 1);
	}

	// --- Der Sockel ist breiter als der Schaft ------------------------------
	{
		double SockelSeite = 0.0;
		double SchaftSeite = 0.0;
		for (const FHqPart& Teil : Teile)
		{
			if (Teil.Floor == 0) { SockelSeite = FMath::Max(SockelSeite, Teil.SizeCm.X); }
			if (Teil.Floor == D.FloorCount - 1) { SchaftSeite = FMath::Max(SchaftSeite, Teil.SizeCm.X); }
		}
		TestTrue(TEXT("Der Sockel steht weiter vor als der Schaft"), SockelSeite > SchaftSeite);
	}

	// --- Landeplatz: liegt auf dem Dach und traegt ein H ---------------------
	{
		const double PadZ = SebboHq::GetHelipadHeightCm(D);
		TestTrue(TEXT("Der Landeplatz liegt ueber der Attika"), PadZ >= SebboHq::GetRoofHeightCm(D));
		TestTrue(TEXT("Und nicht mehr als einen Meter darueber - kein schwebendes Deck"),
			PadZ - SebboHq::GetRoofHeightCm(D) <= 100.0);

		TestEqual(TEXT("Das H besteht aus drei Balken"),
			CountMaterial(Teile, EHqMaterial::Marking), 3);

		const FHqPart* Pad = nullptr;
		for (const FHqPart& Teil : Teile)
		{
			if (Teil.Primitive == EHqPrimitive::Cylinder
				&& Teil.Material == EHqMaterial::Concrete
				&& Teil.SizeCm.X >= D.HelipadDiameterCm - 1.0)
			{
				Pad = &Teil;
			}
		}
		if (TestNotNull(TEXT("Es gibt eine Aufsetzflaeche"), Pad))
		{
			TestEqual(TEXT("Sie hat den vorgegebenen Durchmesser"),
				Pad->SizeCm.X, D.HelipadDiameterCm, 1.0);
			TestTrue(TEXT("Sie passt auf den Grundriss"), Pad->SizeCm.X <= D.FootprintCm);
			TestTrue(TEXT("Sie liegt versetzt zum Kern - der Ausstieg soll frei bleiben"),
				FMath::Abs(Pad->CenterCm.X) > D.CoreCm * 0.5);
		}
	}

	// --- Die Huelle baut den Kern NICHT mehr ---------------------------------
	//
	// Seit Stufe 2 ist er hohl und gehoert BuildVerticalCore. Stuende er hier
	// weiter als Vollkoerper, saesse im begehbaren Treppenhaus ein Betonklotz.
	{
		for (const FHqPart& Teil : Teile)
		{
			const bool bKernklotz = Teil.Floor == -1 && Teil.Primitive == EHqPrimitive::Box
				&& FMath::IsNearlyEqual(Teil.SizeCm.X, D.CoreCm, 1.0)
				&& FMath::IsNearlyEqual(Teil.SizeCm.Y, D.CoreCm, 1.0)
				&& Teil.SizeCm.Z > D.FloorHeightCm;
			TestFalse(TEXT("Kein massiver Kern in der Huelle"), bKernklotz);
		}
		TestTrue(TEXT("Krone sitzt auf der Kernoberkante"),
			SebboHq::GetCoreTopHeightCm(D) > SebboHq::GetRoofHeightCm(D));
	}

	// --- Die Masse sind Einstellung, nicht Code -----------------------------
	{
		FSebboHqDimensions Hoch = D;
		Hoch.FloorCount = 30;
		TArray<FHqPart> Doppelt;
		SebboHq::BuildShell(Hoch, Doppelt);
		TestTrue(TEXT("Doppelte Geschosszahl gibt mehr Bauteile"), Doppelt.Num() > Teile.Num());
		TestEqual(TEXT("Und doppelte Hoehe"),
			SebboHq::GetRoofHeightCm(Hoch), SebboHq::GetRoofHeightCm(D) * 2.0, 1.0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSebboHqVerticalCoreTest,
	"WiesbadenReal.World.SebboHq.VerticalCore",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FSebboHqVerticalCoreTest::RunTest(const FString& Parameters)
{
	const FSebboHqDimensions D;

	TArray<FHqPart> Kern;
	SebboHq::BuildVerticalCore(D, Kern);
	TestTrue(TEXT("Der Kern hat Bauteile"), Kern.Num() > 0);

	// --- Er bleibt IM Turm ---------------------------------------------------
	//
	// Ein frueherer Entwurf legte den Schacht auf X = 1500 - genau die
	// Fassadenkante - und liess ihn 3 m aus dem Gebaeude ragen. Das faellt im
	// Spiel sofort auf und hier in einer Zeile.
	const FBox Bounds = PartsBounds(Kern);
	const double Half = D.FootprintCm * 0.5;
	TestTrue(TEXT("Nichts ragt in +X aus der Fassade"), Bounds.Max.X <= Half + 1.0);
	TestTrue(TEXT("Nichts ragt in -X aus der Fassade"), Bounds.Min.X >= -Half - 1.0);
	TestTrue(TEXT("Nichts ragt in +Y aus der Fassade"), Bounds.Max.Y <= Half + 1.0);
	TestTrue(TEXT("Nichts ragt in -Y aus der Fassade"), Bounds.Min.Y >= -Half - 1.0);

	// --- Er steht auf dem Boden und reicht ueber die Attika ------------------
	TestTrue(TEXT("Der Kern beginnt am Fusspunkt"), Bounds.Min.Z <= 1.0);
	TestTrue(TEXT("Und nicht darunter"), Bounds.Min.Z >= -1.0);
	TestTrue(TEXT("Der Dachaufbau reicht ueber die Attika"),
		Bounds.Max.Z > SebboHq::GetRoofHeightCm(D));

	// --- Jedes Geschoss ist erschlossen --------------------------------------
	for (int32 Floor = 0; Floor < D.FloorCount; ++Floor)
	{
		int32 Teile = 0;
		for (const FHqPart& Teil : Kern)
		{
			Teile += Teil.Floor == Floor ? 1 : 0;
		}
		TestTrue(*FString::Printf(TEXT("Geschoss %d hat Kernbauteile"), Floor), Teile > 0);
	}

	// --- KEINE Luecke zwischen den Geschossen und dem Dachaufbau -------------
	//
	// Der Dachausstieg schwebte im ersten Entwurf 155 cm ueber der Attika.
	// Geprueft wird deshalb, dass die Bauteile in Z lueckenlos aneinander
	// anschliessen: zu jeder Hoehe zwischen Fuss und Oberkante gibt es Material.
	{
		double GroessteLuecke = 0.0;
		for (double Z = 0.0; Z < Bounds.Max.Z; Z += 50.0)
		{
			bool bGetroffen = false;
			for (const FHqPart& Teil : Kern)
			{
				const double Unten = Teil.CenterCm.Z - Teil.SizeCm.Z * 0.5;
				const double Oben = Teil.CenterCm.Z + Teil.SizeCm.Z * 0.5;
				if (Z >= Unten - 0.01 && Z <= Oben + 0.01)
				{
					bGetroffen = true;
					break;
				}
			}
			GroessteLuecke = bGetroffen ? 0.0 : GroessteLuecke + 50.0;
			TestTrue(*FString::Printf(TEXT("Kein Loch bei Z = %.0f cm"), Z), GroessteLuecke <= 50.0);
		}
	}

	// --- Hohl, nicht massiv --------------------------------------------------
	//
	// Der Kern ist 900 x 900 im Grundriss. Waere er ein Vollkoerper, haette
	// mindestens ein Bauteil beide Kantenlaengen; Waende sind schmal.
	{
		for (const FHqPart& Teil : Kern)
		{
			const bool bVollquerschnitt =
				Teil.SizeCm.X > D.CoreCm - 60.0 && Teil.SizeCm.Y > D.CoreCm - 60.0
				&& Teil.SizeCm.Z > 100.0;
			TestFalse(TEXT("Kein Bauteil fuellt den ganzen Kernquerschnitt"), bVollquerschnitt);
		}
	}

	// --- Das Treppenhaus traegt, der Schacht bleibt leer ---------------------
	//
	// Der Schacht (+Y) darf KEINE waagerechten Boeden haben - sonst faehrt dort
	// spaeter keine Kabine, sondern es ist ein zugemauerter Raum.
	{
		int32 PodesteTreppe = 0;
		int32 BoedenSchacht = 0;
		for (const FHqPart& Teil : Kern)
		{
			const bool bWaagerecht = Teil.SizeCm.Z <= 40.0
				&& Teil.SizeCm.X > 100.0 && Teil.SizeCm.Y > 100.0;
			if (!bWaagerecht)
			{
				continue;
			}
			// Der Dachaufbau-Deckel liegt ganz oben und zaehlt nicht als Boden.
			if (Teil.CenterCm.Z >= SebboHq::GetCoreTopHeightCm(D))
			{
				continue;
			}
			if (Teil.CenterCm.Y < 0.0) { ++PodesteTreppe; } else { ++BoedenSchacht; }
		}
		TestEqual(TEXT("Je Geschoss ein Treppenpodest"), PodesteTreppe, D.FloorCount);
		TestEqual(TEXT("Der Aufzugsschacht bleibt ohne Boeden"), BoedenSchacht, 0);
	}

	// --- Die Stufen sind steigbar --------------------------------------------
	//
	// Eine Stufe je Geschosshoehe waere eine Wand. Unrealss Spielfigur schafft
	// rund 45 cm; darunter muss jede Stufe bleiben.
	{
		double GroessterTritt = 0.0;
		TArray<double> Hoehen;
		for (const FHqPart& Teil : Kern)
		{
			if (Teil.Floor == 0 && Teil.SizeCm.Z <= 40.0 && Teil.CenterCm.Y < 0.0)
			{
				Hoehen.Add(Teil.CenterCm.Z + Teil.SizeCm.Z * 0.5);
			}
		}
		Hoehen.Sort();
		for (int32 i = 1; i < Hoehen.Num(); ++i)
		{
			GroessterTritt = FMath::Max(GroessterTritt, Hoehen[i] - Hoehen[i - 1]);
		}
		TestTrue(TEXT("Im ersten Geschoss gibt es Stufen"), Hoehen.Num() > 2);
		TestTrue(*FString::Printf(TEXT("Groesste Stufe %.0f cm bleibt unter 45 cm"), GroessterTritt),
			GroessterTritt <= 45.0);
	}

	// --- Huelle und Kern stossen nicht ineinander ----------------------------
	{
		TArray<FHqPart> Huelle;
		SebboHq::BuildShell(D, Huelle);
		TestTrue(TEXT("Die Huelle steht noch"), Huelle.Num() > 0);
		TestTrue(TEXT("Zusammen mehr Bauteile als je einzeln"),
			Huelle.Num() + Kern.Num() > FMath::Max(Huelle.Num(), Kern.Num()));
	}

	return true;
}
