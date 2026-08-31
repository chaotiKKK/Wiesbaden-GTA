// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "GIS/OSMDataParser.h"

namespace
{
	/** Baut einen Way aus einer Liste von Tag-Paaren (fuer Tag-Parser-Tests). */
	FOSMWay MakeWay(std::initializer_list<TPair<const TCHAR*, const TCHAR*>> InTags)
	{
		FOSMWay Way;
		for (const TPair<const TCHAR*, const TCHAR*>& Pair : InTags)
		{
			Way.Tags.Add(FName(Pair.Key), FString(Pair.Value));
		}
		return Way;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOSMXmlParseTest,
	"WiesbadenReal.GIS.OSMDataParser.XmlParse",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FOSMXmlParseTest::RunTest(const FString& Parameters)
{
	const FString Xml =
		TEXT("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n")
		TEXT("<osm version=\"0.6\" generator=\"AutomationTest\">\n")
		TEXT("  <node id=\"1\" lat=\"50.0824\" lon=\"8.2400\"/>\n")
		TEXT("  <node id=\"2\" lat=\"50.0825\" lon=\"8.2405\">\n")
		TEXT("    <tag k=\"highway\" v=\"traffic_signals\"/>\n")
		TEXT("  </node>\n")
		TEXT("  <node id=\"3\" lat=\"50.0830\" lon=\"8.2410\"/>\n")
		TEXT("  <way id=\"100\">\n")
		TEXT("    <nd ref=\"1\"/>\n")
		TEXT("    <nd ref=\"2\"/>\n")
		TEXT("    <nd ref=\"3\"/>\n")
		TEXT("    <tag k=\"highway\" v=\"residential\"/>\n")
		TEXT("    <tag k=\"name\" v=\"Teststrasse\"/>\n")
		TEXT("    <tag k=\"oneway\" v=\"yes\"/>\n")
		TEXT("  </way>\n")
		TEXT("  <way id=\"101\">\n")
		TEXT("    <nd ref=\"1\"/>\n")
		TEXT("  </way>\n")
		TEXT("  <relation id=\"200\">\n")
		TEXT("    <member type=\"way\" ref=\"100\" role=\"outer\"/>\n")
		TEXT("    <tag k=\"type\" v=\"multipolygon\"/>\n")
		TEXT("    <tag k=\"building\" v=\"yes\"/>\n")
		TEXT("  </relation>\n")
		TEXT("</osm>\n");

	UOSMDataParser* Parser = NewObject<UOSMDataParser>();
	FOSMDataSet DataSet;
	const FOSMParseResult Result = Parser->ParseXmlString(Xml, DataSet);

	TestTrue(TEXT("XML-Parse erfolgreich"), Result.bSuccess);
	TestEqual(TEXT("NodeCount == 3"), Result.NodeCount, 3);
	TestEqual(TEXT("WayCount == 1"), Result.WayCount, 1);
	TestEqual(TEXT("RelationCount == 1"), Result.RelationCount, 1);
	TestEqual(TEXT("SkippedElementCount == 1 (Way mit 1 Node)"), Result.SkippedElementCount, 1);

	// Der getaggte Node 2 muss als Ampel erkannt werden.
	TestTrue(TEXT("Node 2 vorhanden"), DataSet.Nodes.Contains(2));
	const FOSMNode* Node2 = DataSet.Nodes.Find(2);
	if (TestNotNull(TEXT("Node 2 gefunden"), Node2))
	{
		TestTrue(TEXT("Node 2 ist Ampel"), Node2->IsTrafficSignal());
	}

	// Way 100 traegt Tags und drei aufloesbare Nodes.
	const FOSMWay* Way100 = DataSet.Ways.Find(100);
	if (TestNotNull(TEXT("Way 100 vorhanden"), Way100))
	{
		TestTrue(TEXT("Way 100 ist Highway"), Way100->IsHighway());
		TestTrue(TEXT("Way 100 Name"), Way100->GetTag(TEXT("name")) == TEXT("Teststrasse"));

		TArray<FGeoCoordinate> Coords;
		int32 Missing = -1;
		TestTrue(TEXT("Way 100 Koordinaten aufloesbar"), DataSet.ResolveWayCoordinates(*Way100, Coords, Missing));
		TestEqual(TEXT("Way 100: 3 Koordinaten"), Coords.Num(), 3);
		TestEqual(TEXT("Way 100: keine fehlenden Nodes"), Missing, 0);
	}

	// Die Relation ist ein Multipolygon mit einem outer-Way-Member.
	const FOSMRelation* Relation200 = DataSet.Relations.Find(200);
	if (TestNotNull(TEXT("Relation 200 vorhanden"), Relation200))
	{
		TestTrue(TEXT("Relation 200 ist Multipolygon"), Relation200->IsMultipolygon());
		TestEqual(TEXT("Relation 200: 1 Member"), Relation200->Members.Num(), 1);
		TestEqual(TEXT("Member-Ref == 100"), static_cast<int64>(Relation200->Members[0].Ref), static_cast<int64>(100));
		TestTrue(TEXT("Member-Typ == Way"), Relation200->Members[0].Type == EOSMMemberType::Way);
		TestTrue(TEXT("Member-Rolle == outer"), Relation200->Members[0].Role == FName(TEXT("outer")));
	}

	// Bounds aus den drei Node-Positionen.
	TestTrue(TEXT("Bounds MinLon"), FMath::IsNearlyEqual(DataSet.Bounds.MinLongitude, 8.2400, 1e-9));
	TestTrue(TEXT("Bounds MinLat"), FMath::IsNearlyEqual(DataSet.Bounds.MinLatitude, 50.0824, 1e-9));
	TestTrue(TEXT("Bounds MaxLon"), FMath::IsNearlyEqual(DataSet.Bounds.MaxLongitude, 8.2410, 1e-9));
	TestTrue(TEXT("Bounds MaxLat"), FMath::IsNearlyEqual(DataSet.Bounds.MaxLatitude, 50.0830, 1e-9));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOSMJsonParseTest,
	"WiesbadenReal.GIS.OSMDataParser.JsonParse",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FOSMJsonParseTest::RunTest(const FString& Parameters)
{
	const FString Json =
		TEXT("{\"elements\":[")
		TEXT("{\"type\":\"node\",\"id\":1,\"lat\":50.0824,\"lon\":8.2400},")
		TEXT("{\"type\":\"node\",\"id\":2,\"lat\":50.0830,\"lon\":8.2410},")
		TEXT("{\"type\":\"way\",\"id\":10,\"nodes\":[1,2],\"tags\":{\"highway\":\"primary\",\"name\":\"Rheinstrasse\",\"oneway\":\"yes\"}},")
		TEXT("{\"type\":\"relation\",\"id\":20,\"members\":[{\"type\":\"way\",\"ref\":10,\"role\":\"outer\"}],\"tags\":{\"type\":\"multipolygon\"}},")
		TEXT("{\"type\":\"area\",\"id\":30}")
		TEXT("]}");

	UOSMDataParser* Parser = NewObject<UOSMDataParser>();
	FOSMDataSet DataSet;
	const FOSMParseResult Result = Parser->ParseJsonString(Json, DataSet);

	TestTrue(TEXT("JSON-Parse erfolgreich"), Result.bSuccess);
	TestEqual(TEXT("NodeCount == 2"), Result.NodeCount, 2);
	TestEqual(TEXT("WayCount == 1"), Result.WayCount, 1);
	TestEqual(TEXT("RelationCount == 1"), Result.RelationCount, 1);
	// Das Overpass-Hilfselement "area" wird als verworfen gezaehlt.
	TestEqual(TEXT("SkippedElementCount == 1 (area)"), Result.SkippedElementCount, 1);

	const FOSMWay* Way10 = DataSet.Ways.Find(10);
	if (TestNotNull(TEXT("Way 10 vorhanden"), Way10))
	{
		TestTrue(TEXT("Way 10 Name"), Way10->GetTag(TEXT("name")) == TEXT("Rheinstrasse"));
		TestEqual(TEXT("Way 10: 2 Nodes"), Way10->NodeIds.Num(), 2);
	}

	const FOSMRelation* Relation20 = DataSet.Relations.Find(20);
	if (TestNotNull(TEXT("Relation 20 vorhanden"), Relation20))
	{
		TestTrue(TEXT("Relation 20 ist Multipolygon"), Relation20->IsMultipolygon());
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOSMParseFailureTest,
	"WiesbadenReal.GIS.OSMDataParser.Failures",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FOSMParseFailureTest::RunTest(const FString& Parameters)
{
	// Fuenf der Fehlerpfade loggen bewusst einen Fehler (erwartet) - ohne
	// Deklaration wuerde der Automation-Runner jedes Error-Log als
	// Testfehler werten. Die leeren Eingabepfade ("Leerer XML-/JSON-Inhalt.")
	// loggen bewusst NICHT (stiller Rueckgabepfad) und brauchen keine
	// Deklaration.
	AddExpectedErrorPlain(TEXT("XML war syntaktisch gueltig, enthielt aber keine Nodes."), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedErrorPlain(TEXT("OSM-XML-Parserfehler"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedErrorPlain(TEXT("JSON konnte nicht deserialisiert werden."), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedErrorPlain(TEXT("JSON enthaelt kein 'elements'-Array"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedErrorPlain(TEXT("Overpass-JSON enthielt keine Nodes."), EAutomationExpectedErrorFlags::Contains, 1);

	UOSMDataParser* Parser = NewObject<UOSMDataParser>();
	FOSMDataSet DataSet;

	// Leerer Eingabestring.
	{
		const FOSMParseResult Result = Parser->ParseXmlString(TEXT(""), DataSet);
		TestTrue(TEXT("Leeres XML schlaegt fehl"), !Result.bSuccess);
		TestTrue(TEXT("Leeres XML mit Fehlermeldung"), !Result.ErrorMessage.IsEmpty());
	}

	// Syntaktisch gueltiges XML ohne Nodes.
	{
		const FOSMParseResult Result = Parser->ParseXmlString(TEXT("<osm version=\"0.6\"></osm>"), DataSet);
		TestTrue(TEXT("XML ohne Nodes schlaegt fehl"), !Result.bSuccess);
		TestTrue(TEXT("XML ohne Nodes meldet fehlende Nodes"), Result.ErrorMessage.Contains(TEXT("Nodes")));
	}

	// Nicht wohlgeformtes XML.
	{
		const FOSMParseResult Result = Parser->ParseXmlString(TEXT("<osm><node id=\"1\"></osm>"), DataSet);
		TestTrue(TEXT("Kaputtes XML schlaegt fehl"), !Result.bSuccess);
	}

	// Leeres JSON.
	{
		const FOSMParseResult Result = Parser->ParseJsonString(TEXT(""), DataSet);
		TestTrue(TEXT("Leeres JSON schlaegt fehl"), !Result.bSuccess);
	}

	// Ungueltiges JSON.
	{
		const FOSMParseResult Result = Parser->ParseJsonString(TEXT("kein json"), DataSet);
		TestTrue(TEXT("Ungueltiges JSON schlaegt fehl"), !Result.bSuccess);
	}

	// JSON ohne elements-Array.
	{
		const FOSMParseResult Result = Parser->ParseJsonString(TEXT("{\"foo\": 1}"), DataSet);
		TestTrue(TEXT("JSON ohne elements schlaegt fehl"), !Result.bSuccess);
	}

	// Leeres elements-Array.
	{
		const FOSMParseResult Result = Parser->ParseJsonString(TEXT("{\"elements\": []}"), DataSet);
		TestTrue(TEXT("Leeres elements-Array schlaegt fehl"), !Result.bSuccess);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOSMTagParserTest,
	"WiesbadenReal.GIS.OSMDataParser.TagParser",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FOSMTagParserTest::RunTest(const FString& Parameters)
{
	// -- highway=* -----------------------------------------------------------
	TestTrue(TEXT("highway residential"), FOSMTagParser::ParseHighwayType(TEXT("residential")) == EOSMHighwayType::Residential);
	TestTrue(TEXT("highway trim+case"), FOSMTagParser::ParseHighwayType(TEXT("  MOTORWAY ")) == EOSMHighwayType::Motorway);
	TestTrue(TEXT("highway motorway_link"), FOSMTagParser::ParseHighwayType(TEXT("motorway_link")) == EOSMHighwayType::MotorwayLink);
	TestTrue(TEXT("highway unbekannt -> None"), FOSMTagParser::ParseHighwayType(TEXT("bogus")) == EOSMHighwayType::None);

	// -- surface=* ----------------------------------------------------------
	TestTrue(TEXT("surface asphalt"), FOSMTagParser::ParseSurfaceType(TEXT("asphalt")) == EOSMSurfaceType::Asphalt);
	TestTrue(TEXT("surface case"), FOSMTagParser::ParseSurfaceType(TEXT("COBBLESTONE")) == EOSMSurfaceType::Cobblestone);
	TestTrue(TEXT("surface paved -> Asphalt"), FOSMTagParser::ParseSurfaceType(TEXT("paved")) == EOSMSurfaceType::Asphalt);
	TestTrue(TEXT("surface unbekannt -> Unknown"), FOSMTagParser::ParseSurfaceType(TEXT("xyz")) == EOSMSurfaceType::Unknown);

	// -- sidewalk=* ---------------------------------------------------------
	TestTrue(TEXT("sidewalk both"), FOSMTagParser::ParseSidewalkType(MakeWay({ { TEXT("sidewalk"), TEXT("both") } })) == EOSMSidewalkType::Both);
	TestTrue(TEXT("sidewalk left"), FOSMTagParser::ParseSidewalkType(MakeWay({ { TEXT("sidewalk"), TEXT("left") } })) == EOSMSidewalkType::Left);
	TestTrue(TEXT("sidewalk:left=yes"), FOSMTagParser::ParseSidewalkType(MakeWay({ { TEXT("sidewalk:left"), TEXT("yes") } })) == EOSMSidewalkType::Left);
	TestTrue(TEXT("sidewalk:both=yes"), FOSMTagParser::ParseSidewalkType(MakeWay({ { TEXT("sidewalk:both"), TEXT("yes") } })) == EOSMSidewalkType::Both);
	TestTrue(TEXT("sidewalk no"), FOSMTagParser::ParseSidewalkType(MakeWay({ { TEXT("sidewalk"), TEXT("no") } })) == EOSMSidewalkType::None);
	TestTrue(TEXT("sidewalk fehlt -> None"), FOSMTagParser::ParseSidewalkType(MakeWay({})) == EOSMSidewalkType::None);

	// -- oneway=* -----------------------------------------------------------
	TestTrue(TEXT("oneway yes -> Forward"), FOSMTagParser::ParseOneway(MakeWay({ { TEXT("oneway"), TEXT("yes") } })) == EOSMOnewayType::Forward);
	TestTrue(TEXT("oneway -1 -> Backward"), FOSMTagParser::ParseOneway(MakeWay({ { TEXT("oneway"), TEXT("-1") } })) == EOSMOnewayType::Backward);
	TestTrue(TEXT("oneway reversible"), FOSMTagParser::ParseOneway(MakeWay({ { TEXT("oneway"), TEXT("reversible") } })) == EOSMOnewayType::Reversible);
	TestTrue(TEXT("oneway no -> No"), FOSMTagParser::ParseOneway(MakeWay({ { TEXT("oneway"), TEXT("no") } })) == EOSMOnewayType::No);
	TestTrue(TEXT("roundabout implizit Forward"), FOSMTagParser::ParseOneway(MakeWay({ { TEXT("junction"), TEXT("roundabout") } })) == EOSMOnewayType::Forward);
	TestTrue(TEXT("roundabout + oneway=no -> Forward"), FOSMTagParser::ParseOneway(MakeWay({ { TEXT("junction"), TEXT("roundabout") }, { TEXT("oneway"), TEXT("no") } })) == EOSMOnewayType::Forward);

	// -- building=* / roof:shape=* ------------------------------------------
	TestTrue(TEXT("building apartments"), FOSMTagParser::ParseBuildingType(TEXT("apartments")) == EOSMBuildingType::Apartments);
	TestTrue(TEXT("building yes -> Generic"), FOSMTagParser::ParseBuildingType(TEXT("yes")) == EOSMBuildingType::Generic);
	TestTrue(TEXT("building unbekannt -> Generic"), FOSMTagParser::ParseBuildingType(TEXT("bogus")) == EOSMBuildingType::Generic);

	TestTrue(TEXT("roof gabled"), FOSMTagParser::ParseRoofShape(TEXT("gabled")) == EOSMRoofShape::Gabled);
	TestTrue(TEXT("roof half-hipped -> Hipped"), FOSMTagParser::ParseRoofShape(TEXT("half-hipped")) == EOSMRoofShape::Hipped);
	TestTrue(TEXT("roof onion -> Dome"), FOSMTagParser::ParseRoofShape(TEXT("onion")) == EOSMRoofShape::Dome);
	TestTrue(TEXT("roof unbekannt -> Flat"), FOSMTagParser::ParseRoofShape(TEXT("bogus")) == EOSMRoofShape::Flat);
	TestTrue(TEXT("roof leer -> Flat"), FOSMTagParser::ParseRoofShape(TEXT("")) == EOSMRoofShape::Flat);

	// -- Laengenangaben ------------------------------------------------------
	double Meters = 0.0;
	TestTrue(TEXT("Laenge \"12\""), FOSMTagParser::ParseLengthMeters(TEXT("12"), Meters) && FMath::IsNearlyEqual(Meters, 12.0, 1e-9));
	TestTrue(TEXT("Laenge \"12 m\""), FOSMTagParser::ParseLengthMeters(TEXT("12 m"), Meters) && FMath::IsNearlyEqual(Meters, 12.0, 1e-9));
	TestTrue(TEXT("Laenge \"12.5m\""), FOSMTagParser::ParseLengthMeters(TEXT("12.5m"), Meters) && FMath::IsNearlyEqual(Meters, 12.5, 1e-9));
	TestTrue(TEXT("Laenge deutsches Komma \"12,5\""), FOSMTagParser::ParseLengthMeters(TEXT("12,5"), Meters) && FMath::IsNearlyEqual(Meters, 12.5, 1e-9));
	TestTrue(TEXT("Laenge \"40'\""), FOSMTagParser::ParseLengthMeters(TEXT("40'"), Meters) && FMath::IsNearlyEqual(Meters, 12.192, 1e-6));
	TestTrue(TEXT("Laenge 40 Fuss 6 Zoll"), FOSMTagParser::ParseLengthMeters(TEXT("40'6\""), Meters) && FMath::IsNearlyEqual(Meters, 12.3444, 1e-6));
	TestTrue(TEXT("Laenge \"3 ft\""), FOSMTagParser::ParseLengthMeters(TEXT("3 ft"), Meters) && FMath::IsNearlyEqual(Meters, 0.9144, 1e-9));
	TestTrue(TEXT("Laenge \"abc\" schlaegt fehl"), !FOSMTagParser::ParseLengthMeters(TEXT("abc"), Meters));

	// -- maxspeed=* ---------------------------------------------------------
	double Kmh = 0.0;
	TestTrue(TEXT("maxspeed 50"), FOSMTagParser::ParseMaxSpeedKmh(TEXT("50"), Kmh) && FMath::IsNearlyEqual(Kmh, 50.0, 1e-9));
	TestTrue(TEXT("maxspeed 30 mph"), FOSMTagParser::ParseMaxSpeedKmh(TEXT("30 mph"), Kmh) && FMath::IsNearlyEqual(Kmh, 48.2803, 1e-3));
	TestTrue(TEXT("maxspeed walk"), FOSMTagParser::ParseMaxSpeedKmh(TEXT("walk"), Kmh) && FMath::IsNearlyEqual(Kmh, 7.0, 1e-9));
	TestTrue(TEXT("maxspeed none -> 250"), FOSMTagParser::ParseMaxSpeedKmh(TEXT("none"), Kmh) && FMath::IsNearlyEqual(Kmh, 250.0, 1e-9));
	TestTrue(TEXT("maxspeed DE:urban -> 50"), FOSMTagParser::ParseMaxSpeedKmh(TEXT("DE:urban"), Kmh) && FMath::IsNearlyEqual(Kmh, 50.0, 1e-9));
	TestTrue(TEXT("maxspeed DE:rural -> 100"), FOSMTagParser::ParseMaxSpeedKmh(TEXT("DE:rural"), Kmh) && FMath::IsNearlyEqual(Kmh, 100.0, 1e-9));
	TestTrue(TEXT("maxspeed 500 gekappt"), FOSMTagParser::ParseMaxSpeedKmh(TEXT("500"), Kmh) && FMath::IsNearlyEqual(Kmh, 250.0, 1e-9));
	TestTrue(TEXT("maxspeed Muell schlaegt fehl"), !FOSMTagParser::ParseMaxSpeedKmh(TEXT("garbage"), Kmh));

	// -- lanes=* ------------------------------------------------------------
	int32 Lanes = 0;
	TestTrue(TEXT("lanes 2"), FOSMTagParser::ParseLaneCount(TEXT("2"), Lanes) && Lanes == 2);
	TestTrue(TEXT("lanes 1.5 -> 1"), FOSMTagParser::ParseLaneCount(TEXT("1.5"), Lanes) && Lanes == 1);
	TestTrue(TEXT("lanes 2;3 -> 2"), FOSMTagParser::ParseLaneCount(TEXT("2;3"), Lanes) && Lanes == 2);
	TestTrue(TEXT("lanes 0 schlaegt fehl"), !FOSMTagParser::ParseLaneCount(TEXT("0"), Lanes));
	TestTrue(TEXT("lanes 13 schlaegt fehl"), !FOSMTagParser::ParseLaneCount(TEXT("13"), Lanes));

	// -- turn:lanes ---------------------------------------------------------
	{
		const TArray<FString> Values = FOSMTagParser::SplitLaneValues(TEXT("left||right"));
		TestEqual(TEXT("turn:lanes 3 Eintraege"), Values.Num(), 3);
		TestTrue(TEXT("turn:lanes [0] == left"), Values[0] == TEXT("left"));
		TestTrue(TEXT("turn:lanes [1] leer erhalten"), Values[1].IsEmpty());
		TestTrue(TEXT("turn:lanes [2] == right"), Values[2] == TEXT("right"));
	}

	// -- building:levels / Hoehenaufloesung ---------------------------------
	TestTrue(TEXT("levels 4"), FOSMTagParser::ParseBuildingLevels(MakeWay({ { TEXT("building:levels"), TEXT("4") } }), Lanes) && Lanes == 4);
	TestTrue(TEXT("levels 2.5 -> 3"), FOSMTagParser::ParseBuildingLevels(MakeWay({ { TEXT("building:levels"), TEXT("2.5") } }), Lanes) && Lanes == 3);
	TestTrue(TEXT("levels Fallback"), FOSMTagParser::ParseBuildingLevels(MakeWay({ { TEXT("levels"), TEXT("3") } }), Lanes) && Lanes == 3);
	TestTrue(TEXT("levels fehlen schlaegt fehl"), !FOSMTagParser::ParseBuildingLevels(MakeWay({}), Lanes));

	TestTrue(TEXT("Hoehe explizit 20 m"), FMath::IsNearlyEqual(
		FOSMTagParser::ResolveBuildingHeightMeters(MakeWay({ { TEXT("height"), TEXT("20") } }), EOSMBuildingType::Generic), 20.0, 1e-9));
	TestTrue(TEXT("Hoehe aus 5 Geschossen"), FMath::IsNearlyEqual(
		FOSMTagParser::ResolveBuildingHeightMeters(MakeWay({ { TEXT("building:levels"), TEXT("5") } }), EOSMBuildingType::Generic), 16.0, 1e-9));
	TestTrue(TEXT("Hoehe Apartments-Default"), FMath::IsNearlyEqual(
		FOSMTagParser::ResolveBuildingHeightMeters(MakeWay({}), EOSMBuildingType::Apartments), 16.0, 1e-9));
	TestTrue(TEXT("Hoehe Kirche-Default 22 m"), FMath::IsNearlyEqual(
		FOSMTagParser::ResolveBuildingHeightMeters(MakeWay({}), EOSMBuildingType::Church), 22.0, 1e-9));

	// -- Reibung / Befahrbarkeit --------------------------------------------
	TestTrue(TEXT("Reibung Asphalt 1.0"), FMath::IsNearlyEqual(FOSMTagParser::GetSurfaceFriction(EOSMSurfaceType::Asphalt), 1.0, 1e-9));
	TestTrue(TEXT("Reibung Gras 0.45"), FMath::IsNearlyEqual(FOSMTagParser::GetSurfaceFriction(EOSMSurfaceType::Grass), 0.45, 1e-9));

	TestTrue(TEXT("Residential befahrbar"), FOSMTagParser::IsDrivable(EOSMHighwayType::Residential));
	TestTrue(TEXT("Footway nicht befahrbar"), !FOSMTagParser::IsDrivable(EOSMHighwayType::Footway));
	TestTrue(TEXT("Residential begehbar"), FOSMTagParser::IsWalkable(EOSMHighwayType::Residential));
	TestTrue(TEXT("Motorway nicht begehbar"), !FOSMTagParser::IsWalkable(EOSMHighwayType::Motorway));

	// -- Way-Helfer ---------------------------------------------------------
	{
		FOSMWay Closed;
		Closed.NodeIds = { 1, 2, 3, 1 };
		TestTrue(TEXT("Way geschlossen"), Closed.IsClosed());

		FOSMWay Open;
		Open.NodeIds = { 1, 2, 3 };
		TestTrue(TEXT("Way offen"), !Open.IsClosed());

		TestTrue(TEXT("Bridge ja"), MakeWay({ { TEXT("bridge"), TEXT("yes") } }).IsBridge());
		TestTrue(TEXT("Bridge nein"), !MakeWay({ { TEXT("bridge"), TEXT("no") } }).IsBridge());
		TestTrue(TEXT("Tunnel ja"), MakeWay({ { TEXT("tunnel"), TEXT("yes") } }).IsTunnel());
		TestEqual(TEXT("Layer 2"), MakeWay({ { TEXT("layer"), TEXT("2") } }).GetLayer(), 2);
		TestEqual(TEXT("Layer Default 0"), MakeWay({}).GetLayer(), 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsmDuplicateNodeTagTest,
	"WiesbadenReal.GIS.OSMDataParser.DuplicateNodeTags",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Ein doppelt gelieferter Knoten darf seine Tags NICHT verlieren.
 *
 * Overpass gibt denselben Knoten mehrfach aus: einmal aus einer Knoten-Abfrage
 * MIT Tags (etwa highway=traffic_signals) und danach nochmals aus der
 * Way-Rekursion OHNE Tags. Ein blindes Nodes.Add() ueberschreibt den getaggten
 * Eintrag.
 *
 * Gemessen an der Wiesbaden-Datei: 1.125.032 Knoten-Eintraege fuer 1.115.437
 * eindeutige Knoten. Von 2.310 Ampelknoten kamen dadurch nur DREI im Datensatz
 * an - die Stadt hatte keine einzige Ampel ("20213 Kreuzungen, 0 Ampeln"), und
 * die komplette Ampel-Steuerung lief ins Leere. Betroffen waren ebenso
 * Zebrastreifen, Stopp- und Vorfahrtsknoten.
 *
 * Der Test prueft beide Reihenfolgen, weil nicht garantiert ist, welches
 * Vorkommen zuerst kommt.
 */
bool FOsmDuplicateNodeTagTest::RunTest(const FString& Parameters)
{
	UOSMDataParser* Parser = NewObject<UOSMDataParser>();

	// Fall 1: getaggter Knoten zuerst, danach das nackte Duplikat.
	{
		const FString Json = TEXT(R"({"elements":[
			{"type":"node","id":1,"lat":50.08,"lon":8.24,"tags":{"highway":"traffic_signals"}},
			{"type":"node","id":1,"lat":50.08,"lon":8.24}
		]})");

		FOSMDataSet DataSet;
		const FOSMParseResult Result = Parser->ParseJsonString(Json, DataSet);

		TestTrue(TEXT("Fall 1: geparst"), Result.bSuccess);
		TestEqual(TEXT("Fall 1: ein Knoten"), DataSet.Nodes.Num(), 1);

		const FOSMNode* Node = DataSet.Nodes.Find(1);
		TestTrue(TEXT("Fall 1: Knoten vorhanden"), Node != nullptr);
		if (Node)
		{
			TestTrue(TEXT("Fall 1: Ampel-Tag ueberlebt das Duplikat"), Node->IsTrafficSignal());
		}
	}

	// Fall 2: nacktes Duplikat zuerst, danach der getaggte Knoten.
	{
		const FString Json = TEXT(R"({"elements":[
			{"type":"node","id":2,"lat":50.08,"lon":8.24},
			{"type":"node","id":2,"lat":50.08,"lon":8.24,"tags":{"highway":"traffic_signals"}}
		]})");

		FOSMDataSet DataSet;
		const FOSMParseResult Result = Parser->ParseJsonString(Json, DataSet);

		TestTrue(TEXT("Fall 2: geparst"), Result.bSuccess);
		TestEqual(TEXT("Fall 2: ein Knoten"), DataSet.Nodes.Num(), 1);

		const FOSMNode* Node = DataSet.Nodes.Find(2);
		TestTrue(TEXT("Fall 2: Knoten vorhanden"), Node != nullptr);
		if (Node)
		{
			TestTrue(TEXT("Fall 2: Ampel-Tag kommt an"), Node->IsTrafficSignal());
		}
	}

	// Fall 3: Tags aus BEIDEN Vorkommen muessen sich vereinen.
	{
		const FString Json = TEXT(R"({"elements":[
			{"type":"node","id":3,"lat":50.08,"lon":8.24,"tags":{"highway":"crossing"}},
			{"type":"node","id":3,"lat":50.08,"lon":8.24,"tags":{"crossing":"zebra"}}
		]})");

		FOSMDataSet DataSet;
		Parser->ParseJsonString(Json, DataSet);

		const FOSMNode* Node = DataSet.Nodes.Find(3);
		if (TestTrue(TEXT("Fall 3: Knoten vorhanden"), Node != nullptr))
		{
			TestTrue(TEXT("Fall 3: Zebrastreifen erkannt"), Node->IsCrossing());
			TestTrue(TEXT("Fall 3: Zebra-Tag vereint"),
				Node->HasTagValue(TEXT("crossing"), TEXT("zebra")));
		}
	}

	return true;
}

/**
 * Ein doppelt gelieferter Way darf seine Tags nicht verlieren.
 *
 * Overpass liefert Ways, die Mitglied einer Relation sind, ZWEIMAL: einmal
 * vollstaendig getaggt und danach als blosses Geruest ohne Tags. Ein blindes
 * TMap::Add ersetzt den getaggten Eintrag durch den tagfreien - highway=*
 * verschwindet, und der Way ist ab da keine Strasse mehr.
 *
 * An der Wiesbaden-Datei: 2.045 doppelte Ways, bei ALLEN steht die tagfreie
 * Kopie hinten. 2.033 davon waren Strassen; im Spiel fehlten dadurch 1.990
 * befahrbare Wege mit 118,6 km.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOSMDuplicateWayTest,
	"WiesbadenReal.GIS.OSMDataParser.DoppelterWayBehaeltTags",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FOSMDuplicateWayTest::RunTest(const FString& Parameters)
{
	// Reihenfolge wie in der echten Datei: erst getaggt, dann Geruest.
	const FString Json = TEXT(R"({"elements":[
		{"type":"node","id":1,"lat":50.08,"lon":8.24},
		{"type":"node","id":2,"lat":50.09,"lon":8.25},
		{"type":"way","id":100,"nodes":[1,2],"tags":{"highway":"secondary","name":"Platter Strasse"}},
		{"type":"way","id":100,"nodes":[1,2]}
	]})");

	UOSMDataParser* Parser = NewObject<UOSMDataParser>();
	FOSMDataSet DataSet;
	const FOSMParseResult Result = Parser->ParseJsonString(Json, DataSet);

	TestTrue(TEXT("geparst"), Result.bSuccess);
	TestEqual(TEXT("ein Way"), DataSet.Ways.Num(), 1);

	const FOSMWay* Way = DataSet.Ways.Find(100);
	if (!TestNotNull(TEXT("Way 100 vorhanden"), Way))
	{
		return false;
	}

	TestTrue(TEXT("ist noch eine Strasse"), Way->IsHighway());
	TestEqual(TEXT("Name erhalten"), Way->GetTag(TEXT("name")), FString(TEXT("Platter Strasse")));
	TestEqual(TEXT("Knoten erhalten"), Way->NodeIds.Num(), 2);
	return true;
}

/** Umgekehrte Reihenfolge - das Geruest zuerst - darf nichts aendern. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOSMDuplicateWayReverseTest,
	"WiesbadenReal.GIS.OSMDataParser.DoppelterWayUmgekehrt",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FOSMDuplicateWayReverseTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({"elements":[
		{"type":"node","id":1,"lat":50.08,"lon":8.24},
		{"type":"node","id":2,"lat":50.09,"lon":8.25},
		{"type":"way","id":100,"nodes":[1,2]},
		{"type":"way","id":100,"nodes":[1,2],"tags":{"highway":"secondary"}}
	]})");

	UOSMDataParser* Parser = NewObject<UOSMDataParser>();
	FOSMDataSet DataSet;
	Parser->ParseJsonString(Json, DataSet);

	const FOSMWay* Way = DataSet.Ways.Find(100);
	if (!TestNotNull(TEXT("Way 100 vorhanden"), Way))
	{
		return false;
	}
	TestTrue(TEXT("ist eine Strasse"), Way->IsHighway());
	return true;
}
