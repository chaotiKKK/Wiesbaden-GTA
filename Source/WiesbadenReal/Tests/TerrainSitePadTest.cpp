// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "GIS/RoadNetworkGenerator.h"
#include "GIS/TerrainGenerator.h"

namespace
{
	/** Ein Raster mit gleichmaessigem Hang in +X, 10 m Maschenweite. */
	FTerrainTile HangTile(double NeigungProCm = 0.2)
	{
		FTerrainTile Tile;
		Tile.GridSize = 41;
		Tile.CellSizeCm = 1000.0;
		Tile.WorldMinXY = FVector2D(-20000.0, -20000.0);
		Tile.HeightsCm.SetNumZeroed(Tile.GridSize * Tile.GridSize);
		for (int32 Y = 0; Y < Tile.GridSize; ++Y)
		{
			for (int32 X = 0; X < Tile.GridSize; ++X)
			{
				const FVector2D P = Tile.CellToWorld(X, Y);
				Tile.SetHeightCm(X, Y, static_cast<float>(P.X * NeigungProCm));
			}
		}
		return Tile;
	}

	/** Eine gerade Fahrbahn auf fester Hoehe, quer zum Hang. */
	FRoadNetwork StrasseAuf(double HoeheCm)
	{
		FRoadNetwork Network;
		FRoadSegment Segment;
		Segment.SegmentId = 1;
		Segment.Centerline.Add(FVector(-10000.0, 8000.0, HoeheCm));
		Segment.Centerline.Add(FVector(10000.0, 8000.0, HoeheCm));
		Network.Segments.Add(Segment);
		return Network;
	}

