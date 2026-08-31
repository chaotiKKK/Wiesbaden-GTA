// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "GIS/WiesbadenRegionAssets.h"

#include "WiesbadenReal.h"

#include "GIS/PolygonUtils.h"

namespace
{
	// Jitter-Amplitude je Kategorie (Baeume streuen mehr als Ufer-Objekte).
	constexpr double TreeJitterCm = 300.0;
	constexpr double WaterfrontJitterCm = 80.0;
	constexpr double IndustrialJitterCm = 400.0;

	// FNV-1a-Basis.
	constexpr uint32 FNVOffsetBasis = 2166136261u;
	constexpr uint32 FNVPrime = 16777619u;
}

FString FRegionAssetReport::ToString() const
{
	if (!bSuccess)
	{
		return FString::Printf(TEXT("Regionen-Assets FEHLGESCHLAGEN: %s"), *ErrorMessage);
	}
	return FString::Printf(
		TEXT("Regionen-Assets: %d Regionen, %d Assets (%d Baeume, %d Ufer, %d Industrie), "
			"%d auf Fahrbahnen weggelassen"),
		RegionCount, AssetCount, TreeCount, WaterfrontCount, IndustrialCount,
		SkippedOnRoadCount);
}

uint32 UWiesbadenRegionAssetGenerator::HashString(const FString& Text)
{
	uint32 Hash = FNVOffsetBasis;
	const TArray<TCHAR>& Chars = Text.GetCharArray();
	for (const TCHAR C : Chars)
	{
		if (C == 0)
		{
			continue;
		}
		Hash ^= static_cast<uint32>(C);
		Hash *= FNVPrime;
	}
	return Hash;
}

void UWiesbadenRegionAssetGenerator::GetCategoriesForRegion(
	ECityRegionType Type, TArray<ERegionAssetCategory>& OutCategories)
{
	OutCategories.Reset();
	switch (Type)
	{
	case ECityRegionType::Green:
		OutCategories.Add(ERegionAssetCategory::Tree);
		break;
	case ECityRegionType::Water:
		OutCategories.Add(ERegionAssetCategory::Waterfront);
		break;
	case ECityRegionType::Industrial:
	case ECityRegionType::Commercial:
		OutCategories.Add(ERegionAssetCategory::Industrial);
		break;
	default:
		break;
	}
}

bool UWiesbadenRegionAssetGenerator::IsCategoryEnabled(
	ERegionAssetCategory Category, const FRegionAssetSettings& Settings)
{
	switch (Category)
	{
	case ERegionAssetCategory::Tree: return Settings.bPlaceTrees;
	case ERegionAssetCategory::Waterfront: return Settings.bPlaceWaterfront;
	case ERegionAssetCategory::Industrial: return Settings.bPlaceIndustrial;
	default: return false;
	}
}

double UWiesbadenRegionAssetGenerator::GetSpacingCm(
	ERegionAssetCategory Category, const FRegionAssetSettings& Settings)
{
	switch (Category)
	{
	case ERegionAssetCategory::Tree: return Settings.TreeSpacingCm;
	case ERegionAssetCategory::Waterfront: return Settings.WaterfrontSpacingCm;
	case ERegionAssetCategory::Industrial: return Settings.IndustrialSpacingCm;
	default: return 1000.0;
	}
}

double UWiesbadenRegionAssetGenerator::GetJitterCm(ERegionAssetCategory Category)
{
	switch (Category)
	{
	case ERegionAssetCategory::Tree: return TreeJitterCm;
	case ERegionAssetCategory::Waterfront: return WaterfrontJitterCm;
	case ERegionAssetCategory::Industrial: return IndustrialJitterCm;
	default: return 200.0;
	}
}

