// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "GIS/BuildingGenerator.h"
#include "GIS/GeoCoordinateConverter.h"
#include "GIS/OSMTypes.h"
#include "GIS/RoadFurnitureGenerator.h"

/**
 * Die Platzierungsregeln der OSM-Strassenmoebel.
 *
 * Aufbau: EINE gerade Ost-West-Strasse durch den Ursprung, 6,5 m Fahrbahn
 * (also +-3,25 m) mit 2,5 m Gehweg (befestigt bis +-5,75 m). Alle Faelle
 * werden an dieser Strasse durchgespielt; die Knoten kommen ueber den
 * Georeferenz-Umweg herein, weil der Pass genau so gefuettert wird.
 */
namespace
{
	FRoadSegment MakeStraightRoad()
	{
		FRoadSegment Segment;
		Segment.SegmentId = 0;
		Segment.StartNodeId = 1;
		Segment.EndNodeId = 2;
		Segment.Centerline = { FVector(-20000.0, 0.0, 0.0), FVector(20000.0, 0.0, 0.0) };
		Segment.TrimmedCenterline = Segment.Centerline;
		Segment.HighwayType = EOSMHighwayType::Residential;
		Segment.CarriagewayWidthCm = 650.0;
		Segment.SidewalkWidthCm = 250.0;
		Segment.MaxSpeedKmh = 50.0;
		Segment.LengthCm = 40000.0;
		return Segment;
	}

	/** Baut einen OSM-Knoten an einer WELT-Position (ueber den Converter zurueck). */
	void AddFurnitureNode(
		FOSMDataSet& DataSet, const UGeoCoordinateConverter& Converter,
		FOSMId NodeId, const FVector2D& WorldCm, const TCHAR* Kind)
	{
		const FGeoCoordinate Geo = Converter.UnrealToGeo(FVector(WorldCm.X, WorldCm.Y, 0.0));
		FOSMNode Node(NodeId, Geo.Longitude, Geo.Latitude);
		Node.Tags.Add(FName(TEXT("wb:furniture")), FString(Kind));
		DataSet.Nodes.Add(NodeId, Node);
	}