	FTerrainSitePad Plateau()
	{
		FTerrainSitePad Pad;
		Pad.CenterCm = FVector2D::ZeroVector;
		Pad.RadiusCm = 2500.0;
		Pad.SlopeRunCm = 2500.0;
		Pad.RoadAnchorCm = FVector2D(0.0, 7000.0);
		Pad.RoadSearchRadiusCm = 5000.0;
		Pad.AccessFloorCm = 60.0;
		return Pad;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerrainSitePadTest,
	"WiesbadenReal.GIS.Terrain.SitePad",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTerrainSitePadTest::RunTest(const FString& Parameters)
{
	// Der Fall, den es gibt: ein zur Laufzeit gespawntes Bauwerk am Hang.
	// FlattenUnderBuildings kennt nur OSM-Grundrisse und laesst es stehen.
	UTerrainGenerator* Generator = NewObject<UTerrainGenerator>();

	FTerrainTile Tile = HangTile();
	FTerrainGenerationSettings Settings;
	Settings.SitePads.Add(Plateau());

	const float VorherAussen = Tile.GetHeightCm(Tile.GridSize - 1, Tile.GridSize / 2);
	const int32 Changed = Generator->FlattenSitePads(StrasseAuf(500.0), Settings, Tile);
	TestTrue(TEXT("Das Plateau hat Zellen veraendert"), Changed > 0);

	// --- Die ebene Flaeche liegt auf EINER Hoehe -----------------------------
	//
	// Und zwar auf der aus der Fahrbahn abgeleiteten: 500 cm Fahrbahn minus
	// 60 cm Bodenhoehe. Ein Plateau auf eigener Rechnung waere eine zweite
	// Wahrheit neben dem Strassennetz.
	const double SollCm = 500.0 - 60.0;
	for (int32 Y = 0; Y < Tile.GridSize; ++Y)
	{
		for (int32 X = 0; X < Tile.GridSize; ++X)
		{
			const FVector2D P = Tile.CellToWorld(X, Y);
			if (P.Size() <= 2500.0)
			{
				TestEqual(TEXT("Stuetzpunkt im Plateau liegt auf der Zufahrtshoehe"),
					static_cast<double>(Tile.GetHeightCm(X, Y)), SollCm, 0.6);
			}
		}
	}

	// --- Ausserhalb der Boeschung bleibt das Gelaende unberuehrt -------------
	TestEqual(TEXT("Jenseits der Boeschung bleibt der Hang stehen"),
		Tile.GetHeightCm(Tile.GridSize - 1, Tile.GridSize / 2), VorherAussen, 0.6f);

	// --- Die Boeschung vermittelt, sie steht nicht als Kante -----------------
	//
	// Genau hier scheiterte schon die Strasseneinebnung: faellt der Uebergang
	// zwischen zwei Stuetzpunkte, interpoliert das Landscape darueber hinweg
	// und die Kante bleibt.
	//
	// Gemessen wird deshalb GEGEN DEN KAPUTTEN STAND, nicht gegen eine
	// ausgedachte Zahl: dasselbe Plateau ohne Boeschung ergibt die Kante, die
	// es zu vermeiden gilt. Eine feste Schranke haette hier nur gesagt, dass
	// der Hang zufaellig flach genug war.
	const auto GroessterSprungCm = [](const FTerrainTile& T)
	{
		double Max = 0.0;
		for (int32 Y = 0; Y < T.GridSize; ++Y)
		{
			for (int32 X = 0; X + 1 < T.GridSize; ++X)
			{
				Max = FMath::Max(Max,
					FMath::Abs(static_cast<double>(T.GetHeightCm(X + 1, Y) - T.GetHeightCm(X, Y))));
			}
		}
		return Max;
	};

	FTerrainTile OhneBoeschung = HangTile();
	FTerrainGenerationSettings Steil;
	FTerrainSitePad Kante = Plateau();
	Kante.SlopeRunCm = 0.0;
	Steil.SitePads.Add(Kante);
	Generator->FlattenSitePads(StrasseAuf(500.0), Steil, OhneBoeschung);

	const double MitCm = GroessterSprungCm(Tile);
	const double OhneCm = GroessterSprungCm(OhneBoeschung);

	TestTrue(*FString::Printf(
		TEXT("Ohne Boeschung entsteht eine Kante (%.0f cm)"), OhneCm),
		OhneCm > 900.0);
	TestTrue(*FString::Printf(
		TEXT("Die Boeschung halbiert den groessten Sprung mindestens (%.0f statt %.0f cm)"),
		MitCm, OhneCm),
		MitCm <= OhneCm * 0.75);

	// Und sie faellt nicht zwischen die Stuetzpunkte: bei 25 m Boeschung und
	// 10 m Maschenweite muessen mehrere Zwischenwerte entstehen.
	int32 Zwischenwerte = 0;
	const double SollPlateau = 500.0 - 60.0;
	for (int32 X = 0; X < Tile.GridSize; ++X)
	{
		const double H = Tile.GetHeightCm(X, Tile.GridSize / 2);
		const double Gewachsen = Tile.CellToWorld(X, Tile.GridSize / 2).X * 0.2;
		const bool bIstPlateau = FMath::IsNearlyEqual(H, SollPlateau, 1.0);
		const bool bIstGewachsen = FMath::IsNearlyEqual(H, Gewachsen, 1.0);
		Zwischenwerte += (!bIstPlateau && !bIstGewachsen) ? 1 : 0;
	}
	TestTrue(*FString::Printf(
		TEXT("Die Boeschung hat eigene Stuetzpunkte (%d)"), Zwischenwerte),
		Zwischenwerte >= 4);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerrainSitePadOhneStrasseTest,
	"WiesbadenReal.GIS.Terrain.SitePadOhneStrasse",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTerrainSitePadOhneStrasseTest::RunTest(const FString& Parameters)
{
	// Ohne Fahrbahn am Anker wird NICHT geraten. Ein Plateau auf geratener
	// Hoehe waere ein zweiter, stiller Hang - schlimmer als gar keines, weil
	// es aussieht, als sei das Grundstueck hergerichtet.
	UTerrainGenerator* Generator = NewObject<UTerrainGenerator>();

	FTerrainTile Tile = HangTile();
	const TArray<float> Vorher = Tile.HeightsCm;

	FTerrainGenerationSettings Settings;
	FTerrainSitePad Pad = Plateau();
	Pad.RoadAnchorCm = FVector2D(0.0, 7000.0);
	Pad.RoadSearchRadiusCm = 100.0;     // die Strasse liegt 10 m entfernt
	Settings.SitePads.Add(Pad);

	const int32 Changed = Generator->FlattenSitePads(StrasseAuf(500.0), Settings, Tile);
	TestEqual(TEXT("Ohne Fahrbahn am Anker bleibt das Gelaende unveraendert"), Changed, 0);
	TestTrue(TEXT("Kein einziger Stuetzpunkt wurde angefasst"), Tile.HeightsCm == Vorher);

	// Und abgeschaltet tut der Durchgang gar nichts.
	Settings.bFlattenSitePads = false;
	FTerrainSitePad Nah = Plateau();
	Settings.SitePads.Reset();
	Settings.SitePads.Add(Nah);
	TestEqual(TEXT("Abgeschaltet aendert der Durchgang nichts"),
		Generator->FlattenSitePads(StrasseAuf(500.0), Settings, Tile), 0);

	return true;
}
