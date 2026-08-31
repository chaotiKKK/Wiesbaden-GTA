// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

/**
 * Deckungsgrad des Strassennetzes gegen die OSM-Quelldaten.
 *
 * Anlass: "Platter Strasse stadtauswaerts fehlt in Spielwelt und Minikarte".
 * Der Abgleich des gebauten Netzes mit den Quelldaten
 * (Tools/audit_streets.mjs) ergab 1.990 fehlende Fahrbahnwege, 118,6 km -
 * darunter Stuecke von Rheinallee, Mainzer Strasse, Konrad-Adenauer-Ring und
 * Kaiser-Friedrich-Ring. Alle 1.990 haben VOLLSTAENDIGE Knoten, scheiden also
 * als "Extrakt abgeschnitten" aus.
 *
 * Dieser Test laeuft die Strecke Parser -> Strassengenerator auf den ECHTEN
 * Daten und sagt, an welcher der Verwerfungsstellen sie verschwinden. Der
 * Alternativweg waere ein kompletter Stadt-Neubau; laut
 * Saved/BuildHistory/CityBuilds.csv dauert der 40 bis 64 Minuten.
 *
 * Der Test wird bei fehlender Datei UEBERSPRUNGEN, nicht rot: Die 170 MB
 * grosse Extraktdatei gehoert nicht ins Repository.
 */

#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"

#include "GIS/GeoCoordinateConverter.h"
#include "GIS/OSMDataParser.h"
#include "GIS/RoadNetworkGenerator.h"
#include "GIS/RoadTypeLibrary.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadCoverageTest,
	"WiesbadenReal.GIS.RoadNetwork.Deckungsgrad",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::PerfFilter)

bool FRoadCoverageTest::RunTest(const FString& Parameters)
{
	const FString OsmPath = FPaths::Combine(
		FPaths::ProjectDir(), TEXT("Data"), TEXT("Raw"), TEXT("OSM"), TEXT("wiesbaden.osm.json"));

	if (!FPlatformFileManager::Get().GetPlatformFile().FileExists(*OsmPath))
	{
		AddInfo(FString::Printf(TEXT("Uebersprungen - %s fehlt."), *OsmPath));
		return true;
	}

	FString Json;
	if (!TestTrue(TEXT("OSM-Datei lesbar"), FFileHelper::LoadFileToString(Json, *OsmPath)))
	{
		return false;
	}

	UOSMDataParser* Parser = NewObject<UOSMDataParser>();
	FOSMDataSet DataSet;
	const FOSMParseResult ParseResult = Parser->ParseJsonString(Json, DataSet);
	if (!TestTrue(TEXT("OSM-Datei gelesen"), ParseResult.bSuccess))
	{
		AddError(ParseResult.ErrorMessage);
		return false;
	}

	UGeoCoordinateConverter* Converter = NewObject<UGeoCoordinateConverter>();
	Converter->InitializeWithWiesbadenOrigin();

	URoadTypeLibrary* TypeLibrary = NewObject<URoadTypeLibrary>();
	if (!TypeLibrary->LoadFromJsonFile(URoadTypeLibrary::GetDefaultConfigPath()))
	{
		// Genau wie die Pipeline: die eingebauten Vorgaben als Rueckfall.
		TypeLibrary->ApplyBuiltInDefaults();
	}

	URoadNetworkGenerator* Generator = NewObject<URoadNetworkGenerator>();
	FRoadNetwork Network;
	FRoadGenerationSettings Settings;
	const FRoadGenerationReport Report = Generator->Generate(
		DataSet, Converter, TypeLibrary, /*HeightSampler=*/nullptr, Settings, Network,
		/*OutMeshData=*/nullptr);

	AddInfo(FString::Printf(
		TEXT("Ways gesamt %d, verarbeitet %d, Segmente %d."),
		DataSet.Ways.Num(), Report.ProcessedWayCount, Report.SegmentCount));
	AddInfo(FString::Printf(
		TEXT("Verworfen: %d unbekannte Art, %d nicht befahrbar, %d ohne Koordinaten, ")
		TEXT("%d unter zwei Punkten."),
		Report.SkippedUnknownTypeCount, Report.SkippedNotDrivableCount,
		Report.SkippedUnresolvedCount, Report.SkippedTooFewPointsCount));
	AddInfo(FString::Printf(
		TEXT("STILL verloren: %d Ways ohne Segment, %d Teilstuecke durchgefallen."),
		Report.WaysWithoutSegmentCount, Report.DroppedSubSegmentCount));

	// Welche befahrbaren Ways haben kein Segment ergeben? Der Zaehler sagt
	// WIEVIELE, diese Liste sagt WELCHE - ohne sie waere die naechste Frage
	// wieder nur zu erraten.
	TSet<int64> WithSegment;
	WithSegment.Reserve(Network.Segments.Num());
	for (const FRoadSegment& Segment : Network.Segments)
	{
		WithSegment.Add(Segment.SourceWayId);
	}

	int32 MissingDrivable = 0;
	int32 Listed = 0;
	for (const TPair<FOSMId, FOSMWay>& Pair : DataSet.Ways)
	{
		const FOSMWay& Way = Pair.Value;
		if (!Way.IsHighway())
		{
			continue;
		}
		const EOSMHighwayType Type = FOSMTagParser::ParseHighwayType(Way.GetTag(TEXT("highway")));
		if (Type == EOSMHighwayType::None || !FOSMTagParser::IsDrivable(Type))
		{
			continue;
		}
		if (WithSegment.Contains(Pair.Key))
		{
			continue;
		}

		++MissingDrivable;
		if (Listed < 25)
		{
			++Listed;
			AddInfo(FString::Printf(TEXT("  ohne Segment: Way %lld  %s  highway=%s  Knoten %d"),
				static_cast<int64>(Pair.Key),
				*Way.GetTag(TEXT("name")),
				*Way.GetTag(TEXT("highway")),
				Way.NodeIds.Num()));
		}
	}

	AddInfo(FString::Printf(TEXT("Befahrbare Ways ohne Segment: %d"), MissingDrivable));

	// Kein TestEqual auf 0: Der Test soll den Zustand SICHTBAR machen, nicht
	// den Testlauf blockieren, solange die Ursache offen ist. Sobald sie
	// behoben ist, gehoert hier eine Schranke hin.
	TestTrue(TEXT("Netz nicht leer"), Network.Segments.Num() > 1000);
	return true;
}
