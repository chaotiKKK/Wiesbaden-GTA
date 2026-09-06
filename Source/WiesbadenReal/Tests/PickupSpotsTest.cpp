// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "GIS/GeoCoordinateConverter.h"
#include "GIS/WiesbadenPickupSpots.h"

/**
 * Pickup-Platzierung (datenrein): OSM-Amenities werden an sinnvollen Orten
 * platziert - Treibstoff an amenity=fuel, Gesundheit an pharmacy/hospital;
 * Cluster werden ausgeduennt, Obergrenzen eingehalten, und mit Strassennetz
 * wird auf den Gehweg neben der Fahrbahn gerastet statt in den Innenhof.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPickupSpotGenerationTest,
	"WiesbadenReal.Pickups.SpotGeneration",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPickupSpotGenerationTest::RunTest(const FString& Parameters)
{
	UGeoCoordinateConverter* Converter = NewObject<UGeoCoordinateConverter>();
	if (!TestTrue(TEXT("Geo-Konverter initialisiert"), Converter->InitializeWithWiesbadenOrigin()))
	{
		return false;
	}

	// -- Klassifikation ----------------------------------------------------
	{
		FOSMNode FuelNode(1001, 8.24, 50.0824);
		FuelNode.Tags.Add(FName(TEXT("amenity")), FString(TEXT("fuel")));
		EWiesbadenPickupSpotKind Kind = EWiesbadenPickupSpotKind::MAX;
		TestTrue(TEXT("amenity=fuel -> Treibstoff"),
			UWiesbadenPickupSpotGenerator::ClassifyNode(FuelNode, Kind)
			&& Kind == EWiesbadenPickupSpotKind::Fuel);

		FOSMNode PharmacyNode(1002, 8.24, 50.0824);
		PharmacyNode.Tags.Add(FName(TEXT("amenity")), FString(TEXT("pharmacy")));
		TestTrue(TEXT("amenity=pharmacy -> Gesundheit"),
			UWiesbadenPickupSpotGenerator::ClassifyNode(PharmacyNode, Kind)
			&& Kind == EWiesbadenPickupSpotKind::Health);

		FOSMNode BenchNode(1003, 8.24, 50.0824);
		BenchNode.Tags.Add(FName(TEXT("amenity")), FString(TEXT("bench")));
		TestFalse(TEXT("amenity=bench -> kein Pickup"),
			UWiesbadenPickupSpotGenerator::ClassifyNode(BenchNode, Kind));
	}

	// -- Platzierung + Ausduennen ------------------------------------------
	{
		FOSMDataSet Data;

		FOSMNode Fuel(2001, 8.2405, 50.0824);
		Fuel.Tags.Add(FName(TEXT("amenity")), FString(TEXT("fuel")));
		Fuel.Tags.Add(FName(TEXT("name")), FString(TEXT("Esso-Station")));
		Data.Nodes.Add(Fuel.Id, Fuel);

		// Zweite Tankstelle ~70 m entfernt - unter dem Mindestabstand (100 m).
		FOSMNode FuelClose(2002, 8.2415, 50.0824);
		FuelClose.Tags.Add(FName(TEXT("amenity")), FString(TEXT("fuel")));
		Data.Nodes.Add(FuelClose.Id, FuelClose);

		FOSMNode Pharmacy(2003, 8.2380, 50.0810);
		Pharmacy.Tags.Add(FName(TEXT("amenity")), FString(TEXT("pharmacy")));
		Pharmacy.Tags.Add(FName(TEXT("name")), FString(TEXT("Apotheke am Platz")));
		Data.Nodes.Add(Pharmacy.Id, Pharmacy);

		Data.RecomputeBounds();

		UWiesbadenPickupSpotGenerator* Generator = NewObject<UWiesbadenPickupSpotGenerator>();

		FWiesbadenPickupSpotSettings Settings;
		FWiesbadenPickupSpotLayout Layout;
		const FPickupSpotReport Report = Generator->Generate(
			Data, *Converter, nullptr, nullptr, Settings, Layout);

		TestEqual(TEXT("Treibstoff-Pickups"), Report.FuelCount, 1);
		TestEqual(TEXT("Gesundheits-Pickups"), Report.HealthCount, 1);
		TestEqual(TEXT("Zu-nahe Platzierung verworfen"), Report.SkippedTooCloseCount, 1);
		TestEqual(TEXT("Keine Obergrenzen-Ueberschreitung"), Report.SkippedOverCapCount, 0);
		TestEqual(TEXT("Ohne Netz keine Rastung"), Report.SnappedToRoadCount, 0);
		TestEqual(TEXT("Zwei Spots gesamt"), Layout.Spots.Num(), 2);

		for (const FWiesbadenPickupSpot& Spot : Layout.Spots)
		{
			if (Spot.Kind == EWiesbadenPickupSpotKind::Fuel)
			{
				TestTrue(TEXT("Treibstoff-Spot stammt von einer Tankstelle"),
					Spot.SourceNodeId == 2001 || Spot.SourceNodeId == 2002);
				const FOSMNode& KeptNode = (Spot.SourceNodeId == 2001) ? Fuel : FuelClose;
				TestTrue(TEXT("Treibstoff-Spot an Node-Position (ohne Netz)"),
					Spot.Location.Equals(Converter->GeoToUnrealGround(KeptNode.Location), 1.0));
			}
			else
			{
				TestEqual(TEXT("Gesundheits-Spot stammt von der Apotheke"), Spot.SourceNodeId, Pharmacy.Id);
				TestEqual(TEXT("Quellenname der Apotheke"), Spot.SourceName, FString(TEXT("Apotheke am Platz")));
				TestTrue(TEXT("Apotheken-Spot an Node-Position (ohne Netz)"),
					Spot.Location.Equals(Converter->GeoToUnrealGround(Pharmacy.Location), 1.0));
			}
			TestTrue(TEXT("Hoehe 0 ohne Hoehensampler"), Spot.Location.Z == 0.0);
			TestFalse(TEXT("Ohne Netz nicht gerastet"), Spot.bSnappedToRoad);
		}
	}

	// -- Obergrenze ---------------------------------------------------------
	{
		FOSMDataSet Data;

		FOSMNode FuelA(3001, 8.2400, 50.0824);
		FuelA.Tags.Add(FName(TEXT("amenity")), FString(TEXT("fuel")));
		Data.Nodes.Add(FuelA.Id, FuelA);

		// ~140 m entfernt - ueber dem Mindestabstand, nur das Cap greift.
		FOSMNode FuelB(3002, 8.2420, 50.0824);
		FuelB.Tags.Add(FName(TEXT("amenity")), FString(TEXT("fuel")));
		Data.Nodes.Add(FuelB.Id, FuelB);

		Data.RecomputeBounds();

		UWiesbadenPickupSpotGenerator* Generator = NewObject<UWiesbadenPickupSpotGenerator>();

		FWiesbadenPickupSpotSettings Settings;
		Settings.MaxFuelPickups = 1;
		FWiesbadenPickupSpotLayout Layout;
		const FPickupSpotReport Report = Generator->Generate(
			Data, *Converter, nullptr, nullptr, Settings, Layout);

		TestEqual(TEXT("Cap: ein Treibstoff-Pickup bleibt"), Report.FuelCount, 1);
		TestEqual(TEXT("Cap: eine Platzierung verworfen"), Report.SkippedOverCapCount, 1);
	}

	// -- Rastung auf den Gehweg neben der Fahrbahn --------------------------
	{
		FRoadNetwork Network;

		FRoadSegment Segment;
		Segment.SegmentId = 0;
		Segment.SidewalkWidthCm = 250.0;
		Segment.Centerline = { FVector(0.0, 0.0, 0.0), FVector(4000.0, 0.0, 0.0) };
		Network.Segments.Add(Segment);

		FRoadLane Lane;
		Lane.LaneId = 0;
		Lane.SegmentId = 0;
		Lane.Centerline = { FVector(0.0, 0.0, 0.0), FVector(4000.0, 0.0, 0.0) };
		Lane.WidthCm = 325.0;
		Network.Lanes.Add(Lane);

		// Node 1000 cm neben der Fahrbahnachse (SnapRadius 1500 cm reicht).
		const FVector TargetWorld(1000.0, 1000.0, 0.0);
		const FGeoCoordinate TargetGeo = Converter->UnrealToGeo(TargetWorld);

		FOSMNode Fuel(4001, TargetGeo.Longitude, TargetGeo.Latitude);
		Fuel.Tags.Add(FName(TEXT("amenity")), FString(TEXT("fuel")));

		FOSMDataSet Data;
		Data.Nodes.Add(Fuel.Id, Fuel);
		Data.RecomputeBounds();

		UWiesbadenPickupSpotGenerator* Generator = NewObject<UWiesbadenPickupSpotGenerator>();

		FWiesbadenPickupSpotSettings Settings;
		FWiesbadenPickupSpotLayout Layout;
		const FPickupSpotReport Report = Generator->Generate(
			Data, *Converter, &Network, nullptr, Settings, Layout);

		TestEqual(TEXT("Rastung: ein Treibstoff-Pickup"), Report.FuelCount, 1);
		TestEqual(TEXT("Rastung: auf die Fahrbahn gerastet"), Report.SnappedToRoadCount, 1);

		const FWiesbadenPickupSpot& Spot = Layout.Spots[0];
		TestTrue(TEXT("Spot als gerastet markiert"), Spot.bSnappedToRoad);

		// Erwartung: Spurpunkt (1000, 0), Outset = 162.5 + 125.0 = 287.5 cm
		// in Richtung des Nodes (+Y) - auf dem Gehweg, nicht auf der Fahrbahn
		// und nicht im Innenhof.
		const FVector Expected(1000.0, 287.5, 0.0);
		TestTrue(TEXT("Position auf dem Gehweg neben der Fahrbahn"),
			Spot.Location.Equals(Expected, 1.0));
	}

	return true;
}