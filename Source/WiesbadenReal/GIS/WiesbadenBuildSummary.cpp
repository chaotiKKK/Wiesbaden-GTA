// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "GIS/WiesbadenBuildSummary.h"

#include "Core/WiesbadenCityData.h"
#include "GIS/BuildingGenerator.h"
#include "GIS/RoadFurnitureGenerator.h"
#include "GIS/RoadNetworkTypes.h"
#include "GIS/WiesbadenRegion.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	// Schuetzt Anhaenge aus Editor (Game-Thread) und Laufzeit (Worker-Thread).
	FCriticalSection GBuildSummaryCsvMutex;

	// CSV-Zellen escapen: Komma, Anfuehrungszeichen und Zeilenumbrueche werden
	// RFC-4180-gequotet (Feld in Anfuehrungszeichen, "" verdoppelt).
	FString EscapeCsvCell(const FString& Value)
	{
		if (!Value.Contains(TEXT(",")) && !Value.Contains(TEXT("\"")) && !Value.Contains(TEXT("\n")))
		{
			return Value;
		}

		const FString Escaped = Value.Replace(TEXT("\""), TEXT("\"\""));
		return FString::Printf(TEXT("\"%s\""), *Escaped);
	}
}

FString FLastBuildInfo::GetSummary() const
{
	if (IsEmpty())
	{
		return TEXT("");
	}
	return FormatBuildSummaryLine(DurationSeconds, Result, Timestamp);
}

FLastBuildInfo FLastBuildInfo::FromBuildSummary(const FWiesbadenBuildSummary& Summary)
{
	FLastBuildInfo Info;
	Info.Timestamp = Summary.Timestamp;
	Info.DurationSeconds = Summary.DurationSeconds;
	Info.Result = Summary.Result;
	return Info;
}

FString BuildSummaryCsvHeader()
{
	return TEXT("Timestamp,Source,DurationSeconds,RoadSegments,Intersections,Buildings,Signs,"
		"TerrainGrid,TerrainMode,TerrainTileTooLarge,TerrainHeightRangeSuspicious,"
		"RegionsWater,RegionsGreen,RegionsResidential,RegionsCommercial,"
		"RegionsIndustrial,RegionAssetsTotal,RegionAssetsTrees,RegionAssetsWaterfront,"
		"RegionAssetsIndustrial,Chunks,ChunkSizeMeters,MapPath,Result");
}

FString FormatBuildSummaryCsvRow(const FWiesbadenBuildSummary& Summary)
{
	// Reihenfolge MUSS zur Kopfzeile passen (24 Spalten).
	return FString::Printf(
		TEXT("%s,%s,%.1f,%d,%d,%d,%d,%d,%s,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%.0f,%s,%s"),
		*EscapeCsvCell(Summary.Timestamp),
		*EscapeCsvCell(Summary.Source),
		Summary.DurationSeconds,
		Summary.RoadSegments,
		Summary.Intersections,
		Summary.Buildings,
		Summary.Signs,
		Summary.TerrainGrid,
		*EscapeCsvCell(Summary.TerrainMode),
		Summary.bTerrainTileTooLarge ? 1 : 0,
		Summary.bTerrainHeightRangeSuspicious ? 1 : 0,
		Summary.RegionsWater,
		Summary.RegionsGreen,
		Summary.RegionsResidential,
		Summary.RegionsCommercial,
		Summary.RegionsIndustrial,
		Summary.RegionAssetsTotal,
		Summary.RegionAssetsTrees,
		Summary.RegionAssetsWaterfront,
		Summary.RegionAssetsIndustrial,
		Summary.Chunks,
		Summary.ChunkSizeMeters,
		*EscapeCsvCell(Summary.MapPath),
		*EscapeCsvCell(Summary.Result));
}

FString GetBuildSummaryCsvPath()
{
	return FPaths::ProjectSavedDir() / TEXT("BuildHistory") / TEXT("CityBuilds.csv");
}

