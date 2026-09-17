// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/WiesbadenBusInterior.h"

namespace WiesbadenBusInterior
{
	namespace
	{
		// Farben des Innenraums (ESWE-Stadtbus: heller Kasten, dunkler Boden,
		// blaue Sitze, gelbe Haltestangen). Dieselbe Groessenordnung wie die
		// Vertexfarben der Nerotalbahn.
		//
		// Farbe UND Oberflaeche stehen hier in EINER Zeile: die Farbe kommt als
		// Vertexfarbe ins Mesh, die Flaechenart waehlt Textur und Mesh-Abschnitt.
		// Eine Flaeche, die beides getrennt fuehrt, wuerde frueher oder spaeter
		// mit dem falschen Stoff dastehen.
		struct FSurface
		{
			FLinearColor Colour;
			ETile Tile;
		};

		// Fussboden: Linoleum ist ein eigenes Material (Fugen, Koernung).
		const FSurface FloorDark{ FLinearColor(0.20f, 0.21f, 0.22f), ETile::Boden };
		// Waende und Heckwand: beplankt.
		const FSurface WallLight{ FLinearColor(0.72f, 0.72f, 0.70f), ETile::Wand };
		// Fensterbank, Haltestangen, Armaturenbrett, Fahrerplatz und die dunkle
		// Heckscheibe laufen unter "Technik": gebuerstetes Metall bzw. Kunststoff.
		const FSurface SillDark{ FLinearColor(0.28f, 0.29f, 0.30f), ETile::Technik };
		const FSurface CeilingLight{ FLinearColor(0.84f, 0.84f, 0.82f), ETile::Decke };
		const FSurface CeilingStrip{ FLinearColor(0.95f, 0.95f, 0.92f), ETile::Decke };
		const FSurface SeatBlue{ FLinearColor(0.15f, 0.22f, 0.45f), ETile::Sitz };
		const FSurface SeatBack{ FLinearColor(0.18f, 0.26f, 0.50f), ETile::Sitz };
		const FSurface RailYellow{ FLinearColor(0.72f, 0.58f, 0.12f), ETile::Technik };
		const FSurface DashDark{ FLinearColor(0.16f, 0.17f, 0.19f), ETile::Technik };
		const FSurface CabDark{ FLinearColor(0.12f, 0.13f, 0.15f), ETile::Technik };
		const FSurface GlassDark{ FLinearColor(0.09f, 0.11f, 0.14f), ETile::Technik };

		/** Innenkante der Sitzflaeche (Gangseite) - alles davor ist Mittelgang. */
		constexpr double AisleHalfWidthCm = 30.0;
		/** Sitzflaeche ueber dem Fusboden. */
		constexpr double CushionTopCm = 8.0;
		/** Hoehe der Rueckenlehne ueber der Sitzflaeche. */
		constexpr double BackRestHeightCm = 62.0;
		/** Länge der Sitzflaeche nach vorn. */
		constexpr double CushionDepthCm = 62.0;
		/** Augenhoehe eines sitzenden Menschen ueber der Sitzflaeche. */
		constexpr double SeatedEyeCm = 88.0;
		/** Dicke von Wand, Himmel und Anzeigetafeln. */
		constexpr double PanelCm = 7.0;
		/** Fensterbank-Planke ueber der Wand. */
		constexpr double SillPlankCm = 5.0;


		double XNose(const FSpec& S) { return S.LengthCm * 0.5; }
		double XTail(const FSpec& S) { return -S.LengthCm * 0.5; }

		/** Ruckenlehne der vordersten Sitzreihe = Fahrgastreihe. */
		double FirstRowXCm(const FSpec& S) { return XNose(S) - S.FirstRowFromNoseCm; }

		/** Mitte der Sitzflaeche einer Seite (Sitz aussen, Gang innen). */
		double SeatCentreYCm(const FSpec& S, double Sign)
		{
			const double Inner = (double)AisleHalfWidthCm;
			const double Outer = S.HalfWidthCm - 12.0;
			return Sign * (Inner + Outer) * 0.5;
		}

