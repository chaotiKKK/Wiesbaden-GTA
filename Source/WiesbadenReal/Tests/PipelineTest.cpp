// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
//
// Headless-Integrationstest fuer die gemeinsame Daten-Pipeline
// (WiesbadenCityPipeline::BuildCityData): FBuildTools mit echten UObjects
// (NewObject), OSM/DEM als echte Temp-Dateien auf Platte. Beweist, dass der
// City-Prompt ("tile faktor 4.0") die Terrain-Qualitaetswarnung im
// Pipeline-Ergebnis deaktiviert - die Kette Prompt -> Spec -> CheckTerrainQuality
// wird hier end-to-end (nicht nur stueckweise) getestet.

#include "Misc/AutomationTest.h"

#include "GIS/BuildingGenerator.h"
#include "GIS/GeoCoordinateConverter.h"
#include "GIS/HeightmapImporter.h"
#include "GIS/OSMDataParser.h"
#include "GIS/RoadFurnitureGenerator.h"
#include "GIS/RoadNetworkGenerator.h"
#include "GIS/RoadTypeLibrary.h"
#include "GIS/TerrainGenerator.h"
#include "GIS/WiesbadenCityPipeline.h"
#include "GIS/WiesbadenRegion.h"
#include "GIS/WiesbadenRegionAssets.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