FString FormatBuildSummaryLine(double DurationSeconds, const FString& Result, const FString& Timestamp)
{
	return FString::Printf(TEXT("Letzter Build: %.1f s, %s (%s)"),
		DurationSeconds, *Result, *Timestamp);
}

FWiesbadenBuildSummary BuildSummaryFromCityData(
	const FWiesbadenCityData& Data,
	const FString& Source,
	const FString& Result,
	double DurationSeconds,
	const FString& Timestamp,
	const FRoadNetwork* MovedRoadNetwork,
	const TArray<FGeneratedBuilding>* MovedBuildings,
	const FRoadFurnitureLayout* MovedFurniture,
	const FString& TerrainMode)
{
	FWiesbadenBuildSummary Summary;
	Summary.Source = Source;
	Summary.Result = Result;
	Summary.DurationSeconds = DurationSeconds;
	Summary.Timestamp = Timestamp;

	// Gemovte Ergebnis-Member (Editor-Erfolgspfad) haben Vorrang vor den
	// (dort leeren) CityData-Feldern; sonst aus dem CityData lesen.
	const FRoadNetwork& Network = MovedRoadNetwork ? *MovedRoadNetwork : Data.RoadNetwork;
	Summary.RoadSegments = Network.Segments.Num();
	Summary.Intersections = Network.Intersections.Num();
	Summary.Buildings = MovedBuildings ? MovedBuildings->Num() : Data.Buildings.Num();
	Summary.Signs = MovedFurniture ? MovedFurniture->Signs.Num() : Data.FurnitureLayout.Signs.Num();

	// Terrain und Regionen/Assets werden NIE gemovt - immer aus dem CityData.
	Summary.TerrainGrid = Data.TerrainReport.GridSize;
	Summary.TerrainMode = TerrainMode;
	Summary.bTerrainTileTooLarge = Data.TerrainQuality.bWarnTileTooLarge;
	Summary.bTerrainHeightRangeSuspicious = Data.TerrainQuality.bWarnHeightRangeSuspicious;
	UWiesbadenRegionGenerator::GetRegionTypeCounts(Data.Regions,
		Summary.RegionsWater, Summary.RegionsGreen, Summary.RegionsResidential,
		Summary.RegionsCommercial, Summary.RegionsIndustrial);
	Summary.RegionAssetsTotal = Data.RegionAssetReport.AssetCount;
	Summary.RegionAssetsTrees = Data.RegionAssetReport.TreeCount;
	Summary.RegionAssetsWaterfront = Data.RegionAssetReport.WaterfrontCount;
	Summary.RegionAssetsIndustrial = Data.RegionAssetReport.IndustrialCount;

	return Summary;
}

bool AppendBuildSummaryToCsv(const FWiesbadenBuildSummary& Summary, FString& OutError)
{
	FScopeLock Lock(&GBuildSummaryCsvMutex);

	const FString CsvPath = GetBuildSummaryCsvPath();
	IFileManager& FileManager = IFileManager::Get();

	// Beim ersten Lauf Datei mit Kopfzeile anlegen.
	if (!FileManager.FileExists(*CsvPath))
	{
		if (!FFileHelper::SaveStringToFile(
				BuildSummaryCsvHeader() + LINE_TERMINATOR,
				*CsvPath,
				FFileHelper::EEncodingOptions::ForceUTF8,
				&FileManager))
		{
			OutError = FString::Printf(TEXT("Kopfzeile konnte nicht geschrieben werden: %s"), *CsvPath);
			return false;
		}
	}

	if (!FFileHelper::SaveStringToFile(
			FormatBuildSummaryCsvRow(Summary) + LINE_TERMINATOR,
			*CsvPath,
			FFileHelper::EEncodingOptions::ForceUTF8,
			&FileManager,
			EFileWrite::FILEWRITE_Append))
	{
		OutError = FString::Printf(TEXT("CSV-Zeile konnte nicht angehaengt werden: %s"), *CsvPath);
		return false;
	}

	return true;
}

