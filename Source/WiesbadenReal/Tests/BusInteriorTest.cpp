// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "World/WiesbadenBusInterior.h"

/**
 * Der Innenraum des Fahrgast-Busses.
 *
 * Diese Pruefung entstand aus einem gemeldeten Fehler: "als Fahrgast im Bus
 * sieht man nichts und es scheint, als wird man mit dem Bus in die Luft
 * teleportiert". Ursache war die Kamera INNERHALB der geschlossenen Aussenhaut
 * ohne jeden Innenraum. Deshalb prueft der Test genau die Eigenschaften, die
 * den Fehler ausgemacht haben - und nicht die Zahlen einzelner Kaesten: der
 * Augenpunkt liegt im offenen Fensterband ueber der Fensterbank, es gibt ein
 * Dach ueber dem Kopf, ein Sitz unter dem Fahrgast, und KEIN Kasten enthaelt
 * den Augenpunkt (sonst schaut man wieder auf eine geschlossene Flaeche).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBusInteriorTest,
	"WiesbadenReal.Traffic.BusInterior",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBusInteriorTest::RunTest(const FString& Parameters)
{
	using namespace WiesbadenBusInterior;

	const FSpec Spec;   // Vorgaben = ESWE-Bus (8,27 m lang, 2,55 m breit, 2,25 m hoch)
	const FVector Eye = PassengerEyeCm(Spec);
	const FVector Seat = PassengerSeatCm(Spec);

	// -- Augenpunkt -----------------------------------------------------------
	TestTrue(TEXT("Augenpunkt liegt in der Kabine"),
		IsInsideBody(Spec, Eye, -1.0));

	// Fensterband: ueber der Fensterbank, unter dem Dachhimmel - nur dort kann
	// der Fahrgast hinaussehen.
	TestTrue(TEXT("Augenpunkt im offenen Fensterband"),
		Eye.Z > Spec.SillCm + 5.0 && Eye.Z < Spec.CeilingCm);

	// -- Kabine: Dach ueber dem Kopf, Sitz unter dem Fahrgast ----------------
	TArray<FPart> Boxes;
	BuildBoxes(Spec, Boxes);
	TestTrue(TEXT("Innenraum hat Kaesten"), Boxes.Num() > 20);

	int32 RoofBoxes = 0;
	int32 SeatBoxes = 0;
	int32 EyeBoxes = 0;
	bool bAllInside = true;
	bool bBoxesValid = true;

	for (const FPart& B : Boxes)
	{
		if (B.Max.X - B.Min.X <= 0.0 || B.Max.Y - B.Min.Y <= 0.0 || B.Max.Z - B.Min.Z <= 0.0)
		{
			bBoxesValid = false;
		}
		// Jeder Kasten braucht eine gueltige Flaechenart - sie waehlt Textur
		// und Mesh-Abschnitt; ein Wert ausserhalb der Liste landet still im
		// Technik-Abschnitt und traegt dort die falsche Oberflaeche.
		if ((int32)B.Tile >= TileCount)
		{
			bBoxesValid = false;
		}
		// Jede Flaeche muss INNERHALB der Aussenhaut liegen: sonst ragt der
		// Innenraum aus dem Bus heraus und ist von aussen zu sehen.
		for (const FVector& Corner : { B.Min, B.Max })
		{
			if (!IsInsideBody(Spec, Corner, 0.5))
			{
				bAllInside = false;
			}
		}

		const bool bCoversEyeXY = B.Min.X <= Eye.X && Eye.X <= B.Max.X
			&& B.Min.Y <= Eye.Y && Eye.Y <= B.Max.Y;
		if (bCoversEyeXY && B.Min.Z <= Eye.Z && Eye.Z <= B.Max.Z)
		{
			++EyeBoxes;   // Kamera in der Wand - genau der alte Fehler
		}
		if (bCoversEyeXY && B.Min.Z >= Spec.CeilingCm - 1.0 && B.Min.Z <= Spec.CeilingCm + 1.0)
		{
			++RoofBoxes;  // Dachhimmel ueber dem Kopf
		}
		if (bCoversEyeXY && FMath::IsNearlyEqual(B.Max.Z, Spec.FloorTopCm + 8.0, 0.5))
		{
			++SeatBoxes;  // Sitzflaeche unter dem Fahrgast
		}
	}

	TestTrue(TEXT("alle Kaesten liegen in der Aussenhaut"), bAllInside);
	TestTrue(TEXT("kein Kasten ist entartet"), bBoxesValid);
	TestTrue(TEXT("kein Kasten umschliesst den Augenpunkt (sonst Blick auf geschlossene Flaeche)"),
		EyeBoxes == 0);
	TestTrue(TEXT("Dachhimmel ueber dem Fahrgast"), RoofBoxes >= 1);
	TestTrue(TEXT("Sitzflaeche unter dem Fahrgast"), SeatBoxes >= 1);

	// -- Stehflaeche ----------------------------------------------------------
	const double StandEye = CabinFloorTopCm(Spec) + 90.0;   // halbe Kapsel des Fussgaengers
	TestTrue(TEXT("Standflaeche traegt den Fussgaenger"),
		FMath::IsNearlyEqual(CabinFloorTopCm(Spec), Spec.FloorTopCm, 0.001));
	TestTrue(TEXT("Fussgaenger passt stehend unter den Himmel"), StandEye < Spec.CeilingCm);
	TestTrue(TEXT("Sitz liegt auf der Standflaeche"), FMath::IsNearlyEqual(Seat.Z, Spec.FloorTopCm, 0.001));

	// -- Mesh: ein Abschnitt je Flaechenart --------------------------------
	// Der Innenraum ist nicht mehr ein einziger vertexgefaerbter Block, sondern
	// je Flaechenart ein Abschnitt mit eigener Textur. Geprueft wird darum:
	// (a) die Abschnitte enthalten zusammen genau alle Kaesten,
	// (b) jede Flaechenart kommt im Mesh vor (sonst bliebe eine Textur unbenutzt),
	// (c) die UVs sind ECHTE Flaechenkoordinaten in Kachelweite - keine
	//     (0,0)-(1,1)-Zuordnung mehr, bei der jede Textur auf jeder Flaeche
		//     anders verzerrt wird.
	const double TileCm = TexCmPerTile();
	TestTrue(TEXT("Kachelweite ist gesetzt"), TileCm > 1.0);

	int32 SectionBoxes = 0;
	int32 SectionVerts = 0;
	int32 UsedTiles = 0;
	int32 BadTiles = 0;
	int32 BadUVs = 0;
	int32 BadNormals = 0;
	int32 BadIndices = 0;

	auto Axis = [](const FVector& V, int32 I)
	{
		return I == 0 ? V.X : (I == 1 ? V.Y : V.Z);
	};

	for (int32 Index = 0; Index < TileCount; ++Index)
	{
		const ETile Tile = (ETile)Index;
		TArray<FVector> Vertices, Normals;
		TArray<int32> Triangles;
		TArray<FVector2D> UV0;
		TArray<FLinearColor> Colours;
		BuildSection(Spec, Tile, Vertices, Triangles, Normals, UV0, Colours);

		if (Vertices.Num() == 0) { continue; }
		++UsedTiles;
		SectionVerts += Vertices.Num();

		// Die Abschnittsgroesse muss zu den Kaesten DIESER Art passen.
		const int32 Expected = Boxes.FilterByPredicate(
			[Tile](const FPart& B) { return B.Tile == Tile; }).Num();
		SectionBoxes += Expected;
		if (Vertices.Num() != Expected * 24) { ++BadTiles; }
		if (Triangles.Num() != Expected * 36) { ++BadTiles; }
		if (Normals.Num() != Vertices.Num() || Colours.Num() != Vertices.Num()
			|| UV0.Num() != Vertices.Num()) { ++BadTiles; }

		for (int32 V = 0; V < Vertices.Num(); ++V)
		{
			const FVector& N = Normals[V];
			if (!FMath::IsNearlyEqual(N.Size(), 1.0, 0.001)) { ++BadNormals; }

			// Flaechenachse aus der Normalen: unten/oben X-Y, vorn/hinten X-Z,
			// links/rechts Y-Z - dieselbe Tabelle wie in AppendBox.
			int32 A0 = 0, A1 = 1;
			if (FMath::Abs(N.Z) > 0.5) { A0 = 0; A1 = 1; }
			else if (FMath::Abs(N.Y) > 0.5) { A0 = 0; A1 = 2; }
			else { A0 = 1; A1 = 2; }

			const FVector2D ExpectedUV(Axis(Vertices[V], A0) / TileCm, Axis(Vertices[V], A1) / TileCm);
			if (!UV0[V].Equals(ExpectedUV, 0.001)) { ++BadUVs; }
		}

		BadIndices += Triangles.FilterByPredicate(
			[&Vertices](int32 I) { return I < 0 || I >= Vertices.Num(); }).Num();
	}

	TestTrue(TEXT("die Abschnitte enthalten zusammen jeden Kasten"), SectionBoxes == Boxes.Num());
	TestTrue(TEXT("jede Flaechenart kommt im Mesh vor"), UsedTiles == TileCount);
	TestTrue(TEXT("Abschnittsgroessen passen zu den Kaesten der Art"), BadTiles == 0);
	TestTrue(TEXT("alle Dreiecksindizes gueltig"), BadIndices == 0);
	TestTrue(TEXT("Normalen sind Einheitsachsen"), BadNormals == 0);
	TestTrue(TEXT("UVs sind Flaechenkoordinaten in Kachelweite (Textur liegt nicht als (0,0)-(1,1) auf jeder Flaeche)"),
		BadUVs == 0);
	TestTrue(TEXT("das Mesh hat Ecken"), SectionVerts == Boxes.Num() * 24);

	// -- Materialnamen -------------------------------------------------------
	// Die Namen sind die einzige Verbindung zwischen diesem Modul, dem
	// Import-Skript und den Assets; doppelte oder leere Namen wuerden zwei
	// Flaechenarten dieselbe Textur geben.
	TArray<FString> Names;
	bool bNamesOk = true;
	for (int32 Index = 0; Index < TileCount; ++Index)
	{
		const FString Tex = TileTextureName((ETile)Index);
		const FString Mat = TileMaterialName((ETile)Index);
		bNamesOk = bNamesOk && !Tex.IsEmpty() && !Mat.IsEmpty() && Tex != Mat;
		bNamesOk = bNamesOk && TileMaterialPath((ETile)Index).Contains(Mat);
		Names.Add(Tex);
	}
	for (int32 A = 0; A < Names.Num(); ++A)
	{
		for (int32 B = A + 1; B < Names.Num(); ++B)
		{
			bNamesOk = bNamesOk && Names[A] != Names[B];
		}
	}
	TestTrue(TEXT("jede Flaechenart hat eigene Textur- und Materialnamen"), bNamesOk);

	// Ein zweiter Aufruf muss dasselbe liefern - der Innenraum wird bei jedem
	// Start neu gebaut, ein wechselndes Ergebnis waere ein Zufallsfehler.
	TArray<FPart> Again;
	BuildBoxes(Spec, Again);
	TestTrue(TEXT("Kastenbau ist wiederholbar"), Again.Num() == Boxes.Num()
		&& Again[0].Min.Equals(Boxes[0].Min) && Again[Again.Num() - 1].Max.Equals(Boxes[Boxes.Num() - 1].Max));

	// -- Andere Wagenlaengen -------------------------------------------------
	// Der Bus wird ueber BusLengthCm skaliert; der Innenraum muss mitgehen,
	// nicht aus dem Wagen laufen.
	for (const double Length : { 600.0, 1200.0 })
	{
		FSpec Other;
		Other.LengthCm = Length;
		TArray<FPart> OtherBoxes;
		BuildBoxes(Other, OtherBoxes);

		int32 Outside = 0;
		for (const FPart& B : OtherBoxes)
		{
			if (!IsInsideBody(Other, B.Min, 0.5) || !IsInsideBody(Other, B.Max, 0.5))
			{
				++Outside;
			}
		}
		TestTrue(FString::Printf(TEXT("Laenge %.0f cm: Innenraum bleibt im Wagen"), Length),
			Outside == 0 && OtherBoxes.Num() > 10);
		TestTrue(FString::Printf(TEXT("Laenge %.0f cm: Auge bleibt in der Kabine"), Length),
			IsInsideBody(Other, PassengerEyeCm(Other), -1.0));
	}

	return true;
}
