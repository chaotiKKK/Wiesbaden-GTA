// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "GIS/PolygonUtils.h"
#include "GIS/WiesbadenRegionAssets.h"
#include "GIS/RoadNetworkTypes.h"

namespace
{
	FWiesbadenRegion MakeSquareRegion(const FString& Name, ECityRegionType Type,
		double X, double Y, double Size)
	{
		FWiesbadenRegion Region;
		Region.Name = Name;
		Region.Type = Type;
		Region.Polygon = {
			FVector2D(X, Y),
			FVector2D(X + Size, Y),
			FVector2D(X + Size, Y + Size),
			FVector2D(X, Y + Size),
		};
		Region.Bounds = FPolygonUtils::ComputeBounds2D(Region.Polygon);
		return Region;
	}

	bool IsPointInRegion(const FPlacedRegionAsset& Asset, const TArray<FWiesbadenRegion>& Regions)
	{
		const FWiesbadenRegion* Found = Regions.FindByPredicate(
			[&Asset](const FWiesbadenRegion& R) { return R.Name == Asset.RegionName; });
		if (!Found)
		{
			return false;
		}
		return FPolygonUtils::IsPointInPolygon(
			FVector2D(Asset.Location.X, Asset.Location.Y), Found->Polygon);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegionAssetCategoryTest,
	"WiesbadenReal.GIS.RegionAssets.Categories",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FRegionAssetCategoryTest::RunTest(const FString& Parameters)
{
	TArray<ERegionAssetCategory> Cats;

	UWiesbadenRegionAssetGenerator::GetCategoriesForRegion(ECityRegionType::Green, Cats);
	TestTrue(TEXT("Green -> nur Baeume"),
		Cats.Num() == 1 && Cats[0] == ERegionAssetCategory::Tree);

	UWiesbadenRegionAssetGenerator::GetCategoriesForRegion(ECityRegionType::Water, Cats);
	TestTrue(TEXT("Water -> nur Ufer"),
		Cats.Num() == 1 && Cats[0] == ERegionAssetCategory::Waterfront);

	UWiesbadenRegionAssetGenerator::GetCategoriesForRegion(ECityRegionType::Industrial, Cats);
	TestTrue(TEXT("Industrial -> Industrie"),
		Cats.Num() == 1 && Cats[0] == ERegionAssetCategory::Industrial);

	UWiesbadenRegionAssetGenerator::GetCategoriesForRegion(ECityRegionType::Commercial, Cats);
	TestTrue(TEXT("Commercial -> Industrie"),
		Cats.Num() == 1 && Cats[0] == ERegionAssetCategory::Industrial);

	UWiesbadenRegionAssetGenerator::GetCategoriesForRegion(ECityRegionType::Residential, Cats);
	TestTrue(TEXT("Residential -> keine Assets"), Cats.Num() == 0);

	UWiesbadenRegionAssetGenerator::GetCategoriesForRegion(ECityRegionType::Other, Cats);
	TestTrue(TEXT("Other -> keine Assets"), Cats.Num() == 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegionAssetGenerateTest,
	"WiesbadenReal.GIS.RegionAssets.Generate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FRegionAssetGenerateTest::RunTest(const FString& Parameters)
{
	TArray<FWiesbadenRegion> Regions;
	Regions.Add(MakeSquareRegion(TEXT("Kurpark"), ECityRegionType::Green, 0.0, 0.0, 3000.0));
	Regions.Add(MakeSquareRegion(TEXT("Rhein"), ECityRegionType::Water, 5000.0, 0.0, 2000.0));
	Regions.Add(MakeSquareRegion(TEXT("Industriegebiet"), ECityRegionType::Industrial, 9000.0, 0.0, 3000.0));
	Regions.Add(MakeSquareRegion(TEXT("Gewerbepark"), ECityRegionType::Commercial, 14000.0, 0.0, 3000.0));
	Regions.Add(MakeSquareRegion(TEXT("Wohngebiet"), ECityRegionType::Residential, 19000.0, 0.0, 3000.0));

	UWiesbadenRegionAssetGenerator* Generator = NewObject<UWiesbadenRegionAssetGenerator>();
	FRegionAssetLayout Layout;
	const FRegionAssetReport Report = Generator->Generate(
		Regions, /*HeightSampler=*/nullptr, FRegionAssetSettings(), Layout);

	TestTrue(TEXT("Pass erfolgreich"), Report.bSuccess);
	TestTrue(TEXT("Wohngebiet erzeugt keine Assets"), Report.RegionCount == 4);
	TestTrue(TEXT("Alle drei Kategorien vorhanden"),
		Report.TreeCount > 0 && Report.WaterfrontCount > 0 && Report.IndustrialCount > 0);

	TestTrue(TEXT("Alle Assets liegen in ihrer Region"),
		[&]() {
			for (const FPlacedRegionAsset& Asset : Layout.Assets)
			{
				if (!IsPointInRegion(Asset, Regions))
				{
					return false;
				}
			}
			return true;
		}());

	// Kategorie-Korrektheit je Region.
	const auto AllOfRegion = [&Layout](const FString& Name, ERegionAssetCategory Cat) {
		for (const FPlacedRegionAsset& Asset : Layout.Assets)
		{
			if (Asset.RegionName == Name && Asset.Category != Cat)
			{
				return false;
			}
		}
		return true;
	};
	TestTrue(TEXT("Kurpark: nur Baeume"), AllOfRegion(TEXT("Kurpark"), ERegionAssetCategory::Tree));
	TestTrue(TEXT("Rhein: nur Ufer"), AllOfRegion(TEXT("Rhein"), ERegionAssetCategory::Waterfront));
	TestTrue(TEXT("Industriegebiet+Gewerbepark: nur Industrie"),
		AllOfRegion(TEXT("Industriegebiet"), ERegionAssetCategory::Industrial)
		&& AllOfRegion(TEXT("Gewerbepark"), ERegionAssetCategory::Industrial));

	// Determinsmus: zweiter Lauf identisch.
	FRegionAssetLayout Layout2;
	Generator->Generate(Regions, /*HeightSampler=*/nullptr, FRegionAssetSettings(), Layout2);
	TestTrue(TEXT("Deterministisch: identische Platzierung"), Layout2.Assets.Num() == Layout.Assets.Num());
	for (int32 i = 0; i < Layout.Assets.Num(); ++i)
	{
		const FPlacedRegionAsset& A = Layout.Assets[i];
		const FPlacedRegionAsset& B = Layout2.Assets[i];
		TestTrue(TEXT("Deterministisch: gleiche Position"),
			A.Category == B.Category && A.Location.Equals(B.Location, 0.01f)
			&& A.YawDegrees == B.YawDegrees);
	}

	// Leere Eingaben.
	FRegionAssetLayout EmptyLayout;
	const FRegionAssetReport EmptyReport = Generator->Generate(
		TArray<FWiesbadenRegion>(), /*HeightSampler=*/nullptr, FRegionAssetSettings(), EmptyLayout);
	TestTrue(TEXT("Leere Regionen -> keine Assets, kein Crash"),
		EmptyReport.bSuccess && EmptyLayout.Assets.Num() == 0 && EmptyReport.RegionCount == 0);

	// Deaktivierte Kategorien.
	FRegionAssetSettings NoTrees;
	NoTrees.bPlaceTrees = false;
	FRegionAssetLayout NoTreeLayout;
	const FRegionAssetReport NoTreeReport = Generator->Generate(
		Regions, /*HeightSampler=*/nullptr, NoTrees, NoTreeLayout);
	TestTrue(TEXT("Ohne Baeume: keine Tree-Assets, Rest bleibt"),
		NoTreeReport.TreeCount == 0 && NoTreeReport.WaterfrontCount > 0 && NoTreeReport.IndustrialCount > 0);

	// -- Fahrbahn freihalten -------------------------------------------------
	//
	// Die Streuung kannte das Strassennetz bis hierher gar nicht: sie rastert
	// das Regionspolygon ab, und Landnutzungsflaechen ueberlappen Strassen
	// regelmaessig. Baeume standen dadurch mitten auf der Fahrbahn. Als
	// Fehler ist das nie aufgefallen, weil die Baum-Instanzen ausdruecklich
	// KEINE Kollision tragen - der Verkehr fuhr lautlos hindurch.
	{
		// Ein Segment quer durch ALLE Testregionen.
		//
		// Die Hoehe y = 1500 ist nicht beliebig: die Regionen oben reichen von
		// y = 0 bis 3000. Der erste Entwurf legte die Strasse auf y = 5000 -
		// vollstaendig ausserhalb - und der Test meldete "nichts weggelassen".
		// Das sah wie ein Fehler in der Freihaltung aus, war aber ein Fehler
		// in den Testdaten.
		FRoadNetwork Network;
		FRoadSegment Segment;
		Segment.SegmentId = 0;
		Segment.CarriagewayWidthCm = 650.0;
		Segment.SidewalkWidthCm = 250.0;
		Segment.Centerline = {
			FVector(0.0, 1500.0, 0.0),
			FVector(20000.0, 1500.0, 0.0),
		};
		Network.Segments.Add(Segment);

		FWiesbadenRoadClearance Clearance;
		Clearance.Build(Network, 150.0);

		TestTrue(TEXT("Index ist nicht leer"), !Clearance.IsEmpty());

		// Freihalteradius: 325 (halbe Fahrbahn) + 250 (Gehweg) + 150 = 725 cm.
		TestTrue(TEXT("Mitten auf der Fahrbahn -> gesperrt"),
			Clearance.IsBlocked(FVector2D(1500.0, 1500.0)));
		TestTrue(TEXT("Knapp neben der Fahrbahn -> gesperrt"),
			Clearance.IsBlocked(FVector2D(1500.0, 2100.0)));
		TestTrue(TEXT("Weit daneben -> frei"),
			!Clearance.IsBlocked(FVector2D(1500.0, 4000.0)));

		// Hinter dem Streckenende darf nicht endlos gesperrt sein - sonst
		// bliebe die Verlaengerung jeder Strasse baumfrei.
		TestTrue(TEXT("Jenseits des Segmentendes -> frei"),
			!Clearance.IsBlocked(FVector2D(28000.0, 1500.0)));

		// Ein Punkt exakt auf einem Endpunkt liegt auf der Fahrbahn.
		TestTrue(TEXT("Auf dem Endpunkt -> gesperrt"),
			Clearance.IsBlocked(FVector2D(20000.0, 1500.0)));

		// Und der Durchgriff auf den Streu-Pass: MIT Netz muessen Punkte
		// wegfallen, und der Bericht muss das melden. Meldet er 0, arbeitet
		// die Freihaltung nicht - sichtbar waere das erst, wenn ein Auto
		// durch einen Stamm faehrt.
		FRegionAssetLayout ClearLayout;
		const FRegionAssetReport ClearReport = Generator->GenerateClearOfRoads(
			Regions, /*HeightSampler=*/nullptr, FRegionAssetSettings(), Network, ClearLayout);

		TestTrue(TEXT("Mit Netz werden Punkte weggelassen"),
			ClearReport.SkippedOnRoadCount > 0);
		TestTrue(TEXT("Mit Netz stehen weniger Assets als ohne"),
			ClearReport.AssetCount < Report.AssetCount);

		// Kein einziges verbliebenes Objekt darf auf der Fahrbahn stehen.
		int32 StillOnRoad = 0;
		for (const FPlacedRegionAsset& Asset : ClearLayout.Assets)
		{
			if (Clearance.IsBlocked(FVector2D(Asset.Location.X, Asset.Location.Y)))
			{
				++StillOnRoad;
			}
		}
		TestEqual(TEXT("Kein Objekt bleibt auf der Fahrbahn"), StillOnRoad, 0);

		// Ohne Netz bleibt alles wie bisher - der alte Weg darf sich nicht
		// veraendert haben.
		TestEqual(TEXT("Ohne Netz nichts weggelassen"), Report.SkippedOnRoadCount, 0);
	}

	return true;
}