namespace
{
	// Naechste Section-Grenze ab Position LineStart (Index der naechsten Zeile,
	// die mit '[' beginnt, oder Ende der Zeilenliste). Die Zeile LineStart selbst
	// (die aktuelle Section-Header-Zeile) wird uebersprungen.
	int32 FindNextSectionLine(const TArray<FString>& Lines, int32 LineStart)
	{
		for (int32 i = LineStart + 1; i < Lines.Num(); ++i)
		{
			const FString Trimmed = Lines[i].TrimStartAndEnd();
			if (Trimmed.StartsWith(TEXT("[")))
			{
				return i;
			}
		}
		return Lines.Num();
	}
}

FString ApplyDefaultMapToIniText(const FString& IniText, const FString& MapRef)
{
	// Zeilenweise arbeiten; LineEnding/Encoding der Datei bleiben unangetastet
	// (FFileHelper liest/schreibt den Text 1:1, wir fassen nur Zeilen an).
	// bCullEmpty=false: Leerzeilen (Section-Trenner) bleiben erhalten, damit
	// die Datei moeglichst byte-stabil bleibt.
	TArray<FString> Lines;
	IniText.ParseIntoArrayLines(Lines, /*bCullEmpty=*/false);

	const FString SectionHeader = TEXT("[/Script/EngineSettings.GameMapsSettings]");
	const FString GameDefaultKey = TEXT("GameDefaultMap");
	const FString EditorStartupKey = TEXT("EditorStartupMap");

	// Section suchen (exakt, mit optionalem CR beim Zeilenende aus ParseIntoArrayLines).
	int32 SectionLine = INDEX_NONE;
	for (int32 i = 0; i < Lines.Num(); ++i)
	{
		FString Line = Lines[i];
		Line.TrimEndInline();
		if (Line == SectionHeader)
		{
			SectionLine = i;
			break;
		}
	}

	// Keys setzen (ersetzen wenn vorhanden, sonst am Section-Ende ergaenzen).
	auto SetKey = [&Lines, &MapRef](int32 SectionStart, int32 SectionEnd, const FString& Key)
	{
		const FString Value = Key + TEXT("=") + MapRef;
		for (int32 i = SectionStart; i < SectionEnd; ++i)
		{
			FString Line = Lines[i];
			Line.TrimStartAndEndInline();
			if (Line.StartsWith(Key + TEXT("=")))
			{
				Lines[i] = Value;
				return;
			}
		}
		// Nicht vorhanden: vor der naechsten Section (bzw. am Dateiende) einfuegen.
		// EmplaceAt statt Insert: TArray::Insert ist [[nodiscard]] (C4834).
		Lines.EmplaceAt(SectionEnd, Value);
	};

	if (SectionLine != INDEX_NONE)
	{
		const int32 SectionEnd = FindNextSectionLine(Lines, SectionLine);
		SetKey(SectionLine + 1, SectionEnd, GameDefaultKey);
		// SectionEnd kann sich durch die erste Einfuegung verschoben haben -
		// neu berechnen, damit der zweite Key nicht in die naechste Section rutscht.
		SetKey(SectionLine + 1, FindNextSectionLine(Lines, SectionLine), EditorStartupKey);
	}
	else
	{
		// Section fehlt: ans Ende anhaengen (mit Leerzeile als Trenner, wenn
		// die Datei nicht leer ist).
		if (!IniText.IsEmpty())
		{
			Lines.Add(TEXT(""));
		}
		Lines.Add(SectionHeader);
		Lines.Add(GameDefaultKey + TEXT("=") + MapRef);
		Lines.Add(EditorStartupKey + TEXT("=") + MapRef);
	}

	// Aus den Zeilen wieder den Text bauen: ParseIntoArrayLines hat die
	// Zeilenenden entfernt, wir fuegen sie mit LINE_TERMINATOR wieder ein.
	FString Result;
	for (int32 i = 0; i < Lines.Num(); ++i)
	{
		Result += Lines[i];
		if (i + 1 < Lines.Num())
		{
			Result += LINE_TERMINATOR;
		}
	}
	return Result;
}