		/**
		 * Textur und Material je Flaechenart - die EINZIGE Liste.
		 *
		 * Die Reihenfolge ist die Abschnittsnummer im Mesh; Tools/import_bus_interior.py
		 * legt die Materialien aus denselben Namen an, und die PNGs dazu macht
		 * Tools/make_bus_interior_textures.py. Ein neues Material braucht also genau
		 * hier eine Zeile, eine Textur dort und eine Flaeche, die es benutzt.
		 */
		struct FTileAssets
		{
			ETile Tile;
			const TCHAR* Texture;
			const TCHAR* Material;
		};

		const FTileAssets TileAssets[] = {
			{ ETile::Boden,   TEXT("T_WbBusIntBoden"),   TEXT("M_WbBusIntBoden") },
			{ ETile::Sitz,    TEXT("T_WbBusIntSitz"),    TEXT("M_WbBusIntSitz") },
			{ ETile::Wand,    TEXT("T_WbBusIntWand"),    TEXT("M_WbBusIntWand") },
			{ ETile::Decke,   TEXT("T_WbBusIntDecke"),   TEXT("M_WbBusIntDecke") },
			{ ETile::Technik, TEXT("T_WbBusIntTechnik"), TEXT("M_WbBusIntTechnik") },
		};

		const FTileAssets& TileAssetFor(ETile Tile)
		{
			for (const FTileAssets& A : TileAssets)
			{
				if (A.Tile == Tile) { return A; }
			}
			// Unbekannte Art (z. B. ETile::Count als Platzhalter): Technik ist die
			// neutralste Oberflaeche - lieber Metall als eine unsichtbare Flaeche.
			return TileAssets[UE_ARRAY_COUNT(TileAssets) - 1];
		}
	}

	const TCHAR* const AssetDir = TEXT("/Game/Vehicles/Bus/Interior");

	double TexCmPerTile()
	{
		// Eine 512-px-Textur deckt 200 cm ab (2,6 px/cm). Bei 100 cm waere das
		// Fugenraster des Bodens schon alle 25 cm sichtbar wiederholt, bei 400 cm
		// verschmaehrt die Sitzstoff-Bindung zu Flecken.
		return 200.0;
	}

	const TCHAR* TileTextureName(ETile Tile)
	{
		return TileAssetFor(Tile).Texture;
	}

	const TCHAR* TileMaterialName(ETile Tile)
	{
		return TileAssetFor(Tile).Material;
	}

	double CabinFloorTopCm(const FSpec& Spec)
	{
		return Spec.FloorTopCm;
	}

	FVector PassengerSeatCm(const FSpec& Spec)
	{
		// Der Fahrgast sitzt auf der vordersten Sitzreihe rechts (Fensterplatz):
		// Die Ruckenlehne liegt hinter ihm, die Sitzflaeche nach vorn.
		return FVector(FirstRowXCm(Spec) + 18.0, SeatCentreYCm(Spec, 1.0), Spec.FloorTopCm);
	}

	FVector PassengerEyeCm(const FSpec& Spec)
	{
		const FVector Seat = PassengerSeatCm(Spec);
		return FVector(Seat.X, Seat.Y, Spec.FloorTopCm + CushionTopCm + SeatedEyeCm);
	}

	bool IsInsideBody(const FSpec& Spec, const FVector& PointCm, double ToleranceCm)
	{
		return FMath::Abs(PointCm.X) <= Spec.LengthCm * 0.5 + ToleranceCm
			&& FMath::Abs(PointCm.Y) <= Spec.HalfWidthCm + ToleranceCm
			&& PointCm.Z >= -ToleranceCm
			&& PointCm.Z <= Spec.HeightCm + ToleranceCm;
	}

