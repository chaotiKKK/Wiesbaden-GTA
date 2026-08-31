// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "GIS/WiesbadenRegion.h"

#include "WiesbadenReal.h"

#include "GIS/GeoCoordinateConverter.h"
#include "GIS/OSMTypes.h"
#include "GIS/PolygonUtils.h"

namespace
{
	constexpr double SqCmToSqM = 1.0 / (100.0 * 100.0);
}

FString FRegionGenerationReport::ToString() const
{
	if (!bSuccess)
	{
		return FString::Printf(TEXT("Regionen FEHLGESCHLAGEN: %s"), *ErrorMessage);
	}

	return FString::Printf(
		TEXT("Regionen: %d (%d Wasser, %d Gruen)"),
		RegionCount, WaterRegionCount, GreenRegionCount);
}

ECityRegionType UWiesbadenRegionGenerator::ClassifyTags(const TMap<FName, FString>& Tags)
{
	const FString Natural = Tags.FindRef(TEXT("natural")).ToLower();
	const FString Landuse = Tags.FindRef(TEXT("landuse")).ToLower();
	const FString Leisure = Tags.FindRef(TEXT("leisure")).ToLower();
	const FString Water = Tags.FindRef(TEXT("water")).ToLower();

	// Wasser zuerst (speziellster Kontext).
	if (Natural == TEXT("water") || Natural == TEXT("bay") || Natural == TEXT("strait"))
	{
		return ECityRegionType::Water;
	}
	if (Landuse == TEXT("reservoir") || Landuse == TEXT("basin") || Landuse == TEXT("salt_pond"))
	{
		return ECityRegionType::Water;
	}
	// water=river/lake/... markiert Gewaesser auch ohne natural-Tag.
	if (!Water.IsEmpty() && Water != TEXT("no"))
	{
		return ECityRegionType::Water;
	}

	// Gruen.
	if (Leisure == TEXT("park") || Leisure == TEXT("garden") || Leisure == TEXT("recreation_ground")
		|| Leisure == TEXT("golf_course") || Leisure == TEXT("nature_reserve"))
	{
		return ECityRegionType::Green;
	}
	if (Natural == TEXT("wood") || Natural == TEXT("grassland") || Natural == TEXT("scrub")
		|| Natural == TEXT("heath") || Natural == TEXT("wetland"))
	{
		return ECityRegionType::Green;
	}
	if (Landuse == TEXT("forest") || Landuse == TEXT("grass") || Landuse == TEXT("meadow")
		|| Landuse == TEXT("orchard") || Landuse == TEXT("vineyard") || Landuse == TEXT("cemetery")
		|| Landuse == TEXT("village_green"))
	{
		return ECityRegionType::Green;
	}

	// Wohnen / Gewerbe / Industrie.
	if (Landuse == TEXT("residential") || Landuse == TEXT("apartments"))
	{
		return ECityRegionType::Residential;
	}
	if (Landuse == TEXT("commercial") || Landuse == TEXT("retail"))
	{
		return ECityRegionType::Commercial;
	}
	if (Landuse == TEXT("industrial"))
	{
		return ECityRegionType::Industrial;
	}

	return ECityRegionType::Other;
}

FString UWiesbadenRegionGenerator::GetTypeName(ECityRegionType Type)
{
	switch (Type)
	{
	case ECityRegionType::Water:		return TEXT("Wasser");
	case ECityRegionType::Green:		return TEXT("Gruen");
	case ECityRegionType::Residential:	return TEXT("Wohngebiet");
	case ECityRegionType::Commercial:	return TEXT("Gewerbe");
	case ECityRegionType::Industrial:	return TEXT("Industrie");
	default:							return TEXT("Sonstiges");
	}
}

const FWiesbadenRegion* UWiesbadenRegionGenerator::FindRegionAt(
	const TArray<FWiesbadenRegion>& Regions,
	const FVector2D& Point)
{
	// Wasser hat Vorrang vor Gruen, dann Wohnen/Gewerbe/Industrie: Ein
	// Uferpark ist erst Gruen, wenn der Punkt nicht mehr im Wasser liegt.
	static const ECityRegionType Priority[] = {
		ECityRegionType::Water,
		ECityRegionType::Green,
		ECityRegionType::Residential,
		ECityRegionType::Commercial,
		ECityRegionType::Industrial
	};

	for (const ECityRegionType Type : Priority)
	{
		for (const FWiesbadenRegion& Region : Regions)
		{
			if (Region.Type != Type || !Region.Bounds.IsInside(Point))
			{
				continue;
			}
			if (FPolygonUtils::IsPointInPolygon(Point, Region.Polygon))
			{
				return &Region;
			}
		}
	}
	return nullptr;
}