void UWiesbadenRegionAssetGenerator::ScatterRegion(
	const FWiesbadenRegion& Region, ERegionAssetCategory Category,
	double SpacingCm, double JitterCm, const IHeightSampler* HeightSampler,
	const FWiesbadenRoadClearance* Clearance, int32& OutSkipped,
	FRegionAssetLayout& OutLayout)
{
	if (Region.Polygon.Num() < 3)
	{
		return;
	}

	// Deterministischer Seed aus Region-Name + Kategorie (stabil ueber Laeufe).
	FRandomStream Stream(HashString(Region.Name + TEXT(":") + FString::FromInt(static_cast<int32>(Category))));

	// Bounds der Region als Gitter-Raster (halber Abstand vom Rand starten).
	const FBox2D Bounds = FPolygonUtils::ComputeBounds2D(Region.Polygon);
	const double Half = SpacingCm * 0.5;

	for (double Gx = Bounds.Min.X + Half; Gx < Bounds.Max.X; Gx += SpacingCm)
	{
		for (double Gy = Bounds.Min.Y + Half; Gy < Bounds.Max.Y; Gy += SpacingCm)
		{
			const FVector2D Point(
				Gx + (Stream.FRand() - 0.5) * JitterCm,
				Gy + (Stream.FRand() - 0.5) * JitterCm);

			if (!FPolygonUtils::IsPointInPolygon(Point, Region.Polygon))
			{
				continue;
			}

			// Fahrbahn freihalten. Ohne diese Pruefung stehen Baeume mitten
			// auf der Strasse - Landnutzungsflaechen ueberlappen Strassen
			// regelmaessig, und das Regionspolygon allein weiss nichts davon.
			if (Clearance && Clearance->IsBlocked(Point))
			{
				++OutSkipped;
				continue;
			}

			FPlacedRegionAsset Asset;
			Asset.Category = Category;
			Asset.RegionName = Region.Name;
			Asset.Location = FVector(Point.X, Point.Y, 0.0);
			if (HeightSampler && HeightSampler->HasValidData())
			{
				Asset.Location.Z = HeightSampler->SampleHeightCm(Point);
			}
			Asset.YawDegrees = Stream.FRandRange(0.0f, 360.0f);
			Asset.Scale = Stream.FRandRange(0.85f, 1.15f);
			OutLayout.Assets.Add(Asset);
		}
	}
}

FRegionAssetReport UWiesbadenRegionAssetGenerator::Generate(
	const TArray<FWiesbadenRegion>& Regions,
	const IHeightSampler* HeightSampler,
	const FRegionAssetSettings& Settings,
	FRegionAssetLayout& OutLayout)
{
	return GenerateInternal(Regions, HeightSampler, Settings, nullptr, OutLayout);
}

FRegionAssetReport UWiesbadenRegionAssetGenerator::GenerateClearOfRoads(
	const TArray<FWiesbadenRegion>& Regions,
	const IHeightSampler* HeightSampler,
	const FRegionAssetSettings& Settings,
	const FRoadNetwork& Network,
	FRegionAssetLayout& OutLayout)
{
	// Zuschlag von 1,5 m auf halbe Fahrbahn plus Gehweg: haelt auch die
	// Kronen aus dem Lichtraum ueber der Fahrbahn.
	FWiesbadenRoadClearance Clearance;
	Clearance.Build(Network, 150.0);

	UE_LOG(LogWbCore, Log,
		TEXT("Fahrbahn-Freihaltung: %d Abschnitte aus %d Segmenten."),
		Clearance.GetSpanCount(), Network.Segments.Num());

	return GenerateInternal(Regions, HeightSampler, Settings, &Clearance, OutLayout);
}

FRegionAssetReport UWiesbadenRegionAssetGenerator::GenerateInternal(
	const TArray<FWiesbadenRegion>& Regions,
	const IHeightSampler* HeightSampler,
	const FRegionAssetSettings& Settings,
	const FWiesbadenRoadClearance* Clearance,
	FRegionAssetLayout& OutLayout)
{
	FRegionAssetReport Report;
	OutLayout.Reset();

	for (const FWiesbadenRegion& Region : Regions)
	{
		TArray<ERegionAssetCategory> Categories;
		GetCategoriesForRegion(Region.Type, Categories);
		if (Categories.Num() == 0)
		{
			continue;
		}

		++Report.RegionCount;
		for (const ERegionAssetCategory Category : Categories)
		{
			if (!IsCategoryEnabled(Category, Settings))
			{
				continue;
			}
			ScatterRegion(Region, Category, GetSpacingCm(Category, Settings),
				GetJitterCm(Category), HeightSampler, Clearance,
				Report.SkippedOnRoadCount, OutLayout);
		}
	}

	for (const FPlacedRegionAsset& Asset : OutLayout.Assets)
	{
		switch (Asset.Category)
		{
		case ERegionAssetCategory::Tree: ++Report.TreeCount; break;
		case ERegionAssetCategory::Waterfront: ++Report.WaterfrontCount; break;
		case ERegionAssetCategory::Industrial: ++Report.IndustrialCount; break;
		default: break;
		}
	}
	Report.AssetCount = OutLayout.Assets.Num();
	Report.bSuccess = true;

	UE_LOG(LogWbCore, Log, TEXT("%s"), *Report.ToString());
	return Report;
}