	void BuildBoxes(const FSpec& Spec, TArray<FPart>& OutBoxes)
	{
		OutBoxes.Reset();

		const double Nose = XNose(Spec);
		const double Tail = XTail(Spec);
		const double YOut = Spec.HalfWidthCm;
		const double Floor = Spec.FloorTopCm;
		const double Sill = Spec.SillCm;
		const double Ceil = Spec.CeilingCm;

		auto Box = [&OutBoxes](const FVector& Min, const FVector& Max, const FSurface& Surface)
		{
			FPart B;
			B.Min = FVector(FMath::Min(Min.X, Max.X), FMath::Min(Min.Y, Max.Y), FMath::Min(Min.Z, Max.Z));
			B.Max = FVector(FMath::Max(Min.X, Max.X), FMath::Max(Min.Y, Max.Y), FMath::Max(Min.Z, Max.Z));
			B.Colour = Surface.Colour;
			B.Tile = Surface.Tile;
			OutBoxes.Add(B);
		};

		// Der Fahrgast soll nicht durch die eigene Sitzlehne schauen: der
		// Innenraum endet vorn an der Armaturentafel und hinten an der Heckwand.
		const double XBodyFront = Nose - 14.0;
		const double XBodyRear = Tail + PanelCm;

		// Die Waende reichen bis an die (fuer den Fahrgast ausgeblendete) Haut:
		// bliebe ein Spalt, schaute man durch den Bus hindurch auf die Strasse.
		// Fenster bleiben offen - genau dafuer ist der Streifen zwischen
		// Fensterbank und Dachhimmel frei.
		const double YWallInner = YOut - PanelCm;

		// -- Fusboden, Seitenwaende, Fensterbank ------------------------------
		Box(FVector(Tail + 4.0, -YOut, Floor - 4.0), FVector(Nose - 4.0, YOut, Floor), FloorDark);
		for (const double Sign : { -1.0, 1.0 })
		{
			Box(FVector(Tail + 4.0, Sign > 0.0 ? YWallInner : -YOut, Floor),
				FVector(Nose - 4.0, Sign > 0.0 ? YOut : -YWallInner, Sill), WallLight);
			// Fensterbank: ragt leicht in den Raum, damit der Arm aufliegen kann.
			Box(FVector(Tail + 4.0, Sign > 0.0 ? YWallInner - 4.0 : -YOut, Sill),
				FVector(Nose - 4.0, Sign > 0.0 ? YOut : -YWallInner + 4.0, Sill + SillPlankCm), SillDark);
		}

		// -- Dachhimmel und Lichtbaender --------------------------------------
		Box(FVector(Tail + 4.0, -YOut, Ceil), FVector(Nose - 4.0, YOut, Ceil + PanelCm), CeilingLight);
		for (const double Sign : { -1.0, 1.0 })
		{
			Box(FVector(Tail + 20.0, Sign * 45.0, Ceil - 2.0), FVector(Nose - 30.0, Sign * 95.0, Ceil),
				CeilingStrip);
		}

		// -- Fensterpfeiler (Fenster bleiben offen - der Fahrgast soll sehen) --
		const double FirstRowX = FirstRowXCm(Spec);
		const int32 Rows = FMath::Max(1, FMath::FloorToInt(
			(FirstRowX - (Tail + 50.0)) / Spec.SeatPitchCm) + 1);
		for (int32 Row = 0; Row < Rows; ++Row)
		{
			const double Px = FirstRowX - 25.0 - Row * Spec.SeatPitchCm;
			for (const double Sign : { -1.0, 1.0 })
			{
				Box(FVector(Px - 6.0, Sign > 0.0 ? YWallInner : -YOut, Sill + SillPlankCm),
					FVector(Px + 6.0, Sign > 0.0 ? YOut : -YWallInner, Ceil), CeilingLight);
			}
		}

		// -- Sitzreihen (Rueckenlehne + Sitzflaeche, beidseits des Gangs) ------
		for (int32 Row = 0; Row < Rows; ++Row)
		{
			const double B = FirstRowX - Row * Spec.SeatPitchCm;
			for (const double Sign : { -1.0, 1.0 })
			{
				const double Y0 = Sign > 0.0 ? AisleHalfWidthCm : -(YOut - 12.0);
				const double Y1 = Sign > 0.0 ? (YOut - 12.0) : -AisleHalfWidthCm;
				Box(FVector(B, Y0, Floor + 2.0), FVector(B + CushionDepthCm, Y1, Floor + CushionTopCm),
					SeatBlue);
				Box(FVector(B - 8.0, Y0, Floor + CushionTopCm),
					FVector(B, Y1, Floor + CushionTopCm + BackRestHeightCm), SeatBack);
			}
		}

		// -- Haltestangen -----------------------------------------------------
		for (int32 Row = 0; Row < Rows; ++Row)
		{
			const double Px = FirstRowX - 30.0 - Row * Spec.SeatPitchCm;
			for (const double Sign : { -1.0, 1.0 })
			{
				Box(FVector(Px - 3.0, Sign * 30.0 - 3.0, Floor), FVector(Px + 3.0, Sign * 30.0 + 3.0, Ceil),
					RailYellow);
			}
		}
		for (const double Sign : { -1.0, 1.0 })
		{
			Box(FVector(Tail + 20.0, Sign * (YOut - 40.0) - 3.0, Ceil - 16.0),
				FVector(Nose - 30.0, Sign * (YOut - 40.0) + 3.0, Ceil - 12.0), RailYellow);
		}

		// -- Heckwand mit dunklem Heckfenster ---------------------------------
		Box(FVector(Tail, -YOut, Floor), FVector(XBodyRear, YOut, Sill), WallLight);
		Box(FVector(Tail, -YOut, Sill), FVector(XBodyRear, YOut, Ceil), WallLight);
		Box(FVector(XBodyRear - 2.5, -90.0, Sill + 10.0), FVector(XBodyRear + 0.5, 90.0, Ceil - 20.0),
			GlassDark);

		// -- Front: Armaturentafel, A-Saeulen, Dachkante ----------------------
		Box(FVector(XBodyFront, -YOut, Floor), FVector(Nose, YOut, Spec.DashTopCm), DashDark);
		Box(FVector(XBodyFront, -YOut, Ceil - 10.0), FVector(Nose, YOut, Ceil), CeilingLight);
		for (const double Sign : { -1.0, 1.0 })
		{
			Box(FVector(XBodyFront, Sign > 0.0 ? YWallInner : -YOut, Spec.DashTopCm),
				FVector(Nose, Sign > 0.0 ? YOut : -YWallInner, Ceil - 10.0), CeilingLight);
		}

		// -- Fahrerplatz (links): Instrumententafel + Sitzlehne ---------------
		Box(FVector(XBodyFront - 46.0, -YOut + 8.0, Spec.DashTopCm - 6.0),
			FVector(XBodyFront, -20.0, Spec.DashTopCm + 4.0), CabDark);
		Box(FVector(XBodyFront - 100.0, -105.0, Floor + CushionTopCm),
			FVector(XBodyFront - 92.0, -45.0, Floor + CushionTopCm + BackRestHeightCm + 10.0), CabDark);
	}