	const FFurnitureInstance* FindByNode(const FRoadFurnitureLayout& Layout, int64 NodeId)
	{
		for (const FFurnitureInstance& Instance : Layout.Furniture)
		{
			if (Instance.NodeId == NodeId)
			{
				return &Instance;
			}
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStreetFurniturePlacementTest,
	"WiesbadenReal.GIS.RoadFurniture.StreetFurniture",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FStreetFurniturePlacementTest::RunTest(const FString& Parameters)
{
	FRoadNetwork Network;
	Network.Segments.Add(MakeStraightRoad());

	UGeoCoordinateConverter* Converter = NewObject<UGeoCoordinateConverter>();
	TestTrue(TEXT("Georeferenz steht"), Converter->InitializeWithWiesbadenOrigin());

	FOSMDataSet DataSet;
	// Auf dem Gehweg, 4,5 m neben der Achse: darf unveraendert stehen bleiben.
	AddFurnitureNode(DataSet, *Converter, 10, FVector2D(0.0, 450.0), TEXT("bench"));
	// Mitten auf der Fahrbahn: muss an den Rand ruecken.
	AddFurnitureNode(DataSet, *Converter, 11, FVector2D(500.0, 0.0), TEXT("waste_basket"));
	// Poller auf der Fahrbahnkante: darf dort BLEIBEN - dafuer ist er da.
	AddFurnitureNode(DataSet, *Converter, 12, FVector2D(1000.0, 300.0), TEXT("bollard"));
	// 30 m neben der Strasse in der Wiese: kein befestigter Rand in Reichweite.
	AddFurnitureNode(DataSet, *Converter, 13, FVector2D(1500.0, 3000.0), TEXT("bench"));
	// Hydrant auf dem Gehweg - fuer die Ausrichtungspruefung.
	AddFurnitureNode(DataSet, *Converter, 14, FVector2D(2000.0, 450.0), TEXT("fire_hydrant"));
	// Unbekannte Art: wird nicht geraten, sondern ignoriert.
	{
		const FGeoCoordinate Geo = Converter->UnrealToGeo(FVector(2500.0, 450.0, 0.0));
		FOSMNode Node(15, Geo.Longitude, Geo.Latitude);
		Node.Tags.Add(FName(TEXT("wb:furniture")), FString(TEXT("kaugummiautomat")));
		DataSet.Nodes.Add(15, Node);
	}
	// Im Gebaeude verortet (OSM-Fehler): muss verworfen werden.
	AddFurnitureNode(DataSet, *Converter, 16, FVector2D(-5000.0, 500.0), TEXT("post_box"));

	TArray<FGeneratedBuilding> Buildings;
	{
		FGeneratedBuilding Building;
		Building.SourceId = 900;
		Building.FootprintCenterCm = FVector2D(-5000.0, 500.0);
		Building.FootprintExtentCm = FVector2D(600.0, 400.0);
		Building.FootprintYawDegrees = 0.0f;
		Buildings.Add(Building);
	}

	FFlatHeightSampler Sampler(1000.0);
	FRoadFurnitureSettings Settings;
	// Nur der Moebel-Pass: die anderen Passe haben eigene Tests und wuerden
	// hier nur Rauschen in die Zahlen bringen.
	Settings.bPlaceSigns = false;
	Settings.bPlaceDelineators = false;
	Settings.bPlaceMarkings = false;
	Settings.bPlaceStreetLamps = false;

	FRoadFurnitureLayout Layout;
	URoadFurnitureGenerator* Generator = NewObject<URoadFurnitureGenerator>();
	const FRoadFurnitureReport Report = Generator->Generate(
		Network, &DataSet, Converter, &Sampler, Settings, Layout, &Buildings);

	TestTrue(TEXT("Pass erfolgreich"), Report.bSuccess);

	// --- Regel: unbekannte Arten werden nicht geraten ---
	TestNull(TEXT("Unbekannte Art kommt nicht in die Stadt"), FindByNode(Layout, 15));

	// --- Regel 2: Gebaeude-Ausschluss ---
	TestNull(TEXT("Briefkasten im Gebaeude ist verworfen"), FindByNode(Layout, 16));
	TestEqual(TEXT("... und wird gezaehlt"), Report.FurnitureInBuildingCount, 1);

	// --- Regel 4: was am Rand steht, bleibt stehen ---
	const FFurnitureInstance* Bench = FindByNode(Layout, 10);
	if (TestNotNull(TEXT("Bank auf dem Gehweg ist uebernommen"), Bench))
	{
		TestEqual(TEXT("Bank blieb an ihrem OSM-Ort (Y)"), Bench->Location.Y, 450.0, 1.0);
		TestEqual(TEXT("Bank steht auf Gehweghoehe (Terrain + Fahrbahn + Bordstein)"),
			Bench->Location.Z,
			1000.0 + Settings.RoadSurfaceOffsetCm + Settings.KerbHeightCm, 1.0);
	}

	// --- Regel 3: von der Fahrbahn herunter ---
	const FFurnitureInstance* Basket = FindByNode(Layout, 11);
	if (TestNotNull(TEXT("Abfallkorb ist uebernommen"), Basket))
	{
		TestTrue(TEXT("Abfallkorb steht nicht mehr auf der Fahrbahn"),
			FMath::Abs(Basket->Location.Y) > 325.0);
		TestTrue(TEXT("... aber noch auf dem befestigten Streifen"),
			FMath::Abs(Basket->Location.Y) <= 575.0 + 1.0);
	}
	TestTrue(TEXT("Das Andocken wird gezaehlt"), Report.FurnitureDockedCount >= 1);

	// --- Regel 3, Ausnahme: der Poller darf auf der Fahrbahn stehen ---
	const FFurnitureInstance* Bollard = FindByNode(Layout, 12);
	if (TestNotNull(TEXT("Poller ist uebernommen"), Bollard))
	{
		TestEqual(TEXT("Poller wurde NICHT verschoben"), Bollard->Location.Y, 300.0, 1.0);
	}

	// --- Regel 4: ohne befestigten Rand in Reichweite wird verworfen ---
	TestNull(TEXT("Bank in der Wiese ist verworfen"), FindByNode(Layout, 13));
	TestEqual(TEXT("... und wird gezaehlt"), Report.FurnitureWithoutEdgeCount, 1);

	// --- Regel 5: Ausrichtung ---
	// Die Strasse liegt bei Y=0, die Moebel bei Y=+450: "zur Strasse" ist -Y
	// (Yaw -90 Grad), "von der Strasse weg" ist +Y (Yaw +90 Grad).
	if (Bench)
	{
		TestEqual(TEXT("Bank kehrt der Strasse den Ruecken"),
			FMath::UnwindDegrees(Bench->Rotation.Yaw), 90.0, 6.0);
	}
	const FFurnitureInstance* Hydrant = FindByNode(Layout, 14);
	if (TestNotNull(TEXT("Hydrant ist uebernommen"), Hydrant))
	{
		TestEqual(TEXT("Hydrant zeigt zur Fahrbahn (Feuerwehr kuppelt von dort an)"),
			FMath::UnwindDegrees(Hydrant->Rotation.Yaw), -90.0, 6.0);
	}
	if (Bollard)
	{
		// Laengs der Kante heisst hier: entlang der Strassenachse (Yaw 0/180).
		const double Yaw = FMath::Abs(FMath::UnwindDegrees(Bollard->Rotation.Yaw));
		TestTrue(TEXT("Poller steht laengs der Fahrbahnkante"),
			Yaw <= 6.0 || Yaw >= 174.0);
	}

	// --- Varianten bleiben im Bereich der Art ---
	for (const FFurnitureInstance& Instance : Layout.Furniture)
	{
		const int32 VariantCount = URoadFurnitureGenerator::GetFurnitureVariantCount(Instance.Kind);
		TestTrue(TEXT("Variante liegt im Bereich der Art"),
			Instance.Variant >= 0 && Instance.Variant < VariantCount);
	}

	// --- Determinismus: zwei Laeufe, dieselbe Liste ---
	FRoadFurnitureLayout Second;
	Generator->Generate(Network, &DataSet, Converter, &Sampler, Settings, Second, &Buildings);
	if (TestEqual(TEXT("Zweiter Lauf liefert gleich viele Moebel"),
		Second.Furniture.Num(), Layout.Furniture.Num()))
	{
		for (int32 i = 0; i < Second.Furniture.Num(); ++i)
		{
			TestEqual(TEXT("Gleiche Reihenfolge (Knoten-Id)"),
				Second.Furniture[i].NodeId, Layout.Furniture[i].NodeId);
			TestTrue(TEXT("Gleiche Position"),
				Second.Furniture[i].Location.Equals(Layout.Furniture[i].Location, 0.01));
			TestEqual(TEXT("Gleiche Variante"),
				Second.Furniture[i].Variant, Layout.Furniture[i].Variant);
		}
	}

	// --- Der Rueckfall auf die rohen OSM-Tags (Datei ohne Nachzug) ---
	{
		FOSMDataSet RawSet;
		const FGeoCoordinate Geo = Converter->UnrealToGeo(FVector(0.0, 450.0, 0.0));
		FOSMNode Node(20, Geo.Longitude, Geo.Latitude);
		Node.Tags.Add(FName(TEXT("amenity")), FString(TEXT("bench")));
		RawSet.Nodes.Add(20, Node);

		FRoadFurnitureLayout RawLayout;
		Generator->Generate(Network, &RawSet, Converter, &Sampler, Settings, RawLayout, nullptr);
		TestEqual(TEXT("Auch ohne wb:furniture kommt die Bank an"), RawLayout.Furniture.Num(), 1);
		if (RawLayout.Furniture.Num() == 1)
		{
			TestEqual(TEXT("... als Bank"), RawLayout.Furniture[0].Kind, EStreetFurnitureKind::Bench);
		}
	}

	// --- Der Waechter der Aufrufstelle kennt JEDEN Kanal --------------------
	//
	// AWiesbadenCityActor ruft den Spawner nur, wenn das Layout etwas
	// enthaelt. Diese Frage zaehlte frueher die Kanaele einzeln auf und war
	// bei Laternen und Moebeln nie nachgezogen worden: eine Stadt mit
	// ausschliesslich Moebeln haette ihren Spawner nie gerufen.
	{
		FRoadFurnitureLayout Leer;
		TestTrue(TEXT("Ein leeres Layout ist leer"), Leer.IsEmpty());

		FRoadFurnitureLayout NurMoebel;
		NurMoebel.Furniture.Add(FFurnitureInstance());
		TestFalse(TEXT("Ein Layout mit NUR Moebeln ist nicht leer"), NurMoebel.IsEmpty());

		FRoadFurnitureLayout NurLaternen;
		NurLaternen.StreetLamps.Add(FStreetLampInstance());
		TestFalse(TEXT("Ein Layout mit NUR Laternen ist nicht leer"), NurLaternen.IsEmpty());

		FRoadFurnitureLayout NurSchilder;
		NurSchilder.Signs.Add(FSignInstance());
		TestFalse(TEXT("Ein Layout mit NUR Schildern ist nicht leer"), NurSchilder.IsEmpty());

		FRoadFurnitureLayout NurLeitpfosten;
		NurLeitpfosten.Delineators.Add(FDelineatorInstance());
		TestFalse(TEXT("Ein Layout mit NUR Leitpfosten ist nicht leer"), NurLeitpfosten.IsEmpty());

		FRoadFurnitureLayout NurMarkierungen;
		NurMarkierungen.Markings.Add(FMarkingInstance());
		TestFalse(TEXT("Ein Layout mit NUR Markierungen ist nicht leer"), NurMarkierungen.IsEmpty());

		NurMoebel.Reset();
		TestTrue(TEXT("Nach Reset ist es wieder leer"), NurMoebel.IsEmpty());
	}

	// --- Der Schalter schaltet wirklich ab ---
	{
		FRoadFurnitureSettings Off = Settings;
		Off.bPlaceStreetFurniture = false;
		FRoadFurnitureLayout OffLayout;
		const FRoadFurnitureReport OffReport = Generator->Generate(
			Network, &DataSet, Converter, &Sampler, Off, OffLayout, &Buildings);
		TestEqual(TEXT("Abgeschaltet bleibt die Liste leer"), OffLayout.Furniture.Num(), 0);
		TestEqual(TEXT("... und der Bericht zaehlt nichts"), OffReport.FurnitureCount, 0);
	}

	return true;
}
