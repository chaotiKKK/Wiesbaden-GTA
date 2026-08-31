// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "GIS/WiesbadenBuildSummary.h"
#include "Core/WiesbadenCityData.h"

/**
 * CSV-Historie der Build-Zusammenfassung: Jeder BuildCity-/Laufzeit-Build
 * schreibt eine Zeile in Saved/BuildHistory/CityBuilds.csv (Kopfzeile beim
 * ersten Lauf), damit Build-Zeiten ueber mehrere Laeufe vergleichbar sind.
 * Getestet wird der datenreine Teil: Kopfzeile + Zeilenformatierung inkl.
 * RFC-4180-Quoting von Komma/Anfuehrungszeichen.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBuildSummaryCsvTest,
	"WiesbadenReal.Core.BuildSummaryCsv",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBuildSummaryCsvTest::RunTest(const FString& Parameters)
{
	// Kopfzeile: alle Kern-Spalten vorhanden.
	const FString Header = BuildSummaryCsvHeader();
	TestTrue(TEXT("Kopfzeile: Timestamp"), Header.Contains(TEXT("Timestamp")));
	TestTrue(TEXT("Kopfzeile: Source"), Header.Contains(TEXT("Source")));
	TestTrue(TEXT("Kopfzeile: DurationSeconds"), Header.Contains(TEXT("DurationSeconds")));
	TestTrue(TEXT("Kopfzeile: Chunks"), Header.Contains(TEXT("Chunks")));
	TestTrue(TEXT("Kopfzeile: MapPath"), Header.Contains(TEXT("MapPath")));
	TestTrue(TEXT("Kopfzeile: Result"), Header.Contains(TEXT("Result")));
	// Terrain-Qualitaetswarnung (seit dem Crop-Fix): beide Warnflags als
	// eigene Spalten, damit Crop-/Hoehenprobleme ueber mehrere Builds
	// vergleichbar sind.
	TestTrue(TEXT("Kopfzeile: TerrainTileTooLarge"), Header.Contains(TEXT("TerrainTileTooLarge")));
	TestTrue(TEXT("Kopfzeile: TerrainHeightRangeSuspicious"), Header.Contains(TEXT("TerrainHeightRangeSuspicious")));

	// Einfacher Datensatz -> exakte CSV-Zeile (Spaltenzahl = Kopfzeile).
	{
		FWiesbadenBuildSummary Summary;
		Summary.Source = TEXT("Editor");
		Summary.Timestamp = TEXT("2026-08-16 12:00:00");
		Summary.DurationSeconds = 83.42;
		Summary.RoadSegments = 1234;
		Summary.Intersections = 96;
		Summary.Buildings = 5678;
		Summary.Signs = 123;
		Summary.TerrainGrid = 513;
		Summary.TerrainMode = TEXT("Landscape");
		Summary.bTerrainTileTooLarge = true;
		Summary.bTerrainHeightRangeSuspicious = false;
		Summary.RegionsWater = 2;
		Summary.RegionsGreen = 5;
		Summary.RegionsResidential = 3;
		Summary.RegionsCommercial = 1;
		Summary.RegionsIndustrial = 1;
		Summary.RegionAssetsTotal = 342;
		Summary.RegionAssetsTrees = 300;
		Summary.RegionAssetsWaterfront = 22;
		Summary.RegionAssetsIndustrial = 20;
		Summary.Chunks = 57;
		Summary.ChunkSizeMeters = 500.0;
		Summary.MapPath = TEXT("/Game/Maps/WiesbadenCity");
		Summary.Result = TEXT("ok");

		const FString Row = FormatBuildSummaryCsvRow(Summary);
		TestEqual(TEXT("CSV-Zeile exakt"),
			Row,
			TEXT("2026-08-16 12:00:00,Editor,83.4,1234,96,5678,123,513,Landscape,1,0,2,5,3,1,1,342,300,22,20,57,500,/Game/Maps/WiesbadenCity,ok"));

		// Spalten-Vertrag: Kopfzeile und Datenzeile muessen gleich viele Spalten
		// haben (24) - ein neues Feld zwischen Header und Row wuerde sonst
		// still alle Spalten verschieben (auch der Node-Parser in
		// Tools/analyze_city_builds.mjs haengt an dieser Reihenfolge).
		TArray<FString> HeaderCols, RowCols;
		Header.ParseIntoArray(HeaderCols, TEXT(","), true);
		Row.ParseIntoArray(RowCols, TEXT(","), true);
		TestEqual(TEXT("CSV: Kopfzeile 24 Spalten"), HeaderCols.Num(), 24);
		TestEqual(TEXT("CSV: Zeile 24 Spalten (== Kopfzeile)"), RowCols.Num(), HeaderCols.Num());
	}

	// RFC-4180-Quoting: Komma und Anfuehrungszeichen im Result werden gequotet
	// ("" verdoppelt, Feld in Anfuehrungszeichen).
	{
		FWiesbadenBuildSummary Summary;
		Summary.Source = TEXT("Runtime");
		Summary.Result = TEXT("fehlgeschlagen: \"World-Partition\", Verifikation");

		const FString Row = FormatBuildSummaryCsvRow(Summary);
		TestTrue(TEXT("Quoting: Anfuehrungszeichen verdoppelt"),
			Row.Contains(TEXT("\"\"World-Partition\"\"")));
		TestTrue(TEXT("Quoting: Feld endet mit schliessendem Anfuehrungszeichen"),
			Row.EndsWith(TEXT("\"")));
	}

	// Leerer Datensatz: alle Spalten trotzdem gefuellt (0/leer), keine Crashs.
	{
		const FWiesbadenBuildSummary Summary;
		const FString Row = FormatBuildSummaryCsvRow(Summary);
		TestTrue(TEXT("Leerer Datensatz: Zeile nicht leer"), !Row.IsEmpty());
		TestTrue(TEXT("Leerer Datensatz: kein Quoting noetig"), !Row.Contains(TEXT("\"")));
	}

	// Einzeiler-Formatierung (Details-Panel + GetLastBuildSummary): identisch
	// zwischen Editor und Runtime, damit beide Pfade dieselbe Zeile zeigen.
	TestEqual(TEXT("Einzeiler exakt"),
		FormatBuildSummaryLine(83.42, TEXT("ok"), TEXT("2026-08-16 15:45:00")),
		TEXT("Letzter Build: 83.4 s, ok (2026-08-16 15:45:00)"));
	TestEqual(TEXT("Einzeiler Fehlerfall"),
		FormatBuildSummaryLine(12.1, TEXT("fehlgeschlagen: OSM-Parserfehler"), TEXT("2026-08-16 15:47:00")),
		TEXT("Letzter Build: 12.1 s, fehlgeschlagen: OSM-Parserfehler (2026-08-16 15:47:00)"));

	// FromCityData-Factory: baut die Summary aus einem FWiesbadenCityData.
	// Gemeinsame Quelle fuer Editor UND Runtime - ersetzt das
	// bUseMovedResults-Flag (WorldBuilder) und die inline-Summary
	// (GameInstance). Regionen/Assets werden nie gemovt und kommen immer aus
	// dem CityData; die gemovten Ergebnis-Member (RoadNetwork/Buildings/
	// FurnitureLayout) koennen als Overrides uebergeben werden.
	{
		FWiesbadenCityData Data;
		Data.RoadNetwork.Segments.Add(FRoadSegment());
		Data.RoadNetwork.Segments.Add(FRoadSegment());
		Data.RoadNetwork.Intersections.Add(FRoadIntersection());
		Data.Buildings.Add(FGeneratedBuilding());
		Data.Buildings.Add(FGeneratedBuilding());
		Data.Buildings.Add(FGeneratedBuilding());
		Data.FurnitureLayout.Signs.Add(FSignInstance());
		Data.TerrainReport.GridSize = 513;

		FWiesbadenRegion Wasser;
		Wasser.Type = ECityRegionType::Water;
		FWiesbadenRegion Gruen;
		Gruen.Type = ECityRegionType::Green;
		FWiesbadenRegion Wohnen;
		Wohnen.Type = ECityRegionType::Residential;
		Data.Regions.Add(Wasser);
		Data.Regions.Add(Gruen);
		Data.Regions.Add(Wohnen);
		Data.RegionAssetReport.AssetCount = 342;
		Data.RegionAssetReport.TreeCount = 300;
		Data.RegionAssetReport.WaterfrontCount = 22;
		Data.RegionAssetReport.IndustrialCount = 20;

		// Terrain-Qualitaetswarnung aus der Pipeline (Crop fehlt / Hoehenspanne
		// unplausibel) soll als eigene CSV-Spalten durchgereicht werden.
		Data.TerrainQuality.bWarnTileTooLarge = true;
		Data.TerrainQuality.bWarnHeightRangeSuspicious = false;
		Data.TerrainQuality.WarningMessage = TEXT("Terrain-Tile deutlich groesser als die OSM-Ausdehnung");

		// Ohne Overrides: alle Zaehler aus dem CityData.
		{
			const FWiesbadenBuildSummary Summary = BuildSummaryFromCityData(
				Data, TEXT("Editor"), TEXT("ok"), 83.4, TEXT("2026-08-16 12:00:00"), nullptr, nullptr, nullptr);
			TestEqual(TEXT("FromCityData: Quelle"), Summary.Source, TEXT("Editor"));
			TestEqual(TEXT("FromCityData: Ergebnis"), Summary.Result, TEXT("ok"));
			TestEqual(TEXT("FromCityData: Dauer"), Summary.DurationSeconds, 83.4);
			TestEqual(TEXT("FromCityData: Zeitpunkt"), Summary.Timestamp, TEXT("2026-08-16 12:00:00"));
			TestEqual(TEXT("FromCityData: Segmente"), Summary.RoadSegments, 2);
			TestEqual(TEXT("FromCityData: Kreuzungen"), Summary.Intersections, 1);
			TestEqual(TEXT("FromCityData: Gebaeude"), Summary.Buildings, 3);
			TestEqual(TEXT("FromCityData: Schilder"), Summary.Signs, 1);
			TestEqual(TEXT("FromCityData: Terrain-Raster"), Summary.TerrainGrid, 513);
			TestEqual(TEXT("FromCityData: Regionen Wasser"), Summary.RegionsWater, 1);
			TestEqual(TEXT("FromCityData: Regionen Gruen"), Summary.RegionsGreen, 1);
			TestEqual(TEXT("FromCityData: Regionen Wohnen"), Summary.RegionsResidential, 1);
			TestEqual(TEXT("FromCityData: Region-Assets gesamt"), Summary.RegionAssetsTotal, 342);
			TestEqual(TEXT("FromCityData: Region-Assets Baeume"), Summary.RegionAssetsTrees, 300);
			TestEqual(TEXT("FromCityData: Region-Assets Ufer"), Summary.RegionAssetsWaterfront, 22);
			TestEqual(TEXT("FromCityData: Region-Assets Industrie"), Summary.RegionAssetsIndustrial, 20);
			TestTrue(TEXT("FromCityData: Terrain-Tile-Warnung uebernommen"), Summary.bTerrainTileTooLarge);
			TestFalse(TEXT("FromCityData: Terrain-Hoehen-Warnung false"), Summary.bTerrainHeightRangeSuspicious);
		}

		// Erfolgspfad (gemovte Member als Overrides): die Zaehler kommen aus
		// den gemovten Membern, Regionen/Assets weiter aus dem CityData.
		{
			FRoadNetwork MovedNetwork;
			MovedNetwork.Segments.Add(FRoadSegment());
			MovedNetwork.Segments.Add(FRoadSegment());
			MovedNetwork.Segments.Add(FRoadSegment());
			MovedNetwork.Segments.Add(FRoadSegment());
			TArray<FGeneratedBuilding> MovedBuildings;
			MovedBuildings.Add(FGeneratedBuilding());
			MovedBuildings.Add(FGeneratedBuilding());
			MovedBuildings.Add(FGeneratedBuilding());
			MovedBuildings.Add(FGeneratedBuilding());
			MovedBuildings.Add(FGeneratedBuilding());
			FRoadFurnitureLayout MovedFurniture;
			MovedFurniture.Signs.Add(FSignInstance());
			MovedFurniture.Signs.Add(FSignInstance());

			const FWiesbadenBuildSummary Summary = BuildSummaryFromCityData(
				Data, TEXT("Editor"), TEXT("ok"), 83.4, TEXT("2026-08-16 12:00:00"),
				&MovedNetwork, &MovedBuildings, &MovedFurniture);
			TestEqual(TEXT("FromCityData: Override Segmente"), Summary.RoadSegments, 4);
			TestEqual(TEXT("FromCityData: Override Gebaeude"), Summary.Buildings, 5);
			TestEqual(TEXT("FromCityData: Override Schilder"), Summary.Signs, 2);
			TestEqual(TEXT("FromCityData: Override laesst Regionen aus Data"), Summary.RegionsWater, 1);
			TestEqual(TEXT("FromCityData: Override laesst Assets aus Data"), Summary.RegionAssetsTotal, 342);
		}

		// Leeres CityData -> alle Zaehler 0, keine Crashs.
		{
			const FWiesbadenCityData Empty;
			const FWiesbadenBuildSummary Summary = BuildSummaryFromCityData(
				Empty, TEXT("Runtime"), TEXT("ok"), 0.0, TEXT(""), nullptr, nullptr, nullptr);
			TestEqual(TEXT("FromCityData: leere Daten -> Segmente 0"), Summary.RoadSegments, 0);
			TestEqual(TEXT("FromCityData: leere Daten -> Regionen 0"), Summary.RegionsWater, 0);
			TestEqual(TEXT("FromCityData: leere Daten -> Assets 0"), Summary.RegionAssetsTotal, 0);
			TestFalse(TEXT("FromCityData: leere Daten -> Terrain-Warnung false"), Summary.bTerrainTileTooLarge);
			TestFalse(TEXT("FromCityData: leere Daten -> Hoehen-Warnung false"), Summary.bTerrainHeightRangeSuspicious);
		}
	}

	return true;
}

/**
 * Gemeinsame LastBuild-Information (FLastBuildInfo): eine USTRUCT fuer
 * Zeitpunkt/Dauer/Ergebnis des letzten Builds, die alle vier Halter
 * (GameInstance, CitySubsystem, GameMode, WorldBuilder) verwenden. Die
 * Einzel-Getter bleiben als Komfort-Wrapper. Getestet wird der datenreine
 * Kern: Defaults, Einzeiler-Formatierung und die FromBuildSummary-Factory.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLastBuildInfoTest,
	"WiesbadenReal.Core.LastBuildInfo",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FLastBuildInfoTest::RunTest(const FString& Parameters)
{
	// Defaults: leer (noch kein Build gelaufen).
	{
		const FLastBuildInfo Info;
		TestTrue(TEXT("Default: leer (IsEmpty)"), Info.IsEmpty());
		TestEqual(TEXT("Default: Dauer 0"), Info.DurationSeconds, 0.0);
		TestTrue(TEXT("Default: Zeitpunkt leer"), Info.Timestamp.IsEmpty());
		TestTrue(TEXT("Default: Ergebnis leer"), Info.Result.IsEmpty());
		TestTrue(TEXT("Default: Einzeiler leer"), Info.GetSummary().IsEmpty());
	}

	// Gefuellt: IsEmpty false, Einzeiler exakt wie FormatBuildSummaryLine.
	{
		FLastBuildInfo Info;
		Info.Timestamp = TEXT("2026-08-16 15:45:00");
		Info.DurationSeconds = 83.4;
		Info.Result = TEXT("ok");
		TestFalse(TEXT("Gefuellt: nicht leer"), Info.IsEmpty());
		TestEqual(TEXT("Einzeiler: identisch zur FormatBuildSummaryLine"),
			Info.GetSummary(),
			FormatBuildSummaryLine(83.4, TEXT("ok"), TEXT("2026-08-16 15:45:00")));
		TestEqual(TEXT("Einzeiler: exaktes Literal"),
			Info.GetSummary(),
			TEXT("Letzter Build: 83.4 s, ok (2026-08-16 15:45:00)"));
	}

	// Fehlerfall-Result: Einzeiler mit "fehlgeschlagen:"-Praefix.
	{
		FLastBuildInfo Info;
		Info.Timestamp = TEXT("2026-08-16 15:47:00");
		Info.DurationSeconds = 12.1;
		Info.Result = TEXT("fehlgeschlagen: OSM-Parserfehler");
		TestEqual(TEXT("Einzeiler: Fehlerfall"),
			Info.GetSummary(),
			TEXT("Letzter Build: 12.1 s, fehlgeschlagen: OSM-Parserfehler (2026-08-16 15:47:00)"));
	}

	// FromBuildSummary: uebernimmt Zeitpunkt/Dauer/Ergebnis aus der Summary.
	{
		FWiesbadenBuildSummary Summary;
		Summary.Timestamp = TEXT("2026-08-16 10:00:00");
		Summary.DurationSeconds = 12.1;
		Summary.Result = TEXT("fehlgeschlagen: Georef nicht gesetzt");

		const FLastBuildInfo Info = FLastBuildInfo::FromBuildSummary(Summary);
		TestEqual(TEXT("FromBuildSummary: Zeitpunkt"), Info.Timestamp, Summary.Timestamp);
		TestEqual(TEXT("FromBuildSummary: Dauer"), Info.DurationSeconds, Summary.DurationSeconds);
		TestEqual(TEXT("FromBuildSummary: Ergebnis"), Info.Result, Summary.Result);
		TestFalse(TEXT("FromBuildSummary: nicht leer"), Info.IsEmpty());
		TestEqual(TEXT("FromBuildSummary: Einzeiler nutzt Summary-Werte"),
			Info.GetSummary(),
			FormatBuildSummaryLine(Summary.DurationSeconds, Summary.Result, Summary.Timestamp));
	}

	return true;
}
