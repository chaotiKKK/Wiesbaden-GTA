// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#include "Misc/AutomationTest.h"
#include "GIS/WiesbadenRegionAssets.h"
#include "GIS/BuildingGenerator.h"
#include "GIS/TerrainGenerator.h"
#include "GIS/RoadFurnitureGenerator.h"
#include "GIS/GeoCoordinateConverter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegionObstacleClearanceTest,
	"WiesbadenReal.GIS.RegionAssets.ObstacleClearance",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FRegionObstacleClearanceTest::RunTest(const FString& Parameters)
{
	UGeoCoordinateConverter* Converter = NewObject<UGeoCoordinateConverter>();
	Converter->InitializeWithWiesbadenOrigin();
	FGeneratedBuilding Building;
	Building.FootprintCenterCm = FVector2D(1000, 1000);
	Building.FootprintExtentCm = FVector2D(500, 200);
	Building.FootprintYawDegrees = 45;
	FOSMDataSet OSM;
	FOSMWay Rail; Rail.Id = 10; Rail.NodeIds = { 1, 2 };
	Rail.Tags.Add(TEXT("railway"), TEXT("funicular")); OSM.Ways.Add(10, Rail);
	for (int32 i = 1; i <= 2; ++i)
	{
		FOSMNode Node; Node.Id = i;
		Node.Location = Converter->UnrealToGeo(FVector(i == 1 ? 0 : 10000, 5000, 0));
		OSM.Nodes.Add(i, Node);
	}
	FTerrainSitePad Pad; Pad.CenterCm = FVector2D(20000, 20000); Pad.BuildingHalfCm = 800;
	FRegionAssetLayout Layout;
	for (const FVector& P : { FVector(1000, 1000, 0), FVector(4000, 5000, 0),
		FVector(20000, 20000, 0), FVector(30000, 30000, 0), FVector(4000, 5700, 0) })
	{
		FPlacedRegionAsset Asset; Asset.Location = P; Layout.Assets.Add(Asset);
	}
	FRoadFurnitureLayout Furniture;
	FStreetLampInstance Lamp; Lamp.Location = FVector(4000, 5000, 0); Furniture.StreetLamps.Add(Lamp);
	Lamp.Location = FVector(30000, 30000, 0); Furniture.StreetLamps.Add(Lamp);
	const int32 Removed = UWiesbadenRegionAssetGenerator::ClearObstacles(
		{ Building }, OSM, *Converter, { Pad }, Layout, &Furniture);
	TestEqual(TEXT("Haus, Bahn und privater Neubau freigehalten"), Removed, 3);
	TestEqual(TEXT("Freie Objekte bleiben"), Layout.Assets.Num(), 2);
	TestEqual(TEXT("Nur Bahnlaterne entfernt"), Furniture.StreetLamps.Num(), 1);
	TestTrue(TEXT("Freie Laterne unveraendert"), Furniture.StreetLamps[0].Location.Equals(FVector(30000, 30000, 0)));
	TestEqual(TEXT("Filter ist idempotent"), UWiesbadenRegionAssetGenerator::ClearObstacles(
		{ Building }, OSM, *Converter, { Pad }, Layout), 0);
	FRegionAssetLayout Empty;
	TestEqual(TEXT("Leere Eingaben sicher"), UWiesbadenRegionAssetGenerator::ClearObstacles(
		{}, {}, *Converter, {}, Empty), 0);
	return true;
}
