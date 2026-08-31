// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "GIS/BuildingGenerator.h"
#include "GIS/GeoCoordinateConverter.h"
#include "GIS/OSMTypes.h"
#include "GIS/PolygonUtils.h"
#include "GIS/WiesbadenRegion.h"

namespace
{
	UGeoCoordinateConverter* NewRegionConverter()
	{
		UGeoCoordinateConverter* Converter = NewObject<UGeoCoordinateConverter>();
		Converter->InitializeWithWiesbadenOrigin();
		return Converter;
	}

	/** Weg mit einem Region-Tag (landuse/natural/leisure/waterway). */
	FOSMWay MakeRegionWay(FOSMId Id, const TArray<FOSMId>& NodeIds,
		const FName& TagKey, const FString& TagValue, const FString& Name)
	{
		FOSMWay Way;
		Way.Id = Id;
		Way.NodeIds = NodeIds;
		Way.Tags.Add(TagKey, TagValue);
		Way.Tags.Add(TEXT("name"), Name);
		return Way;
	}

	/** Lon/Lat -> Weltkoordinaten (2D). */
	FVector2D ToWorld2D(UGeoCoordinateConverter* Converter, double Lon, double Lat)
	{
		const FVector World = Converter->GeoToUnrealGround(FGeoCoordinate(Lon, Lat));
		return FVector2D(World.X, World.Y);
	}