	void AppendBox(TArray<FVector>& Vertices, TArray<int32>& Triangles, TArray<FVector>& Normals,
		TArray<FVector2D>& UV0, TArray<FLinearColor>& Colours, const FPart& Box)
	{
		const FVector P[8] = {
			{ Box.Min.X, Box.Min.Y, Box.Min.Z }, { Box.Max.X, Box.Min.Y, Box.Min.Z },
			{ Box.Max.X, Box.Max.Y, Box.Min.Z }, { Box.Min.X, Box.Max.Y, Box.Min.Z },
			{ Box.Min.X, Box.Min.Y, Box.Max.Z }, { Box.Max.X, Box.Min.Y, Box.Max.Z },
			{ Box.Max.X, Box.Max.Y, Box.Max.Z }, { Box.Min.X, Box.Max.Y, Box.Max.Z },
		};
		// Sechs Flaechen, je zwei Dreiecke, mit nach aussen zeigenden Normalen
		// (dasselbe Muster wie der Wagenkasten der Nerotalbahn).
		const int32 Faces[6][4] = {
			{ 0, 1, 2, 3 },   // unten  (-Z)
			{ 4, 7, 6, 5 },   // oben   (+Z)
			{ 0, 4, 5, 1 },   // vorn   (-Y)
			{ 3, 2, 6, 7 },   // hinten (+Y)
			{ 0, 3, 7, 4 },   // links  (-X)
			{ 1, 5, 6, 2 },   // rechts (+X)
		};
		const FVector FaceNormals[6] = {
			{ 0, 0, -1 }, { 0, 0, 1 }, { 0, -1, 0 },
			{ 0, 1, 0 }, { -1, 0, 0 }, { 1, 0, 0 },
		};
		// Die beiden Achsen, die die jeweilige Flaeche AUFSPANNEN (X=0, Y=1, Z=2).
		const int32 FaceAxes[6][2] = {
			{ 0, 1 }, { 0, 1 },   // unten / oben   -> X, Y
			{ 0, 2 }, { 0, 2 },   // vorn / hinten  -> X, Z
			{ 1, 2 }, { 1, 2 },   // links / rechts -> Y, Z
		};
		const double TileCm = TexCmPerTile();
		auto Axis = [](const FVector& V, int32 I)
		{
			return I == 0 ? V.X : (I == 1 ? V.Y : V.Z);
		};

		for (int32 F = 0; F < 6; ++F)
		{
			// Die UV-Achsen liegen IN der Flaeche: unten/oben X-Y, vorn/hinten
			// X-Z, links/rechts Y-Z. Damit steht die Maserung auf der Wand aufrecht
			// und die Bodenfugen laufen in Fahrtrichtung - eine feste (0,0)-(1,1)-
			// Zuordnung wuerde dieselbe Textur je Flaeche anders verzerren.
			const int32 Ai = FaceAxes[F][0];
			const int32 Bi = FaceAxes[F][1];
			const int32 Base = Vertices.Num();
			for (int32 C = 0; C < 4; ++C)
			{
				const FVector& V = P[Faces[F][C]];
				Vertices.Add(V);
				Normals.Add(FaceNormals[F]);
				// Kachelweite: absolute Wagenkoordinate / 200 cm (nicht kastenrelativ),
				// damit das Muster ueber benachbarte Kaesten fortlaeuft.
				UV0.Add(FVector2D(Axis(V, Ai), Axis(V, Bi)) / TileCm);
				Colours.Add(Box.Colour);
			}
			Triangles.Append({ Base, Base + 1, Base + 2, Base, Base + 2, Base + 3 });
		}
	}

	void BuildSection(const FSpec& Spec, ETile Tile, TArray<FVector>& Vertices, TArray<int32>& Triangles,
		TArray<FVector>& Normals, TArray<FVector2D>& UV0, TArray<FLinearColor>& Colours)
	{
		Vertices.Reset();
		Triangles.Reset();
		Normals.Reset();
		UV0.Reset();
		Colours.Reset();

		TArray<FPart> Boxes;
		BuildBoxes(Spec, Boxes);
		for (const FPart& B : Boxes)
		{
			if (B.Tile != Tile) { continue; }
			AppendBox(Vertices, Triangles, Normals, UV0, Colours, B);
		}
	}

	FString TileMaterialPath(ETile Tile)
	{
		const FString Name = TileMaterialName(Tile);
		return FString::Printf(TEXT("%s/%s.%s"), AssetDir, *Name, *Name);
	}
}
