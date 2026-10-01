// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
#include "Misc/AutomationTest.h"
#include "GIS/RoadNetworkGenerator.h"
#include "GIS/GeoCoordinateConverter.h"

namespace
{
	FRoadNetwork PaintNetwork()
	{
		FRoadNetwork Network;
		FRoadSegment S;
		S.SegmentId = 0; S.SourceWayId = 10; S.StartNodeId = 1; S.EndNodeId = 2;
		S.ForwardLaneCount = 1; S.BackwardLaneCount = 0; S.CarriagewayWidthCm = 320.0;
		S.Centerline = S.TrimmedCenterline = { FVector(0, 0, 100), FVector(10000, 0, 1100) };
		Network.Segments.Add(S);
		FRoadLane L;
		L.LaneId = 0; L.SegmentId = 0; L.LaneIndexFromLeft = 0; L.WidthCm = 320;
		L.Centerline = S.Centerline; L.LengthCm = 10000;
		Network.Lanes.Add(L);
		return Network;
	}
	int32 VertexCount(const FRoadMeshData& Data, ERoadMeshChannel Channel)
	{
		int32 Count = 0;
		for (const auto& S : Data.Sections) { if (S.Channel == Channel) { Count += S.Vertices.Num(); } }
		return Count;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadSupplementaryPaintTest,
	"WiesbadenReal.GIS.RoadNetwork.SupplementaryPaint",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FRoadSupplementaryPaintTest::RunTest(const FString& Parameters)
{
	UGeoCoordinateConverter* Converter = NewObject<UGeoCoordinateConverter>();
	Converter->InitializeWithWiesbadenOrigin();
	FRoadNetwork Network = PaintNetwork();
	FOSMDataSet OSM;
	FOSMWay Way; Way.Id = 10; Way.Tags.Add(TEXT("highway"), TEXT("residential"));
	Way.NodeIds = { 1, 3, 2 }; OSM.Ways.Add(10, Way);
	FOSMNode Zebra; Zebra.Id = 3;
	Zebra.Location = Converter->UnrealToGeo(FVector(5000, 0, 0));
	Zebra.Tags.Add(TEXT("highway"), TEXT("crossing")); OSM.Nodes.Add(3, Zebra);
	FOSMNode Yield; Yield.Id = 2; OSM.Nodes.Add(2, Yield);
	FRoadGenerationSettings Settings;
	auto Build = [&]() {
		FRoadMeshData Data;
		URoadNetworkGenerator::BuildSupplementaryMarkings(Network, OSM, *Converter, Settings, Data);
		return Data;
	};
	TestEqual(TEXT("Ohne explizite Fakten kein Paint trotz Through-Default"), Build().GetTotalVertexCount(), 0);
	OSM.Ways[10].Tags.Add(TEXT("turn:lanes"), TEXT("none"));
	TestEqual(TEXT("none ist kein Geradeaus-Pfeil"), Build().GetTotalVertexCount(), 0);
	OSM.Ways[10].Tags.Add(TEXT("turn:lanes"), TEXT("unknown"));
	TestEqual(TEXT("Unbekannter Token erfindet keinen Pfeil"), Build().GetTotalVertexCount(), 0);
	OSM.Ways[10].Tags.Add(TEXT("turn:lanes"), TEXT("through|left"));
	TestEqual(TEXT("Tokenzahl falsch: keine Pfeile"), Build().GetTotalVertexCount(), 0);
	OSM.Ways[10].Tags.Add(TEXT("turn:lanes"), TEXT("through;left"));
	FRoadMeshData Arrows = Build();
	TestTrue(TEXT("Expliziter kombinierter Pfeil erzeugt weisses Paint"), VertexCount(Arrows, ERoadMeshChannel::LaneMarking) > 0);
	bool LeftOfAxis = false;
	for (const auto& S : Arrows.Sections)
	{
		TestEqual(TEXT("Alle Attribute haben Vertexanzahl"), S.Normals.Num(), S.Vertices.Num());
		TestEqual(TEXT("UVs vollstaendig"), S.UVs.Num(), S.Vertices.Num());
		TestEqual(TEXT("Farben vollstaendig"), S.VertexColors.Num(), S.Vertices.Num());
		for (int32 i = 0; i < S.Vertices.Num(); ++i)
		{
			const FVector& P = S.Vertices[i]; LeftOfAxis |= P.Y < -80;
			TestTrue(TEXT("Paint folgt Fahrbahngefaelle, nicht dem Lane-Index"), FMath::IsNearlyEqual(P.Z, 100 + P.X * .1 + Settings.MarkingOffsetCm, .001));
			TestEqual(TEXT("Symbole sind durchgezogen"), S.VertexColors[i].R, static_cast<uint8>(255));
		}
		for (int32 i = 0; i + 2 < S.Triangles.Num(); i += 3)
		{
			const FVector A = S.Vertices[S.Triangles[i]], B = S.Vertices[S.Triangles[i + 1]], C = S.Vertices[S.Triangles[i + 2]];
			TestTrue(TEXT("UE-Frontseite ist nach oben sichtbar"), FVector::CrossProduct(B - A, C - A).Z < 0);
		}
	}
	TestTrue(TEXT("Linksabbiegen bei Fahrt +X zeigt nach -Y"), LeftOfAxis);
	Settings.bGenerateTurnArrows = false;
	TestEqual(TEXT("Pfeilmodul separat abschaltbar"), Build().GetTotalVertexCount(), 0);
	OSM.Nodes[3].Tags.Add(TEXT("crossing"), TEXT("zebra"));
	TestEqual(TEXT("3 Streifen auf 3.20 m Fahrbahn"), VertexCount(Build(), ERoadMeshChannel::Crossing), 12);
	// Gleiches Way wird am Crossing-Node geteilt: nicht doppelt painten.
	const FRoadSegment Duplicate = Network.Segments[0];
	Network.Segments.Add(Duplicate);
	TestEqual(TEXT("Knoten wird nur einmal bemalt"), VertexCount(Build(), ERoadMeshChannel::Crossing), 12);
	Network.Segments.SetNum(1);
	Settings.bGenerateCrossings = false;
	OSM.Nodes[2].Tags.Add(TEXT("highway"), TEXT("give_way"));
	TestTrue(TEXT("Give-way erzeugt Zaehne nur auf Zufahrt"), VertexCount(Build(), ERoadMeshChannel::LaneMarking) > 0);
	Network.Lanes[0].Direction = ELaneDirection::Backward;
	TestEqual(TEXT("Ausfahrt nicht faelschlich mit Zaehnen versehen"), Build().GetTotalVertexCount(), 0);
	Network.Lanes[0].Direction = ELaneDirection::Forward;
	Settings.bGenerateGiveWayTeeth = false;
	Network.Lanes[0].bIsBusLane = true;
	FRoadMeshData Bus = Build();
	TestTrue(TEXT("Bus bekommt Schriftzug"), VertexCount(Bus, ERoadMeshChannel::LaneMarking) > 0);
	TestEqual(TEXT("Bus bekommt keine Farbflaeche"), VertexCount(Bus, ERoadMeshChannel::BikeLaneSurface), 0);
	Settings.bGenerateBusLanes = false;
	TestEqual(TEXT("Busmodul separat abschaltbar"), Build().GetTotalVertexCount(), 0);
	Network.Lanes[0].bIsBikeLane = true;
	FRoadMeshData Bike = Build();
	TestTrue(TEXT("Radspur bekommt rote Flaeche"), VertexCount(Bike, ERoadMeshChannel::BikeLaneSurface) > 0);
	TestTrue(TEXT("Radspur bekommt Piktogramm"), VertexCount(Bike, ERoadMeshChannel::LaneMarking) > 0);
	Settings.bGenerateBikeLanes = false;
	TestEqual(TEXT("Radmodul separat abschaltbar"), Build().GetTotalVertexCount(), 0);
	Settings.bGenerateLaneMarkings = false;
	Settings.bGenerateBikeLanes = true;
	TestEqual(TEXT("Master-Schalter unterdrueckt alle Markierungen"), Build().GetTotalVertexCount(), 0);
	Settings.bGenerateLaneMarkings = true;
	Network.Segments[0].TrimmedCenterline.Reset();
	TestEqual(TEXT("Leere Fahrbahn ist No-op"), Build().GetTotalVertexCount(), 0);
	return true;
}