	/** Fuegt ein geschlossenes Quadrat aus vier Ecken (lon/lat) als Nodes ein. */
	TArray<FOSMId> AddSquare(FOSMDataSet& DataSet, FOSMId FirstNodeId,
		double LonMin, double LonMax, double LatMin, double LatMax)
	{
		const TArray<FOSMId> Ids = { FirstNodeId, FirstNodeId + 1, FirstNodeId + 2, FirstNodeId + 3, FirstNodeId };
		const TArray<FGeoCoordinate> Corners = {
			FGeoCoordinate(LonMin, LatMin),
			FGeoCoordinate(LonMax, LatMin),
			FGeoCoordinate(LonMax, LatMax),
			FGeoCoordinate(LonMin, LatMax),
			FGeoCoordinate(LonMin, LatMin),
		};
		for (int32 i = 0; i < Ids.Num(); ++i)
		{
			DataSet.Nodes.Add(Ids[i], FOSMNode(Ids[i], Corners[i].Longitude, Corners[i].Latitude));
		}
		return Ids;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegionClassifyTagsTest,
	"WiesbadenReal.GIS.Regions.ClassifyTags",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FRegionClassifyTagsTest::RunTest(const FString& Parameters)
{
	// Wasser.
	{
		TMap<FName, FString> Tags;
		Tags.Add(TEXT("natural"), TEXT("water"));
		TestEqual(TEXT("natural=water -> Water"),
			UWiesbadenRegionGenerator::ClassifyTags(Tags), ECityRegionType::Water);
	}
	{
		TMap<FName, FString> Tags;
		Tags.Add(TEXT("landuse"), TEXT("reservoir"));
		TestEqual(TEXT("landuse=reservoir -> Water"),
			UWiesbadenRegionGenerator::ClassifyTags(Tags), ECityRegionType::Water);
	}
	{
		TMap<FName, FString> Tags;
		Tags.Add(TEXT("water"), TEXT("lake"));
		TestEqual(TEXT("water=lake -> Water"),
			UWiesbadenRegionGenerator::ClassifyTags(Tags), ECityRegionType::Water);
	}

	// Gruen.
	{
		TMap<FName, FString> Tags;
		Tags.Add(TEXT("leisure"), TEXT("park"));
		TestEqual(TEXT("leisure=park -> Green"),
			UWiesbadenRegionGenerator::ClassifyTags(Tags), ECityRegionType::Green);
	}
	{
		TMap<FName, FString> Tags;
		Tags.Add(TEXT("landuse"), TEXT("forest"));
		TestEqual(TEXT("landuse=forest -> Green"),
			UWiesbadenRegionGenerator::ClassifyTags(Tags), ECityRegionType::Green);
	}

	// Wohnen / Gewerbe / Industrie.
	{
		TMap<FName, FString> Tags;
		Tags.Add(TEXT("landuse"), TEXT("residential"));
		TestEqual(TEXT("landuse=residential -> Residential"),
			UWiesbadenRegionGenerator::ClassifyTags(Tags), ECityRegionType::Residential);
	}
	{
		TMap<FName, FString> Tags;
		Tags.Add(TEXT("landuse"), TEXT("commercial"));
		TestEqual(TEXT("landuse=commercial -> Commercial"),
			UWiesbadenRegionGenerator::ClassifyTags(Tags), ECityRegionType::Commercial);
	}
	{
		TMap<FName, FString> Tags;
		Tags.Add(TEXT("landuse"), TEXT("industrial"));
		TestEqual(TEXT("landuse=industrial -> Industrial"),
			UWiesbadenRegionGenerator::ClassifyTags(Tags), ECityRegionType::Industrial);
	}

	// Irrelevant.
	{
		TMap<FName, FString> Tags;
		Tags.Add(TEXT("building"), TEXT("yes"));
		TestEqual(TEXT("building=yes -> Other"),
			UWiesbadenRegionGenerator::ClassifyTags(Tags), ECityRegionType::Other);
	}
	{
		TMap<FName, FString> Empty;
		TestEqual(TEXT("Ohne Tags -> Other"),
			UWiesbadenRegionGenerator::ClassifyTags(Empty), ECityRegionType::Other);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegionTypeCountsTest,
	"WiesbadenReal.GIS.Regions.TypeCounts",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FRegionTypeCountsTest::RunTest(const FString& Parameters)
{
	// Leere Liste -> alle Zaehler 0 (auch wenn die Ausgangswerte anders sind).
	{
		TArray<FWiesbadenRegion> Regions;
		int32 Water = -1, Green = -1, Residential = -1, Commercial = -1, Industrial = -1;
		UWiesbadenRegionGenerator::GetRegionTypeCounts(
			Regions, Water, Green, Residential, Commercial, Industrial);
		TestEqual(TEXT("Leer: Wasser"), Water, 0);
		TestEqual(TEXT("Leer: Gruen"), Green, 0);
		TestEqual(TEXT("Leer: Wohnen"), Residential, 0);
		TestEqual(TEXT("Leer: Gewerbe"), Commercial, 0);
		TestEqual(TEXT("Leer: Industrie"), Industrial, 0);
	}

	// Gemischte Liste: jeder Typ einmal, Other wird ignoriert.
	{
		TArray<FWiesbadenRegion> Regions;
		const auto Add = [&Regions](ECityRegionType Type)
		{
			FWiesbadenRegion Region;
			Region.Type = Type;
			Regions.Add(Region);
		};
		Add(ECityRegionType::Water);
		Add(ECityRegionType::Green);
		Add(ECityRegionType::Residential);
		Add(ECityRegionType::Commercial);
		Add(ECityRegionType::Industrial);
		Add(ECityRegionType::Other);
		Add(ECityRegionType::Other);

		int32 Water = 0, Green = 0, Residential = 0, Commercial = 0, Industrial = 0;
		UWiesbadenRegionGenerator::GetRegionTypeCounts(
			Regions, Water, Green, Residential, Commercial, Industrial);
		TestEqual(TEXT("Gemischte Liste: Wasser"), Water, 1);
		TestEqual(TEXT("Gemischte Liste: Gruen"), Green, 1);
		TestEqual(TEXT("Gemischte Liste: Wohnen"), Residential, 1);
		TestEqual(TEXT("Gemischte Liste: Gewerbe"), Commercial, 1);
		TestEqual(TEXT("Gemischte Liste: Industrie"), Industrial, 1);
	}

	// Duplikate akkumulieren.
	{
		TArray<FWiesbadenRegion> Regions;
		FWiesbadenRegion Green1; Green1.Type = ECityRegionType::Green; Regions.Add(Green1);
		FWiesbadenRegion Green2; Green2.Type = ECityRegionType::Green; Regions.Add(Green2);
		FWiesbadenRegion Water1; Water1.Type = ECityRegionType::Water; Regions.Add(Water1);

		int32 Water = 0, Green = 0, Residential = 0, Commercial = 0, Industrial = 0;
		UWiesbadenRegionGenerator::GetRegionTypeCounts(
			Regions, Water, Green, Residential, Commercial, Industrial);
		TestEqual(TEXT("Duplikate: Wasser"), Water, 1);
		TestEqual(TEXT("Duplikate: Gruen"), Green, 2);
		TestEqual(TEXT("Duplikate: Wohnen"), Residential, 0);
		TestEqual(TEXT("Duplikate: Gewerbe"), Commercial, 0);
		TestEqual(TEXT("Duplikate: Industrie"), Industrial, 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegionGenerateTest,
	"WiesbadenReal.GIS.Regions.Generate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FRegionGenerateTest::RunTest(const FString& Parameters)
{
	UGeoCoordinateConverter* Converter = NewRegionConverter();
	if (!TestTrue(TEXT("Konverter initialisiert"), Converter && Converter->IsInitialized()))
	{
		return false;
	}

	// Synthetischer Datensatz: Wasser-Quad, Park-Quad, winzige Gruen-Flaeche
	// (unter MinRegionAreaSqm -> verworfen) und ein offener Way (verworfen).
	FOSMDataSet DataSet;
	{
		const TArray<FOSMId> WaterNodes = AddSquare(DataSet, 1, 8.2400, 8.2420, 50.0800, 50.0820);
		DataSet.Ways.Add(100, MakeRegionWay(100, WaterNodes, TEXT("natural"), TEXT("water"), TEXT("See")));

		const TArray<FOSMId> ParkNodes = AddSquare(DataSet, 11, 8.2440, 8.2460, 50.0800, 50.0820);
		DataSet.Ways.Add(101, MakeRegionWay(101, ParkNodes, TEXT("leisure"), TEXT("park"), TEXT("Kurpark")));

		// ~0.0001 Grad Seitenlaenge (~8 m) -> deutlich unter 500 m^2.
		const TArray<FOSMId> TinyNodes = AddSquare(DataSet, 21, 8.2500, 8.2501, 50.0850, 50.0851);
		DataSet.Ways.Add(102, MakeRegionWay(102, TinyNodes, TEXT("landuse"), TEXT("grass"), TEXT("Tropfen")));

		// Offener Way (3 Nodes, nicht geschlossen).
		DataSet.Nodes.Add(31, FOSMNode(31, 8.2600, 50.0900));
		DataSet.Nodes.Add(32, FOSMNode(32, 8.2610, 50.0900));
		DataSet.Nodes.Add(33, FOSMNode(33, 8.2610, 50.0910));
		DataSet.Ways.Add(103, MakeRegionWay(103, { 31, 32, 33 }, TEXT("natural"), TEXT("wood"), TEXT("Waldstueck")));
	}

	UWiesbadenRegionGenerator* Generator = NewObject<UWiesbadenRegionGenerator>();
	TArray<FWiesbadenRegion> Regions;
	const FRegionGenerationReport Report = Generator->Generate(DataSet, Converter, Regions);

	TestTrue(TEXT("Regionen-Pass erfolgreich"), Report.bSuccess);
	TestTrue(TEXT("Fehlermeldung leer"), Report.ErrorMessage.IsEmpty());
	TestEqual(TEXT("Nur grosse geschlossene Regionen (2)"), Report.RegionCount, 2);
	TestEqual(TEXT("Eine Wasser-Region"), Report.WaterRegionCount, 1);
	TestEqual(TEXT("Eine Gruen-Region"), Report.GreenRegionCount, 1);

	// Regionen-Geometrie: der See enthaelt sein Zentrum.
	const FVector WaterWorld = Converter->GeoToUnrealGround(FGeoCoordinate(8.2410, 50.0810));
	const FVector ParkWorld = Converter->GeoToUnrealGround(FGeoCoordinate(8.2450, 50.0810));
	const FVector OutsideWorld = Converter->GeoToUnrealGround(FGeoCoordinate(8.2700, 50.1000));
	const FVector2D WaterCenter(WaterWorld.X, WaterWorld.Y);
	const FVector2D ParkCenter(ParkWorld.X, ParkWorld.Y);
	const FVector2D Outside(OutsideWorld.X, OutsideWorld.Y);

	const FWiesbadenRegion* See = nullptr;
	const FWiesbadenRegion* Kurpark = nullptr;
	for (const FWiesbadenRegion& Region : Regions)
	{
		if (Region.Type == ECityRegionType::Water) { See = &Region; }
		else if (Region.Type == ECityRegionType::Green) { Kurpark = &Region; }
	}
	TestTrue(TEXT("Wasser-Region gefunden"), See != nullptr);
	TestTrue(TEXT("Gruen-Region gefunden"), Kurpark != nullptr);
	if (See && Kurpark)
	{
		TestEqual(TEXT("See-Name aus OSM"), See->Name, TEXT("See"));
		TestEqual(TEXT("Park-Name aus OSM"), Kurpark->Name, TEXT("Kurpark"));
		TestTrue(TEXT("See-Bounds enthaelt Zentrum"), See->Bounds.IsInside(WaterCenter));
		TestTrue(TEXT("See-Flaeche plausibel (>10000 m^2)"), See->AreaSqm > 10000.0);
	}

	// Klassifikation.
	TestEqual(TEXT("Punkt im See -> Water"),
		UWiesbadenRegionGenerator::ClassifyPoint(Regions, WaterCenter), ECityRegionType::Water);
	TestEqual(TEXT("Punkt im Park -> Green"),
		UWiesbadenRegionGenerator::ClassifyPoint(Regions, ParkCenter), ECityRegionType::Green);
	TestEqual(TEXT("Punkt ausserhalb -> Other"),
		UWiesbadenRegionGenerator::ClassifyPoint(Regions, Outside), ECityRegionType::Other);

	// Wasser-Entfernung + Zuordnung beim Bauen testet der Generator-Integrations-
	// test (Regions.GeneratorIntegration) - die Region-Zuordnung lebt dort im
	// UBuildingGenerator (RegionMap), nicht mehr als Nach-Pass hier.

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegionBuildingRulesTest,
	"WiesbadenReal.GIS.Regions.BuildingRules",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FRegionBuildingRulesTest::RunTest(const FString& Parameters)
{
	// Regionale Hoehen-Faktoren (WorldClaw: Objekt-Hoehen je Region).
	TestEqual(TEXT("Industrie-Hoehenfaktor 1.1"),
		UBuildingGenerator::GetRegionalHeightScale(ECityRegionType::Industrial), 1.1f);
	TestEqual(TEXT("Gruen-Hoehenfaktor 0.85"),
		UBuildingGenerator::GetRegionalHeightScale(ECityRegionType::Green), 0.85f);
	TestEqual(TEXT("Wohnen-Hoehenfaktor 1.0"),
		UBuildingGenerator::GetRegionalHeightScale(ECityRegionType::Residential), 1.0f);
	TestEqual(TEXT("Wasser-Hoehenfaktor 1.0"),
		UBuildingGenerator::GetRegionalHeightScale(ECityRegionType::Water), 1.0f);

	// Regionale Fassaden-Keys (nur Industrie/Gewerbe).
	TestEqual(TEXT("Industrie-Fassaden-Key"),
		UBuildingGenerator::GetRegionalFacadeKey(ECityRegionType::Industrial), TEXT("Region:Industrie"));
	TestEqual(TEXT("Gewerbe-Fassaden-Key"),
		UBuildingGenerator::GetRegionalFacadeKey(ECityRegionType::Commercial), TEXT("Region:Gewerbe"));
	TestTrue(TEXT("Wasser ohne Fassaden-Key"),
		UBuildingGenerator::GetRegionalFacadeKey(ECityRegionType::Water).IsEmpty());
	TestTrue(TEXT("Gruen ohne Fassaden-Key"),
		UBuildingGenerator::GetRegionalFacadeKey(ECityRegionType::Green).IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegionGeneratorIntegrationTest,
	"WiesbadenReal.GIS.Regions.GeneratorIntegration",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FRegionGeneratorIntegrationTest::RunTest(const FString& Parameters)
{
	UGeoCoordinateConverter* Converter = NewRegionConverter();
	if (!TestTrue(TEXT("Konverter initialisiert"), Converter && Converter->IsInitialized()))
	{
		return false;
	}

	// Zwei Gebaeude: eines im See-Quadrat, eines im Park-Quadrat.
	FOSMDataSet DataSet;
	{
		const TArray<FOSMId> SeehausNodes = AddSquare(DataSet, 1, 8.2408, 8.2412, 50.0808, 50.0812);
		DataSet.Ways.Add(100, MakeRegionWay(100, SeehausNodes, TEXT("building"), TEXT("yes"), TEXT("Seehaus")));

		const TArray<FOSMId> ParkhalleNodes = AddSquare(DataSet, 11, 8.2448, 8.2452, 50.0808, 50.0812);
		DataSet.Ways.Add(101, MakeRegionWay(101, ParkhalleNodes, TEXT("building"), TEXT("yes"), TEXT("Parkhalle")));
	}

	// Regionen-Karte manuell in Weltkoordinaten (um die Gebaeude herum).
	TArray<FWiesbadenRegion> Regions;
	{
		FWiesbadenRegion See;
		See.Name = TEXT("See");
		See.Type = ECityRegionType::Water;
		See.Polygon = {
			ToWorld2D(Converter, 8.2400, 50.0800),
			ToWorld2D(Converter, 8.2420, 50.0800),
			ToWorld2D(Converter, 8.2420, 50.0820),
			ToWorld2D(Converter, 8.2400, 50.0820),
		};
		See.Bounds = FPolygonUtils::ComputeBounds2D(See.Polygon);
		Regions.Add(See);

		FWiesbadenRegion Park;
		Park.Name = TEXT("Kurpark");
		Park.Type = ECityRegionType::Green;
		Park.Polygon = {
			ToWorld2D(Converter, 8.2440, 50.0800),
			ToWorld2D(Converter, 8.2460, 50.0800),
			ToWorld2D(Converter, 8.2460, 50.0820),
			ToWorld2D(Converter, 8.2440, 50.0820),
		};
		Park.Bounds = FPolygonUtils::ComputeBounds2D(Park.Polygon);
		Regions.Add(Park);
	}

	FBuildingGenerationSettings Settings;
	Settings.MinFootprintAreaSqm = 1.0;
	Settings.RegionMap = Regions;
	Settings.bApplyRegionRules = true;
	Settings.bRemoveWaterBuildings = true;

	UBuildingGenerator* Generator = NewObject<UBuildingGenerator>();
	TArray<FGeneratedBuilding> Buildings;
	FBuildingMeshData Mesh;

	const FBuildingGenerationReport Report = Generator->Generate(
		DataSet, Converter, /*HeightSampler=*/nullptr, Settings, Buildings, &Mesh);

	TestTrue(TEXT("Generator erfolgreich"), Report.bSuccess);
	TestEqual(TEXT("See-Gebaeude verworfen (kein Haus im See)"), Report.WaterRemovedCount, 1);
	TestEqual(TEXT("Nur das Park-Gebaeude bleibt"), Buildings.Num(), 1);
	TestEqual(TEXT("Park-Gebaeude: RegionType Green"),
		Buildings[0].RegionType, ECityRegionType::Green);
	TestEqual(TEXT("Park-Gebaeude: RegionName"), Buildings[0].RegionName, TEXT("Kurpark"));
	TestTrue(TEXT("Mesh enthaelt nur das Park-Gebaeude"), Mesh.GetTotalVertexCount() > 0);

	// Ohne Entfernen bleiben beide Gebaeude; das See-Gebaeude ist als Water markiert.
	Settings.bRemoveWaterBuildings = false;
	Buildings.Reset();
	Mesh.Reset();
	const FBuildingGenerationReport KeepReport = Generator->Generate(
		DataSet, Converter, /*HeightSampler=*/nullptr, Settings, Buildings, &Mesh);

	TestEqual(TEXT("Ohne Entfernen: 2 Gebaeude"), Buildings.Num(), 2);
	TestEqual(TEXT("Ohne Entfernen: kein Wasser-Verlust"), KeepReport.WaterRemovedCount, 0);
	bool bFoundWater = false;
	bool bFoundGreen = false;
	for (const FGeneratedBuilding& Building : Buildings)
	{
		if (Building.RegionType == ECityRegionType::Water) { bFoundWater = true; }
		if (Building.RegionType == ECityRegionType::Green) { bFoundGreen = true; }
	}
	TestTrue(TEXT("See-Gebaeude als Water markiert"), bFoundWater);
	TestTrue(TEXT("Park-Gebaeude als Green markiert"), bFoundGreen);

	return true;
}