ECityRegionType UWiesbadenRegionGenerator::ClassifyPoint(
	const TArray<FWiesbadenRegion>& Regions,
	const FVector2D& Point)
{
	const FWiesbadenRegion* Region = FindRegionAt(Regions, Point);
	return Region ? Region->Type : ECityRegionType::Other;
}

void UWiesbadenRegionGenerator::GetRegionTypeCounts(
	const TArray<FWiesbadenRegion>& Regions,
	int32& OutWater, int32& OutGreen, int32& OutResidential,
	int32& OutCommercial, int32& OutIndustrial)
{
	OutWater = 0;
	OutGreen = 0;
	OutResidential = 0;
	OutCommercial = 0;
	OutIndustrial = 0;

	for (const FWiesbadenRegion& Region : Regions)
	{
		switch (Region.Type)
		{
		case ECityRegionType::Water:		++OutWater; break;
		case ECityRegionType::Green:		++OutGreen; break;
		case ECityRegionType::Residential:	++OutResidential; break;
		case ECityRegionType::Commercial:	++OutCommercial; break;
		case ECityRegionType::Industrial:	++OutIndustrial; break;
		default: break; // Other ist kein echter Regionstyp
		}
	}
}

FRegionGenerationReport UWiesbadenRegionGenerator::Generate(
	const FOSMDataSet& DataSet,
	const UGeoCoordinateConverter* Converter,
	TArray<FWiesbadenRegion>& OutRegions)
{
	FRegionGenerationReport Report;
	OutRegions.Reset();

	if (!Converter || !Converter->IsInitialized())
	{
		Report.ErrorMessage = TEXT("Georeferenzierung fehlt oder ist nicht initialisiert.");
		UE_LOG(LogWbCore, Error, TEXT("%s"), *Report.ErrorMessage);
		return Report;
	}

	// Deterministisch: sortierte Way-Ids (TMap-Iteration ist nicht sortiert).
	TArray<FOSMId> SortedWayIds;
	SortedWayIds.Reserve(DataSet.Ways.Num());
	for (const TPair<FOSMId, FOSMWay>& Pair : DataSet.Ways)
	{
		SortedWayIds.Add(Pair.Key);
	}
	SortedWayIds.Sort();

	TArray<FGeoCoordinate> Coords;
	for (const FOSMId WayId : SortedWayIds)
	{
		const FOSMWay& Way = DataSet.Ways[WayId];

		if (!Way.IsClosed())
		{
			continue;
		}

		const ECityRegionType Type = ClassifyTags(Way.Tags);
		if (Type == ECityRegionType::Other)
		{
			continue;
		}

		int32 MissingNodes = 0;
		if (!DataSet.ResolveWayCoordinates(Way, Coords, MissingNodes) || MissingNodes > 0)
		{
			continue;
		}

		TArray<FVector2D> Polygon;
		Polygon.Reserve(Coords.Num());
		for (const FGeoCoordinate& Coord : Coords)
		{
			const FVector World = Converter->GeoToUnrealGround(Coord);
			Polygon.Add(FVector2D(World.X, World.Y));
		}
		FPolygonUtils::RemoveDuplicatePoints(Polygon);
		if (Polygon.Num() < 3)
		{
			continue;
		}

		// Kleine Flecken (Baumgruppen, Muellinseln) filtern: Sie zerlegen die
		// Karte in unnoetig viele Regionen.
		const double AreaSqm = FPolygonUtils::ComputeArea(Polygon) * SqCmToSqM;
		if (AreaSqm < MinRegionAreaSqm)
		{
			continue;
		}

		FWiesbadenRegion Region;
		Region.Name = Way.GetTag(TEXT("name"));
		if (Region.Name.IsEmpty())
		{
			Region.Name = GetTypeName(Type);
		}
		Region.Type = Type;
		Region.Polygon = MoveTemp(Polygon);
		Region.Bounds = FPolygonUtils::ComputeBounds2D(Region.Polygon);
		Region.AreaSqm = AreaSqm;

		++Report.RegionCount;
		switch (Type)
		{
		case ECityRegionType::Water:	++Report.WaterRegionCount; break;
		case ECityRegionType::Green:	++Report.GreenRegionCount; break;
		default: break;
		}
		OutRegions.Add(MoveTemp(Region));
	}

	Report.bSuccess = true;
	UE_LOG(LogWbCore, Log, TEXT("%s"), *Report.ToString());
	return Report;
}