namespace
{
	/** Schreibt Text in eine temporaere Datei unter Saved/. Leerer Pfad bei Fehler. */
	FString WriteTempText(const TCHAR* Extension, const FString& Text)
	{
		const FString Dir = FPaths::ProjectSavedDir();
		IFileManager::Get().MakeDirectory(*Dir, /*Tree=*/true);
		const FString Path = Dir / FString::Printf(
			TEXT("wb_pipeline_%s.%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits), Extension);
		return FFileHelper::SaveStringToFile(Text, *Path) ? Path : FString();
	}

	/** Loescht registrierte Temp-Dateien beim Scope-Exit (auch bei Test-Fruehabbruch). */
	class FScopedTempFiles
	{
	public:
		void Add(const FString& Path)
		{
			Paths.Add(Path);
		}

		~FScopedTempFiles()
		{
			for (const FString& Path : Paths)
			{
				IFileManager::Get().Delete(*Path, /*bRequireExists=*/false, /*bEvenReadOnly=*/true);
			}
		}

	private:
		TArray<FString> Paths;
	};

	/**
	 * Baut die komplette FBuildTools-Struktur mit echten UObjects per NewObject
	 * - exakt das Muster von WorldBuilder/GameInstance (dort mit Actor-/GI-Outer,
	 * hier transient; GC laeuft nicht mitten im synchronen Lauf).
	 */
	WiesbadenCityPipeline::FBuildTools NewPipelineTools()
	{
		WiesbadenCityPipeline::FBuildTools Tools;
		Tools.Converter = NewObject<UGeoCoordinateConverter>();
		Tools.Converter->InitializeWithWiesbadenOrigin();
		Tools.Parser = NewObject<UOSMDataParser>();
		Tools.Importer = NewObject<UHeightmapImporter>();
		Tools.TypeLibrary = NewObject<URoadTypeLibrary>();
		Tools.RoadGenerator = NewObject<URoadNetworkGenerator>();
		Tools.BuildingGenerator = NewObject<UBuildingGenerator>();
		Tools.RegionGenerator = NewObject<UWiesbadenRegionGenerator>();
		Tools.RegionAssetGenerator = NewObject<UWiesbadenRegionAssetGenerator>();
		Tools.TerrainGenerator = NewObject<UTerrainGenerator>();
		Tools.FurnitureGenerator = NewObject<URoadFurnitureGenerator>();
		return Tools;
	}

	/**
	 * Minimale OSM-Stadt (5 Nodes, 4 Wohnstrassen im Kreuz um Node 1). Die
	 * Ausdehnung ist absichtlich winzig (~100 m lange Seite): mit dem
	 * 100-m-Crop-Rand wird das Terrain-Tile ~300 m breit -> Verhaeltnis ~3.0,
	 * also ueber dem Default-Schwellwert 2.0, aber unter 4.0. Genau diese
	 * Konstellation laesst den Prompt-Schwellwert die Warnung umschalten.
	 */
	FString MinimalOsmXml()
	{
		return
			TEXT("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n")
			TEXT("<osm version=\"0.6\" generator=\"AutomationTest\">\n")
			TEXT("  <node id=\"1\" lat=\"50.08285\" lon=\"8.2407\"/>\n")
			TEXT("  <node id=\"2\" lat=\"50.08285\" lon=\"8.2414\"/>\n")
			TEXT("  <node id=\"3\" lat=\"50.0833\" lon=\"8.2407\"/>\n")
			TEXT("  <node id=\"4\" lat=\"50.08285\" lon=\"8.2400\"/>\n")
			TEXT("  <node id=\"5\" lat=\"50.0824\" lon=\"8.2407\"/>\n")
			TEXT("  <way id=\"100\">\n")
			TEXT("    <nd ref=\"1\"/>\n")
			TEXT("    <nd ref=\"2\"/>\n")
			TEXT("    <tag k=\"highway\" v=\"residential\"/>\n")
			TEXT("    <tag k=\"name\" v=\"Oststrasse\"/>\n")
			TEXT("  </way>\n")
			TEXT("  <way id=\"101\">\n")
			TEXT("    <nd ref=\"1\"/>\n")
			TEXT("    <nd ref=\"3\"/>\n")
			TEXT("    <tag k=\"highway\" v=\"residential\"/>\n")
			TEXT("    <tag k=\"name\" v=\"Nordstrasse\"/>\n")
			TEXT("  </way>\n")
			TEXT("  <way id=\"102\">\n")
			TEXT("    <nd ref=\"1\"/>\n")
			TEXT("    <nd ref=\"4\"/>\n")
			TEXT("    <tag k=\"highway\" v=\"residential\"/>\n")
			TEXT("    <tag k=\"name\" v=\"Weststrasse\"/>\n")
			TEXT("  </way>\n")
			TEXT("  <way id=\"103\">\n")
			TEXT("    <nd ref=\"1\"/>\n")
			TEXT("    <nd ref=\"5\"/>\n")
			TEXT("    <tag k=\"highway\" v=\"residential\"/>\n")
			TEXT("    <tag k=\"name\" v=\"Suedstrasse\"/>\n")
			TEXT("  </way>\n")
			TEXT("</osm>\n");
	}

	/**
	 * 9x9-ASCII-Grid, das den OSM-Bereich + 100-m-Rand vollstaendig abdeckt.
	 * Hoehen 100..160 m -> Span 60 m (plausibel, keine Hoehen-Warnung).
	 */
	FString MinimalDemAscii()
	{
		FString Content =
			TEXT("ncols 9\n")
			TEXT("nrows 9\n")
			TEXT("xllcorner 8.238\n")
			TEXT("yllcorner 50.080\n")
			TEXT("cellsize 0.001\n")
			TEXT("NODATA_value -9999\n");
		const TCHAR* Rows[9] = {
			TEXT("100 105 110 115 120 125 130 135 140\n"),
			TEXT("105 110 115 120 125 130 135 140 145\n"),
			TEXT("110 115 120 125 130 135 140 145 150\n"),
			TEXT("115 120 125 130 135 140 145 150 155\n"),
			TEXT("120 125 130 135 140 145 150 155 160\n"),
			TEXT("125 130 135 140 145 150 155 160 155\n"),
			TEXT("130 135 140 145 150 155 160 155 150\n"),
			TEXT("135 140 145 150 155 160 155 150 145\n"),
			TEXT("140 145 150 155 160 155 150 145 140\n"),
		};
		for (const TCHAR* Row : Rows)
		{
			Content += Row;
		}
		return Content;
	}

	/**
	 * Gemeinsames Fixture: Temp-OSM + Temp-DEM, reduzierter Build (nur Strassen
	 * + Terrain, kleines Raster), Toos mit echten UObjects.
	 */
	bool SetupMinimalBuild(
		FAutomationTestBase& Test,
		FScopedTempFiles& TempFiles,
		WiesbadenCityPipeline::FBuildInput& OutInput,
		WiesbadenCityPipeline::FBuildTools& OutTools)
	{
		const FString OsmPath = WriteTempText(TEXT("osm"), MinimalOsmXml());
		const FString DemPath = WriteTempText(TEXT("asc"), MinimalDemAscii());
		if (!Test.TestTrue(TEXT("Temp-OSM geschrieben"), !OsmPath.IsEmpty())
			|| !Test.TestTrue(TEXT("Temp-DEM geschrieben"), !DemPath.IsEmpty()))
		{
			return false;
		}
		TempFiles.Add(OsmPath);
		TempFiles.Add(DemPath);

		OutInput.OsmFilePath = OsmPath;
		OutInput.DemFilePath = DemPath;
		OutInput.bGenerateRegions = false;
		OutInput.bGenerateRegionAssets = false;
		OutInput.bGenerateBuildings = false;
		OutInput.bGenerateFurniture = false;
		// Kleines Raster fuer den Headless-Lauf (Default 4033 waere zu langsam).
		OutInput.TerrainSettings.GridSize = 32;
		// 100-m-Rand statt Default 200 m: Tile ~300 m bei ~100 m OSM-Ausdehnung
		// -> Verhaeltnis ~3.0, damit beide Prompt-Schwellen (2.0/4.0) greifen.
		OutInput.TerrainSettings.CropMarginMeters = 100.0;

		OutTools = NewPipelineTools();
		if (!Test.TestTrue(TEXT("Konverter initialisiert"),
			OutTools.Converter != nullptr && OutTools.Converter->IsInitialized()))
		{
			return false;
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPipelineHeadlessBuildTest,
	"WiesbadenReal.GIS.Pipeline.HeadlessBuild",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPipelineHeadlessBuildTest::RunTest(const FString& Parameters)
{
	FScopedTempFiles TempFiles;
	WiesbadenCityPipeline::FBuildInput Input;
	WiesbadenCityPipeline::FBuildTools Tools;
	if (!SetupMinimalBuild(*this, TempFiles, Input, Tools))
	{
		return false;
	}

	FWiesbadenCityData Data;
	const WiesbadenCityPipeline::EBuildResult Result =
		WiesbadenCityPipeline::BuildCityData(Input, Tools, Data);

	TestTrue(TEXT("Pipeline-Ergebnis Success"),
		Result == WiesbadenCityPipeline::EBuildResult::Success);
	TestTrue(TEXT("OSM geparst"), Data.ParseResult.bSuccess);
	TestTrue(TEXT("Strassen ok"), Data.RoadReport.bSuccess);
	TestEqual(TEXT("4 Strassen-Segmente"), Data.RoadReport.SegmentCount, 4);
	TestTrue(TEXT("Netz nicht leer"), !Data.RoadNetwork.IsEmpty());
	TestTrue(TEXT("Terrain ok"), Data.TerrainReport.bSuccess);
	TestTrue(TEXT("Tile gueltig"), Data.TerrainTile.IsValid());
	TestTrue(TEXT("Weltbreite ~300 m (OSM + 2x100 m Rand)"),
		FMath::IsNearlyEqual(Data.TerrainReport.WorldWidthMeters, 300.0, 25.0));

	// Canary: Default-Schwellwert 2.0, Verhaeltnis ~3.0 -> Warnung MUSS aktiv
	// sein (ohne Prompt). Die Hoehenspanne (60 m) bleibt plausibel.
	TestTrue(TEXT("Tile-zu-gross-Warnung aktiv (Default 2.0)"),
		Data.TerrainQuality.bWarnTileTooLarge);
	TestTrue(TEXT("Keine Hoehen-Warnung"),
		!Data.TerrainQuality.bWarnHeightRangeSuspicious);
	TestTrue(TEXT("Warnung in Status eingebettet"),
		Data.Status.Contains(TEXT("Terrain-Warnung")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPipelinePromptTerrainThresholdTest,
	"WiesbadenReal.GIS.Pipeline.PromptTerrainThreshold",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPipelinePromptTerrainThresholdTest::RunTest(const FString& Parameters)
{
	FScopedTempFiles TempFiles;
	WiesbadenCityPipeline::FBuildInput Input;
	WiesbadenCityPipeline::FBuildTools Tools;
	if (!SetupMinimalBuild(*this, TempFiles, Input, Tools))
	{
		return false;
	}

	// Prompt hebt die Schwelle auf 4.0 -> Verhaeltnis ~3.0 liegt darunter,
	// die Tile-Warnung MUSS verschwinden (Kette Prompt -> Spec -> Check).
	Input.CityPrompt = TEXT("dichte Innenstadt, tile faktor 4.0");

	FWiesbadenCityData Data;
	const WiesbadenCityPipeline::EBuildResult Result =
		WiesbadenCityPipeline::BuildCityData(Input, Tools, Data);

	TestTrue(TEXT("Pipeline-Ergebnis Success"),
		Result == WiesbadenCityPipeline::EBuildResult::Success);
	TestTrue(TEXT("Prompt-Schwelle 4.0 uebernommen"),
		FMath::IsNearlyEqual(Data.CityPromptSpec.TerrainMaxTileToOsmRatio, 4.0f, 1e-4f));
	TestTrue(TEXT("Tile-Warnung deaktiviert (Ratio 3.0 < 4.0)"),
		!Data.TerrainQuality.bWarnTileTooLarge);
	TestTrue(TEXT("Keine Hoehen-Warnung"),
		!Data.TerrainQuality.bWarnHeightRangeSuspicious);
	TestTrue(TEXT("Keine Terrain-Warnung im Status"),
		!Data.Status.Contains(TEXT("Terrain-Warnung")));

	// Kontrollprobe: Schwelle 1.0 liegt UNTER dem Verhaeltnis (~3.0) -> die
	// Warnungs-Maschinerie lebt also noch (nicht nur global unterdrueckt).
	{
		Input.CityPrompt = TEXT("tile faktor 1.0");
		FWiesbadenCityData CtrlData;
		const WiesbadenCityPipeline::EBuildResult CtrlResult =
			WiesbadenCityPipeline::BuildCityData(Input, Tools, CtrlData);
		TestTrue(TEXT("Kontrolllauf Success"),
			CtrlResult == WiesbadenCityPipeline::EBuildResult::Success);
		TestTrue(TEXT("Kontrolllauf: Warnung wieder aktiv (1.0 < 3.0)"),
			CtrlData.TerrainQuality.bWarnTileTooLarge);
	}

	return true;
}
