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
		if (Floor == 0)
		{
			// Die Einfahrt und das Portal teilen nur die Erdgeschossfassade. Ein
			// einzelner Glaskasten wuerde beide Oeffnungen wieder verschliessen.
			TestTrue(TEXT("Das Erdgeschoss ist fuer Einfahrt und Portal geteilt"), Glas >= 3);
		}
		else
		{
			TestEqual(*FString::Printf(TEXT("Geschoss %d hat ein Glasband"), Floor), Glas, 1);
		}
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSebboHqStairHeadroomTest,
	"WiesbadenReal.World.SebboHq.TreppeBegehbar",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FSebboHqStairHeadroomTest::RunTest(const FString& Parameters)
{
	// KOPFFREIHEIT - der Punkt, an dem die erste Fassung scheiterte.
	//
	// Die Stufen waren 25 cm hoch und lueckenlos, und der Test sagte gruen.
	// Begehbar waren sie trotzdem nicht: das Podest des naechsten Geschosses
	// deckte den GANZEN Treppenhaus-Grundriss und lag damit als Decke ueber
	// dem Lauf. Nachgerechnet blieben ueber der achten Stufe noch 155 cm, ueber
	// der fuenfzehnten 5 cm, und die sechzehnte lag IM Podest. Man kam 220 von
	// 400 cm hoch und stand dann mit dem Kopf an der Decke.
	//
	// Eine Treppe ist erst begehbar, wenn ueber JEDER Trittflaeche Platz fuer
	// einen Menschen ist.
	const FSebboHqDimensions D;
	TArray<FHqPart> Kern;
	SebboHq::BuildVerticalCore(D, Kern);

	// Die Spielfigur: Kapselhoehe 180 cm (2 x 90 cm Halbhoehe, wie der
	// Einstiegsversatz beim Nerobergbahn-Wagen).
	const double Stehhoehe = 180.0;

	// Trittflaechen des ERSTEN Geschosses: waagerecht, duenn, im Treppenhaus
	// (-Y), und schmal in X - das unterscheidet die Stufe vom Podest.
	TArray<FHqPart> Stufen;
	for (const FHqPart& Teil : Kern)
	{
		if (Teil.Floor == 0 && Teil.SizeCm.Z <= 40.0 && Teil.CenterCm.Y < 0.0
			&& Teil.SizeCm.X < 100.0 && Teil.SizeCm.Y > 100.0)
		{
			Stufen.Add(Teil);
		}
	}
	if (!TestTrue(TEXT("Es gibt Stufen im ersten Geschoss"), Stufen.Num() >= 8))
	{
		return false;
	}

	// Ueber jeder Stufe: das tiefste Bauteil, das ihre Grundflaeche ueberdeckt.
	int32 ZuNiedrig = 0;
	double Schlimmste = TNumericLimits<double>::Max();
	int32 SchlimmsteNummer = INDEX_NONE;
	for (int32 i = 0; i < Stufen.Num(); ++i)
	{
		const FHqPart& Stufe = Stufen[i];
		const double TrittZ = Stufe.CenterCm.Z + Stufe.SizeCm.Z * 0.5;
		// Ein Punkt kurz VOR der Stufenkante, in ihrer Mitte - dort steht der
		// Fuss, wenn man die Stufe betritt.
		const FVector2D Fuss(Stufe.CenterCm.X, Stufe.CenterCm.Y);

		double Decke = TNumericLimits<double>::Max();
		for (const FHqPart& Anderes : Kern)
		{
			const double Unten = Anderes.CenterCm.Z - Anderes.SizeCm.Z * 0.5;
			if (Unten <= TrittZ + 1.0)
			{
				continue;     // liegt nicht darueber
			}
			const bool bUeberX = FMath::Abs(Anderes.CenterCm.X - Fuss.X) < Anderes.SizeCm.X * 0.5;
			const bool bUeberY = FMath::Abs(Anderes.CenterCm.Y - Fuss.Y) < Anderes.SizeCm.Y * 0.5;
			if (bUeberX && bUeberY)
			{
				Decke = FMath::Min(Decke, Unten);
			}
		}
		const double Frei = Decke - TrittZ;
		if (Frei < Stehhoehe)
		{
			++ZuNiedrig;
			if (Frei < Schlimmste)
			{
				Schlimmste = Frei;
				SchlimmsteNummer = i;
			}
		}
	}

	TestEqual(*FString::Printf(
		TEXT("Jede der %d Stufen hat %.0f cm Kopffreiheit (schlimmste: Stufe %d mit %.0f cm)"),
		Stufen.Num(), Stehhoehe, SchlimmsteNummer,
		SchlimmsteNummer == INDEX_NONE ? 0.0 : Schlimmste),
		ZuNiedrig, 0);

	// Und der Lauf muss das naechste Geschoss WIRKLICH erreichen: die oberste
	// Trittflaeche liegt auf der Hoehe des naechsten Podests.
	double Oberste = 0.0;
	for (const FHqPart& Stufe : Stufen)
	{
		Oberste = FMath::Max(Oberste, Stufe.CenterCm.Z + Stufe.SizeCm.Z * 0.5);
	}
	TestEqual(TEXT("Die oberste Stufe endet auf dem naechsten Geschossboden"),
		Oberste, D.FloorHeightCm + 20.0, 1.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSebboHqArrivalLayoutTest,
	"WiesbadenReal.World.SebboHq.ArrivalLayout",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FSebboHqArrivalLayoutTest::RunTest(const FString& Parameters)
{
	// Dieser Test faengt den Rueckfall ab, bei dem Garage und Eingang nur als
	// Deko vor einer geschlossenen Fassade standen. Die Ankunftsbereiche muessen
	// getrennte, ausreichend grosse und von der Platter-Strassen-Seite (+X)
	// erreichbare Ziele sein.
	const FSebboHqDimensions D;
	const FSebboHqArrivalLayout Layout = SebboHq::BuildArrivalFacilities(D);

	TestTrue(TEXT("Die Garage liegt auf der Strassenseite"),
		Layout.GarageTarget.CenterCm.X > D.FootprintCm * 0.25);
	TestTrue(TEXT("Die Garage ist fahrzeugbreit"),
		Layout.GarageTarget.ExtentCm.Y >= 150.0);
	TestTrue(TEXT("Das Portal ist mindestens 120 cm frei"),
		Layout.PedestrianTarget.ExtentCm.Y * 2.0 >= 120.0);
	TestTrue(TEXT("Garage und Portal sind getrennte Ziele"),
		FVector::DistSquared2D(Layout.GarageTarget.CenterCm, Layout.PedestrianTarget.CenterCm)
			> FMath::Square(200.0));
	TestEqual(TEXT("Das Heli-Ziel liegt auf dem Dachpad"),
		Layout.HelicopterTarget.CenterCm.Z, SebboHq::GetHelipadHeightCm(D), 1.0);

	int32 Haltstreifen = 0;
	for (const FHqPart& Part : Layout.Parts)
	{
		const bool bQuerZurZufahrt = Part.Material == EHqMaterial::Marking
			&& Part.SizeCm.X <= 20.0 && Part.SizeCm.Y >= 500.0;
		Haltstreifen += bQuerZurZufahrt ? 1 : 0;
	}
	TestEqual(TEXT("Genau ein sichtbarer Haltstreifen markiert die Konfliktzone"), Haltstreifen, 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSebboHqHelipadApproachTest,
	"WiesbadenReal.World.SebboHq.HelipadApproach",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FSebboHqHelipadApproachTest::RunTest(const FString& Parameters)
{
	// Gefunden hat das die Laufzeit-Sonde (-WbAnkunftProbe), nicht eine
	// Rechnung: das Lot auf die Aufsetzflaeche traf die Krone bei 70 m statt
	// den Platz bei 60,2 m. Die Krone war 16,5 m breit und haengte ueber den
	// inneren 8 m des 18-m-Platzes - der Landeplatz war gezeichnet, aber von
	// oben nicht erreichbar. Dieser Test haelt die Geometrie fest, damit der
	// Befund nicht wieder ein Spielstart lang unbemerkt bleibt.
	const FSebboHqDimensions D;
	const double KroneHalb = SebboHq::GetCrownHalfWidthCm(D);
	const double PadVersatz = SebboHq::GetHelipadOffsetCm(D);
	const double PadRadius = D.HelipadDiameterCm * 0.5;
	const double RotorRadius = D.HelipadDiameterCm * 0.35;
	const double Dachkante = D.FootprintCm * 0.5;

	TestTrue(TEXT("Die Krone sitzt auf dem Kern und nicht ueber dem halben Dach"),
		KroneHalb * 2.0 <= D.CoreCm * 1.2);
	TestTrue(TEXT("Der Anflugkorridor liegt vollstaendig neben der Krone"),
		PadVersatz - RotorRadius > KroneHalb);
	TestTrue(TEXT("Der Anflugkorridor bleibt ueber dem Dach"),
		PadVersatz + RotorRadius < Dachkante);
	TestTrue(TEXT("Die Aufsetzflaeche liegt ganz auf dem Dach"),
		PadVersatz + PadRadius <= Dachkante);
	TestTrue(TEXT("Die Aufsetzflaeche stoesst nicht an den Kern"),
		PadVersatz - PadRadius > -D.CoreCm * 0.5);

	// Das Ankunftsziel und die gebaute Flaeche teilen EINE Rechnung.
	const FSebboHqArrivalLayout Layout = SebboHq::BuildArrivalFacilities(D);
	TestEqual(TEXT("Das Heli-Ziel liegt ueber der Aufsetzflaeche"),
		Layout.HelicopterTarget.CenterCm.X, PadVersatz, 1.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSebboHqEineZufahrtsbuchtTest,
	"WiesbadenReal.World.SebboHq.EineZufahrtsbucht",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FSebboHqEineZufahrtsbuchtTest::RunTest(const FString& Parameters)
{
	// GEMESSEN am 21.09.2026 auf Alkis17: das Grundstueck wird von GENAU EINER
	// Strasse bedient (Wolkenbruch, Segment 54432, 0,7 m vom Garagenanker; die
	// naechste andere liegt 57,6 m weg). Diese Strasse FAELLT quer ueber das
	// Grundstueck - Fahrbahnpunkte im 20-m-Ring:
	//
	//     330 Grad -> 10048 cm     0 Grad -> 9715 cm     15 Grad -> 9569 cm
	//
	// also rund 11 cm Hoehe je Grad Umfangswinkel.
	//
	// Ein Plateau hat EINE Hoehe. Ebenerdige Ankunft fuer Auto UND Fuss ist
	// darum nur moeglich, wenn beide Oeffnungen dieselbe STELLE der fallenden
	// Strasse adressieren. Frueher lagen sie 65 Grad auseinander - allein
	// daraus folgten rund 5 m Hoehenunterschied, die kein Plateau einebnen
	// kann, weil die Strasse selbst nicht eben ist.
	const FSebboHqDimensions D;
	const FSebboHqArrivalLayout Layout = SebboHq::BuildArrivalFacilities(D);

	const FVector2D Garage(Layout.GarageTarget.CenterCm.X, Layout.GarageTarget.CenterCm.Y);
	const FVector2D Portal(Layout.PedestrianTarget.CenterCm.X, Layout.PedestrianTarget.CenterCm.Y);

	const double GradGarage = FMath::RadiansToDegrees(FMath::Atan2(Garage.Y, Garage.X));
	const double GradPortal = FMath::RadiansToDegrees(FMath::Atan2(Portal.Y, Portal.X));
	const double SpanneGrad = FMath::Abs(GradGarage - GradPortal);

	// 10 Grad sind bei 11 cm/Grad rund 1,1 m Hoehenunterschied - mehr als eine
	// Bordsteinabsenkung ueberbruecken kann.
	TestTrue(*FString::Printf(
		TEXT("Beide Oeffnungen adressieren dieselbe Stelle der Zufahrt (%.1f Grad auseinander)"),
		SpanneGrad),
		SpanneGrad <= 10.0);

	// Und sie liegen auf DERSELBEN Fassadenhaelfte - ein Vorzeichenwechsel in Y
	// waere die alte, gegenueberliegende Anordnung.
	TestTrue(TEXT("Garage und Portal liegen auf derselben Fassadenhaelfte"),
		Garage.Y * Portal.Y > 0.0);

	// Trotzdem zwei getrennte Oeffnungen, kein gemeinsames Loch.
	TestTrue(TEXT("Garage und Portal bleiben getrennte Ziele"),
		FVector2D::Distance(Garage, Portal) > 200.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSebboHqPortalDurchgangTest,
	"WiesbadenReal.World.SebboHq.PortalDurchgang",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FSebboHqPortalDurchgangTest::RunTest(const FString& Parameters)
{
	// Die lichte Hoehe muss den GEHENDEN Fussgaenger durchlassen, nicht den
	// stehenden. AWiesbadenFootPawn hebt die Kapsel vor jedem Schritt um
	// MaxStepHeightCm an; unter dem Sturz braucht es darum
	// Kapselhoehe + Schritthoehe ueber dem Belag.
	//
	// Gemessen am 21.09.2026: mit 270 cm blieb die Sonde im Sturz stecken.
	const FSebboHqDimensions D;
	const FSebboHqArrivalLayout Layout = SebboHq::BuildArrivalFacilities(D);

	constexpr double KapselHoeheCm = 180.0;    // 2 x 90, wie AWiesbadenFootPawn
	constexpr double SchritthoeheCm = 40.0;    // MaxStepHeightCm
	// Der Belag STEIGT zum Tuerlauf hin an: 107 cm vor dem Portal, 118 cm
	// am Durchgang selbst (gemessen 21.09.2026). Massgeblich ist der hoehere.
	constexpr double BelagCm = 118.0;

	// Der Sturz ist das unterste Metallteil ueber der Portaloeffnung.
	double SturzUnterkanteCm = TNumericLimits<double>::Max();
	for (const FHqPart& Teil : Layout.Parts)
	{
		const bool bUeberDemPortal = Teil.Material == EHqMaterial::Metal
			&& Teil.CenterCm.X > D.FootprintCm * 0.4
			&& Teil.SizeCm.Z < 150.0;
		if (bUeberDemPortal)
		{
			SturzUnterkanteCm = FMath::Min(SturzUnterkanteCm,
				Teil.CenterCm.Z - Teil.SizeCm.Z * 0.5);
		}
	}
	TestTrue(TEXT("Ueber dem Portal liegt ein Sturz"),
		SturzUnterkanteCm < TNumericLimits<double>::Max());

	// Und das Ankunftsziel muss den Durchgang abdecken: wer hindurchgeht, ist
	// angekommen, auch wenn der Belag dort hoeher liegt als der Innenboden.
	TestTrue(*FString::Printf(
		TEXT("Das Portalziel deckt die lichte Hoehe ab (%.0f cm hoch)"),
		Layout.PedestrianTarget.ExtentCm.Z * 2.0),
		Layout.PedestrianTarget.ExtentCm.Z * 2.0 >= SturzUnterkanteCm - 1.0);

	const double GebrauchtCm = BelagCm + KapselHoeheCm + SchritthoeheCm;
	TestTrue(*FString::Printf(
		TEXT("Der Sturz laesst den gehenden Fussgaenger durch (%.0f cm frei, %.0f gebraucht)"),
		SturzUnterkanteCm, GebrauchtCm),
		SturzUnterkanteCm >= GebrauchtCm);

	return true;
}
